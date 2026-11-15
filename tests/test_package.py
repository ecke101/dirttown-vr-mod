from pathlib import Path
import tempfile
import unittest

from tools.bundle import BINARY_NAMES,payload_files
from tools.package import release_files


class PackageBoundaryTests(unittest.TestCase):
    def test_only_native_launcher_is_distributed(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);binaries=root/'bin';binaries.mkdir()
            for name in ('dirttown-vr','dirttown-vr.exe'):(binaries/name).write_bytes(b'fixture')
            (root/'private-notes.txt').write_text('private');(root/'game.pack').write_bytes(b'game')
            for platform,name in [('linux','dirttown-vr'),('windows','dirttown-vr.exe')]:
                self.assertEqual(list(release_files(platform,root,binaries)),[name])

    def test_embedded_payload_excludes_private_and_game_data(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);binaries=root/'bin';binaries.mkdir()
            for name in BINARY_NAMES:(binaries/name).write_bytes(b'fixture')
            for name in ('README.rst','LICENSE','controls.example.ini'):(root/name).write_text('fixture')
            (root/'licenses').mkdir();(root/'licenses/dependency.txt').write_text('fixture')
            (root/'.local').mkdir();(root/'.local/config.json').write_text('private')
            (root/'research-notes.txt').write_text('not for distribution');(root/'data.pack').write_bytes(b'game')
            names=payload_files(root,binaries)
            self.assertNotIn('data.pack',names);self.assertNotIn('research-notes.txt',names)
            self.assertFalse(any(name.startswith('.local/') or name.endswith('.py') for name in names))
            self.assertIn('bin/dirttown-vr.dll',names)
