#!/usr/bin/python3
import sys
import os
import regex

def main():
    if len(sys.argv) < 3:
        print("Usage: fixlog.py input output")
        return
    inputPath = sys.argv[1]
    outputPath = sys.argv[2]
    lines = []
    with open(inputPath, 'r') as file:
        lines = file.readlines()
    lines = list(map(lambda l: l.strip(), lines))
    if os.path.exists(outputPath):
        os.remove(outputPath)
    outputFile = open(outputPath, 'w')

    outputFile.write(lines[0] + '\n')
    benchline = regex.compile('^[0-9]+,[0-9]+')
    partline = None
    for line in lines[1:]:
        line = str(line)
        if regex.match(benchline, line) != None:
            if ',us,' not in line:
                if ',native.' not in line:
                    continue
                f = ',local_io_ms_writing_cnt,'
                idx = line.index(f)
                idx += len(f)
                idx = line.index(',', idx) + 1
                partline = line[0:idx]
            else:
                outputFile.write(line + '\n')
        elif line.startswith('us,') and partline is not None:
            outputFile.write(partline + line + '\n')
            partline = None
    outputFile.close()


if __name__ == '__main__':
    main()
