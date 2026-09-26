#!/usr/bin/env python3
"""Exercise actual MIME/codec code with strings from the shipped German CTLG."""
import importlib.util
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location('catalog_tool', ROOT / 'tools/catalog_tool.py')
TOOL = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = TOOL
SPEC.loader.exec_module(TOOL)


def c_bytes(value: bytes) -> str:
    return '"' + ''.join('\\%03o' % byte for byte in value) + '"'


def main() -> None:
    catalog = TOOL.load_catalog(ROOT)
    data = (ROOT / '_Catalogs/deutsch/AmiMAIL.catalog').read_bytes()
    TOOL.verify_binary(data, catalog)
    _, records = TOOL.read_binary(data)
    with tempfile.TemporaryDirectory(prefix='amimail-locale-') as directory:
        tmp = Path(directory)
        header = tmp / 'catalog_marker_values.h'
        header.write_text(
            '#define CATALOG_GRAPHIC_UTF8 ' + c_bytes(records[catalog.ids['MSG_GRAPHIC_PLACEHOLDER_UTF8']]) + '\n' +
            '#define CATALOG_GRAPHIC_LOCAL ' + c_bytes(records[catalog.ids['MSG_GRAPHIC_PLACEHOLDER_LOCAL']]) + '\n',
            encoding='ascii',
        )
        binary = tmp / ('locale-markers.exe' if os.name == 'nt' else 'locale-markers')
        compiler = shlex.split(os.environ.get('HOST_CC', 'gcc'))
        flags = shlex.split(os.environ.get('HOST_TEST_FLAGS',
            '-std=c99 -O2 -Wall -Wextra -Wshadow -Wpointer-arith '
            '-Wstrict-prototypes -Wmissing-prototypes -Wformat=2 -Werror'))
        sources = ['tests/test_locale_markers.c', 'src/mime.c', 'src/codec.c',
                   'src/charset.c', 'src/buffer.c', 'src/common.c']
        subprocess.run(compiler + flags + ['-Iinclude', '-I' + str(tmp)] +
                       sources + ['-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
