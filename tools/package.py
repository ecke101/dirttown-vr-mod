#!/usr/bin/env python3
"""Build a release zip from an explicit allowlist; never include game/private data."""
from pathlib import Path
import argparse
import hashlib
import json
import sys
import zipfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
from dirttown_vr import __version__
from dirttown_vr.config import binary_dir


def release_files(root=ROOT,binaries=None):
    binaries=binary_dir() if binaries is None else binaries
    files={name:root/name for name in ('README.rst','LICENSE','run.sh','run.bat','controls.example.ini')}
    for file in sorted((root/'dirttown_vr').glob('*.py')):files['dirttown_vr/'+file.name]=file
    for file in sorted((root/'licenses').glob('*.txt')):files['licenses/'+file.name]=file
    for name in ('dirttown-vr.dll','vr-launch.exe','vr-probe.exe','temporal-test.exe','core-test.exe','eye-transfer-test.exe','openxr_loader.dll'):
        files['bin/'+name]=binaries/name
    for file in files.values():
        if not file.is_file():raise RuntimeError('Release file missing: '+str(file))
    if not any(name.startswith('licenses/') for name in files):raise RuntimeError('Dependency licences are missing.')
    return files


def package(destination,root=ROOT,binaries=None):
    files=release_files(root,binaries)
    checksums={name:hashlib.sha256(file.read_bytes()).hexdigest() for name,file in files.items()}
    destination.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(destination,'w',zipfile.ZIP_DEFLATED) as archive:
        for name,file in files.items():archive.write(file,'dirttown-vr-mod/'+name)
        archive.writestr('dirttown-vr-mod/SHA256SUMS.json',json.dumps(checksums,indent=2)+'\n')
    return destination


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform',choices=('portable','windows','linux'),default='portable')
    args=parser.parse_args()
    print(package(ROOT/'dist'/f'dirttown-vr-mod-{__version__}-{args.platform}.zip'))


if __name__=='__main__':main()
