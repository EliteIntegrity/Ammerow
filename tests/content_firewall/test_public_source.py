"""The source publication boundary is explicit and independent of private IP tools."""

# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

import importlib.util
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("public_source", ROOT / "utils/check-public-source.py")
TOOL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TOOL)


class PublicSourceTests(unittest.TestCase):
    def make_package(self, root, *, revision='a' * 40, extra=None):
        files = {name: b'notice\n' for name in (
            'COPYING', 'NOTICE.md', 'README.md', 'SOURCE-CODE.txt',
            'ASSET-LICENSE.md', 'THIRD_PARTY_NOTICES.md')}
        files['packaging/runtime.txt'] = b'data|lib/gamedata/test.txt|gamedata/test.txt\n'
        files['lib/gamedata/test.txt'] = b'editable data\n'
        files['SOURCE-REVISION.txt'] = revision.encode()
        for name, data in files.items():
            path = root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (root / 'source').mkdir()
        archive = root / 'source/Ammerow-source.zip'
        with zipfile.ZipFile(archive, 'w') as output:
            for name, data in files.items():
                output.writestr('source/' + name, data)
            if extra:
                output.writestr('source/' + extra, b'private')
        archive.with_suffix('.zip.sha256').write_text(
            hashlib.sha256(archive.read_bytes()).hexdigest() + '  Ammerow-source.zip\n')
        return {'required_files': list(files), 'forbidden_extensions': ['.wav'],
                'exact_files': True}

    def test_bundled_source_and_installed_data_match(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy = self.make_package(root)
            (root / 'lib/gamedata/test.txt').write_bytes(b'editable data\r\n')
            self.assertEqual(TOOL.check_package_sources(root, policy, []), [])

    def test_bundled_source_rejects_missing_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(FileNotFoundError):
                TOOL.check_package_sources(Path(directory), {}, [])

    def test_bundled_source_rejects_checksum_and_data_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy = self.make_package(root)
            (root / 'lib/gamedata/test.txt').write_bytes(b'other version')
            self.assertIn('differs from installed file', TOOL.check_package_sources(root, policy, [])[0])
            (root / 'source/Ammerow-source.zip.sha256').write_text('wrong')
            self.assertIn('checksum', TOOL.check_package_sources(root, policy, [])[0])

    def test_bundled_source_rejects_private_paths_and_assets(self):
        for extra in ('private/ledger.txt', 'music.wav'):
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                policy = self.make_package(root, extra=extra)
                self.assertTrue(TOOL.check_package_sources(root, policy, ['private']))

    def test_bundled_source_rejects_invalid_revision(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy = self.make_package(root, revision='HEAD')
            self.assertIn('invalid bundled source revision', TOOL.check_package_sources(root, policy, []))

    def test_policy_has_no_duplicate_keys(self):
        policy = TOOL.read_policy(ROOT)
        self.assertEqual(TOOL.check_names(policy["required_files"], policy, []), [])
        commands = {row[0] for row in policy["generated_checks"]}
        self.assertIn("utils/generate-original-monsters.py", commands)
        self.assertNotIn("utils/source-ownership.py", commands)
        self.assertNotIn("utils/verify-generated-names.py", commands)

    def test_unlisted_document_is_rejected_even_without_a_denylist(self):
        policy = {'required_files': ['COPYING'], 'forbidden_extensions': [],
                  'exact_files': True}
        errors = TOOL.check_names(['COPYING', 'docs/new-diary.md'], policy, [])
        self.assertEqual(errors, ['unapproved public source: docs/new-diary.md'])

    def test_public_document_cannot_link_to_unlisted_existing_file(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'README.md').write_text('[notes](notes.md)')
            (root / 'notes.md').write_text('not selected')
            self.assertTrue(TOOL.check_links(root, ['README.md']))

    def test_private_inline_paths_are_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'README.md').write_text('Read `docs/records/evidence.md`.')
            self.assertTrue(TOOL.check_links(root, ['README.md'], ['docs/records/']))

    def test_plain_document_references_are_checked_in_notices_and_preferences(self):
        for name in ('docs/copying.rst', 'lib/screens/legal.txt', 'lib/customize/user.prf'):
            with self.subTest(name=name), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                path = root / name
                path.parent.mkdir(parents=True)
                path.write_text('See docs/removed.md for instructions.')
                self.assertTrue(any('missing public documentation reference' in error
                                    for error in TOOL.check_links(root, [name])))
                target = root / 'docs/removed.md'
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text('Instructions.')
                self.assertEqual(TOOL.check_links(root, [name, 'docs/removed.md']), [])

    def test_rst_notices_cannot_reference_private_records(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'notice.rst').write_text('See private commit abc123.')
            self.assertTrue(TOOL.check_links(root, ['notice.rst'], ['private commit']))

    def test_missing_editable_generator_input_is_rejected(self):
        policy = {'required_files': ['COPYING', 'lib/authoring/roster.txt'],
                  'forbidden_extensions': [], 'exact_files': True}
        self.assertEqual(TOOL.check_names(['COPYING'], policy, []),
                         ['missing public source: lib/authoring/roster.txt'])

    def test_control_characters_are_not_paths(self):
        for name in ('file\nother', '/root', '../outside', 'C:/source'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                TOOL.safe_path(name)

    def test_duplicate_json_keys_are_rejected_even_when_values_agree(self):
        with self.assertRaises(ValueError):
            json.loads('{"same": 1, "same": 1}', object_pairs_hook=TOOL.unique_object)

    def test_private_media_and_case_collisions_are_rejected(self):
        policy = {"required_files": ["COPYING"], "forbidden_extensions": [".icns", ".wav"]}
        errors = TOOL.check_names(["COPYING", "copying", "private/a.txt", "icon.icns"],
                                 policy, ["private"])
        self.assertEqual(len(errors), 3)
        self.assertTrue(TOOL.check_names([], policy, [], archive=True))

    def test_zip_paths_cannot_escape_the_source_root(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.zip"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("source/../private.txt", "no")
            with self.assertRaises(ValueError):
                TOOL.archive_names(path)

    def test_zip_has_one_root(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "test.zip"
            with zipfile.ZipFile(path, "w") as archive:
                archive.writestr("source/COPYING", "terms")
            self.assertEqual(TOOL.archive_names(path), ["COPYING"])
            with zipfile.ZipFile(path, "a") as archive:
                archive.writestr("other/README.md", "no")
            with self.assertRaises(ValueError):
                TOOL.archive_names(path)


if __name__ == "__main__":
    unittest.main()
