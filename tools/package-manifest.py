#!/usr/bin/env python3
from pathlib import Path
import hashlib
import json
root=Path(__file__).resolve().parents[1]
folder=root/'dist/PPSA99082'
files={str(path.relative_to(folder)):hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(folder.rglob('*')) if path.is_file()}
if not files or 'eboot.bin' not in files:
    raise SystemExit('Native package missing')
(root/'dist/manifest.json').write_text(json.dumps({'titleId':'PPSA99082','version':json.loads((folder/'sce_sys/param.json').read_text())['contentVersion'],'stage':'account-verification-prototype','consoleTested':False,'files':files},indent=2)+'\n')
print(f'Manifest written: {len(files)} files')
