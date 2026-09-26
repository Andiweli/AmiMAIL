"""Catalog checks use temporary copies; no installed/source catalogs are altered."""
import copy
import importlib.util
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parent.parent
SPEC = importlib.util.spec_from_file_location('catalog_tool', ROOT / 'tools/catalog_tool.py')
TOOL = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = TOOL
SPEC.loader.exec_module(TOOL)


class CatalogTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        for relative in ('_Catalogs/AmiMAIL.cd', '_Catalogs/deutsch/AmiMAIL.ct',
                         'include/catalog_ids.h', 'src/i18n.c'):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        self.catalog = TOOL.load_catalog(self.root)

    def change(self, relative, old, new):
        path = self.root / relative
        text = path.read_text(encoding='ascii')
        self.assertIn(old, text)
        path.write_text(text.replace(old, new, 1), encoding='ascii')

    def test_roundtrip_all_translations(self):
        binary = TOOL.build_bytes(self.catalog)
        TOOL.verify_binary(binary, self.catalog)
        _, records = TOOL.read_binary(binary)
        self.assertEqual(len(records), len(self.catalog.ids))
        self.assertEqual(records[self.catalog.ids['MSG_EMBEDDED_GRAPHICS_7F31']],
                         b'\n\nEingebettete Grafiken:\n')
        self.assertIn(b'sp\xe4ter', records[self.catalog.ids['MSG_THE_MAIL_CAN_BE_EDITED_LATER']])
        self.assertIn(b'\xc3\xbc', records[self.catalog.ids['MSG_MIME_MESSAGE_TOO_LARGE']])

    def test_reproducible_build_and_readback(self):
        path = self.root / 'AmiMAIL.catalog'
        TOOL.build_file(path, self.catalog)
        before, stamp = path.read_bytes(), path.stat().st_mtime_ns
        TOOL.build_file(path, self.catalog)
        self.assertEqual(path.read_bytes(), before)
        self.assertEqual(path.stat().st_mtime_ns, stamp)

    def test_failed_write_preserves_old_file(self):
        path = self.root / 'AmiMAIL.catalog'
        path.write_bytes(b'old')
        with mock.patch.object(TOOL.os, 'replace', side_effect=OSError('injected')):
            with self.assertRaises(OSError):
                TOOL.build_file(path, self.catalog)
        self.assertEqual(path.read_bytes(), b'old')
        self.assertFalse(list(self.root.glob('*.new')))

    def test_stale_translation(self):
        stale = copy.deepcopy(self.catalog)
        stale.translated['MSG_EMBEDDED_GRAPHICS_7F31'] = b'wrong'
        with self.assertRaises(TOOL.CatalogError):
            TOOL.verify_binary(TOOL.build_bytes(stale), self.catalog)

    def test_missing_translation_in_binary(self):
        stale = copy.deepcopy(self.catalog)
        del stale.ids['MSG_EMBEDDED_GRAPHICS_7F31']
        with self.assertRaises(TOOL.CatalogError):
            TOOL.verify_binary(TOOL.build_bytes(stale), self.catalog)

    def test_transfer_and_selection_translations(self):
        _, records = TOOL.read_binary(TOOL.build_bytes(self.catalog))
        self.assertEqual(self.catalog.version, 9)
        for key in ('MSG_ATTACHMENT_SELECTION', 'MSG_SELECT_REGULAR_ATTACHMENTS',
                    'MSG_SELECT_EMBEDDED_GRAPHICS', 'MSG_TRANSFER_CANCELLED',
                    'MSG_LOCAL_COPY_KEPT', 'MSG_DELIVERY_UNCERTAIN'):
            self.assertIn(key, self.catalog.ids)
            self.assertEqual(records[self.catalog.ids[key]], self.catalog.translated[key])
        self.assertNotEqual(self.catalog.english['MSG_ATTACHMENT_SELECTION'],
                            self.catalog.translated['MSG_ATTACHMENT_SELECTION'])

    def test_stale_binary_version(self):
        stale = copy.deepcopy(self.catalog)
        stale.version_string = '$VER: AmiMAIL.catalog 6.0 (10.09.2026)'
        with self.assertRaises(TOOL.CatalogError):
            TOOL.verify_binary(TOOL.build_bytes(stale), self.catalog)

    def test_corrupt_chunk_lengths_and_nul(self):
        binary = bytearray(TOOL.build_bytes(self.catalog))
        for offset in (4, 16):
            bad = binary[:]
            struct.pack_into('>I', bad, offset, 0xffffffff)
            with self.assertRaises(TOOL.CatalogError):
                TOOL.read_binary(bytes(bad))
        for length in (0, 4, 8, 12, len(binary) - 1):
            with self.assertRaises(TOOL.CatalogError):
                TOOL.read_binary(bytes(binary[:length]))

    def test_duplicate_cd_name(self):
        path = self.root / '_Catalogs/AmiMAIL.cd'
        with path.open('a', encoding='ascii') as stream:
            stream.write(';\nMSG_YES (9999//)\nYes\n')
        with self.assertRaises(TOOL.CatalogError):
            TOOL.load_catalog(self.root)

    def test_duplicate_numeric_id(self):
        path = self.root / '_Catalogs/AmiMAIL.cd'
        with path.open('a', encoding='ascii') as stream:
            stream.write(';\nMSG_DUPLICATE (%d//)\nYes\n' % self.catalog.ids['MSG_YES'])
        with self.assertRaises(TOOL.CatalogError):
            TOOL.load_catalog(self.root)

    def test_missing_ct_id(self):
        self.change('_Catalogs/deutsch/AmiMAIL.ct', 'MSG_YES\n_Ja', 'MSG_NEW_ID\n_Ja')
        with self.assertRaises(TOOL.CatalogError):
            TOOL.load_catalog(self.root)

    def test_printf_argument_mismatch(self):
        self.change('_Catalogs/deutsch/AmiMAIL.ct', '%lu Kontakte', '%s Kontakte')
        with self.assertRaises(TOOL.CatalogError):
            TOOL.load_catalog(self.root)

    def test_header_id_mismatch(self):
        self.change('include/catalog_ids.h', '#define MSG_YES 12675763L', '#define MSG_YES 9999L')
        with self.assertRaises(TOOL.CatalogError):
            TOOL.load_catalog(self.root)

    def test_runtime_version_mismatch(self):
        self.change('src/i18n.c', 'tags[2].ti_Data=AMIMAIL_CATALOG_VERSION;', 'tags[2].ti_Data=6UL;')
        with self.assertRaises(TOOL.CatalogError):
            TOOL.load_catalog(self.root)

    def test_header_version_mismatch(self):
        self.change('include/catalog_ids.h', f'AMIMAIL_CATALOG_VERSION {self.catalog.version}UL', 'AMIMAIL_CATALOG_VERSION 6UL')
        with self.assertRaises(TOOL.CatalogError):
            TOOL.load_catalog(self.root)

    def test_cd_ct_version_mismatch(self):
        self.change('_Catalogs/AmiMAIL.cd', f'## version {self.catalog.version}', '## version 6')
        with self.assertRaises(TOOL.CatalogError):
            TOOL.load_catalog(self.root)

    def test_escapes_preserve_bytes(self):
        self.assertEqual(TOOL.decode_string(r'\344\303\244\n\\'), b'\xe4\xc3\xa4\n\\')
        for invalid in (r'\000', r'\400', r'\q', 'x\\', '\u00e4'):
            with self.assertRaises(TOOL.CatalogError):
                TOOL.decode_string(invalid)

    def test_unsupported_directive(self):
        self.change('_Catalogs/AmiMAIL.cd', '## basename', '## unsupported')
        with self.assertRaises(TOOL.CatalogError):
            TOOL.load_catalog(self.root)

    def test_flexcat_style_nul_excluded_from_length(self):
        # Independent literal fixture, not produced by build_bytes().
        records = struct.pack('>II', 123, 3) + b'abc\0'
        payload = (b'CTLG' + TOOL.chunk(b'FVER', b'$VER: test 1.0\0') +
                   TOOL.chunk(b'LANG', b'deutsch\0') + TOOL.chunk(b'CSET', bytes(32)) +
                   TOOL.chunk(b'STRS', records))
        _, decoded = TOOL.read_binary(b'FORM' + struct.pack('>I', len(payload)) + payload)
        self.assertEqual(decoded, {123: b'abc'})


    def copy_audited_sources(self):
        for relative in set(TOOL.AUDITED_SOURCE_IDS.values()):
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)

    def test_new_error_and_marker_translations(self):
        _, records = TOOL.read_binary(TOOL.build_bytes(self.catalog))
        expected = {
            'MSG_REACTION_WINDOW_COULD_NOT_BE_CREATED':
                b'ReAction-Fenster konnte nicht erzeugt werden.',
            'MSG_GUI_ARROW_IMAGES_COULD_NOT_BE_CREATED':
                b'Pfeilgrafiken konnten nicht erzeugt werden.',
            'MSG_GRAPHIC_PLACEHOLDER_UTF8': b'[Grafik]',
            'MSG_GRAPHIC_PLACEHOLDER_LOCAL': b'[Grafik]',
        }
        for name, german in expected.items():
            self.assertEqual(records[self.catalog.ids[name]], german)
        self.assertEqual(self.catalog.english['MSG_GRAPHIC_PLACEHOLDER_UTF8'], b'[Graphic]')
        self.assertNotEqual(self.catalog.ids['MSG_GRAPHIC_PLACEHOLDER_UTF8'],
                            self.catalog.ids['MSG_GRAPHIC_PLACEHOLDER_LOCAL'])

    def test_source_lookup_audit_passes(self):
        self.copy_audited_sources()
        self.assertGreater(TOOL.check_source_lookups(self.root, self.catalog), 10)

    def test_source_fallback_mismatch_fails(self):
        self.copy_audited_sources()
        self.change('src/gui_attachments.c',
                    'T(MSG_ATTACHMENT_SELECTION, "Select attachments to save")',
                    'T(MSG_ATTACHMENT_SELECTION, "Select attachments")')
        with self.assertRaisesRegex(TOOL.CatalogError, 'differs from CD'):
            TOOL.check_source_lookups(self.root, self.catalog)

    def test_direct_german_output_fails(self):
        self.copy_audited_sources()
        path = self.root / 'src/mime.c'
        with path.open('a', encoding='ascii') as stream:
            stream.write('\nconst char *bad_marker(void) { return "[Grafik]"; }\n')
        with self.assertRaisesRegex(TOOL.CatalogError, 'untranslated protected'):
            TOOL.check_source_lookups(self.root, self.catalog)

    def test_direct_english_error_fails(self):
        self.copy_audited_sources()
        path = self.root / 'src/gui.c'
        with path.open('a', encoding='ascii') as stream:
            stream.write('\nconst char *bad_error(void) { return "GUI arrow images could not be created."; }\n')
        with self.assertRaisesRegex(TOOL.CatalogError, 'untranslated protected'):
            TOOL.check_source_lookups(self.root, self.catalog)

    def test_unknown_source_id_fails(self):
        self.copy_audited_sources()
        path = self.root / 'src/gui.c'
        with path.open('a', encoding='ascii') as stream:
            stream.write('\nconst char *bad_id(void) { return T(MSG_NONEXISTENT_LOCALE_ID, "Unknown"); }\n')
        with self.assertRaisesRegex(TOOL.CatalogError, 'unknown catalog ID'):
            TOOL.check_source_lookups(self.root, self.catalog)

    def test_comment_is_not_an_output(self):
        self.copy_audited_sources()
        path = self.root / 'src/mime.c'
        with path.open('a', encoding='ascii') as stream:
            stream.write('\n/* "[Grafik]" T(MSG_MISSING, "Example") */\n')
        TOOL.check_source_lookups(self.root, self.catalog)

    def test_wrong_marker_encoding_path_fails(self):
        self.copy_audited_sources()
        self.change('src/codec.c', 'MSG_GRAPHIC_PLACEHOLDER_LOCAL',
                    'MSG_GRAPHIC_PLACEHOLDER_UTF8')
        with self.assertRaisesRegex(TOOL.CatalogError, 'lookup missing'):
            TOOL.check_source_lookups(self.root, self.catalog)

    def test_missing_audited_module_fails(self):
        self.copy_audited_sources()
        (self.root / 'src/gui_window.c').unlink()
        with self.assertRaisesRegex(TOOL.CatalogError, 'Missing locale-audit input'):
            TOOL.check_source_lookups(self.root, self.catalog)

    def test_previous_v8_catalog_rejected_by_build_check(self):
        stale = copy.deepcopy(self.catalog)
        stale.version_string = '$VER: AmiMAIL.catalog 8.0 (26.09.2026)'
        with self.assertRaises(TOOL.CatalogError):
            TOOL.verify_binary(TOOL.build_bytes(stale), self.catalog)



if __name__ == '__main__':
    unittest.main()
