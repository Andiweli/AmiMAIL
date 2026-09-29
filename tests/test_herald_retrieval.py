#!/usr/bin/env python3
"""Subject/interval/checkbox regressions; native calls use explicit doubles."""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent

def wiring():
    dialogs = (ROOT/'src/gui_dialogs.c').read_text()
    assert 'candidate->periodic_fetch_minutes =' in dialogs
    assert 'BAG_CHECKBOX' not in dialogs
    assert 'BUTTON_PushButton' not in dialogs
    assert len(re.findall(r'= native_checkbox\(', dialogs)) == 9
    assert 'NewObjectA(checkbox_class, NULL, tags)' in dialogs
    assert '{ CHECKBOX_Checked, selected ? TRUE : FALSE }' in dialogs
    gui = (ROOT/'src/gui.c').read_text()
    assert 'CheckBoxBase = OpenLibrary((CONST_STRPTR)"gadgets/checkbox.gadget", 44);' in gui
    assert 'if (CheckBoxBase) CloseLibrary(CheckBoxBase);' in gui
    assert 'CheckBoxBase = NULL;' in gui
    assert 'left->periodic_fetch_minutes == right->periodic_fetch_minutes' in dialogs
    assert 'page.periodic_interval = periodic_interval_gadget;' in dialogs
    assert 'CHOOSER_PopUp, TRUE' in dialogs and 'CHOOSER_LabelArray,' in dialogs
    assert 'T(MSG_PERIODIC_FETCH, "Periodic fetch")' in dialogs
    assert 'Periodic fetch (5 min)' not in dialogs
    assert 'T(MSG_HERALD_TEST, "?")' in dialogs
    for name, text in [('MSG_EMAIL_RETRIEVAL_LABEL','Email retrieval:'),
                       ('MSG_NOTIFICATIONS_LABEL','Notifications:'),
                       ('MSG_EXTERNAL_LABEL','External:')]:
        assert f'T({name}, "{text}")' in dialogs
    runtime=(ROOT/'src/gui_runtime.c').read_text()
    assert 'ReadEClock(&ticks)' in runtime
    assert 'tr_time.tv_secs = wait_seconds;' in runtime
    assert 'periodic_fetch_mail(gui, due_accounts, error);' in runtime
    assert 'GUI_PERIODIC_FETCH_SECONDS' not in runtime
    actions=(ROOT/'src/gui_actions.c').read_text()
    assert 'if (!(due_accounts & (1UL << index)) ||' in actions
    assert actions.count('event.payload_length, previous_uid);')==2
    assert actions.count('event.payload_length, old_uid);')==1
    header=(ROOT/'include/account.h').read_text()
    assert 'subject_enabled' not in header and 'herald_subject' not in header
    assert 'PROJECT := AmiMAIL\nVERSION := 2.2.0' in (ROOT/'Makefile').read_text()



def production_fragments():
    dialogs=(ROOT/'src/gui_dialogs.c').read_text()
    runtime=(ROOT/'src/gui_runtime.c').read_text()
    actions=(ROOT/'src/gui_actions.c').read_text()
    checkbox=dialogs[dialogs.index('static struct Gadget *native_checkbox('):dialogs.index('static int requester_rawkey_accept(')]
    enum=re.search(r'enum AccountGadgetId\s*\{.*?\};',dialogs,re.S).group()
    page=re.search(r'typedef struct AccountPageGadgets\s*\{.*?\} AccountPageGadgets;',dialogs,re.S).group()
    collect_show=dialogs[dialogs.index('static int account_page_collect('):dialogs.index('static void account_config_free_tab_nodes(')]
    anchor=dialogs.index('                        T(MSG_EMAIL_RETRIEVAL_LABEL,')
    first=dialogs.rfind('                LAYOUT_AddChild, HGroupObject,',0,anchor)
    last=dialogs.index('\n            EndObject,\n            CHILD_WeightedHeight, 0,',anchor)
    rows=dialogs[first:last]
    init=dialogs[dialogs.index('    for (interval_index = 0U; interval_index < AMG_PERIODIC_INTERVAL_COUNT;'):dialogs.index('    if (gui->screen) {',dialogs.index('    for (interval_index = 0U; interval_index < AMG_PERIODIC_INTERVAL_COUNT;'))]
    builder='''
static Object *build_test_rows(AmgGui *gui,AccountPageGadgets *page,ULONG account_label_width)
{
    struct Gadget *fetch_on_start_gadget, *periodic_fetch_gadget;
    struct Gadget *periodic_interval_gadget, *notification_sound_gadget;
    struct Gadget *notification_sound_path_gadget, *herald_notifications_gadget;
    char interval_text[AMG_PERIODIC_INTERVAL_COUNT][16];
    STRPTR interval_labels[AMG_PERIODIC_INTERVAL_COUNT + 1U];
    size_t interval_index;
    Object *result;
'''+init+'''
    result = VGroupObject,
'''+rows+'''
        TAG_DONE);
    page->fetch_on_start=fetch_on_start_gadget;
    page->periodic_fetch=periodic_fetch_gadget;
    page->periodic_interval=periodic_interval_gadget;
    page->notification_sound=notification_sound_gadget;
    page->notification_path=notification_sound_path_gadget;
    page->herald_notifications=herald_notifications_gadget;
    return result;
}
'''
    case=dialogs[dialogs.index('                            case GID_ACCOUNT_PERIODIC_FETCH:'):dialogs.index('                            case GID_ACCOUNT_HERALD_TEST:')]
    toggle='''
static void test_toggle_periodic(AccountPageGadgets *page,struct Window *window)
{
    struct Gadget *periodic_fetch_gadget=page->periodic_fetch;
    struct Gadget *periodic_interval_gadget=page->periodic_interval;
    switch(GID_ACCOUNT_PERIODIC_FETCH) {
'''+case+'    }\n}\n'
    timer=runtime[runtime.index('static int any_periodic_account_enabled('):runtime.index('static ULONG periodic_timer_signal_mask(')]
    event=runtime[runtime.index('    if (timer_signal && (signals & timer_signal)) {'):runtime.index('    if ((window_signal && (signals & window_signal)) ||')]
    signal='''
static void test_timer_signal(AmgGui *gui,AmgError *error)
{
    ULONG timer_signal=1UL << gui->periodic_timer_port->mp_SigBit;
    ULONG signals=timer_signal;
'''+event+'}\n'
    queue=actions[actions.index('void periodic_fetch_mail('):actions.index('void fetch_mail(')]
    folder_enum=re.search(r'enum FolderMappingGadgetId\s*\{.*?\};',dialogs,re.S).group()
    assignments=re.findall(r'([a-z_]+_gadget) = native_checkbox\(\s*(GID_\w+),\s*([^;]+?)\),',dialogs,re.S)
    assert len(assignments)==9
    constructors = """
static Object *build_all_test_checkboxes(AmgGui *gui, int *save_sent_copy,
                                         Object *boxes[9])
{
"""
    for var, ident, value in assignments:
        constructors += '    struct Gadget *'+var+';\n'
    for i,(var, ident, value) in enumerate(assignments):
        constructors += '    '+var+' = native_checkbox('+ident+', '+value+');\n'
        constructors += '    boxes['+str(i)+'] = '+var+';\n'
    constructors += '    return boxes[0];\n}\n'
    smtp_case=dialogs[dialogs.index('                            case GID_ACCOUNT_SMTP_SAME_CREDENTIALS:'):dialogs.index('                            case GID_ACCOUNT_FOLDER_MAPPING:')]
    smtp_toggle = """
static void test_toggle_smtp(AccountPageGadgets *page,struct Window *window)
{
    struct Gadget *smtp_same_credentials_gadget=page->smtp_same_credentials;
    struct Gadget *smtp_username_gadget=page->smtp_username;
    struct Gadget *smtp_password_gadget=page->smtp_password;
    switch (GID_ACCOUNT_SMTP_SAME_CREDENTIALS) {
"""+smtp_case+'    }\n}\n'
    return '\n'.join([enum,folder_enum,checkbox,page,collect_show,builder,
                       constructors,toggle,smtp_toggle,timer,queue,signal])


def main():
    wiring()
    cc=shlex.split(os.environ.get('HOST_CC','gcc'))
    flags=['-std=c99','-O1','-g','-Wall','-Wextra','-Wshadow','-Wpointer-arith',
           '-Wstrict-prototypes','-Wmissing-prototypes','-Wformat=2','-Werror']
    flags+=shlex.split(os.environ.get('RETRIEVAL_TEST_FLAGS',''))
    clang='clang' in subprocess.check_output(cc+['--version'],text=True).lower()
    make=(ROOT/'Makefile').read_text()
    sources=make.split('HOST_SOURCES := ',1)[1].split('\nHOST_TEST :=',1)[0].replace('\\\n',' ').split()
    with tempfile.TemporaryDirectory(prefix='amimail-subject-interval-') as tmp:
        tmp=Path(tmp)
        if clang:
            i18n=tmp/'i18n.o'
            subprocess.run(cc+flags+['-Wno-format-nonliteral','-Iinclude','-c','src/i18n.c','-o',str(i18n)],cwd=ROOT,check=True)
            sources=[s for s in sources if s!='src/i18n.c']+[str(i18n)]
        exe=tmp/'portable-tests'
        subprocess.run(cc+flags+['-Iinclude','tests/test_herald_retrieval.c']+sources+['-o',str(exe)],cwd=ROOT,check=True)
        subprocess.run([str(exe)],cwd=tmp,check=True,timeout=30)
        (tmp/'retrieval_production.inc').write_text(production_fragments())
        exe=tmp/'native-tests'
        subprocess.run(cc+flags+['-Iinclude','-Itests','-I'+str(tmp),
                       'tests/test_retrieval_native.c']+sources+['-o',str(exe)],cwd=ROOT,check=True)
        subprocess.run([str(exe)],cwd=tmp,check=True,timeout=30)
    print('Herald subject, per-account interval and native checkbox wiring passed (host).')

if __name__=='__main__':
    main()
