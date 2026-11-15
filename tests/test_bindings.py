from pathlib import Path
import tempfile
import unittest

from dirttown_vr.bindings import loader_modifier,profile_bindings


class LoaderBindingTests(unittest.TestCase):
    def test_default_loader_uses_left_bumper(self):
        self.assertEqual(loader_modifier(None),'lb')

    def test_custom_right_click_binding_is_read_without_edits(self):
        with tempfile.TemporaryDirectory() as folder:
            file=Path(folder)/'bindings.toml'
            text="[loader.curl]\npad = 'RS click + -RS x'\n[loader.lift]\npad = 'RS click + RS y'\n"
            file.write_text(text)
            self.assertEqual(loader_modifier(file),'rs_click')
            self.assertEqual(file.read_text(),text)

    def test_unmodified_right_stick_and_quoted_comment(self):
        with tempfile.TemporaryDirectory() as folder:
            file=Path(folder)/'bindings.toml'
            file.write_text('[loader.curl]\npad = "-RS x" # player override\n[loader.lift]\npad = "RS y"\n')
            self.assertEqual(loader_modifier(file),'none')

    def test_mismatched_modifiers_are_not_silently_guessed(self):
        with tempfile.TemporaryDirectory() as folder:
            file=Path(folder)/'bindings.toml';file.write_text("[loader.curl]\npad = 'RS click + -RS x'\n")
            with self.assertRaisesRegex(RuntimeError,'same modifier'):loader_modifier(file)

    def test_windows_profile_discovery(self):
        with tempfile.TemporaryDirectory() as folder:
            profile=Path(folder)/'Dirt Town Demo/123';profile.mkdir(parents=True)
            file=profile/'bindings.toml';file.write_text('version = 2\n')
            self.assertEqual(profile_bindings(Path(folder)/'game','win32',{'APPDATA':folder}),file)
