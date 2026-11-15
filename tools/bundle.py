"""Embed the runtime files in a native launcher (build time only)."""
from pathlib import Path
import hashlib
import json
import struct
import zlib

ROOT=Path(__file__).resolve().parents[1]
BINARY_NAMES=('dirttown-vr.dll','vr-launch.exe','vr-probe.exe','temporal-test.exe',
              'core-test.exe','eye-transfer-test.exe','openxr_loader.dll')


def payload_files(root=ROOT,binaries=None):
    binaries=root/'.local/vr/bin' if binaries is None else binaries
    files={name:root/name for name in ('README.rst','LICENSE','controls.example.ini')}
    files.update({'bin/'+name:binaries/name for name in BINARY_NAMES})
    files.update({'licenses/'+file.name:file for file in sorted((root/'licenses').glob('*.txt'))})
    for file in files.values():
        if not file.is_file():raise RuntimeError('Embedded file missing: '+str(file))
    return files


def bundle(source,target,root=ROOT,binaries=None):
    payload=bytearray();index=[]
    for name,file in sorted(payload_files(root,binaries).items()):
        data=file.read_bytes();packed=zlib.compress(data,9)
        index.append(dict(name=name,offset=len(payload),size=len(data),packed=len(packed),sha256=hashlib.sha256(data).hexdigest()))
        payload.extend(packed)
    manifest=json.dumps(dict(version=(root/'VERSION').read_text().strip(),files=index),separators=(',',':')).encode()
    payload.extend(manifest)
    temporary=target.with_name(target.name+'.build')
    temporary.write_bytes(source.read_bytes()+payload+struct.pack('<QQ8s',len(manifest),len(payload),b'DTVRBND1'))
    temporary.chmod(source.stat().st_mode);temporary.replace(target)
    return target
