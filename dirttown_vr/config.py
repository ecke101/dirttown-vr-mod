"""Portable installation discovery and launcher preferences."""
from dataclasses import dataclass
import json
import os
from pathlib import Path
import re
import sys

ROOT = Path(sys.executable).resolve().parent if getattr(sys, 'frozen', False) else Path(__file__).resolve().parents[1]
LOCAL = ROOT / '.local'
APP_ID = '5471710'
BUILD = '26658761'
EXE_SHA = '4f830861d4be3afd43cf45b2056a48afc716118efec2f3b28380766f8b9cc97f'


def steam_roots(platform=None, environ=None, home=None):
    platform = platform or sys.platform
    env = os.environ if environ is None else environ
    home = Path.home() if home is None else home
    if platform == 'win32':
        roots = []
        try:
            import winreg
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r'Software\Valve\Steam') as key:
                roots.append(Path(winreg.QueryValueEx(key, 'SteamPath')[0]))
        except (ImportError, OSError):
            pass
        roots.append(Path(env.get('ProgramFiles(x86)', r'C:\Program Files (x86)')) / 'Steam')
    else:
        roots = [home/'.local/share/Steam', home/'.steam/steam',
                 home/'.var/app/com.valvesoftware.Steam/.local/share/Steam']
    return list(dict.fromkeys(roots))


def steam_libraries(roots):
    libraries = list(roots)
    for root in roots:
        vdf = root/'steamapps/libraryfolders.vdf'
        if vdf.is_file():
            libraries += [Path(value.replace('\\\\', '\\')) for value in
                          re.findall(r'"path"\s+"([^"]+)"', vdf.read_text(errors='replace'))]
    return list(dict.fromkeys(libraries))


@dataclass
class Config:
    game: str = ''
    proton: str = ''
    runtime: str = ''
    scale: float = .75
    controllers: bool = True
    antialiasing: bool = True
    graphics: str = 'auto'

    @classmethod
    def load(cls):
        file = LOCAL/'config.json'
        values = json.loads(file.read_text()) if file.exists() else {}
        result = cls(**{key:value for key,value in values.items() if key in cls.__dataclass_fields__})
        if not result.game:
            result.game = next((str(p/'steamapps/common/Dirt Town Demo') for p in steam_libraries(steam_roots())
                                if (p/'steamapps/common/Dirt Town Demo/dirttown.exe').is_file()), '')
        return result

    def save(self):
        LOCAL.mkdir(parents=True, exist_ok=True)
        temporary = LOCAL/'config.json.tmp'
        temporary.write_text(json.dumps(vars(self), indent=2)+'\n')
        temporary.replace(LOCAL/'config.json')


def binary_dir():
    return ROOT/'bin' if (ROOT/'bin/vr-launch.exe').exists() else LOCAL/'vr/bin'
