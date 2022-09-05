#!/usr/bin/python3
import sys
import os


def main():
    if len(sys.argv) < 3:
        print("Usage: log2csv.py input output")
        return
    inputPath = sys.argv[1]
    outputPath = sys.argv[2]
    lines = []
    with open(inputPath, 'r') as file:
        lines = file.readlines()
    lines = list(map(lambda l: l.strip(), lines))
    outputFile = open(outputPath, 'w')
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
    maxUsInALine = min(100_000, maxUsInALine)
    for i in range(maxUsInALine):
        fields.append(f'mus_{i}')  # Measurement microseconds
    outputFile.write(
        ','.join(map(lambda f: f.replace('-', '_'), fields)))
    outputFile.write("\n")
    fieldNo = 0
    fieldHead = 'pre-fields'
    for line in lines[1:]:
        csv = list(line.split(','))
        outputFile.write(','.join(csv[:fixedFieldsLen]) + ',')
        outputFile.write(','.join(csv[fixedFieldsLen + 1:usOffset:2]) + ',')
        usData = csv[usOffset+1:]
        if len(usData) > maxUsInALine:
            usData = usData[-maxUsInALine:]
        try:
            usData = [str(int(us)) for us in usData]
        except:
            print("error line: " + line)
            exit(1)
        outputFile.write(','.join(usData) + "\n")

    outputFile.flush()
    outputFile.close()


if __name__ == '__main__':
    main()
