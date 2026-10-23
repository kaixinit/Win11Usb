"""Package an existing native EXE and committed source; never publishes a release."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parent.parent
VERSION = '0.2.0-preview.14'
STEM = f'Win11Usb-v{VERSION}'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True, help='Already built full native EXE')
    parser.add_argument('--output', type=Path, default=ROOT / 'release' / f'v{VERSION}')
    parser.add_argument('--expected-exe-sha256', required=True, help='Require exact bytes of the tested full EXE')
    args = parser.parse_args()
    exe = args.exe.resolve(strict=True)
    raw = exe.read_bytes()
    if VERSION.encode('utf-16le') not in raw:
        raise SystemExit('EXE does not contain the expected native product version')
    if b'requireAdministrator' not in raw or b'fat32format x64 helper for Win11Usb native review' not in raw:
        raise SystemExit('Expected the full creation EXE with administrator manifest and embedded helper notice')
    exe_hash = digest(exe)
    if exe_hash != args.expected_exe_sha256.lower():
        raise SystemExit('EXE SHA-256 differs from the tested candidate')
    pe = json.loads(subprocess.check_output(
        [sys.executable, str(ROOT / 'native/verify-pe.py'), str(exe)], text=True))[0]
    if pe['bytes'] != len(raw) or pe['sha256'].lower() != exe_hash:
        raise SystemExit('PE verification and EXE bytes disagree')
    source_commit = subprocess.check_output(
        ['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    tracked_status = subprocess.check_output(
        ['git', '-C', str(ROOT), 'status', '--porcelain', '--untracked-files=normal'], text=True)
    if tracked_status.strip():
        raise SystemExit('Commit the release source and documentation before packaging')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    names = [f'{STEM}-windows-x64.exe', f'{STEM}-windows-x64.zip',
             f'{STEM}-source.zip', 'SHA256SUMS.txt']
    if any((output / name).exists() for name in names):
        raise SystemExit('Output assets already exist; choose a fresh output directory')
    direct = output / names[0]
    shutil.copyfile(exe, direct)
    runtime_files = set()
    for name in ('README.md', 'README.en.md', 'LICENSE', 'CHANGELOG.md',
                 'THIRD_PARTY_NOTICES.md', 'SECURITY.md', 'assets/usb-eject-source.png'):
        runtime_files.add(ROOT / name)
    for directory in ('docs', 'third_party', 'native/third_party', 'native/test-results/preview14'):
        runtime_files.update(p for p in (ROOT / directory).rglob('*') if p.is_file())
    runtime_files.update((ROOT / 'native').glob('*.md'))
    runtime_prefix = STEM + '/'
    with zipfile.ZipFile(output / names[1], 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as package:
        package.write(direct, runtime_prefix + direct.name)
        for path in sorted(runtime_files):
            package.write(path, runtime_prefix + path.relative_to(ROOT).as_posix())
        package.writestr(runtime_prefix + 'SHA256SUMS.txt', exe_hash + '  ' + direct.name + '\n')
        package.writestr(runtime_prefix + 'SOURCE_COMMIT.txt', source_commit + '\n')
    subprocess.run(['git', '-C', str(ROOT), 'archive', '--format=zip',
                    '--prefix=Win11Usb/', '-o', str(output / names[2]), 'HEAD'], check=True)
    with zipfile.ZipFile(output / names[1]) as package:
        assert hashlib.sha256(package.read(runtime_prefix + direct.name)).hexdigest() == exe_hash
        for required in ('LICENSE', 'THIRD_PARTY_NOTICES.md', 'native/third_party/fat32format.c',
                         'native/third_party/NOTICE.txt', 'native/third_party/RUNTIME-LICENSES.txt',
                         'third_party/fat32format/fat32formatsrc-vendor.zip',
                         'third_party/fat32format/COPYING.GPL-2.0.txt',
                         'third_party/material-design-icons/LICENSE-APACHE-2.0.txt'):
            assert runtime_prefix + required in package.namelist(), required
    with zipfile.ZipFile(output / names[2]) as source:
        for required in ('native/src/eject.cpp', 'native/src/backend.cpp',
                         'native/third_party/fat32format.c', 'native/package-release.py',
                         'README.md', 'docs/releases/v' + VERSION + '.md'):
            assert 'Win11Usb/' + required in source.namelist(), required
        assert not any('/native/out/' in name or '/.git/' in name for name in source.namelist())
    (output / names[3]).write_text(''.join(
        digest(output / name) + '  ' + name + '\n' for name in names[:3]), encoding='utf-8')
    print(json.dumps({'version': VERSION, 'source_commit': source_commit,
                      'assets': [{'file': name, 'bytes': (output / name).stat().st_size,
                                  'sha256': digest(output / name)} for name in names]}, indent=2))


if __name__ == '__main__':
    main()
