#!/usr/bin/env python3
"""Herald production-code tests. Native APIs are explicit doubles, not an SDK."""
import importlib.util
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parent.parent
HEADERS = """devices/timer.h exec/io.h exec/libraries.h exec/memory.h exec/ports.h
rexx/storage.h rexx/errors.h proto/exec.h proto/rexxsyslib.h""".split()


def c_string(value):
    return '"' + ''.join('\\%03o' % byte if byte < 32 or byte >= 127 or byte in (34, 92)
                          else chr(byte) for byte in value) + '"'


def check_wiring():
    dialogs = (ROOT / 'src/gui_dialogs.c').read_text()
    account = dialogs.split(' int account_dialog(', 1)[1].split(
        'static UWORD about_header_fill_pattern', 1)[0]
    assert 'GID_ACCOUNT_HERALD,GID_ACCOUNT_HERALD_TEST' in dialogs
    assert 'page.herald_notifications = herald_notifications_gadget;' in account
    assert 'GetAttr(GA_Selected, (Object *)page->herald_notifications, &selected);' in dialogs
    assert 'candidate->herald_notifications = selected ? 1 : 0;' in dialogs
    assert 'SetGadgetAttrs(page->herald_notifications, window, NULL, GA_Selected,' in dialogs
    assert 'left->herald_notifications == right->herald_notifications' in dialogs
    assert 'Wait(signal_mask | herald_mask | SIGBREAKF_CTRL_C)' in account
    assert account.index('herald_notifications_gadget = native_checkbox(') < account.index('GA_ID, GID_ACCOUNT_STATUS,')
    assert account.index('GA_ID, GID_ACCOUNT_STATUS,') < account.index('GA_ID, GID_ACCOUNT_SAVE,')
    assert 'if (herald_test_account == active_tab)' in account
    assert 'string_text(account_name_gadget)' in account
    assert 'amg_herald_discard_account(' in account
    # No changes to the pre-open centering fix or account-order click guard.
    assert '(LONG)gui->screen->WBorBottom;' in account
    assert 'if (!account_order_click_take(' in account
    actions = (ROOT / 'src/gui_actions.c').read_text()
    calls = re.findall(r'if \(had_baseline && !generation_changed(?: &&\s*notify_count > 0U)?\)\s*'
                       r'gui_herald_new_mail\(gui, (gui->active_account|account_index),\s*'
                       r'\(unsigned long\)notify_count, event.payload,\s*'
                       r'event.payload_length, (previous_uid|old_uid)\);', actions)
    assert calls == [('gui->active_account', 'previous_uid'), ('gui->active_account', 'previous_uid'), ('account_index', 'old_uid')], calls
    assert actions.count('gui_herald_new_mail(') == 3
    assert 'gui_notify_new_mail(gui);' in actions and 'gui_notify_new_mail_for_account(gui, account);' in actions
    runtime = (ROOT / 'src/gui_runtime.c').read_text()
    assert 'amg_herald_signal_mask(gui->herald)' in runtime
    assert 'amg_herald_poll(gui->herald);' in runtime
    cleanup = (ROOT / 'src/gui.c').read_text().split('void amg_gui_destroy(AmgGui *gui)', 1)[1]
    assert cleanup.index('amg_herald_destroy(gui->herald);') < cleanup.index('DisposeObject(gui->window_object)')
    client = (ROOT / 'src/herald.c').read_text()
    assert 'WaitPort(' not in client and 'SystemTags(' not in client
    assert 'UNREGISTERAPP APP=' not in client and 'BACKRXMSG=' not in client
    # All normal operation is asynchronous. Wait is restricted to bounded exit.
    assert 'Wait(' not in client.split('void amg_herald_destroy(AmgHerald *client)', 1)[0]
    assert '250000UL' in client and 'PA_IGNORE' in client


def main():
    check_wiring()
    spec = importlib.util.spec_from_file_location('herald_catalog_tool', ROOT / 'tools/catalog_tool.py')
    catalog_tool = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = catalog_tool
    spec.loader.exec_module(catalog_tool)
    catalog = catalog_tool.load_catalog(ROOT)
    assert catalog.version == 12
    catalog_tool.verify_binary((ROOT / '_Catalogs/deutsch/AmiMAIL.catalog').read_bytes(), catalog)
    rows = ['struct TestTranslation { long id; const char *de; };',
            'static const struct TestTranslation translations[] = {']
    for key, number in catalog.ids.items():
        if key.startswith('MSG_HERALD_') or key in ('MSG_ACCOUNT', 'MSG_NO_SUBJECT'):
            rows.append('    { %dL, %s },' % (number, c_string(catalog.translated[key])))
    rows.append('};\n')
    cc = shlex.split(os.environ.get('HOST_CC', 'gcc'))
    flags = ['-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Wshadow', '-Wpointer-arith',
             '-Wstrict-prototypes', '-Wmissing-prototypes', '-Wformat=2', '-Werror']
    flags += shlex.split(os.environ.get('HERALD_TEST_FLAGS', ''))
    is_clang = 'clang' in subprocess.check_output(cc + ['--version'], text=True).lower()
    with tempfile.TemporaryDirectory(prefix='amimail-herald-') as tmp:
        directory = Path(tmp)
        for name in HEADERS:
            header = directory / name
            header.parent.mkdir(parents=True, exist_ok=True)
            header.write_text('/* Explicit API double: tests/herald_native_double.h */\n')
        (directory / 'herald_translations.inc').write_text('\n'.join(rows), encoding='ascii')
        binary = directory / 'herald-native-tests'
        subprocess.run(cc + flags + ['-I' + str(directory), '-Iinclude', '-Itests',
                       'tests/test_herald_native.c', 'src/mail_notice.c', 'src/common.c',
                       'src/buffer.c', 'src/codec.c', 'src/charset.c', 'src/mime.c',
                       'src/imap_parser.c', '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=directory, check=True, timeout=30)
        # Real account serialization and backward compatibility, not a storage double.
        makefile = (ROOT / 'Makefile').read_text()
        source_block = makefile.split('HOST_SOURCES := ', 1)[1].split('\nHOST_TEST :=', 1)[0]
        sources = source_block.replace('\\\n', ' ').split()
        binary = directory / 'herald-settings-tests'
        if is_clang:
            # Existing i18n.c deliberately passes a validated catalog format
            # to vsnprintf. This legacy helper is unchanged by the patch.
            # Narrow the Clang-only exception to that translation unit; all
            # Herald and changed application code retains -Wformat=2 -Werror.
            legacy_i18n = directory / 'i18n.o'
            subprocess.run(cc + flags + ['-Wno-format-nonliteral', '-Iinclude',
                           '-c', 'src/i18n.c', '-o', str(legacy_i18n)],
                           cwd=ROOT, check=True)
            sources = [s for s in sources if s != 'src/i18n.c'] + [str(legacy_i18n)]
        subprocess.run(cc + flags + ['-Iinclude', 'tests/test_herald_settings.c'] +
                       sources + ['-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=directory, check=True, timeout=30)
    print('Herald configuration/event/catalog wiring checks passed.')


if __name__ == '__main__':
    main()
