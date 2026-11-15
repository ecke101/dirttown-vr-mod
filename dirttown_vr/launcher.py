"""Validated Windows and Proton launch plans; game files are read-only."""
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys

from .config import APP_ID,EXE_SHA,LOCAL,ROOT,Config,binary_dir,steam_roots,steam_libraries
from .platform import is_game_running,launch_lock
from .runtime import inspect_runtime,runtime_environment
from .bindings import loader_modifier,profile_bindings


def validate_game(game):
    exe=Path(game).expanduser()/'dirttown.exe'
    if not exe.is_file():raise RuntimeError('Select the Dirt Town Demo folder containing dirttown.exe.')
    if hashlib.sha256(exe.read_bytes()).hexdigest()!=EXE_SHA:
        raise RuntimeError('Unsupported game executable. This mod supports Steam build 26658761 only.')
    return exe.resolve()


def find_proton(game, override=''):
    prefix=game.parents[1]/'compatdata'/APP_ID
    if override:
        proton=Path(override).expanduser()
        if proton.is_dir():proton/='proton'
        if not proton.is_file():raise RuntimeError('The selected Proton executable does not exist.')
    else:
        marker='/files/share/fonts/'
        recorded=prefix/'config_info'
        font=next((line for line in recorded.read_text(errors='replace').splitlines() if line.endswith(marker)),None) if recorded.exists() else None
        proton=Path(font.removesuffix(marker))/'proton' if font else None
        if proton is None or not proton.is_file():
            proton=next((root/'steamapps/common'/name/'proton' for root in steam_libraries(steam_roots())
                         for name in ('Proton - Experimental','Proton 11.0','Proton 10.0')
                         if (root/'steamapps/common'/name/'proton').is_file()),None)
        if proton is None:raise RuntimeError('No Proton found. Start the game once through Steam or select a Proton executable.')
    client=next((root for root in steam_roots() if root.is_dir()),game.parents[2])
    return proton.resolve(),prefix,client


def windows_path(path):return 'Z:'+str(Path(path).resolve()).replace('/','\\')


@dataclass
class LaunchPlan:
    command:list[str]
    environment:dict[str,str]
    cwd:Path


def make_plan(config, executable='game', diagnostic=False, platform=None, base_env=None):
    platform=platform or sys.platform
    if platform not in ('win32','linux'):raise RuntimeError('Windows x64 and Linux x86-64 are supported.')
    if not .25<=float(config.scale)<=1.5:raise ValueError('Resolution scale must be between 0.25 and 1.5.')
    if config.graphics not in ('auto','d3d12','d3d11'):raise ValueError('Graphics must be auto, d3d12 or d3d11.')
    game=Path(config.game).expanduser().resolve()
    env=runtime_environment(config.runtime,base_env,platform)
    folder=binary_dir()
    required=['dirttown-vr.dll','openxr_loader.dll','vr-launch.exe'] if executable=='game' else [executable,'openxr_loader.dll']
    for name in required:
        if not (folder/name).is_file():raise RuntimeError('Missing '+name+'. Build the mod or use a complete release archive.')
    env.update(DIRTTOWN_VR_RESOLUTION_SCALE=str(config.scale),DIRTTOWN_VR_CONTROLLERS='1' if config.controllers else '0',
               DIRTTOWN_VR_TAA='1' if config.antialiasing else '0',DIRTTOWN_VR_GRAPHICS=config.graphics)
    if config.controllers and executable=='game':
        env['DIRTTOWN_VR_LOADER_MODIFIER']=loader_modifier(profile_bindings(game,platform,env))
    logs=LOCAL/'logs';logs.mkdir(parents=True,exist_ok=True)
    controls=LOCAL/'controls.ini'
    if not controls.exists():shutil.copy2(ROOT/'controls.example.ini',controls)
    if diagnostic:env['DIRTTOWN_VR_DIAGNOSTIC']='1'
    else:env.pop('DIRTTOWN_VR_DIAGNOSTIC',None)
    if platform=='win32':
        env['DIRTTOWN_VR_LOG_DIR']=str(logs)
        env['DIRTTOWN_VR_CONTROLS']=str(controls)
        if executable=='game':command=[str(folder/'vr-launch.exe'),str(game/'dirttown.exe'),str(folder/'dirttown-vr.dll')]
        else:command=[str(folder/executable)]
    else:
        proton,prefix,client=find_proton(game,config.proton)
        if executable!='game':prefix=LOCAL/'probe-prefix'
        prefix.mkdir(parents=True,exist_ok=True)
        env.update(STEAM_COMPAT_DATA_PATH=str(prefix),STEAM_COMPAT_CLIENT_INSTALL_PATH=str(client),SteamAppId=APP_ID if executable=='game' else '0',
                   SteamGameId=APP_ID if executable=='game' else '0',PROTON_LOG='1',PROTON_LOG_DIR=str(logs),
                   PRESSURE_VESSEL_IMPORT_OPENXR_1_RUNTIMES='1',DIRTTOWN_VR_LOG_DIR=windows_path(logs),DIRTTOWN_VR_CONTROLS=windows_path(controls))
        # Keep the Windows loader's bridge selection; native override uses XDG.
        env.pop('XR_RUNTIME_JSON',None)
        command=[str(proton),'run',str(folder/('vr-launch.exe' if executable=='game' else executable))]
        if executable=='game':command += [windows_path(game/'dirttown.exe'),windows_path(folder/'dirttown-vr.dll')]
    return LaunchPlan(command,env,game if executable=='game' else logs)


def run(config, executable='game', diagnostic=False, extra=(), readiness=True):
    with launch_lock():
        if is_game_running():raise RuntimeError('Quit Dirt Town before launching a game or GPU/headset test.')
        if executable=='game':validate_game(config.game)
        plan=make_plan(config,executable,diagnostic)
        if readiness and not diagnostic and executable!='temporal-test.exe':
            info=inspect_runtime(plan.environment)
            print(f"OpenXR: {info['runtime']} · {info['headset']} · recommended eyes {info['eyes']}",flush=True)
        file=LOCAL/'logs'/('launch.log' if executable=='game' else 'test.log')
        with file.open('w') as output:
            result=subprocess.run(plan.command+list(extra),cwd=plan.cwd,env=plan.environment,stdout=output,stderr=subprocess.STDOUT)
        print('Exit:',result.returncode,'Log:',file,flush=True)
        return result.returncode
