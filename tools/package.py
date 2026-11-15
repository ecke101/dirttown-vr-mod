#!/usr/bin/env python3
"""Build a release zip from an explicit allowlist; never include game/private data."""
from pathlib import Path
import argparse
import hashlib
import json
import zipfile

ROOT=Path(__file__).resolve().parents[1]


def release_files(platform,root=ROOT,binaries=None):
    if platform not in ('windows','linux'):raise ValueError('Choose windows or linux')
    binaries=root/'.local/vr/bin' if binaries is None else binaries
    name='dirttown-vr.exe' if platform=='windows' else 'dirttown-vr'
    files={name:binaries/name}
    for file in files.values():
        if not file.is_file():raise RuntimeError('Release file missing: '+str(file))
    return files


def package(destination,platform,root=ROOT,binaries=None):
    files=release_files(platform,root,binaries)
    checksums={name:hashlib.sha256(file.read_bytes()).hexdigest() for name,file in files.items()}
    destination.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED) as archive:
        for name,file in files.items():archive.write(file,'dirttown-vr-mod/'+name)
        archive.writestr('dirttown-vr-mod/SHA256SUMS.json',json.dumps(checksums,indent=2)+'\n')
    return destination


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform',choices=('windows','linux'),required=True)
    args=parser.parse_args()
    version=(ROOT/'VERSION').read_text().strip()
    print(package(ROOT/'dist'/f'dirttown-vr-mod-{version}-{args.platform}.zip',args.platform))


if __name__=='__main__':main()
