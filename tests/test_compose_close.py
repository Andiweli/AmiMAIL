#!/usr/bin/env python3
"""Exercise the production composer event loop and editor ESC dispatcher.

Native API test doubles, not an Amiga SDK or a ReAction rendering test.
The loop/helper/dispatcher are extracted verbatim from gui_compose.c.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def part(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


def production():
    source = (ROOT / 'src/gui_compose.c').read_text()
    header = (ROOT / 'src/gui_internal.h').read_text()
    enum = part(header, 'typedef enum GuiConfirmationResult {',
                'GuiConfirmationResult confirm_question_result_for_window(')
    ids = part(source, 'enum ComposeGadgetId {', 'static int build_from_header(')
    keys = '\n'.join(re.findall(r'^#define COMPOSE_(?:RAWKEY_\w+|CLOSE_VERIFY_CODE)[^\n]+',
                               source, re.M))
    dispatcher = part(source, 'static ULONG compose_texteditor_dispatcher(',
                      'static Class *compose_make_texteditor_class(')
    close = part(source, 'typedef enum ComposeCloseResult {', 'int compose_dialog(')
    body = source[source.index('int compose_dialog('):]
    loop = part(body, '    while (!done) {', '    compose_state_save(window);')
    assert 'RA_HandleInput(dialog, &code)' in loop
    assert 'case GID_COMPOSE_BODY:' in loop
    assert loop.count('compose_confirm_close(') == 1
    assert body.index('GA_RelVerify, TRUE,', body.index('GA_ID, GID_COMPOSE_BODY')) < body.index(
        'GA_TEXTEDITOR_Contents,', body.index('GA_ID, GID_COMPOSE_BODY'))
    assert 'gui->window ? gui->window : window' in close
    declarations = enum + ids + keys + '''
static GuiConfirmationResult confirm_question_result_for_window(
    AmgGui *, struct Window *, const char *, const char *, LONG);
'''
    wrapper = '''
static void run_compose_loop(int *was_submitted, int *was_sent)
{
    AmgGui *gui = &test_gui;
    struct Window *window = &test_window;
    Object *dialog = &test_dialog;
    struct Gadget *to_gadget=&test_to,*cc_gadget=&test_cc,*bcc_gadget=&test_bcc;
    struct Gadget *subject_gadget=&test_subject,*body_gadget=&test_body;
    struct Gadget *compose_status=&test_status,*body_scroller=&test_scroll;
    struct Gadget *attachments_gadget=&test_attachments,*attachments_scroller=&test_scroll;
    struct List attachment_list;
    ComposeAttachment *attachments=test_files;
    size_t attachment_count=2U, attachment_capacity=2U;
    ComposeMode mode=test_mode;
    DraftEditData *draft_seed=&test_seed;
    AmgError error_storage={AMG_ERR_IO,"Queue failed"};
    AmgError *error=&error_storage;
    ULONG signal_mask=COMPOSE_SIGNAL;
    int done=0, submitted=0, sent_queued=0, select_all_pending=0;
    int close_pending=0, quit_pending=0;
    ComposeScrollerSyncState body_scroll_state={8};
    ComposeInputHookState compose_input_state={0};
''' + loop + '''
    *was_submitted=submitted; *was_sent=sent_queued;
}
'''
    return declarations, dispatcher + close + wrapper


def main():
    cc = shlex.split(os.environ.get('HOST_CC', 'gcc'))
    flags = ['-std=c99', '-O1', '-g', '-Wall', '-Wextra', '-Wshadow',
             '-Wpointer-arith', '-Wstrict-prototypes', '-Wmissing-prototypes',
             '-Wformat=2', '-Werror']
    flags += shlex.split(os.environ.get('COMPOSE_TEST_FLAGS', ''))
    with tempfile.TemporaryDirectory(prefix='amimail-compose-close-') as name:
        path = Path(name)
        declarations, code = production()
        (path/'compose_declarations.inc').write_text(declarations)
        (path/'compose_production.inc').write_text(code)
        binary = path/'compose-tests'
        subprocess.run(cc+flags+['-I'+name, '-Iinclude',
            'tests/test_compose_close.c', '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True, timeout=20)


if __name__ == '__main__':
    main()
