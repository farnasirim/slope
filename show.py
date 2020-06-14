#!/usr/bin/env python3
import sys
fl_name = sys.argv[1]

lines = open(fl_name, "r").readlines()

contents = []
now = ""
for ln in lines:
    if ln.startswith("add "):
        contents.append(now)
        now = ""
        now += ln
    else:
        now += ln

if now != "":
    contents.append(now)


print(''.join(sorted(contents)))
