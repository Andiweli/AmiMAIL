#!/usr/bin/env python3
"""Run native-dialog control flow against explicit API doubles (not an SDK)."""
import importlib.util
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location('native_syntax', ROOT / 'tests/check_native_syntax.py')
syntax = importlib.util.module_from_spec(spec)
spec.loader.exec_module(syntax)

def check_account_geometry():
    source = (ROOT / 'src/gui_dialogs.c').read_text(encoding='utf-8')
    start = source.index(' int account_dialog(')
    end = source.index('window = RA_OpenWindow(dialog);', start)
    setup = source[start:end]
    match = re.search(r'account_outer_height = \(LONG\)account_limits.MinHeight \+\s*'
                      r'\(LONG\)gui->window->BorderTop \+\s*'
                      r'\(LONG\)gui->screen->WBorBottom;', setup)
    assert match, 'Account height must not use the main sizing border'
    assert '((LONG)gui->window->Height - account_outer_height) / 2L' in setup
    print('Account height: plain screen border; original pre-open centering retained.')

def main():
    check_account_geometry()
    cc = shlex.split(os.environ.get('HOST_CC', 'gcc'))
    flags = ['-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Wshadow',
             '-Wstrict-prototypes', '-Wmissing-prototypes', '-Wformat=2',
             '-Werror', '-D__amigaos__']
    flags += shlex.split(os.environ.get('DIALOG_TEST_FLAGS', ''))
    with tempfile.TemporaryDirectory(prefix='amimail-dialog-tests-') as name:
        directory = Path(name)
        (directory / 'native_api.h').write_text(syntax.API, encoding='ascii')
        for path in syntax.HEADERS:
            header = directory / path
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('/* API double, not an Amiga SDK. */\n', encoding='ascii')
        binary = directory / 'dialog-tests'
        subprocess.run(cc + flags + ['-include', str(directory / 'native_api.h'),
                       '-I' + str(directory), '-Iinclude', '-Isrc',
                       'tests/test_dialog_regressions.c', '-o', str(binary)],
                       cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)

if __name__ == '__main__':
    main()
