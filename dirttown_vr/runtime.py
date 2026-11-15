"""OpenXR runtime selection without changing the system's active runtime."""
from contextlib import contextmanager
import ctypes as c
import hashlib
import json
import os
from pathlib import Path
import sys

from .config import LOCAL,binary_dir


def runtime_environment(manifest='', environ=None, platform=None):
    env = dict(os.environ if environ is None else environ)
    platform = platform or sys.platform
    selected = manifest or env.get('XR_RUNTIME_JSON','')
    if not selected:return env
    source = Path(selected).expanduser().resolve(strict=True)
    data = json.loads(source.read_text())
    library = data.get('runtime',{}).get('library_path')
    if not library or not isinstance(library,str):raise ValueError('Runtime manifest has no library_path.')
    if platform == 'win32':
        env['XR_RUNTIME_JSON'] = str(source)
        return env
    # Proton's Windows loader must keep its wineopenxr bridge manifest. Its
    # native Linux loader discovers this per-launch XDG overlay instead.
    for key in ('library_path','MND_libmonado_path'):
        value=data['runtime'].get(key)
        if isinstance(value,str) and value and not Path(value).is_absolute():
            data['runtime'][key] = str((source.parent/value).resolve())
    folder = LOCAL/'runtimes'/hashlib.sha256(str(source).encode()).hexdigest()[:16]
    target = folder/'openxr/1/active_runtime.json'
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_text(json.dumps(data,indent=2)+'\n')
    previous = env.get('XDG_CONFIG_HOME',str(Path.home()/'.config'))
    # Proton reads this file directly rather than searching XDG_CONFIG_DIRS.
    # Keep its VR bootstrap configuration when overriding only OpenXR.
    openvr = Path(previous)/'openvr/openvrpaths.vrpath'
    if 'VR_PATHREG_OVERRIDE' not in env and openvr.is_file():
        env['VR_PATHREG_OVERRIDE'] = str(openvr)
    env['XDG_CONFIG_HOME'] = str(folder)
    env['XDG_CONFIG_DIRS'] = previous+os.pathsep+env.get('XDG_CONFIG_DIRS','/etc/xdg')
    env.pop('XR_RUNTIME_JSON',None)
    return env


@contextmanager
def selected_environment(env):
    names = ('XR_RUNTIME_JSON','XDG_CONFIG_HOME','XDG_CONFIG_DIRS')
    old = {name:os.environ.get(name) for name in names}
    try:
        for name in names:
            if name in env:os.environ[name]=env[name]
            else:os.environ.pop(name,None)
        yield
    finally:
        for name,value in old.items():
            if value is None:os.environ.pop(name,None)
            else:os.environ[name]=value


class Application(c.Structure):
    _fields_=[('name',c.c_char*128),('version',c.c_uint32),('engine',c.c_char*128),
              ('engineVersion',c.c_uint32),('api',c.c_uint64)]
class CreateInfo(c.Structure):
    _fields_=[('type',c.c_int32),('next',c.c_void_p),('flags',c.c_uint64),('app',Application),
              ('layerCount',c.c_uint32),('layers',c.c_void_p),('extensionCount',c.c_uint32),('extensions',c.c_void_p)]
class SystemInfo(c.Structure):
    _fields_=[('type',c.c_int32),('next',c.c_void_p),('formFactor',c.c_int32)]
class InstanceProperties(c.Structure):
    _fields_=[('type',c.c_int32),('next',c.c_void_p),('version',c.c_uint64),('name',c.c_char*128)]
class GraphicsProperties(c.Structure):
    _fields_=[('maxHeight',c.c_uint32),('maxWidth',c.c_uint32),('maxLayers',c.c_uint32)]
class TrackingProperties(c.Structure):
    _fields_=[('orientation',c.c_uint32),('position',c.c_uint32)]
class SystemProperties(c.Structure):
    _fields_=[('type',c.c_int32),('next',c.c_void_p),('system',c.c_uint64),('vendor',c.c_uint32),
              ('name',c.c_char*256),('graphics',GraphicsProperties),('tracking',TrackingProperties)]
class View(c.Structure):
    _fields_=[('type',c.c_int32),('next',c.c_void_p),('width',c.c_uint32),('maxWidth',c.c_uint32),
              ('height',c.c_uint32),('maxHeight',c.c_uint32),('samples',c.c_uint32),('maxSamples',c.c_uint32)]


def inspect_runtime(env=None):
    """Read runtime/headset metadata without opening a presentation session."""
    with selected_environment(os.environ if env is None else env):
        try:
            loader = c.WinDLL(str(binary_dir()/'openxr_loader.dll')) if sys.platform=='win32' else c.CDLL('libopenxr_loader.so.1')
        except OSError as exc:
            raise RuntimeError('OpenXR loader unavailable. Install a runtime and the OpenXR loader first.') from exc
        def api(name,args):
            fn=getattr(loader,name);fn.argtypes=args;fn.restype=c.c_int32;return fn
        create=api('xrCreateInstance',[c.POINTER(CreateInfo),c.POINTER(c.c_void_p)])
        destroy=api('xrDestroyInstance',[c.c_void_p])
        get_runtime=api('xrGetInstanceProperties',[c.c_void_p,c.POINTER(InstanceProperties)])
        get_system=api('xrGetSystem',[c.c_void_p,c.POINTER(SystemInfo),c.POINTER(c.c_uint64)])
        get_properties=api('xrGetSystemProperties',[c.c_void_p,c.c_uint64,c.POINTER(SystemProperties)])
        get_views=api('xrEnumerateViewConfigurationViews',[c.c_void_p,c.c_uint64,c.c_int32,c.c_uint32,c.POINTER(c.c_uint32),c.POINTER(View)])
        def check(result,message):
            if result<0:raise RuntimeError(f'{message} (OpenXR {result}). Start your OpenXR runtime and connect/wake the headset.')
        instance=c.c_void_p();info=CreateInfo(type=3,app=Application(b'Dirt Town VR readiness',2,b'DirtTownVRMod',2,1<<48))
        check(create(c.byref(info),c.byref(instance)),'Cannot connect to the selected OpenXR runtime')
        try:
            runtime=InstanceProperties(type=32);check(get_runtime(instance,c.byref(runtime)),'Cannot read runtime')
            system=c.c_uint64();check(get_system(instance,c.byref(SystemInfo(type=4,formFactor=1)),c.byref(system)),'No ready VR headset')
            properties=SystemProperties(type=5);check(get_properties(instance,system,c.byref(properties)),'Cannot read headset')
            count=c.c_uint32();check(get_views(instance,system,2,0,c.byref(count),None),'Cannot read stereo views')
            if count.value!=2:raise RuntimeError('The runtime does not provide a two-eye stereo view.')
            views=(View*2)(View(type=41),View(type=41))
            check(get_views(instance,system,2,2,c.byref(count),views),'Cannot read eye resolution')
            return {'runtime':runtime.name.decode(errors='replace'),'headset':properties.name.decode(errors='replace'),
                    'eyes':[[v.width,v.height] for v in views],
                    'orientation_tracking':bool(properties.tracking.orientation),'position_tracking':bool(properties.tracking.position)}
        finally:destroy(instance)
