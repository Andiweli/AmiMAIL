/* Explicit API doubles. Real composer event routing, no actual UI renderer. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amigmail.h"
#include "catalog_ids.h"

typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned short UWORD;
typedef void *APTR;
typedef struct Object { int unused; } Object;
typedef struct TestClass { int unused; } Class;
typedef struct { ULONG MethodID; } *Msg;
struct Hook { APTR h_Data; };
struct Window { int unused; };
struct Gadget { int Height; const char *text; };
struct GadgetInfo { int unused; };
struct InputEvent { UWORD ie_Class, ie_Code, ie_Qualifier; };
struct gpInput { ULONG MethodID; struct GadgetInfo *gpi_GInfo;
 struct InputEvent *gpi_IEvent; LONG *gpi_Termination; };
struct GP_TEXTEDITOR_InsertText { ULONG MethodID; struct GadgetInfo *GInfo; unsigned char *text; LONG pos; };
typedef unsigned char *STRPTR;
struct List { int unused; };
typedef struct { const char *path; } ComposeAttachment;
typedef struct { int unused; } DraftEditData;
typedef enum { COMPOSE_MODE_NEW=0,COMPOSE_MODE_REPLY,COMPOSE_MODE_EDIT_DRAFT,COMPOSE_MODE_FORWARD } ComposeMode;
typedef struct { int running; struct Window *window; unsigned confirmation_failure; } AmgGui;
typedef struct { int editor_height; } ComposeScrollerSyncState;
typedef struct { int scroller_sync_pending; } ComposeInputHookState;
#define T(id,en) amg_tr(id,en)
#define GM_HANDLEINPUT 4UL
#define GMR_MEACTIVE 0UL
#define GMR_NOREUSE 2UL
#define GMR_VERIFY 4UL
#define IECLASS_RAWKEY 1U
#define IECODE_UP_PREFIX 0x80U
#define IEQUALIFIER_REPEAT 0x200U
#define IEQUALIFIER_RCOMMAND 0x8000U
#define GM_TEXTEDITOR_InsertText 0xad001UL
#define GV_TEXTEDITOR_InsertText_Cursor 0L
#define WMHI_LASTMSG 0UL
#define WMHI_IGNORE (~0UL)
#define WMHI_CLASSMASK 0xffff0000UL
#define WMHI_GADGETMASK 0xffffUL
#define WMHI_KEYMASK 0xffUL
#define WMHI_CLOSEWINDOW (1UL<<16)
#define WMHI_GADGETUP (2UL<<16)
#define WMHI_MENUPICK (6UL<<16)
#define WMHI_RAWKEY (11UL<<16)
#define WINDOW_InputEvent 801UL
#define SIGBREAKF_CTRL_C (1UL<<12)
#define COMPOSE_SIGNAL (1UL<<5)
#define RUNTIME_SIGNAL (1UL<<6)
#define MENU_EDIT_SELECT_ALL 10UL
#define MENU_EDIT_COPY 11UL
#define MENU_EDIT_CUT 12UL
#define MENU_EDIT_PASTE 13UL
#define MENU_QUIT 14UL
#define RA_HandleInput(o,c) test_HandleInput(o,c)
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"%d: %s\n",__LINE__,#x);abort(); } } while(0)
#include "compose_declarations.inc"
static struct Window test_window;
static Object test_dialog;
static AmgGui test_gui;
static struct Gadget test_to={8,"a@example.invalid"},test_cc={8,""},test_bcc={8,""};
static struct Gadget test_subject={8,"Changed subject"},test_body={8,"Changed body\nLatest text"};
static struct Gadget test_status={8,""},test_scroll={8,""},test_attachments={8,""};
static ComposeAttachment test_files[2]={{"RAM:one.bin"},{"RAM:two.bin"}};
static DraftEditData test_seed;
static ComposeMode test_mode;
typedef struct { ULONG result; UWORD code, qualifier; } TestEvent;
typedef struct { ULONG signal; TestEvent events[8]; size_t count; GuiConfirmationResult answer; int queue_result; } TestBatch;
static TestBatch batches[8];
static size_t batch_count,batch_index, event_index;
static TestBatch *current;
static struct InputEvent test_input;
static int question_calls,queue_calls,send_calls,super_calls,edit_calls,front_calls;
static char status_text[256];
static const char *amg_tr(long id,const char *en) { (void)id;return en; }
static ULONG Wait(ULONG mask)
{
    CHECK(batch_index<batch_count);current=&batches[batch_index++];event_index=0;
    CHECK(current->signal & mask);return current->signal;
}
static ULONG test_HandleInput(Object *o,UWORD *code)
{
    CHECK(o==&test_dialog && code && current);
    if(event_index==current->count) return WMHI_LASTMSG;
    *code=current->events[event_index].code;
    test_input.ie_Qualifier=current->events[event_index].qualifier;
    return current->events[event_index++].result;
}
static ULONG GetAttr(ULONG tag,Object *o,ULONG *value)
{ CHECK(tag==WINDOW_InputEvent && o==&test_dialog);*value=(ULONG)(uintptr_t)&test_input;return 1UL; }
static int rawkey_is_cancel(ULONG result) { return (result&WMHI_KEYMASK)==COMPOSE_RAWKEY_ESCAPE; }
static int rawkey_is_help(ULONG result) { return (result&WMHI_KEYMASK)==0x5f; }
static void gui_requester_trace(const AmgGui *g,const char *event,unsigned long a,unsigned long b)
{ CHECK(g==&test_gui && event);(void)a;(void)b; }
static void WindowToFront(struct Window *w) { CHECK(w==&test_window);++front_calls; }
static void ActivateWindow(struct Window *w) { CHECK(w==&test_window); }
static void ActivateGadget(struct Gadget *g,struct Window *w,APTR r) { CHECK(g==&test_body && w==&test_window);(void)r; }
static void set_string(struct Gadget *g,struct Window *w,const char *text)
{ CHECK(g==&test_status && w==&test_window);snprintf(status_text,sizeof(status_text),"%s",text); }
static void set_utf8_string(struct Gadget *g,struct Window *w,const char *text) { set_string(g,w,text); }
static GuiConfirmationResult confirm_question_result_for_window(AmgGui *g,
    struct Window *w,const char *question,const char *note,LONG width)
{
    CHECK(g==&test_gui && w==&test_window && width==310);
    CHECK(!strcmp(question,"Do you want to save the draft?"));
    CHECK(!strcmp(note,"The mail can be edited later."));
    /* No nested modal is allowed while the opening activation is undrained. */
    CHECK(event_index==current->count);++question_calls;
    g->confirmation_failure=current->answer==GUI_CONFIRM_FAILED?4U:0U;
    return current->answer;
}
static int queue_composed_mail(AmgGui *g,struct Window *w,struct Gadget *to,
    struct Gadget *cc,struct Gadget *bcc,struct Gadget *subject,struct Gadget *body,
    const ComposeAttachment *files,size_t count,ComposeMode mode,const DraftEditData *seed,
    int save,AmgError *error)
{
    CHECK(g==&test_gui && w==&test_window && to==&test_to && cc==&test_cc && bcc==&test_bcc);
    CHECK(subject==&test_subject && body==&test_body && files==test_files && count==2);
    CHECK(mode==test_mode && seed==&test_seed && error);
    CHECK(!strcmp(body->text,"Changed body\nLatest text"));
    CHECK(!strcmp(files[0].path,"RAM:one.bin"));
    if(save) {++queue_calls;CHECK(question_calls>0);} else ++send_calls;
    return current->queue_result;
}
static ULONG gui_runtime_signal_mask(AmgGui *g) { CHECK(g==&test_gui);return RUNTIME_SIGNAL; }
static void gui_runtime_process_signals(AmgGui *g,ULONG flags,AmgError *e)
{ CHECK(g==&test_gui && flags==RUNTIME_SIGNAL && e);g->running=0; }
static void about_dialog(AmgGui *g) { CHECK(g==&test_gui); }
static void handle_menu(AmgGui *g,ULONG code,AmgError *e)
{ CHECK(g==&test_gui && code==MENU_QUIT && e);g->running=0; }
static int compose_handle_edit_menu(ULONG code,struct Window *w,struct Gadget *g)
{ (void)code;CHECK(w==&test_window && g==&test_body);++edit_calls;return 1; }
static int compose_texteditor_mark_all(struct Gadget *g,struct Window *w)
{ CHECK(g==&test_body && w==&test_window);return 1; }
static void compose_sync_texteditor_scroller(struct Window *w,struct Gadget *e,
    struct Gadget *s,ComposeScrollerSyncState *st,int force)
{ (void)w;(void)e;(void)s;(void)st;(void)force; }
static void sync_listbrowser_scroller(struct Window *w,struct Gadget *g,struct Gadget *s)
{ (void)w;(void)g;(void)s; }
static void handle_listbrowser_scroller(struct Window *w,struct Gadget *g,struct Gadget *s)
{ (void)w;(void)g;(void)s; }
static void gui_contacts_select_emails(AmgGui *g,struct Window *w,struct Gadget *s,AmgError *e)
{ (void)g;(void)w;(void)s;(void)e; }
static void add_attachment(struct Window *w,struct Gadget *g,struct Gadget *s,
    struct List *list,ComposeAttachment **files,size_t *count,size_t *cap)
{ (void)w;(void)g;(void)s;(void)list;(void)files;(void)count;(void)cap; }
static void remove_attachment(struct Window *w,struct Gadget *g,struct Gadget *s,
    struct List *list,ComposeAttachment *files,size_t *count)
{ (void)w;(void)g;(void)s;(void)list;(void)files;(void)count; }
static ULONG DoSuperMethodA(Class *cl,Object *o,Msg m)
{ CHECK(cl && o && m);++super_calls;return 123UL; }
static ULONG compose_texteditor_super_arexx(Class *c,Object *o,struct GadgetInfo *g,const char *s)
{ (void)c;(void)o;(void)g;(void)s;++edit_calls;return 0UL; }
static ULONG compose_texteditor_super_mark_all(Class *c,Object *o,struct GadgetInfo *g)
{ (void)c;(void)o;(void)g;++edit_calls;return 0UL; }
#include "compose_production.inc"
static void reset(void)
{
    memset(batches,0,sizeof(batches));batch_count=batch_index=event_index=0;current=NULL;
    test_gui.running=1;test_gui.window=&test_window;test_gui.confirmation_failure=0;
    question_calls=queue_calls=send_calls=super_calls=edit_calls=front_calls=0;
    status_text[0]=0;test_mode=COMPOSE_MODE_NEW;test_input.ie_Qualifier=0;
}
static void batch(TestEvent event,GuiConfirmationResult answer,int queue_result)
{
    TestBatch *b=&batches[batch_count++];CHECK(batch_count<=8);
    b->signal=COMPOSE_SIGNAL;b->events[0]=event;b->count=1;
    b->answer=answer;b->queue_result=queue_result;
}
static void test_all_close_paths(void)
{
    const TestEvent paths[]={
        {WMHI_GADGETUP|GID_COMPOSE_CANCEL,0,0},
        {WMHI_RAWKEY|COMPOSE_RAWKEY_ESCAPE,0,0},
        {WMHI_CLOSEWINDOW,0,0},
        {WMHI_GADGETUP|GID_COMPOSE_BODY,COMPOSE_CLOSE_VERIFY_CODE,0},
        {WMHI_MENUPICK|MENU_QUIT,0,0}};
    size_t i;int mode,answer,submitted,sent;
    for(mode=0;mode<4;++mode) for(i=0;i<5;++i) for(answer=0;answer<4;++answer) {
        reset();test_mode=(ComposeMode)mode;
        batch(paths[i],(GuiConfirmationResult)answer,AMG_OK);
        if(answer==GUI_CONFIRM_DISMISSED || answer==GUI_CONFIRM_FAILED)
            batch(paths[i],GUI_CONFIRM_YES,AMG_OK);
        run_compose_loop(&submitted,&sent);
        CHECK(question_calls==((answer==GUI_CONFIRM_DISMISSED || answer==GUI_CONFIRM_FAILED)?2:1));
        CHECK(!sent && !send_calls);
        CHECK(submitted==(answer!=GUI_CONFIRM_NO));
        CHECK(queue_calls==(answer!=GUI_CONFIRM_NO));
        CHECK(test_gui.running==(i==4 && answer==GUI_CONFIRM_NO?0:1));
    }
}
static void test_batches_and_failures(void)
{
    TestEvent cancel={WMHI_GADGETUP|GID_COMPOSE_CANCEL,0,0};int submitted,sent;
    reset();batch(cancel,GUI_CONFIRM_YES,AMG_OK);
    batches[0].events[1]=cancel;
    batches[0].events[2]=(TestEvent){WMHI_RAWKEY|COMPOSE_RAWKEY_ESCAPE,0,0};
    batches[0].events[3]=(TestEvent){WMHI_GADGETUP|GID_COMPOSE_SEND,0,0};
    batches[0].count=4;
    run_compose_loop(&submitted,&sent);CHECK(submitted && !sent && question_calls==1 && queue_calls==1 && !send_calls);
    reset();batch(cancel,GUI_CONFIRM_YES,AMG_ERR_IO);batch(cancel,GUI_CONFIRM_YES,AMG_OK);
    run_compose_loop(&submitted,&sent);CHECK(submitted && question_calls==2 && queue_calls==2 && front_calls==1);
    reset();batch(cancel,GUI_CONFIRM_FAILED,AMG_OK);batch(cancel,GUI_CONFIRM_DISMISSED,AMG_OK);batch(cancel,GUI_CONFIRM_NO,AMG_OK);
    run_compose_loop(&submitted,&sent);CHECK(!submitted && question_calls==3 && !queue_calls);
    CHECK(strstr(status_text,"(R4)") && front_calls==2);
    reset();test_gui.window=NULL;batch(cancel,GUI_CONFIRM_YES,AMG_OK);
    run_compose_loop(&submitted,&sent);CHECK(submitted);
    reset();batch((TestEvent){WMHI_RAWKEY|COMPOSE_RAWKEY_ESCAPE,0,IEQUALIFIER_REPEAT},GUI_CONFIRM_NO,AMG_OK);
    batch(cancel,GUI_CONFIRM_YES,AMG_OK);run_compose_loop(&submitted,&sent);CHECK(question_calls==1 && submitted);
    reset();batch((TestEvent){WMHI_RAWKEY|COMPOSE_RAWKEY_ESCAPE|IECODE_UP_PREFIX,0,0},GUI_CONFIRM_NO,AMG_OK);
    batch(cancel,GUI_CONFIRM_YES,AMG_OK);run_compose_loop(&submitted,&sent);CHECK(question_calls==1 && submitted);
}
static void test_shutdown_and_send(void)
{
    int submitted,sent,signal_kind,answer;
    for(signal_kind=0;signal_kind<2;++signal_kind) for(answer=1;answer<3;++answer) {
        reset();batch((TestEvent){0,0,0},(GuiConfirmationResult)answer,AMG_OK);
        batches[0].count=0;batches[0].signal=signal_kind?RUNTIME_SIGNAL:SIGBREAKF_CTRL_C;
        run_compose_loop(&submitted,&sent);
        CHECK(question_calls==1 && !sent && submitted==(answer==GUI_CONFIRM_YES));
        CHECK(test_gui.running==(answer==GUI_CONFIRM_YES));
    }
    reset();batch((TestEvent){WMHI_GADGETUP|GID_COMPOSE_SEND,0,0},GUI_CONFIRM_NO,AMG_OK);
    batches[0].events[1]=batches[0].events[0];batches[0].count=2;
    run_compose_loop(&submitted,&sent);CHECK(submitted && sent && !question_calls && send_calls==1);
}
static void test_editor_escape(void)
{
    Class cl;struct Hook hook={&cl};struct InputEvent e={IECLASS_RAWKEY,COMPOSE_RAWKEY_ESCAPE,0};
    LONG code=0;struct gpInput in={GM_HANDLEINPUT,NULL,&e,&code};
    reset();CHECK(compose_texteditor_dispatcher(&hook,&test_dialog,&in)==(GMR_NOREUSE|GMR_VERIFY));
    CHECK(code==COMPOSE_CLOSE_VERIFY_CODE && !super_calls && !edit_calls);
    e.ie_Qualifier=IEQUALIFIER_REPEAT;code=0;
    CHECK(compose_texteditor_dispatcher(&hook,&test_dialog,&in)==GMR_MEACTIVE && code==0);
    e.ie_Qualifier=0;e.ie_Code|=IECODE_UP_PREFIX;
    CHECK(compose_texteditor_dispatcher(&hook,&test_dialog,&in)==GMR_MEACTIVE);
    e.ie_Code=COMPOSE_RAWKEY_C;e.ie_Qualifier=IEQUALIFIER_RCOMMAND;
    CHECK(compose_texteditor_dispatcher(&hook,&test_dialog,&in)==GMR_MEACTIVE && edit_calls==1);
    e.ie_Code=0x44;e.ie_Qualifier=IEQUALIFIER_REPEAT;
    CHECK(compose_texteditor_dispatcher(&hook,&test_dialog,&in)==GMR_MEACTIVE && super_calls==1);
    e.ie_Code=0x10;e.ie_Qualifier=0;
    CHECK(compose_texteditor_dispatcher(&hook,&test_dialog,&in)==123UL && super_calls==2);
}
int main(void)
{
    test_all_close_paths();test_batches_and_failures();test_shutdown_and_send();test_editor_escape();
    printf("composer close: %u checks passed (actual event loop, API doubles).\n",checks);
    return 0;
}
