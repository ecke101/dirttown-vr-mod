import hashlib
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from dirttown_vr.config import Config,steam_libraries
from dirttown_vr.launcher import find_proton,make_plan,validate_game
from dirttown_vr.runtime import runtime_environment,selected_environment


class PortableLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.local=self.root/'local';self.local.mkdir()
        self.bin=self.root/'bin';self.bin.mkdir()
        for name in ('dirttown-vr.dll','openxr_loader.dll','vr-launch.exe'):(self.bin/name).write_bytes(b'fixture')
        (self.root/'controls.example.ini').write_text('[controls]\n')
        self.game=self.root/'steam/steamapps/common/Dirt Town Demo';self.game.mkdir(parents=True)
        self.proton=self.root/'proton';self.proton.write_text('fixture')
        for target,value in [('dirttown_vr.launcher.LOCAL',self.local),('dirttown_vr.launcher.ROOT',self.root),
                             ('dirttown_vr.launcher.binary_dir',lambda:self.bin),('dirttown_vr.runtime.LOCAL',self.local)]:
            mocked=patch(target,value);mocked.start();self.addCleanup(mocked.stop)
        self.config=Config(game=str(self.game),proton=str(self.proton))

    def test_windows_launch_uses_native_executables(self):
        plan=make_plan(self.config,platform='win32',base_env={})
        self.assertEqual(plan.command,[str(self.bin/'vr-launch.exe'),str(self.game/'dirttown.exe'),str(self.bin/'dirttown-vr.dll')])
        self.assertNotIn('STEAM_COMPAT_DATA_PATH',plan.environment)
        self.assertEqual(plan.environment['DIRTTOWN_VR_CONTROLS'],str(self.local/'controls.ini'))

    def test_linux_launch_uses_proton_and_correct_game_prefix(self):
        plan=make_plan(self.config,platform='linux',base_env={})
        self.assertEqual(plan.command[:3],[str(self.proton),'run',str(self.bin/'vr-launch.exe')])
        self.assertEqual(plan.environment['STEAM_COMPAT_DATA_PATH'],str(self.root/'steam/steamapps/compatdata/5471710'))
        self.assertTrue(plan.command[3].startswith('Z:'))
        self.assertEqual(plan.environment['DIRTTOWN_VR_CONTROLLERS'],'1')

    def test_disabled_input_and_taa_are_per_launch(self):
        self.config.controllers=False;self.config.antialiasing=False;self.config.graphics='d3d11'
        env=make_plan(self.config,platform='win32',base_env={}).environment
        self.assertEqual((env['DIRTTOWN_VR_CONTROLLERS'],env['DIRTTOWN_VR_TAA'],env['DIRTTOWN_VR_GRAPHICS']),('0','0','d3d11'))

    def test_resolution_validation_rejects_nan_and_out_of_range(self):
        for scale in (0,.1,2,float('nan'),float('inf')):
            self.config.scale=scale
            with self.subTest(scale=scale),self.assertRaises(ValueError):make_plan(self.config,platform='win32',base_env={})

    def test_missing_binary_is_reported_before_launch(self):
        (self.bin/'dirttown-vr.dll').unlink()
        with self.assertRaisesRegex(RuntimeError,'Missing dirttown-vr.dll'):make_plan(self.config,platform='win32',base_env={})

    def test_modified_game_is_never_written(self):
        file=self.game/'dirttown.exe';file.write_bytes(b'unknown executable')
        before=file.read_bytes()
        with self.assertRaisesRegex(RuntimeError,'Unsupported game'):validate_game(self.game)
        self.assertEqual(file.read_bytes(),before)
        with patch('dirttown_vr.launcher.EXE_SHA',hashlib.sha256(before).hexdigest()):
            self.assertEqual(validate_game(self.game),file)

    def test_linux_runtime_override_does_not_change_global_selection(self):
        manifest=self.root/'custom.json';manifest.write_text(json.dumps({'runtime':{'library_path':'lib/runtime.so','MND_libmonado_path':'lib/monado.so'}}))
        original=manifest.read_bytes();base={'XDG_CONFIG_HOME':str(self.root/'user-config'),'KEEP':'yes'}
        openvr=self.root/'user-config/openvr/openvrpaths.vrpath'
        openvr.parent.mkdir(parents=True);openvr.write_text('{"runtime":["/test/runtime"]}')
        env=runtime_environment(str(manifest),base,'linux')
        overlay=Path(env['XDG_CONFIG_HOME'])/'openxr/1/active_runtime.json'
        self.assertEqual(json.loads(overlay.read_text())['runtime']['library_path'],str(self.root/'lib/runtime.so'))
        self.assertEqual(json.loads(overlay.read_text())['runtime']['MND_libmonado_path'],str(self.root/'lib/monado.so'))
        self.assertEqual(manifest.read_bytes(),original)
        self.assertNotIn('XR_RUNTIME_JSON',env)
        self.assertEqual(env['VR_PATHREG_OVERRIDE'],str(openvr))
        self.assertEqual(base,{'XDG_CONFIG_HOME':str(self.root/'user-config'),'KEEP':'yes'})
        base['VR_PATHREG_OVERRIDE']='/custom/openvrpaths.vrpath'
        self.assertEqual(runtime_environment(str(manifest),base,'linux')['VR_PATHREG_OVERRIDE'],base['VR_PATHREG_OVERRIDE'])

    def test_windows_runtime_override_preserves_native_manifest(self):
        manifest=self.root/'runtime.json';manifest.write_text('{"runtime":{"library_path":"runtime.dll"}}')
        env=runtime_environment(str(manifest),{},'win32')
        self.assertEqual(env['XR_RUNTIME_JSON'],str(manifest))
        self.assertNotIn('XDG_CONFIG_HOME',env)

    def test_environment_is_restored_after_failed_probe(self):
        before={name:os.environ.get(name) for name in ('XR_RUNTIME_JSON','XDG_CONFIG_HOME','XDG_CONFIG_DIRS')}
        with self.assertRaisesRegex(RuntimeError,'test'):
            with selected_environment({'XR_RUNTIME_JSON':'temporary'}):
                self.assertEqual(os.environ['XR_RUNTIME_JSON'],'temporary');raise RuntimeError('test')
        self.assertEqual({name:os.environ.get(name) for name in before},before)

    def test_recorded_proton_version_is_used(self):
        prefix=self.root/'steam/steamapps/compatdata/5471710';prefix.mkdir(parents=True)
        proton_dir=self.root/'Recorded Proton';proton_dir.mkdir();(proton_dir/'proton').write_text('fixture')
        (prefix/'config_info').write_text(str(proton_dir)+'/files/share/fonts/\n')
        selected,_,_=find_proton(self.game)
        self.assertEqual(selected,proton_dir/'proton')

    def test_secondary_steam_library_is_discovered(self):
        root=self.root/'Steam';(root/'steamapps').mkdir(parents=True)
        other=self.root/'Games Library'
        (root/'steamapps/libraryfolders.vdf').write_text('"path" "'+str(other).replace('\\','\\\\')+'"')
        self.assertIn(other,steam_libraries([root]))
