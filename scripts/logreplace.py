#!/usr/bin/python3
import sys
import os


def getLineId(line: str):
    return ','.join(line.split(',')[2:6])


def main():
    if len(sys.argv) < 4:
        print("Usage: log2csv.py input-bad input-good output")
        return
    inputPathBad = sys.argv[1]
    inputPathGood = sys.argv[2]
    outputPath = sys.argv[3]
    linesBad = []
    linesGood = []
    with open(inputPathBad, 'r') as file:
        linesBad = file.readlines()
    with open(inputPathGood, 'r') as file:
        linesGood = file.readlines()
    linesBad = list(map(lambda l: l.strip(), linesBad))
    linesGood = list(map(lambda l: l.strip(), linesGood))
    lineHeader = linesGood[0]
    if len(linesBad[0]) > len(linesGood[0]):
        lineHeader = linesBad[0]
    outputFile = open(outputPath, 'w')
    outputFile.write(lineHeader + '\n')
    linesBad = linesBad[1:]
    linesGood = linesGood[1:]
    goodReplacements = dict()
    for line in linesGood:
        lid = getLineId(line)
        goodReplacements[lid] = line
    for line in linesBad:
        lid = getLineId(line)
        if lid in goodReplacements:
            print(f'Replacing {lid}')
            outputFile.write(goodReplacements[lid])
            outputFile.write('\n')
            del goodReplacements[lid]
        else:
            outputFile.write(line)
            outputFile.write('\n')
    for lid, line in goodReplacements.items():
        print(f'Stray {lid}')
        outputFile.write(line)
        outputFile.write('\n')

    outputFile.flush()
    outputFile.close()


if __name__ == '__main__':
    main()
