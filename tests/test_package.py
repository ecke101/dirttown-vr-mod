from pathlib import Path
import tempfile
import unittest

from tools.package import release_files


class PackageBoundaryTests(unittest.TestCase):
    def test_release_allowlist_excludes_private_and_game_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);binaries=root/'bin';binaries.mkdir()
            for name in ('dirttown-vr.dll','vr-launch.exe','vr-probe.exe','temporal-test.exe','core-test.exe','eye-transfer-test.exe','openxr_loader.dll'):
                (binaries/name).write_bytes(b'fixture')
            for name in ('README.rst','LICENSE','run.sh','run.bat','controls.example.ini'):(root/name).write_text('fixture')
            (root/'dirttown_vr').mkdir();(root/'dirttown_vr/__init__.py').write_text('')
            (root/'licenses').mkdir();(root/'licenses/dependency.txt').write_text('fixture')
            (root/'.local').mkdir();(root/'.local/config.json').write_text('private')
            (root/'research-notes.txt').write_text('not for distribution');(root/'data.pack').write_bytes(b'game')
            names=release_files(root,binaries)
            self.assertNotIn('data.pack',names);self.assertNotIn('research-notes.txt',names)
            self.assertFalse(any(name.startswith('.local/') for name in names))
            self.assertIn('bin/dirttown-vr.dll',names)
