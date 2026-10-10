"""Check portable test registration in full and public-only source trees."""

# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CHECKER = ROOT / 'src/cmake/scripts/check_test_manifest.cmake'


class TestManifestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cmake = os.environ.get('CMAKE_COMMAND') or shutil.which('cmake')
        if not cls.cmake:
            raise RuntimeError('CMake is required: put it on PATH or set CMAKE_COMMAND.')

    def check(self, root, *, private_enabled=False):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / 'check.cmake'
            script.write_text(
                'cmake_minimum_required(VERSION 3.7...3.26)\n'
                f'include("{CHECKER.as_posix()}")\n'
                'check_test_manifest("${SOURCE_ROOT}")\n', encoding='utf-8')
            result = subprocess.run(
                [self.cmake, f'-DSOURCE_ROOT={root.as_posix()}',
                 '-DAMMEROW_PRIVATE_AUDITS=' + ('ON' if private_enabled else 'OFF'),
                 '-P', str(script)], text=True, stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT, check=False)
            return result.returncode, result.stdout

    def fixture(self, root, *, private=False, private_suite=False,
                public_suite=True, extra_suite=False):
        (root / 'CMakeLists.txt').write_text(
            'set(ANGBAND_TEST_CASE_SOURCES public/required.c)\n'
            'if(AMMEROW_PRIVATE_AUDITS)\n'
            '    list(APPEND ANGBAND_TEST_CASE_SOURCES identity/terms.c)\n'
            'endif()\n', encoding='utf-8')
        public = root / 'src/tests/public'
        public.mkdir(parents=True)
        (public / 'required.c').write_text('/* Test fixture. */\n')
        if public_suite:
            (public / 'suite.mk').write_text('TESTPROGS += public/required\n')
        if private or private_suite:
            identity = root / 'src/tests/identity'
            identity.mkdir()
            if private:
                (identity / 'terms.c').write_text('/* Optional test fixture. */\n')
            if private_suite:
                (identity / 'suite.mk').write_text('TESTPROGS += identity/terms\n')
        if extra_suite:
            extra = root / 'src/tests/extra'
            extra.mkdir()
            (extra / 'suite.mk').write_text('TESTPROGS += extra/unregistered\n')

    def test_current_source_tree(self):
        code, output = self.check(ROOT)
        self.assertEqual(code, 0, output)

    def test_public_tree_omits_private_source_and_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            code, output = self.check(root)
            self.assertEqual(code, 0, output)

    def test_private_tree_checks_registration_with_audits_on_or_off(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root, private=True, private_suite=True)
            for enabled in (False, True):
                with self.subTest(private_enabled=enabled):
                    code, output = self.check(root, private_enabled=enabled)
                    self.assertEqual(code, 0, output)

    def test_present_private_source_still_requires_portable_registration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root, private=True)
            code, output = self.check(root)
            self.assertNotEqual(code, 0, output)
            self.assertIn('Missing from suite.mk files: identity/terms', output)

    def test_missing_public_registration_is_not_ignored(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root, public_suite=False)
            code, output = self.check(root)
            self.assertNotEqual(code, 0, output)
            self.assertIn('Missing from suite.mk files: public/required', output)

    def test_orphan_private_manifest_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root, private_suite=True)
            code, output = self.check(root)
            self.assertNotEqual(code, 0, output)
            self.assertIn('Missing from CMakeLists.txt: identity/terms', output)

    def test_make_only_suite_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root, extra_suite=True)
            code, output = self.check(root)
            self.assertNotEqual(code, 0, output)
            self.assertIn('Missing from CMakeLists.txt: extra/unregistered', output)


if __name__ == '__main__':
    unittest.main()
