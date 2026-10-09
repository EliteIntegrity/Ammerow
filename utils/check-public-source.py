#!/usr/bin/env python3
"""Verify the public source contract without private evidence or Git ancestry."""

# Copyright (c) 2026 John Horton
# SPDX-License-Identifier: GPL-2.0-only

import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess
import sys
import zipfile


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate policy key: {key}")
        result[key] = value
    return result


def safe_path(value):
    if (not value or any(ord(char) < 32 for char in value) or "\\" in value or ":" in value or
            any(part in {"", ".", ".."} for part in value.split("/"))):
        raise ValueError(f"unsafe relative path: {value!r}")
    return value


def read_policy(root):
    policy = json.loads((root / 'packaging/public-source-policy.json').read_text(
        encoding='utf-8'), object_pairs_hook=unique_object)
    if policy['version'] != 2:
        raise ValueError('unsupported public source policy version')
    manifest = root / safe_path(policy['file_list'])
    names = [safe_path(line.strip()) for line in manifest.read_text(
        encoding='utf-8').splitlines() if line.strip() and not line.startswith('#')]
    if len({name.casefold() for name in names}) != len(names):
        raise ValueError('duplicate/case-colliding allowlist entry')
    policy['required_files'] = names
    policy['exact_files'] = True
    for command in policy['generated_checks']:
        if command[0] not in names:
            raise ValueError(f'generator absent from allowlist: {command[0]}')
    return policy


def excluded(path, exclusions):
    return any(path == entry or path.startswith(entry + "/")
               for entry in exclusions)


def check_names(names, policy, exclusions, *, archive=False):
    errors = []
    for required in policy["required_files"]:
        safe_path(required)
        if required not in names:
            errors.append(f"missing public source: {required}")
    if archive and "SOURCE-REVISION.txt" not in names:
        errors.append("missing source revision")
    seen = set()
    allowed = set(policy['required_files']) | ({'SOURCE-REVISION.txt'} if archive else set())
    for name in names:
        safe_path(name)
        folded = name.casefold()
        if folded in seen:
            errors.append(f"duplicate/case-colliding source path: {name}")
        seen.add(folded)
        if policy.get('exact_files') and name not in allowed:
            errors.append(f'unapproved public source: {name}')
        if ".git" in PurePosixPath(name).parts:
            errors.append(f"Git metadata in source: {name}")
        if excluded(name, exclusions):
            errors.append(f"excluded source: {name}")
        if PurePosixPath(name).suffix.lower() in policy["forbidden_extensions"]:
            errors.append(f"unexpected media in source: {name}")
    return errors


def archive_names(path):
    names = []
    roots = set()
    with zipfile.ZipFile(path) as archive:
        corrupt = archive.testzip()
        if corrupt:
            raise ValueError(f"corrupt ZIP member: {corrupt}")
        for entry in archive.infolist():
            safe_path(entry.filename.rstrip("/"))
            if entry.is_dir():
                continue
            if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError(f"symbolic link in source archive: {entry.filename}")
            root, separator, relative = entry.filename.partition("/")
            if not separator:
                raise ValueError("source archive must have one enclosing directory")
            roots.add(root)
            names.append(relative)
    if len(roots) != 1:
        raise ValueError("source archive must have exactly one enclosing directory")
    return names


def canonical_text(data):
    return data.decode('utf-8-sig').replace('\r\n', '\n').replace('\r', '\n')


def check_package_sources(package, policy, exclusions):
    """Check the shipped source boundary, checksum and installed editable data.

    Binary-to-source provenance still needs the release build record; comparing
    data or a checksum alone cannot establish how an executable was compiled.
    """
    archive_path = package / 'source/Ammerow-source.zip'
    digest = hashlib.sha256(archive_path.read_bytes()).hexdigest()
    checksum = archive_path.with_suffix('.zip.sha256').read_text(encoding='utf-8').strip()
    if checksum != f'{digest}  Ammerow-source.zip':
        return ['bundled source checksum does not match its archive']
    names = archive_names(archive_path)
    errors = check_names(names, policy, exclusions, archive=True)
    if errors:
        return errors
    with zipfile.ZipFile(archive_path) as archive:
        prefix = next(entry.filename.split('/')[0] for entry in archive.infolist()
                      if not entry.is_dir()) + '/'
        revision = canonical_text(archive.read(prefix + 'SOURCE-REVISION.txt')).strip()
        if not re.fullmatch(r'[0-9a-f]{40}', revision):
            errors.append('invalid bundled source revision')
        pairs = [(name, name) for name in (
            'COPYING', 'NOTICE.md', 'README.md', 'SOURCE-CODE.txt',
            'ASSET-LICENSE.md', 'THIRD_PARTY_NOTICES.md')]
        # runtime.txt maps source fonts and data to their installed lib/ paths.
        # Owned image/plate/audio bytes are deliberately not public source.
        for line in canonical_text(archive.read(prefix + 'packaging/runtime.txt')).splitlines():
            if not line.strip() or line.lstrip().startswith('#'):
                continue
            component, source, destination = line.strip().split('|')
            safe_path(source)
            safe_path(destination)
            if component not in {'data', 'config'}:
                raise ValueError(f'invalid runtime component: {component}')
            if PurePosixPath(source).suffix.lower() not in policy['forbidden_extensions']:
                if source not in names:
                    errors.append(f'missing editable runtime input: {source}')
                    continue
                pairs.append((source, 'lib/' + destination))
        for source, installed in pairs:
            original = archive.read(prefix + source)
            shipped = (package / installed).read_bytes()
            if PurePosixPath(source).suffix.lower() in {'.txt', '.md', '.prf', '.rst'}:
                original, shipped = canonical_text(original), canonical_text(shipped)
            if original != shipped:
                errors.append(f'bundled source differs from installed file: {installed}')
    return errors


def check_links(root, names, patterns=()):
    errors = []
    documents = [root / name for name in names
                 if name.endswith(('.md', '.rst')) or
                 name.startswith(('lib/customize/', 'lib/screens/'))]
    for path in documents:
        relative = path.relative_to(root).as_posix()
        if not path.is_file():
            continue
        content = path.read_text(encoding='utf-8')
        for pattern in patterns:
            if re.search(pattern, content, re.IGNORECASE):
                errors.append(f'private-development reference in public prose: {relative}: {pattern}')
        # Plain paths in preference comments and the in-game licence screen
        # are references too, even though they are not Markdown hyperlinks.
        for target in set(re.findall(
                r'(?<![\w./])docs/[\w./-]+\.(?:md|rst|csv)\b', content)):
            if target not in names or not (root / target).is_file():
                errors.append(f'missing public documentation reference: {relative}: {target}')
        for target in re.findall(r"\[[^\]\n]*\]\(([^)\n]+)\)",
                                 content):
            target = target.strip().strip("<>").split("#", 1)[0]
            if not target or ":" in target:
                continue
            destination = (path.parent / target).resolve()
            try:
                dest_relative = destination.relative_to(root.resolve()).as_posix()
            except ValueError:
                errors.append(f"documentation link leaves source: {relative}: {target}")
                continue
            if dest_relative not in names or not destination.is_file():
                errors.append(f"missing public documentation link: {relative}: {target}")
    return errors


def export_archive(root, output, revision, policy):
    """Read only explicitly selected committed blobs, never filesystem extras."""
    if not re.fullmatch(r'[0-9a-f]{40}', revision or ''):
        raise ValueError('export needs a full commit ID')
    timestamp = int(subprocess.check_output(
        ['git', 'show', '-s', '--format=%ct', revision], cwd=root))
    date = datetime.fromtimestamp(timestamp, timezone.utc).timetuple()[:6]
    prefix = f'ammerow-source-{revision[:12]}/'
    output.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation protects an earlier review or release archive.
    with output.open('xb') as stream, zipfile.ZipFile(
            stream, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        with subprocess.Popen(['git', 'cat-file', '--batch'], cwd=root,
                              stdin=subprocess.PIPE, stdout=subprocess.PIPE) as process:
            for name in policy['required_files']:
                process.stdin.write(f'{revision}:{name}\n'.encode('utf-8'))
                process.stdin.flush()
                header = process.stdout.readline().split()
                if len(header) != 3 or header[1] != b'blob':
                    raise ValueError(f'not a committed file: {name}')
                data = process.stdout.read(int(header[2]))
                if process.stdout.read(1) != b'\n':
                    raise ValueError(f'incomplete committed file: {name}')
                entry = zipfile.ZipInfo(prefix + name, date)
                entry.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(entry, data)
            process.stdin.close()
        entry = zipfile.ZipInfo(prefix + 'SOURCE-REVISION.txt', date)
        archive.writestr(entry, revision + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--archive", type=Path, help="inspect a ZIP without extracting it")
    mode.add_argument("--package", type=Path, help="verify source included in a playable install")
    mode.add_argument('--tree', action='store_true', help='reject extra files in an extracted tree (run before building)')
    mode.add_argument('--export', type=Path, help='export exact committed files to a new ZIP')
    parser.add_argument('--revision', help='full Git commit ID for --export')
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        policy = read_policy(root)
        exclusions = []
        if args.export:
            export_archive(root, args.export.resolve(), args.revision, policy)
            args.archive = args.export
        if args.package:
            errors = check_package_sources(args.package.resolve(), policy, exclusions)
        elif args.archive:
            names = archive_names(args.archive)
            errors = check_names(names, policy, exclusions, archive=True)
        else:
            errors = [f"missing public source: {name}" for name in policy["required_files"]
                      if not (root / safe_path(name)).is_file()]
            if args.tree:
                names = [path.relative_to(root).as_posix() for path in root.rglob('*') if path.is_file()]
                errors.extend(check_names(names, policy, [], archive=True))
            for name in policy['required_files']:
                if (root / name).is_symlink():
                    errors.append(f'symbolic link in source: {name}')
            errors.extend(check_links(root, policy['required_files'],
                                      policy['forbidden_prose_patterns']))
            for command in policy["generated_checks"]:
                safe_path(command[0])
                result = subprocess.run([sys.executable, *command], cwd=root,
                                        capture_output=True, text=True)
                if result.returncode:
                    errors.append(f"{' '.join(command)}: {result.stdout}{result.stderr}")
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print(f"public source error: {error}", file=sys.stderr)
        return 1
    if errors:
        for error in errors:
            print(f"public source error: {error}", file=sys.stderr)
        return 1
    print("Bundled source checksum, boundary and installed data OK" if args.package else
          "Public source archive boundary OK" if args.archive else
          "Public source documentation and generated-data checks OK")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
