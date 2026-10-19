#!/usr/bin/env python3
import argparse
import json
from pathlib import Path
from metrics import parse
parser=argparse.ArgumentParser()
parser.add_argument('serial',type=Path)
parser.add_argument('-o','--out',required=True,type=Path)
args=parser.parse_args()
args.out.write_text(json.dumps(parse(args.serial.read_text(),args.serial.parent.name),indent=2)+'\n')
