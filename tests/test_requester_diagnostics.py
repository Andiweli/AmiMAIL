#!/usr/bin/env python3
"""Test the actual requester/logger/delete/draft branches with API doubles.

The snippets are extracted, not reimplemented; no Amiga runtime/ABI claim.
Ordered constructors, ownership, close outcomes and layout tags are checked.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent

def fragment(source, first, end):
    start = source.index(first)
    return source[start:source.index(end, start)]

def production():
    dialogs = (ROOT/'src/gui_dialogs.c').read_text()
    actions = (ROOT/'src/gui_actions.c').read_text()
    compose = (ROOT/'src/gui_compose.c').read_text()
    diag = (ROOT/'src/gui_requester_diag.c').read_text()
    diag = re.sub(r'^#include[^\n]*\n', '', diag, flags=re.M)
    requester = fragment(dialogs, 'typedef struct ConfirmationObjects {',
                         '#endif /* AMIGMAIL_AMIGA */')
    declarations = fragment((ROOT/'src/gui_internal.h').read_text(),
        'typedef enum GuiConfirmationResult {',
        'int confirm_question_dialog_for_window(') + '''
void gui_requester_trace(const AmgGui *, const char *, unsigned long, unsigned long);
void gui_requester_failure(AmgGui *, unsigned int, long);
int confirm_question_dialog_for_window(AmgGui *, struct Window *, const char *, const char *, LONG);
int confirm_delete_dialog(AmgGui *);
int confirm_empty_trash_dialog(AmgGui *);
int confirm_empty_spam_dialog(AmgGui *);
'''
    key = fragment(dialogs, 'static int requester_rawkey_accept(',
                    'static int requester_string_accept_code(')
    delete = fragment(actions, 'static void delete_selected_messages(',
                      'static void empty_trash(')
    close = fragment(compose, 'typedef enum ComposeCloseResult {',
                     'int compose_dialog(')
    wrapper = """
static void test_compose_cancel(AmgGui *gui, int *out_done, int *out_submitted)
{
    AmgError error={AMG_ERR_IO,"Queue failed"};
    ComposeCloseResult choice = compose_confirm_close(gui, &main_window,
        NULL,NULL,NULL,NULL,NULL,NULL,NULL,0U,COMPOSE_MODE_NEW,NULL,&error);
    *out_done = choice != COMPOSE_CLOSE_KEEP;
    *out_submitted = choice == COMPOSE_CLOSE_SAVED;
}
"""
    return declarations + '\n' + diag + '\n' + key + requester + delete + close + wrapper


def wiring():
    dialogs = (ROOT/'src/gui_dialogs.c').read_text()
    actions = (ROOT/'src/gui_actions.c').read_text()
    requester = fragment(dialogs, 'typedef struct ConfirmationObjects {',
                         'static int confirm_question_dialog(')
    assert 'ButtonObject' not in requester and 'WindowObject' not in requester
    assert 'NewObject(' not in requester
    assert 'CHILD_NoDispose' in requester
    assert requester.index('NewObjectA(window_class, NULL, tags)') < requester.index(
        'SetAttrs(objects.dialog, WINDOW_Layout')
    assert 'note && *note' in requester
    assert 'GUI_CONFIRM_NO' in requester and 'GUI_CONFIRM_DISMISSED' in requester
    assert 'return GUI_CONFIRM_FAILED;' in requester
    assert 'note_height = font_height;' in requester
    assert '((LONG)ref_window->Width - outer_width) / 2L' in requester
    dispatch = actions[actions.index('void handle_main_gadget('):]
    assert dispatch.index('"delete.dispatch"') < dispatch.index('gui_reply_popup_close(gui);')
    assert 'src/gui_requester_diag.c' in (ROOT/'Makefile').read_text()
    print('Direct requester classes, ownership, close outcomes and centering wiring verified.')

def main():
    wiring()
    cc = shlex.split(os.environ.get('HOST_CC', 'gcc'))
    flags = ['-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Wshadow',
             '-Wpointer-arith', '-Wstrict-prototypes', '-Wmissing-prototypes',
             '-Wformat=2', '-Werror', '-D__amigaos__']
    flags += shlex.split(os.environ.get('REQUESTER_TEST_FLAGS', ''))
    with tempfile.TemporaryDirectory(prefix='amimail-requester-tests-') as name:
        path = Path(name)
        (path/'requester_production.inc').write_text(production())
        binary = path/'requester-tests'
        subprocess.run(cc+flags+['-I'+name,'-Iinclude',
                       'tests/test_requester_diagnostics.c','-o',str(binary)],
                       cwd=ROOT,check=True)
        subprocess.run([str(binary)],cwd=ROOT,check=True,timeout=20)

if __name__ == '__main__':
    main()
