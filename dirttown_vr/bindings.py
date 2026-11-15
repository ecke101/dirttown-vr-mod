"""Inspect native loader bindings without editing the player's profile."""
import ast
import os
from pathlib import Path
import re
import sys


def profile_bindings(game,platform=None,environ=None):
    platform=platform or sys.platform;env=os.environ if environ is None else environ
    if platform=='win32':
        root=Path(env.get('APPDATA',str(Path.home()/'AppData/Roaming')))/'Dirt Town Demo'
    else:
        root=game.parents[1]/'compatdata/5471710/pfx/drive_c/users/steamuser/AppData/Roaming/Dirt Town Demo'
    files=[p/'bindings.toml' for p in root.glob('*') if p.is_dir() and p.name.isdigit() and (p/'bindings.toml').is_file()]
    return max(files,key=lambda p:p.stat().st_mtime) if files else None


def loader_modifier(file):
    """Recognize the supported native right-stick chords, including this user's override."""
    values={'loader.lift':'LB + RS y','loader.curl':'LB + -RS x'}
    if file is not None:
        text=file.read_text(encoding='utf-8-sig')
        for table in values:
            section=re.search(r'(?ms)^\['+re.escape(table)+r'\]\s*\n(.*?)(?=^\[|\Z)',text)
            field=re.search(r'(?m)^pad\s*=\s*((?:\"(?:[^\"\\]|\\.)*\")|(?:\x27[^\x27]*\x27))\s*(?:#.*)?$',section[1]) if section else None
            if field:
                value=ast.literal_eval(field[1])
                if value:values[table]=value
    modifiers=[]
    for table,value in values.items():
        axis='y' if table.endswith('lift') else 'x'
        match=re.fullmatch(r'\s*(?:(LB|RS click)\s*\+\s*)?-?RS\s+'+axis+r'\s*',value,re.IGNORECASE)
        if not match:raise RuntimeError('Loader '+table+' must use the right-stick '+axis+' axis (optional LB or RS click modifier). Check native game bindings.')
        modifiers.append({None:'none','lb':'lb','rs click':'rs_click'}[match[1].lower() if match[1] else None])
    if modifiers[0]!=modifiers[1]:raise RuntimeError('Use the same modifier for native loader lift/curl bindings before launching VR.')
    return modifiers[0]
