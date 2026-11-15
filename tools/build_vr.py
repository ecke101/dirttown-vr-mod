#!/usr/bin/env python3
"""Build the Windows OpenXR probe and experimental game injector/launcher."""
from pathlib import Path
import hashlib
import shutil
import subprocess
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
LOCAL=ROOT/'.local/vr'
SDK_URL='https://github.com/KhronosGroup/OpenXR-SDK/releases/download/release-1.1.60/OpenXR.Loader.1.1.60.nupkg'
SDK_SHA='93c800cfe3269a19683fe6fef51236c16a164c641a6903eff84c6b2816afe556'
HOOK_URL='https://codeload.github.com/TsudaKageyu/minhook/zip/refs/tags/v1.3.4'
HOOK_SHA='172708123daa0c98d20d3a980b16a50be14af243dc95dee6f79c24193ad010e4'

def copy_dependency(source, target):
    """Keep identical loaded images intact; replace changed files by rename."""
    if not target.exists() or target.read_bytes()!=source.read_bytes():
        temporary=target.with_name(target.stem+'.build'+target.suffix)
        shutil.copy2(source,temporary)
        temporary.replace(target)

def main():
    import argparse
    import os
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generator',help='Optional CMake generator')
    args=parser.parse_args()
    LOCAL.mkdir(parents=True,exist_ok=True)
    package=LOCAL/'OpenXR.Loader.1.1.60.nupkg'
    if not package.exists():urllib.request.urlretrieve(SDK_URL,package)
    if hashlib.sha256(package.read_bytes()).hexdigest()!=SDK_SHA:
        raise RuntimeError('OpenXR SDK package hash mismatch')
    sdk=LOCAL/'openxr-sdk'
    with zipfile.ZipFile(package) as archive:archive.extractall(sdk)
    hook_package=LOCAL/'minhook-1.3.4.zip'
    if not hook_package.exists():urllib.request.urlretrieve(HOOK_URL,hook_package)
    if hashlib.sha256(hook_package.read_bytes()).hexdigest()!=HOOK_SHA:
        raise RuntimeError('MinHook source package hash mismatch')
    with zipfile.ZipFile(hook_package) as archive:archive.extractall(LOCAL)
    hook=LOCAL/'minhook-1.3.4'
    trampoline=hook/'src/trampoline.c'
    source=trampoline.read_text()
    old='        if (hs.flags & F_ERROR)\n            return FALSE;'
    new='''        if (hs.flags & F_ERROR) {
            if (oldPos < sizeof(JMP_REL) || pOldInst < jmpDest)
                return FALSE;
            hs.len = 0; /* Jump back before AVX; do not relocate it. */
        }'''
    if source.count(old)!=1:raise RuntimeError('Unexpected MinHook trampoline source')
    trampoline.write_text(source.replace(old,new))
    build=ROOT/'build'
    command=['cmake','-S',str(ROOT),'-B',str(build),'-DCMAKE_BUILD_TYPE=Release',
             '-DOPENXR_INCLUDE='+str(sdk/'include'),'-DMINHOOK_DIR='+str(hook)]
    if os.name!='nt':command+=['-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'tools/mingw.cmake'),'-G','Ninja']
    elif args.generator:command+=['-G',args.generator]
    if os.name=='nt':command+=['-A','x64'] if not args.generator or 'Visual Studio' in args.generator else []
    subprocess.run(command,check=True)
    subprocess.run(['cmake','--build',str(build),'--config','Release','--parallel','4'],check=True)
    output=LOCAL/'bin';output.mkdir(exist_ok=True)
    for name in ('dirttown-vr.dll','vr-launch.exe','vr-probe.exe','temporal-test.exe','core-test.exe','eye-transfer-test.exe'):
        source=build/'Release'/name if (build/'Release'/name).exists() else build/name
        copy_dependency(source,output/name)
    copy_dependency(sdk/'native/x64/release/bin/openxr_loader.dll',output/'openxr_loader.dll')
    shutil.copy2(hook/'LICENSE.txt',output/'MinHook-LICENSE.txt')
    print('Built',output)

if __name__=='__main__':main()
