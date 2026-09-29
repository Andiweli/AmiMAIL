#!/usr/bin/env python3
"""Exercise production popup/arrow code with explicit input/window API doubles.

No SDK, pixel, ABI or real Intuition scheduling claim is made by these tests.
"""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent


def production():
    actions = (ROOT / 'src/gui_actions.c').read_text()
    icons = (ROOT / 'src/gui_icons.c').read_text()
    header = (ROOT / 'src/gui_icons.h').read_text()
    # Use the actual entire icon dispatcher/rendering implementation.
    icons = re.sub(r'^#include .*\n', '', icons, flags=re.M)
    decls = header[header.index('int gui_icons_init('):header.index('#endif')]
    internal = (ROOT/'src/gui_internal.h').read_text()
    decls += '\n'.join(re.findall(r'void gui_reply_popup_\w+\(AmgGui \*gui\);', internal)) + '\n'
    helpers = actions[actions.index('static Object *deferred_reply_popup;'):
                      actions.index('static void update_reply_button_mode(')]
    popup = actions[actions.index('static void label_without_shortcut('):
                    actions.index('static int request_label_index(')]
    a = actions.index('        case GID_REPLY_MENU:', actions.index('void handle_main_gadget('))
    b = actions.index('        case GID_DELETE:', a)
    branch = actions[a:b]
    release_handler = '''
static void dispatch_main_arrow(AmgGui *gui)
{
    AmgError *error = NULL;
    switch (GID_REPLY_MENU) {
''' + branch + '''
    default: break;
    }
}
'''
    transfer = (ROOT/'src/gui_transfer.c').read_text()
    identity = transfer[transfer.index('const char *gui_transfer_mailbox('):
                        transfer.index('int gui_transfer_message_matches(')]
    indices = actions[actions.index('static size_t label_index_for_mailbox('):
                      actions.index('static int current_mailbox_is_drafts(')]
    delete_fn = actions[actions.index('static void delete_selected_messages('):
                        actions.index('static void empty_trash(')]
    a = actions.index('                case AMG_NET_DELETE:', actions.index('void handle_network('))
    b = actions.index('                case AMG_NET_EMPTY_TRASH:', a)
    delete_case = actions[a:b]
    a = actions.index('                case AMG_NET_MOVE:', actions.index('void handle_network('))
    b = actions.index('                default:', a)
    move_case = actions[a:b]
    events = "static void dispatch_mutation(AmgGui *gui, TestEvent event) { switch(event.type) {\n" + delete_case + move_case + "default: break; } }\n"
    return (decls+'\n'+icons+'\n'+helpers+'\n'+popup+'\n'+release_handler+
            '\n'+indices+'\n'+identity+'\n'+delete_fn+'\n'+events)


def wiring():
    runtime = (ROOT/'src/gui_runtime.c').read_text()
    gui = (ROOT/'src/gui.c').read_text()
    actions = (ROOT/'src/gui_actions.c').read_text()
    popup=actions.split('static int reply_action_popup(',1)[1].split('static int request_label_index(',1)[0]
    assert 'static char reply_all_label[96];' in popup
    assert 'static char forward_label[96];' in popup
    assert runtime.index('gui_reply_popup_finish_input(gui);') > runtime.index('handle_main_gadget(gui, result & WMHI_GADGETMASK, error);')
    iconify = runtime.split('void gui_iconify(',1)[1].split('int gui_uniconify(',1)[0]
    assert iconify.index('gui_reply_popup_close(gui);') < iconify.index('WM_ICONIFY')
    destroy=gui.split('void amg_gui_destroy(AmgGui *gui)',1)[1]
    assert destroy.index('gui_reply_popup_close(gui);') < destroy.index('DisposeObject(gui->window_object)')
    delete=actions.split('static void delete_selected_messages(',1)[1].split('static void empty_trash(',1)[0]
    assert delete.index('snprintf(source_copy,') < delete.index('confirm_delete_dialog(gui)')
    assert 'network, AMG_NET_DELETE, uids[i],\n            trash_copy, source_copy' in delete
    print('Popup ownership/release wiring and immutable delete source checks passed.')


def main():
    wiring()
    cc=shlex.split(os.environ.get('HOST_CC','gcc'))
    flags=['-std=c99','-O1','-g','-Wall','-Wextra','-Wshadow','-Wpointer-arith',
           '-Wstrict-prototypes','-Wmissing-prototypes','-Wformat=2','-Werror']
    flags+=shlex.split(os.environ.get('POPUP_TEST_FLAGS',''))
    with tempfile.TemporaryDirectory(prefix='amimail-popup-') as name:
        directory=Path(name)
        (directory/'reply_popup_production.inc').write_text(production())
        binary=directory/'popup-tests'
        subprocess.run(cc+flags+['-I'+name,'tests/test_reply_popup.c','-o',str(binary)],cwd=ROOT,check=True)
        subprocess.run([str(binary)],cwd=ROOT,check=True,timeout=15)

if __name__=='__main__':
    main()
