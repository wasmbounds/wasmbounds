#!/usr/bin/env python3
import sys
import os
import multiprocessing

lines = sys.stdin.readlines()

jobs = max(1, multiprocessing.cpu_count() - 1)
jc = 0

for i, line in enumerate(lines):
    line = line.strip()
    next_line = lines[i+1 if i+1 < len(lines) else i].strip()
    print(line ,end='')
    next_args = next_line.split()
    if ('-c' in next_args) and (jc < jobs):
        print(' &')
        jc += 1
    elif jc >= jobs:
        jc = 0
        print('; wait')
    else:
        print('; wait')
