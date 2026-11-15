"""Host-specific process detection and cooperative launcher locking."""
from contextlib import contextmanager
import ctypes
import os
from pathlib import Path
import sys

from .config import LOCAL


def is_game_running():
    if sys.platform != 'win32':
        for file in Path('/proc').glob('[0-9]*/comm'):
            try:
                if file.read_text().strip().lower() == 'dirttown.exe':
                    return True
            except (OSError, UnicodeError):
                pass
        return False
    from ctypes import wintypes as w
    class Entry(ctypes.Structure):
        _fields_ = [('size',w.DWORD),('usage',w.DWORD),('pid',w.DWORD),('heap',ctypes.c_size_t),
                    ('module',w.DWORD),('threads',w.DWORD),('parent',w.DWORD),
                    ('priority',w.LONG),('flags',w.DWORD),('exe',w.WCHAR*260)]
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.CreateToolhelp32Snapshot.argtypes = [w.DWORD,w.DWORD]
    kernel.CreateToolhelp32Snapshot.restype = w.HANDLE
    kernel.Process32FirstW.argtypes = kernel.Process32NextW.argtypes = [w.HANDLE,ctypes.POINTER(Entry)]
    kernel.CloseHandle.argtypes = [w.HANDLE]
    snapshot = kernel.CreateToolhelp32Snapshot(2,0)
    if snapshot == ctypes.c_void_p(-1).value:
        raise OSError(ctypes.get_last_error(), 'Cannot check running processes')
    try:
        entry = Entry();entry.size = ctypes.sizeof(entry)
        more = kernel.Process32FirstW(snapshot,ctypes.byref(entry))
        while more:
            if entry.exe.lower() == 'dirttown.exe':return True
            more = kernel.Process32NextW(snapshot,ctypes.byref(entry))
        return False
    finally:
        kernel.CloseHandle(snapshot)


@contextmanager
def launch_lock():
    LOCAL.mkdir(parents=True,exist_ok=True)
    with (LOCAL/'launch.lock').open('a+b') as file:
        if file.tell() == 0:file.write(b'0');file.flush()
        file.seek(0)
        try:
            if sys.platform == 'win32':
                import msvcrt
                msvcrt.locking(file.fileno(),msvcrt.LK_NBLCK,1)
            else:
                import fcntl
                fcntl.flock(file,fcntl.LOCK_EX|fcntl.LOCK_NB)
        except OSError as exc:
            raise RuntimeError('Another VR launch or test is running.') from exc
        try:yield
        finally:
            if sys.platform == 'win32':
                file.seek(0);msvcrt.locking(file.fileno(),msvcrt.LK_UNLCK,1)
            else:fcntl.flock(file,fcntl.LOCK_UN)
