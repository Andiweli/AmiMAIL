#!/usr/bin/env python3
"""Build/check AmiMAIL's explicit-ID CD/CT files (Python 3.8+, no packages).

This deliberately supports the subset used by AmiMAIL, not all FlexCat syntax.
It preserves octal bytes: some messages are local Latin-1, others internal UTF-8.
Unsupported directives/escapes fail instead of silently changing a translation.
IFF CTLG uses big-endian chunks, four-byte STRS record alignment and NUL strings.
"""
from __future__ import annotations

import argparse
import os
import re
import struct
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Tuple


class CatalogError(ValueError):
    """A source, mapping or compiled catalog is inconsistent."""


@dataclass
class Catalog:
    version: int
    version_string: str
    language: str
    codeset: int
    ids: Dict[str, int]
    english: Dict[str, bytes]
    translated: Dict[str, bytes]


def decode_string(value: str) -> bytes:
    """Decode source escapes, never apply a blanket UTF-8 recoding."""
    result = bytearray()
    pos = 0
    escapes = {'n': 10, 'r': 13, 't': 9, 'b': 8, 'f': 12,
               'e': 27, '\\': 92, '"': 34, "'": 39}
    while pos < len(value):
        char = value[pos]
        pos += 1
        if char != '\\':
            if ord(char) > 127:
                raise CatalogError('Use octal byte escapes for non-ASCII source characters')
            result.append(ord(char))
            continue
        if pos == len(value):
            raise CatalogError('Trailing backslash / line continuations are unsupported')
        char = value[pos]
        pos += 1
        if char in escapes:
            result.append(escapes[char])
        elif char in '01234567':
            digits = char
            while pos < len(value) and len(digits) < 3 and value[pos] in '01234567':
                digits += value[pos]
                pos += 1
            number = int(digits, 8)
            if number > 255:
                raise CatalogError('Octal escape exceeds one byte')
            result.append(number)
        elif char == 'x':
            digits = value[pos:pos + 2]
            if len(digits) != 2 or not re.fullmatch('[0-9a-fA-F]{2}', digits):
                raise CatalogError('Hex escapes require exactly two digits')
            result.append(int(digits, 16))
            pos += 2
        else:
            raise CatalogError('Unsupported escape: \\' + char)
    if 0 in result:
        raise CatalogError('Embedded NUL in catalog text')
    return bytes(result)


def parse_source(path: Path, description: bool) -> Tuple[dict, dict, dict]:
    lines = path.read_text(encoding='ascii').splitlines()
    directives, ids, strings = {}, {}, {}
    used_ids = set()
    index = 0
    descriptor = re.compile(r'(MSG_[A-Z0-9_]+)\s+\((\d+)//\)')
    while index < len(lines):
        line = lines[index]
        line_number = index + 1
        index += 1
        if not line.strip() or line.lstrip().startswith(';'):
            continue
        if line.startswith('## '):
            parts = line[3:].split(None, 1)
            if len(parts) != 2:
                raise CatalogError(f'{path}:{line_number}: invalid directive')
            key, val = parts
            allowed = {'version', 'language', 'basename'} if description else {'version', 'language', 'codeset'}
            if key not in allowed or key in directives:
                raise CatalogError(f'{path}:{line_number}: duplicate/unsupported directive {key}')
            directives[key] = val
            continue
        if description:
            match = descriptor.fullmatch(line)
            if not match:
                raise CatalogError(f'{path}:{line_number}: expected explicit numeric ID (number//)')
            name, number = match.group(1), int(match.group(2))
            if number > 0x7fffffff or number in used_ids:
                raise CatalogError(f'{path}:{line_number}: invalid/duplicate numeric ID')
            used_ids.add(number)
            ids[name] = number
        else:
            if not re.fullmatch(r'MSG_[A-Z0-9_]+', line):
                raise CatalogError(f'{path}:{line_number}: expected message name')
            name = line
        if name in strings or index >= len(lines):
            raise CatalogError(f'{path}:{line_number}: duplicate ID or missing text')
        value = lines[index]
        index += 1
        if value.startswith(('MSG_', '## ', ';')):
            raise CatalogError(f'{path}:{line_number + 1}: missing text')
        try:
            strings[name] = decode_string(value)
        except CatalogError as exc:
            raise CatalogError(f'{path}:{line_number + 1}: {exc}') from exc
    return directives, ids, strings


def format_arguments(text: bytes) -> tuple:
    """Compare printf argument types/order; literal %% consumes no argument."""
    fmt = re.compile(rb'%([-+ #0]*)(\d+|\*)?(?:\.(\d+|\*))?(hh|ll|[hljztL])?([diouxXfFeEgGaAcspn%])')
    result = []
    pos = 0
    while pos < len(text):
        if text[pos] != 37:
            pos += 1
            continue
        match = fmt.match(text, pos)
        if not match:
            raise CatalogError('Invalid/unsupported printf placeholder')
        flags, width, precision, modifier, kind = match.groups()
        if kind == b'%':
            if flags or width or precision or modifier:
                raise CatalogError('Invalid literal percent placeholder')
        else:
            if kind == b'n':
                raise CatalogError('%n must not appear in translated messages')
            if width == b'*':
                result.append(b'int')
            if precision == b'*':
                result.append(b'int')
            if kind in (b'd', b'i'):
                kind = b'd'
            result.append((modifier or b'') + kind)
        pos = match.end()
    return tuple(result)


def load_catalog(root: Path) -> Catalog:
    cd, ids, english = parse_source(root / '_Catalogs/AmiMAIL.cd', True)
    ct, _, translated = parse_source(root / '_Catalogs/deutsch/AmiMAIL.ct', False)
    if set(cd) != {'version', 'language', 'basename'} or set(ct) != {'version', 'language', 'codeset'}:
        raise CatalogError('Missing catalog directives')
    if cd['language'] != 'english' or cd['basename'] != 'AmiMAIL' or ct['language'] != 'deutsch':
        raise CatalogError('Unexpected language or basename')
    try:
        version, codeset = int(cd['version']), int(ct['codeset'])
    except ValueError as exc:
        raise CatalogError('Invalid version or codeset') from exc
    match = re.fullmatch(r'\$VER: AmiMAIL\.catalog (\d+)\.(\d+) \((\d{2}\.\d{2}\.\d{4})\)', ct['version'])
    if not match or int(match.group(1)) != version or not 1 <= version <= 65535:
        raise CatalogError('CD/CT catalog version mismatch')
    if codeset != 0:
        raise CatalogError('AmiMAIL currently uses codeset 0 with per-message byte encodings')
    if set(ids) != set(translated):
        raise CatalogError('CD/CT message sets differ: ' + ', '.join(sorted(set(ids) ^ set(translated))))
    header = (root / 'include/catalog_ids.h').read_text(encoding='ascii')
    definitions = re.findall(r'^#define\s+(MSG_[A-Z0-9_]+)\s+(\d+)L\s*$', header, re.M)
    if len(definitions) != len(dict(definitions)):
        raise CatalogError('Duplicate message name in catalog_ids.h')
    if {name: int(value) for name, value in definitions} != ids:
        raise CatalogError('CD and catalog_ids.h numeric IDs differ')
    macro = re.findall(r'^#define\s+AMIMAIL_CATALOG_VERSION\s+(\d+)UL\s*$', header, re.M)
    if macro != [str(version)]:
        raise CatalogError('AMIMAIL_CATALOG_VERSION differs from CD/CT')
    runtime = (root / 'src/i18n.c').read_text(encoding='utf-8')
    if not re.search(r'tags\[2\]\.ti_Data\s*=\s*AMIMAIL_CATALOG_VERSION\s*;', runtime):
        raise CatalogError('i18n.c must use AMIMAIL_CATALOG_VERSION')
    for name, original in english.items():
        if format_arguments(original) != format_arguments(translated[name]):
            raise CatalogError(f'{name}: English/German printf argument mismatch')
    return Catalog(version, ct['version'], ct['language'], codeset, ids, english, translated)


# These are the deliberately scoped corrections from the 2.1.0 locale audit.
# This is not a claim that every UI string in the C code has been localized.
AUDITED_SOURCE_IDS = {
    'MSG_REACTION_WINDOW_COULD_NOT_BE_CREATED': 'src/gui_window.c',
    'MSG_GUI_ARROW_IMAGES_COULD_NOT_BE_CREATED': 'src/gui.c',
    'MSG_GRAPHIC_PLACEHOLDER_UTF8': 'src/mime.c',
    'MSG_GRAPHIC_PLACEHOLDER_LOCAL': 'src/codec.c',
    'MSG_ATTACHMENT_SELECTION': 'src/gui_attachments.c',
    'MSG_ATTACHMENT_NAME': 'src/gui_attachments.c',
    'MSG_SELECT_NONE': 'src/gui_attachments.c',
}

# Keep comments/character literals separate, so quoted text inside a comment
# is never mistaken for an output string. This lexer does not preprocess C.
SOURCE_TOKEN = re.compile(
    r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|'
    r"'(?:\\.|[^'\\])*'|[A-Za-z_][A-Za-z_0-9]*|[^\s]",
    re.S,
)


def source_arguments(tokens: list, opening: int):
    """Return token ranges for a balanced call; skip macros we cannot parse."""
    arguments, stack = [], ['(']
    start = opening + 1
    closing = {')': '(', ']': '[', '}': '{'}
    for index in range(start, len(tokens)):
        token = tokens[index][0]
        if token in ('(', '[', '{'):
            stack.append(token)
        elif token in closing:
            if not stack or stack[-1] != closing[token]:
                return None
            stack.pop()
            if not stack:
                arguments.append((start, index))
                return arguments, index
        elif token == ',' and len(stack) == 1:
            arguments.append((start, index))
            start = index + 1
    return None


def source_argument_tokens(tokens: list, bounds: tuple) -> list:
    values = [token for token, _ in tokens[bounds[0]:bounds[1]]]
    # Wrappers such as amg_tr((MSG_...), ("text")) are common in C macros.
    while len(values) >= 2 and values[0] == '(' and values[-1] == ')':
        values = values[1:-1]
    return values


def check_source_lookups(root: Path, catalog: Catalog) -> int:
    """Check static MSG lookups and guard the named locale corrections.

    All literal T/amg_tr/amg_tr_snprintf IDs in src/*.c must exist. For the
    audited IDs, the literal English fallback must match the CD exactly.
    The new diagnostic/graphic texts cannot reappear as untranslated literals
    in their respective source modules. Comments, protocol data and dynamic
    lookups are not a full localization audit and are not treated as errors.
    """
    for key, relative in AUDITED_SOURCE_IDS.items():
        if key not in catalog.ids or not (root / relative).is_file():
            raise CatalogError(f'Missing locale-audit input: {key} / {relative}')
    seen, lookups = set(), 0
    marker_ids = {'MSG_GRAPHIC_PLACEHOLDER_UTF8', 'MSG_GRAPHIC_PLACEHOLDER_LOCAL'}
    protected_ids = marker_ids | {
        'MSG_REACTION_WINDOW_COULD_NOT_BE_CREATED',
        'MSG_GUI_ARROW_IMAGES_COULD_NOT_BE_CREATED',
    }
    for path in sorted((root / 'src').glob('*.c')):
        source = path.read_text(encoding='utf-8')
        relative = path.relative_to(root).as_posix()
        tokens = [(match.group(), match.start())
                  for match in SOURCE_TOKEN.finditer(source)
                  if not match.group().startswith(('//', '/*'))]
        allowed = []
        for index in range(len(tokens) - 1):
            function = tokens[index][0]
            if function not in ('T', 'amg_tr', 'amg_tr_snprintf') or tokens[index + 1][0] != '(':
                continue
            parsed = source_arguments(tokens, index + 1)
            if parsed is None:
                continue
            arguments, _ = parsed
            id_index, text_index = (2, 3) if function == 'amg_tr_snprintf' else (0, 1)
            if len(arguments) <= text_index:
                continue
            names = source_argument_tokens(tokens, arguments[id_index])
            if len(names) != 1 or not re.fullmatch(r'MSG_[A-Z0-9_]+', names[0]):
                continue
            name = names[0]
            line = source.count('\n', 0, tokens[index][1]) + 1
            if name not in catalog.ids:
                raise CatalogError(f'{relative}:{line}: unknown catalog ID {name}')
            lookups += 1
            if name not in AUDITED_SOURCE_IDS:
                continue
            literals = source_argument_tokens(tokens, arguments[text_index])
            if not literals or not all(value.startswith('"') for value in literals):
                raise CatalogError(f'{relative}:{line}: audited fallback must be a string literal')
            fallback = b''.join(decode_string(value[1:-1]) for value in literals)
            if fallback != catalog.english[name]:
                raise CatalogError(f'{relative}:{line}: English fallback differs from CD: {name}')
            if relative == AUDITED_SOURCE_IDS[name]:
                seen.add(name)
            begin, end = arguments[text_index]
            allowed.append((begin, end, name))

        protected = {catalog.english[key] for key in protected_ids
                     if AUDITED_SOURCE_IDS[key] == relative}
        protected |= {catalog.translated[key] for key in protected_ids
                      if AUDITED_SOURCE_IDS[key] == relative}
        if not protected:
            continue
        for index, (token, position) in enumerate(tokens):
            if not token.startswith('"'):
                continue
            try:
                value = decode_string(token[1:-1])
            except CatalogError:
                continue  # Unrelated C escapes are outside this scoped check.
            if value in protected and not any(
                begin <= index < end and key in protected_ids
                for begin, end, key in allowed
            ):
                line = source.count('\n', 0, position) + 1
                raise CatalogError(f'{relative}:{line}: untranslated protected UI literal')
    missing = set(AUDITED_SOURCE_IDS) - seen
    if missing:
        raise CatalogError('Audited source lookup missing: ' + ', '.join(sorted(missing)))
    return lookups


def chunk(tag: bytes, data: bytes) -> bytes:
    return tag + struct.pack('>I', len(data)) + data + b'\0' * (len(data) & 1)


def build_bytes(catalog: Catalog) -> bytes:
    records = bytearray()
    for name, number in catalog.ids.items():
        value = catalog.translated[name] + b'\0'
        records.extend(struct.pack('>II', number, len(value)))
        records.extend(value)
        records.extend(b'\0' * (-len(value) % 4))
    payload = (b'CTLG' + chunk(b'FVER', catalog.version_string.encode('ascii') + b'\0') +
               chunk(b'LANG', catalog.language.encode('ascii') + b'\0') +
               chunk(b'CSET', struct.pack('>8I', catalog.codeset, 0, 0, 0, 0, 0, 0, 0)) +
               chunk(b'STRS', bytes(records)))
    return b'FORM' + struct.pack('>I', len(payload)) + payload


def read_binary(data: bytes) -> Tuple[dict, dict]:
    """Also accepts STRS lengths excluding NUL, as emitted by FlexCat."""
    if len(data) < 12 or data[:4] != b'FORM' or data[8:12] != b'CTLG':
        raise CatalogError('Not an IFF CTLG catalog')
    if struct.unpack_from('>I', data, 4)[0] != len(data) - 8:
        raise CatalogError('Invalid FORM length')
    chunks, records = {}, {}
    pos = 12
    while pos < len(data):
        if len(data) - pos < 8:
            raise CatalogError('Truncated IFF chunk header')
        tag, length = data[pos:pos+4], struct.unpack_from('>I', data, pos + 4)[0]
        pos += 8
        if tag in chunks or length > len(data) - pos:
            raise CatalogError('Duplicate/truncated IFF chunk')
        chunks[tag] = data[pos:pos + length]
        pos += length + (length & 1)
    if pos != len(data) or not {b'FVER', b'LANG', b'CSET', b'STRS'} <= set(chunks):
        raise CatalogError('Missing chunks or invalid alignment')
    strings = chunks[b'STRS']
    pos = 0
    while pos < len(strings):
        if len(strings) - pos < 8:
            raise CatalogError('Truncated STRS header')
        number, length = struct.unpack_from('>II', strings, pos)
        pos += 8
        padded = (length + 3) & ~3
        if not length or padded > len(strings) - pos or number in records:
            raise CatalogError('Invalid STRS length or duplicate ID')
        value = strings[pos:pos + padded]
        nul = value.find(b'\0')
        if nul < 0 or nul > length or any(value[nul:]):
            raise CatalogError('Unterminated catalog string or invalid padding')
        records[number] = value[:nul]
        pos += padded
    if len(chunks[b'CSET']) != 32:
        raise CatalogError('Invalid CSET length')
    return chunks, records


def verify_binary(data: bytes, catalog: Catalog) -> None:
    chunks, records = read_binary(data)
    if chunks[b'FVER'] != catalog.version_string.encode('ascii') + b'\0':
        raise CatalogError('Stale catalog version/revision: rebuild the binary')
    if chunks[b'LANG'] != catalog.language.encode('ascii') + b'\0':
        raise CatalogError('Binary catalog language mismatch')
    if chunks[b'CSET'] != struct.pack('>8I', catalog.codeset, 0, 0, 0, 0, 0, 0, 0):
        raise CatalogError('Binary catalog codeset mismatch')
    expected = {number: catalog.translated[name] for name, number in catalog.ids.items()}
    if records != expected:
        raise CatalogError('Binary catalog has missing, stale or unexpected translations')


def build_file(destination: Path, catalog: Catalog) -> None:
    data = build_bytes(catalog)
    verify_binary(data, catalog)
    if destination.exists() and destination.read_bytes() == data:
        return  # Reproducible: avoid gratuitous timestamp changes.
    destination.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(prefix=destination.name + '.', suffix='.new', dir=destination.parent)
    try:
        with os.fdopen(descriptor, 'wb') as stream:
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        verify_binary(Path(temporary).read_bytes(), catalog)
        os.replace(temporary, destination)
    finally:
        if os.path.exists(temporary):
            os.unlink(temporary)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    operation = parser.add_mutually_exclusive_group(required=True)
    operation.add_argument('--build', action='store_true')
    operation.add_argument('--check', action='store_true')
    args = parser.parse_args()
    try:
        catalog = load_catalog(args.root)
        lookup_count = check_source_lookups(args.root, catalog)
        destination = args.root / '_Catalogs/deutsch/AmiMAIL.catalog'
        if args.build:
            build_file(destination, catalog)
        verify_binary(destination.read_bytes(), catalog)
        print(f'Catalog v{catalog.version}: {len(catalog.ids)} IDs, translations and binary verified.')
        print(f'Source checks: {lookup_count} static lookup IDs and '
              f'{len(AUDITED_SOURCE_IDS)} audited fallback IDs verified.')
        return 0
    except (OSError, UnicodeError, CatalogError) as exc:
        print(f'Catalog error: {exc}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
