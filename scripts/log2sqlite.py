#!/usr/bin/python3
import sys
import os
import sqlite3

def main():
    if len(sys.argv) < 3:
        print("Usage: log2sqlite.py input output")
        return
    inputPath = sys.argv[1]
    outputPath = sys.argv[2]
    lines = []
    with open(inputPath, 'r') as file:
        lines = file.readlines()
    lines = list(map(lambda l: l.strip(), lines))
    if os.path.exists(outputPath):
        os.remove(outputPath)
    outputDb = sqlite3.connect(outputPath)
    db = outputDb.cursor()
    # detect fields
    fields = lines[0].removesuffix(',data').split(',')
    fixedFieldsLen = len(fields)
    varFieldsLen = 0
    for fieldName in lines[1].split(',')[fixedFieldsLen::2]:
        if fieldName == 'us':
            break
        varFieldsLen += 1
        fields.append(fieldName)
    usOffset = fixedFieldsLen + 2 * varFieldsLen
    maxUsInALine = 0
    for line in lines[1:]:
        maxUsInALine = max(len(line.split(',')) - usOffset, maxUsInALine)
    print(f"Maximum datapoints in a line: {maxUsInALine}", file=sys.stderr)
    # maxUsInALine = min(100_000, maxUsInALine)
    fields[0] = 'benchid int'
    fields[1] = 'runid int'
    fields[2] = 'benchname int'
    fields[3] = 'runner text'
    fields[4] = 'nthreads int'
    fields[5] = 'bounds text'
    for i in range(6, len(fields)):
        fields[i] = fields[i] + ' real'
    fieldStr = (','.join(map(lambda f: f.replace('-', '_'), fields)))
    db.execute(f'CREATE TABLE info ({fieldStr});')
    db.execute(f'CREATE TABLE us (runid int, us int);')
    outputDb.commit()
    fieldNo = 0
    fieldHead = 'pre-fields'
    infoInsertTemplate = 'INSERT INTO info VALUES (' + '?,'*(len(fields) - 1) + '?);'
    usInsertTemplate = 'INSERT INTO us VALUES (?,?);'
    lineno = 0
    print()
    for line in lines[1:]:
        csv = list(line.split(','))
        lfields = []
        lfields += csv[:fixedFieldsLen]
        lfields += csv[fixedFieldsLen + 1:usOffset:2]
        if len(lfields) != len(fields):
            print("Warning: missing fields in line: " + ','.join(lfields))
            continue
        runId = int(lfields[1])
        usData = csv[usOffset+1:]
        if len(usData) > maxUsInALine:
            usData = usData[:maxUsInALine]
        db.execute(infoInsertTemplate, lfields)
        usData = [(runId, us) for us in usData]
        db.executemany(usInsertTemplate, usData)
        if (lineno % 100) == 0:
            print("\rLine {:8}/{:8} {:6.1f}%".format(lineno, len(lines), 100.0 * lineno / len(lines)), end='', flush=True)
        lineno += 1
    print()
    outputDb.commit()
    db.close()
    outputDb.close()


if __name__ == '__main__':
    main()
