"""Exercise the shipped binary and cache without Python on its PATH."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from tools.bundle import BINARY_NAMES,bundle,payload_files

ROOT=Path(__file__).resolve().parents[1]
BINARY=ROOT/'.local/vr/bin'/('dirttown-vr.exe' if sys.platform=='win32' else 'dirttown-vr')


@unittest.skipUnless(BINARY.is_file(),'Build the native launcher first')
class NativeLauncherTests(unittest.TestCase):
    def setUp(self):
        temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup)
        self.root=Path(temporary.name)/'å space $()';self.root.mkdir()
        self.bin=self.root/'payload';self.bin.mkdir()
        for name in BINARY_NAMES:(self.bin/name).write_bytes(b'fixture '+name.encode())
        for name in ('README.rst','LICENSE','controls.example.ini'):(self.root/name).write_text('fixture '+name)
        (self.root/'VERSION').write_text('0.2.0');(self.root/'licenses').mkdir();(self.root/'licenses/test.txt').write_text('fixture')
        self.app=self.root/BINARY.name;bundle(BINARY,self.app,self.root,self.bin)
        self.env=dict(os.environ,PATH=str(self.root/'no-python'),XDG_CONFIG_HOME=str(self.root/'config'),
                      XDG_CACHE_HOME=str(self.root/'cache'),XDG_STATE_HOME=str(self.root/'state'),LOCALAPPDATA=str(self.root/'appdata'))

    def call(self,*args,success=True):
        report=self.root/'report.json';report.unlink(missing_ok=True)
        result=subprocess.run([str(self.app),*args,'--report',str(report)],env=self.env,capture_output=True,timeout=30)
        if success:self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))
        else:self.assertNotEqual(result.returncode,0)
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else result

    def test_native_self_checks_without_python(self):
        result=subprocess.run([str(self.app),'self-test'],env=self.env,capture_output=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))

    def test_option_values_are_not_parsed_as_commands(self):
        self.assertEqual(self.call('prepare','--game','self-test')['settings']['game'],'self-test')

    def test_single_file_extracts_and_repairs_corrupt_cache(self):
        result=self.call('prepare');cache=Path(result['cache'])
        dll=cache/'bin/dirttown-vr.dll';expected=(self.bin/'dirttown-vr.dll').read_bytes()
        self.assertEqual(dll.read_bytes(),expected)
        dll.write_bytes(b'corrupt');self.call('prepare');self.assertEqual(dll.read_bytes(),expected)
        self.assertEqual((cache/'licenses/test.txt').read_text(),'fixture')

    def test_imports_old_preferences_and_controls_once(self):
        legacy=self.root/'.local';legacy.mkdir()
        settings={'game':str(self.root/'My Game'),'scale':1.0,'controllers':False,'antialiasing':False,'graphics':'d3d11'}
        (legacy/'config.json').write_text(json.dumps(settings));(legacy/'controls.ini').write_text('[controls]\ndeadzone=0.2\n')
        result=self.call('prepare');config=Path(result['config'])
        self.assertEqual(result['settings']['scale'],1.0);self.assertFalse(result['settings']['controllers'])
        self.assertEqual((config/'controls.ini').read_text(),'[controls]\ndeadzone=0.2\n')
        (legacy/'config.json').write_text('{}');self.assertEqual(self.call('prepare')['settings']['scale'],1.0)

    def test_corrupt_embedded_payload_is_rejected(self):
        image=bytearray(self.app.read_bytes());index_size,payload_size,_=struct.unpack('<QQ8s',image[-24:])
        index=json.loads(image[-24-index_size:-24]);entry=next(i for i in index['files'] if i['name']=='bin/dirttown-vr.dll')
        image[len(image)-24-payload_size+entry['offset']]^=0xff;self.app.write_bytes(image)
        result=self.call('prepare',success=False);self.assertIn('Corrupt embedded VR file',result['error'])

    def test_payload_cannot_extract_outside_its_cache(self):
        files=payload_files(self.root,self.bin);files['../escaped.txt']=self.root/'README.rst'
        with patch('tools.bundle.payload_files',return_value=files):bundle(BINARY,self.app,self.root,self.bin)
        result=self.call('prepare',success=False);self.assertIn('Invalid embedded file name',result['error'])
        self.assertFalse((self.root/'escaped.txt').exists())

    def test_unsupported_game_is_rejected_without_changes(self):
        game=self.root/'Game';game.mkdir();exe=game/'dirttown.exe';exe.write_bytes(b'unknown game')
        result=self.call('launch','--game',str(game),success=False)
        self.assertIn('Unsupported game executable',result['error']);self.assertEqual(exe.read_bytes(),b'unknown game')
