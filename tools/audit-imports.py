#!/usr/bin/env python3
"""Reject unresolved symbols accidentally assigned to the unavailable WebKit module."""
from pathlib import Path
import subprocess
root = Path(__file__).resolve().parents[1]
def symbols(path, undefined):
    output = subprocess.check_output(['llvm-readelf-18','--dyn-syms',str(path)],text=True)
    return {line.split()[-1] for line in output.splitlines() if ' GLOBAL ' in line and (' UND ' in line) == undefined}
app = root/'build/llvm-pie.elf'
webkit = root/'.deps/native/ps5-payload-sdk/target/lib/libScePosixForWebKit.so'
imports = symbols(app,True)
if 'sceRandomGetRandomNumber' in imports:
    raise SystemExit('Unsafe startup import: unloaded libSceRandom')
bad = imports & symbols(webkit,False)
if bad:
    raise SystemExit('Unavailable WebKit imports: '+', '.join(sorted(bad)))
if symbols(app,False):
    raise SystemExit('Application unexpectedly exports dynamic symbols')
print('Native import audit passed (does not establish runtime compatibility)')
