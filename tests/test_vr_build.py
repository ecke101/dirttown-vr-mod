"""Do not corrupt DLL images mapped by a running Wine process during a build."""
import mmap
from pathlib import Path
import tempfile
import unittest

from tools.build_vr import copy_dependency


class LoadedImageTests(unittest.TestCase):
    def test_identical_dependency_keeps_inode(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'source.dll';target=Path(folder)/'loaded.dll'
            source.write_bytes(b'unchanged image');target.write_bytes(source.read_bytes())
            inode=target.stat().st_ino
            copy_dependency(source,target)
            self.assertEqual(target.stat().st_ino,inode)

    def test_changed_dependency_preserves_old_mapping(self):
        with tempfile.TemporaryDirectory() as folder:
            source=Path(folder)/'source.dll';target=Path(folder)/'loaded.dll'
            source.write_bytes(b'new image'*1024);target.write_bytes(b'old image'*1024)
            with target.open('rb') as loaded,mmap.mmap(loaded.fileno(),0,access=mmap.ACCESS_COPY) as image:
                # Model Wine's relocated pointer data in a private file mapping.
                image[64:72]=b'RELOCATE'
                expected=image[:]
                try:
                    copy_dependency(source,target)
                except PermissionError:
                    # Windows can forbid replacing a mapped image. Refusing
                    # safely is correct; truncating that image is never safe.
                    self.assertEqual(target.read_bytes(),b'old image'*1024)
                    self.assertEqual(image[:],expected)
                    return
                self.assertEqual(image[:],expected)
                self.assertEqual(target.read_bytes(),source.read_bytes())


if __name__=='__main__':unittest.main()
