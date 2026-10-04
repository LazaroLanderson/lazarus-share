#!/usr/bin/env python3
"""Supplemental rendering comparison; run after, never alongside, media tests."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

p=argparse.ArgumentParser()
p.add_argument('--binary',default='build/display-benchmark')
p.add_argument('--output',default='dist/benchmarks/0.2.1-display.jsonl')
a=p.parse_args()
binary=Path(a.binary).resolve()
digest=hashlib.sha256(binary.read_bytes()).hexdigest()
output=Path(a.output);output.parent.mkdir(parents=True,exist_ok=True)
if output.exists():raise SystemExit('Use a new output file to avoid mixing runs')
modes=['legacy','software','opengl']
for repeat in range(1,4):
    for mode in modes[repeat-1:]+modes[:repeat-1]:
        run=subprocess.run([str(binary),'--renderer='+mode],env=os.environ|{'GST_DEBUG':'0'},stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,timeout=30)
        if run.returncode==3:
            result={'renderer':mode,'available':False}
        elif run.returncode:
            raise SystemExit(f'Renderer {mode} failed with code {run.returncode}')
        else:
            result=json.loads(run.stdout);result['available']=True
        result.update(repeat=repeat,binary_sha256=digest)
        with output.open('a') as f:f.write(json.dumps(result,separators=(',',':'))+'\n')
        print(json.dumps({'repeat':repeat,'renderer':mode,'available':result['available']}),flush=True)
