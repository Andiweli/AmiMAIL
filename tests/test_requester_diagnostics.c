/* Explicit native API doubles. These exercise production C control flow, not
 * an Amiga ABI, the real window.class, memory manager, or hardware corruption. */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "amigmail.h"
#include "catalog_ids.h"

typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned short UWORD;
typedef unsigned char UBYTE;
typedef void *APTR;
typedef unsigned char *STRPTR;
typedef const unsigned char *CONST_STRPTR;
typedef unsigned long BPTR;
typedef struct Object Object;
struct Object { int kind, live; size_t children; Object *child[4]; int keep[4];
    ULONG inner_width, inner_height, width, height, min_height, max_width, max_height;
    LONG left, top;
    ULONG position, lock_width, lock_height;
    ULONG child_min_height[4], child_max_height[4], child_weight[4];
    struct Window *reference;
};
typedef struct TestClass { int kind; } Class;
struct TagItem { ULONG ti_Tag, ti_Data; };
struct InputEvent { UWORD ie_Qualifier; };
#define IEQUALIFIER_REPEAT 0x200U
typedef struct ComposeAttachment { int unused; } ComposeAttachment;
typedef struct DraftEditData { int unused; } DraftEditData;
typedef enum ComposeMode { COMPOSE_MODE_NEW=0 } ComposeMode;
struct Node { UBYTE ln_Type; };
struct Task { struct Node tc_Node; };
struct Process { struct Task pr_Task; APTR pr_WindowPtr; };
struct Library { UWORD lib_Version, lib_Revision; };
struct TextAttr { UWORD ta_YSize; };
struct TextFont { int unused; };
struct RastPort { struct TextFont *Font; };
struct Screen { struct TextAttr *Font; struct RastPort RastPort; LONG Width, Height; };
struct Window { LONG LeftEdge, TopEdge, Width, Height;
    UBYTE BorderLeft, BorderRight, BorderTop, BorderBottom; };
struct Gadget { int unused; };
struct LayoutLimits { UWORD MinWidth, MinHeight, MaxWidth, MaxHeight; };
struct DateStamp { LONG ds_Days, ds_Minute, ds_Tick; };
typedef struct AmgNetwork { int connected; } AmgNetwork;
typedef struct AmgGui {
    size_t active_account; ULONG active_message_uid;
    struct Window *window; struct Screen *screen;
    unsigned int confirmation_failure;
    AmgNetwork *network;
    int running;
    struct { char server_mailbox_utf8[512]; int available; } labels[7];
    size_t label_count;
    char current_mailbox_utf8[512];
} AmgGui;

#define TRUE 1
#define FALSE 0
#define NT_PROCESS 13
#define MODE_READWRITE 1004L
#define OFFSET_END 1L
#define OFFSET_CURRENT 0L
#define GVF_GLOBAL_ONLY 0x100UL
#define MEMF_CHIP 2UL
#define MEMF_FAST 4UL
#define MEMF_LARGEST (1UL << 17)
#define SIGBREAKF_CTRL_C (1UL << 12)
#define WMHI_CLASSMASK 0xffff0000UL
#define WMHI_GADGETMASK 0xffffUL
#define WMHI_KEYMASK 0xffUL
#define WMHI_LASTMSG 0UL
#define WMHI_IGNORE (~0UL)
#define WMHI_CLOSEWINDOW (1UL << 16)
#define WMHI_GADGETUP (2UL << 16)
#define WMHI_RAWKEY (11UL << 16)
#define GUI_RAWKEY_ESCAPE 0x45UL
#define GUI_RAWKEY_RETURN 0x44UL
#define GUI_RAWKEY_NP_ENTER 0x43UL
#define WFLG_CLOSEGADGET 1UL
#define WFLG_DRAGBAR 2UL
#define WFLG_DEPTHGADGET 4UL
#define WFLG_ACTIVATE 8UL
#define IDCMP_CLOSEWINDOW 1UL
#define IDCMP_GADGETUP 2UL
#define IDCMP_RAWKEY 4UL
#define TAG_DONE 0UL
#define TAG_IGNORE 1UL
#define BVS_NONE 0UL
#define AMG_NET_DELETE 25

enum { LAYOUT_SpaceOuter=100, LAYOUT_SpaceInner, LAYOUT_AddChild, LAYOUT_EvenSize,
 CHILD_MinHeight, CHILD_MaxHeight, CHILD_WeightedHeight, GA_ReadOnly, GA_Text,
 BUTTON_BevelStyle, BUTTON_Transparent, GA_ID, GA_RelVerify,
 WA_Title, WA_Left, WA_Top, WA_Width, WA_Height, WA_MinWidth, WA_MaxWidth,
 WA_MinHeight, WA_MaxHeight, WA_PubScreen, WA_Flags, WA_IDCMP, WINDOW_Layout,
 WINDOW_SigMask, WINDOW_InputEvent, LAYOUT_Orientation, CHILD_NoDispose,
 WA_InnerWidth, WA_InnerHeight, WINDOW_LockWidth, WINDOW_LockHeight,
 WINDOW_Position, WINDOW_RefWindow };
#define WPOS_CENTERWINDOW 4UL
#define LAYOUT_ORIENT_HORIZ 0UL
#define LAYOUT_ORIENT_VERT 1UL
enum { GID_CONFIRM_YES=300, GID_CONFIRM_NO, GID_COMPOSE_CANCEL=400 };
enum { TEST_VGROUP=1, TEST_HGROUP, TEST_BUTTON, TEST_WINDOW };
#define RA_OpenWindow(o) test_OpenWindow(o)
#define RA_HandleInput(o,c) test_HandleInput(o,c)

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); abort(); } } while (0)
static struct Process test_process;
static struct Library window_lib={47,24}, layout_lib={47,12}, button_lib={47,8};
static struct Library *WindowBase=&window_lib, *LayoutBase=&layout_lib, *ButtonBase=&button_lib;
static struct Window main_window={40,30,640,430,4,4,12,4};
static struct Window confirm_window;
static struct TextAttr text_attr={8};
static struct TextFont font;
static struct Screen screen={&text_attr,{&font},800,600};
static AmgNetwork test_network={1};
static AmgGui test_gui;
static LONG native_error;
static int debug_enabled, open_fail, seek_fail, write_fail, forced_full;
static int mem_calls, open_calls, close_calls, write_calls, wait_calls, input_calls;
static ULONG pending;
static char log_data[270000];
static long log_size, log_position;
static char last_status[256], compose_status_text[256];
static int allocation_fail, selected_count, queue_calls, queue_failure_at;
static int fail_stage, object_count, disposed_layout, disposed_window, window_open_calls;
static Object test_objects[32];
static Class test_button_class={TEST_BUTTON}, test_layout_class={TEST_VGROUP}, test_window_class={TEST_WINDOW};
static int live_objects, allocation_call, fail_allocation;
static UWORD domain_width, domain_height, measured_height;
static UBYTE actual_border_left, actual_border_right, actual_border_top, actual_border_bottom;
static ULONG raw_qualifier;
static struct InputEvent input_event;
static ULONG events[8];
static size_t event_count, event_index;
static int compose_queue_result, compose_queue_calls;

static struct Task *FindTask(CONST_STRPTR name)
{ (void)name; return &test_process.pr_Task; }
static LONG IoErr(void) { return native_error; }
static LONG SetIoErr(LONG value) { LONG old=native_error; native_error=value; return old; }
static LONG GetVar(CONST_STRPTR name, STRPTR buffer, LONG size, ULONG flags)
{
    CHECK(!strcmp((const char *)name,"AmiMAIL_RequesterDebug"));
    CHECK(size>=2 && flags==GVF_GLOBAL_ONLY);
    CHECK(test_process.pr_WindowPtr==(APTR)(intptr_t)-1);
    native_error=205; errno=EIO;
    if (!debug_enabled) return -1;
    buffer[0]='1';buffer[1]=0;return 1;
}
static ULONG SetSignal(ULONG value, ULONG mask)
{ CHECK(value==0 && mask==0); return pending; }
static ULONG AvailMem(ULONG flags)
{
    ++mem_calls;
    if(flags==MEMF_CHIP) return 1048576UL;
    if(flags==(MEMF_CHIP|MEMF_LARGEST)) return 524288UL;
    if(flags==MEMF_FAST) return 8388608UL;
    CHECK(flags==(MEMF_FAST|MEMF_LARGEST));return 4194304UL;
}
static struct DateStamp *DateStamp(struct DateStamp *s)
{ s->ds_Days=17804;s->ds_Minute=1200;s->ds_Tick=25;return s; }
static BPTR Open(CONST_STRPTR path,LONG mode)
{
    CHECK(!strcmp((const char *)path,"T:AmiMAIL-requesters.log"));
    CHECK(mode==MODE_READWRITE);++open_calls;native_error=207;errno=EIO;
    if(open_fail) return 0;
    log_position=0;return 1;
}
static LONG Seek(BPTR file,LONG delta,LONG mode)
{
    LONG old=log_position;CHECK(file==1 && delta==0);
    if(seek_fail) return -1;
    if(mode==OFFSET_END) log_position=forced_full?262144L:log_size;
    else CHECK(mode==OFFSET_CURRENT);
    return old;
}
static LONG Write(BPTR file,APTR buffer,LONG size)
{
    CHECK(file==1 && size>0);++write_calls;
    CHECK(log_position+size<(long)sizeof(log_data));
    if(write_fail) return -1;
    memcpy(log_data+log_position,buffer,(size_t)size);log_position+=size;
    log_size=log_position;log_data[log_size]=0;native_error=999;errno=EIO;
    return size;
}
static LONG Close(BPTR file)
{ CHECK(file==1);++close_calls;native_error=211;errno=EIO;return 1; }
static const char *amg_tr(long id,const char *en) { (void)id; return en; }
static int amg_tr_snprintf(char *out,size_t capacity,long id,const char *fmt,...)
    __attribute__((format(printf,4,5)));
static int amg_tr_snprintf(char *out,size_t capacity,long id,const char *fmt,...)
{ va_list args; int n; (void)id;va_start(args,fmt);n=vsnprintf(out,capacity,fmt,args);va_end(args);return n; }
static void status_local(AmgGui *g,const char *text)
{ CHECK(g!=NULL);snprintf(last_status,sizeof(last_status),"%s",text); }
static Class *BUTTON_GetClass(void) { return &test_button_class; }
static Class *LAYOUT_GetClass(void) { return &test_layout_class; }
static Class *WINDOW_GetClass(void) { return &test_window_class; }
static void DisposeObject(Object *o)
{
    size_t i;
    CHECK(o && o->live);
    if(o->kind==TEST_WINDOW) ++disposed_window;
    if(o->kind==TEST_VGROUP) ++disposed_layout;
    for(i=0;i<o->children;++i) if(!o->keep[i]) DisposeObject(o->child[i]);
    o->live=0;--live_objects;
    native_error=222;
}
static Object *NewObjectA(Class *cl, CONST_STRPTR name, const struct TagItem *tags)
{
    Object *o=NULL;size_t i; int fail;
    CHECK(cl && name==NULL && tags);
    for(i=0;i<32;++i) if(!test_objects[i].live) { o=&test_objects[i]; break; }
    CHECK(o); memset(o,0,sizeof(*o));o->kind=cl->kind;o->live=1;++live_objects;
    for(i=0;tags[i].ti_Tag!=TAG_DONE;++i) {
        ULONG tag=tags[i].ti_Tag,value=tags[i].ti_Data;CHECK(i<32);
        if(tag==LAYOUT_Orientation && value==LAYOUT_ORIENT_HORIZ) o->kind=TEST_HGROUP;
        if(tag==LAYOUT_AddChild) {CHECK(value && o->children<4);o->child[o->children++]=(Object *)(uintptr_t)value;}
        if(tag==CHILD_NoDispose) {CHECK(o->children);o->keep[o->children-1U]=value!=0;}
        if(tag==CHILD_MinHeight) {CHECK(o->children);o->child_min_height[o->children-1U]=value;}
        if(tag==CHILD_MaxHeight) {CHECK(o->children);o->child_max_height[o->children-1U]=value;}
        if(tag==CHILD_WeightedHeight) {CHECK(o->children);o->child_weight[o->children-1U]=value;}
        if(tag==WA_InnerWidth) o->inner_width=value;
        if(tag==WA_InnerHeight) o->inner_height=value;
        if(tag==WA_Width) o->width=value;
        if(tag==WA_Height) o->height=value;
        if(tag==WA_MinHeight) o->min_height=value;
        if(tag==WA_MaxWidth) o->max_width=value;
        if(tag==WA_MaxHeight) o->max_height=value;
        if(tag==WINDOW_Position) o->position=value;
        if(tag==WINDOW_RefWindow) o->reference=(struct Window *)(uintptr_t)value;
        if(tag==WINDOW_LockWidth) o->lock_width=value;
        if(tag==WINDOW_LockHeight) o->lock_height=value;
        if(tag==WA_Left) o->left=(LONG)value;
        if(tag==WA_Top) o->top=(LONG)value;
    }
    ++allocation_call; ++object_count;
    fail=(fail_allocation && allocation_call==fail_allocation) ||
         (fail_stage==2 && o->kind==TEST_VGROUP) ||
         (fail_stage==3 && o->kind==TEST_WINDOW);
    if(fail) {
        /* Model a constructor which disposes accepted children on failure. */
        DisposeObject(o);native_error=103;return NULL;
    }
    return o;
}
static ULONG SetAttrs(Object *o,...)
{
    va_list ap; ULONG tag; CHECK(o && o->live && o->kind==TEST_WINDOW);
    va_start(ap,o); tag=va_arg(ap,ULONG);CHECK(tag==WINDOW_Layout);
    o->child[0]=(Object *)(uintptr_t)va_arg(ap,ULONG);o->children=1;
    o->keep[0]=0;CHECK(va_arg(ap,ULONG)==TAG_DONE);va_end(ap);return 1;
}
static void WindowToFront(struct Window *w) { CHECK(w); }
static void ActivateWindow(struct Window *w) { CHECK(w); }
static int rawkey_is_cancel(ULONG result)
{ return (result&WMHI_KEYMASK)==GUI_RAWKEY_ESCAPE; }
static void LayoutLimits(struct Gadget *g,struct LayoutLimits *l,
                         struct TextFont *f,struct Screen *s)
{
    Object *layout=(Object *)g;
    ULONG height=6UL;size_t i;
    CHECK(g && f==&font && s==&screen);
    for(i=0;i<layout->children;++i) height+=layout->child_min_height[i];
    /* Measure the production tags, so removing a blank row changes the
     * result. This is a geometry double, not native font/theme rendering. */
    l->MinWidth=domain_width;l->MinHeight=domain_height?domain_height:(UWORD)height;
    measured_height=l->MinHeight;
}
static struct Window *test_OpenWindow(Object *o)
{
    LONG min_width, min_height;
    CHECK(o && o->kind==TEST_WINDOW);++window_open_calls;native_error=104;
    if(fail_stage==4) return NULL;
    CHECK(o->children==1 && o->child[0]->kind==TEST_VGROUP);
    min_width=(LONG)domain_width+actual_border_left+actual_border_right;
    min_height=(LONG)measured_height+actual_border_top+actual_border_bottom;
    /* The fixed-height child rows also bound the layout's maximum height.
     * Reference borders can cause either a too-small maximum or a too-large
     * minimum. Model those constraints, not the full window.class algorithm. */
    if((o->max_width && (LONG)o->max_width<min_width) ||
       (o->max_height && (LONG)o->max_height<min_height) ||
       (o->min_height && (LONG)o->min_height>min_height)) return NULL;
    confirm_window.Width=o->inner_width
        ? (LONG)o->inner_width+actual_border_left+actual_border_right : (LONG)o->width;
    confirm_window.Height=o->inner_height
        ? (LONG)o->inner_height+actual_border_top+actual_border_bottom : (LONG)o->height;
    if(confirm_window.Width<min_width) confirm_window.Width=min_width;
    if(confirm_window.Height<min_height) confirm_window.Height=min_height;
    confirm_window.LeftEdge=o->left;confirm_window.TopEdge=o->top;
    if(o->position==WPOS_CENTERWINDOW) {
        CHECK(o->reference);
        confirm_window.LeftEdge=o->reference->LeftEdge+(o->reference->Width-confirm_window.Width)/2;
        confirm_window.TopEdge=o->reference->TopEdge+(o->reference->Height-confirm_window.Height)/2;
    }
    return &confirm_window;
}
static ULONG GetAttr(ULONG tag,Object *o,ULONG *value)
{
    CHECK(o);
    if(tag==WINDOW_InputEvent) {input_event.ie_Qualifier=(UWORD)raw_qualifier;*value=(ULONG)(uintptr_t)&input_event;}
    else {CHECK(tag==WINDOW_SigMask);*value=fail_stage==5?0:1UL<<5;}
    return 1;
}
static ULONG Wait(ULONG mask)
{
    ULONG got;
    CHECK(++wait_calls<4);
    if(!pending) pending=1UL<<5;
    got=pending & mask;pending &= ~mask;return got;
}
static ULONG test_HandleInput(Object *o,UWORD *code)
{ CHECK(o && code==NULL);++input_calls;return event_index<event_count?events[event_index++]:WMHI_LASTMSG; }
static ULONG *selected_message_uids_alloc(AmgGui *g,size_t *count)
{
    ULONG *result;size_t i;CHECK(g);*count=(size_t)selected_count;
    if(allocation_fail) return NULL;
    result=malloc((selected_count?(size_t)selected_count:1U)*sizeof(*result));CHECK(result);
    for(i=0;i<*count;++i) result[i]=42UL+(ULONG)i;
    return result;
}
static int amg_network_is_connected(const AmgNetwork *n) { return n && n->connected; }
static size_t label_index_for_mailbox(const AmgGui *g,const char *name)
{ (void)g;return !strcmp(name,"INBOX")?0U:7U; }
static int amg_network_request(AmgNetwork *n,int command,ULONG uid,
                                const char *target,const char *source,AmgError *error)
{
    CHECK(n==&test_network && command==AMG_NET_DELETE && uid>=42);
    CHECK(!strcmp(target,"Trash") && !strcmp(source,"INBOX"));
    ++queue_calls;
    if(queue_calls==queue_failure_at) {error->code=AMG_ERR_IO;strcpy(error->message,"Queue failed");return AMG_ERR_IO;}
    return AMG_OK;
}
static void status_utf8(AmgGui *g,const char *text) { status_local(g,text); }
static int queue_composed_mail(AmgGui *g,...)
{ CHECK(g);++compose_queue_calls;return compose_queue_result; }
static void set_string(struct Gadget *g,struct Window *w,const char *text)
{ (void)g;CHECK(w);snprintf(compose_status_text,sizeof(compose_status_text),"%s",text); }
static void set_utf8_string(struct Gadget *g,struct Window *w,const char *text)
{ set_string(g,w,text); }

#include "requester_production.inc"

static void reset(void)
{
    memset(&test_gui,0,sizeof(test_gui));test_gui.window=&main_window;test_gui.screen=&screen;
    test_gui.active_account=0;test_gui.active_message_uid=42;test_gui.network=&test_network;
    test_gui.label_count=7;strcpy(test_gui.current_mailbox_utf8,"INBOX");
    strcpy(test_gui.labels[0].server_mailbox_utf8,"INBOX");
    strcpy(test_gui.labels[6].server_mailbox_utf8,"Trash");test_gui.labels[6].available=1;
    test_process.pr_Task.tc_Node.ln_Type=NT_PROCESS;test_process.pr_WindowPtr=(APTR)&main_window;
    native_error=765;errno=ERANGE;debug_enabled=0;open_fail=seek_fail=write_fail=forced_full=0;
    mem_calls=open_calls=close_calls=write_calls=wait_calls=input_calls=0;
    log_size=log_position=0;log_data[0]=last_status[0]=compose_status_text[0]=0;
    pending=0;fail_stage=object_count=disposed_layout=disposed_window=window_open_calls=0;
    event_index=0;event_count=1;events[0]=WMHI_GADGETUP|GID_CONFIRM_NO;
    selected_count=2;allocation_fail=queue_calls=queue_failure_at=0;test_network.connected=1;
    compose_queue_result=AMG_OK;compose_queue_calls=0;
    memset(test_objects,0,sizeof(test_objects));live_objects=allocation_call=fail_allocation=0;
    raw_qualifier=0;
    domain_width=302;domain_height=measured_height=0;
    text_attr.ta_YSize=8;
    actual_border_left=main_window.BorderLeft;actual_border_right=main_window.BorderRight;
    actual_border_top=main_window.BorderTop;actual_border_bottom=main_window.BorderBottom;
    memset(&confirm_window,0,sizeof(confirm_window));
}

static void test_log(void)
{
    int mode;
    for(mode=0;mode<6;++mode) {
        reset();debug_enabled=mode!=0;open_fail=mode==2;seek_fail=mode==3;
        write_fail=mode==4;forced_full=mode==5;pending=SIGBREAKF_CTRL_C;
        gui_requester_trace(&test_gui,"test.event",12UL,34UL);
        CHECK(native_error==765 && errno==ERANGE);
        CHECK(pending==SIGBREAKF_CTRL_C && test_process.pr_WindowPtr==(APTR)&main_window);
        CHECK(close_calls==((mode && !open_fail)?1:0));
        if(mode==0) CHECK(!mem_calls && !open_calls && !log_size);
        if(mode==1) {
            CHECK(strstr(log_data,"test.event v1=12")!=NULL);
            CHECK(strstr(log_data,"chip_largest=524288")!=NULL);
            CHECK(strstr(log_data,"classes=window:47.24,layout:47.12,button:47.8")!=NULL);
            CHECK(strstr(log_data,"pending=00001000")!=NULL);
        }
        if(mode>1) CHECK(!log_size);
    }
    reset();gui_requester_failure(&test_gui,4U,104L);
    CHECK(test_gui.confirmation_failure==4 && strstr(last_status,"R4"));
    CHECK(strstr(log_data,"requester.failure v1=4") && strstr(log_data,"v2=104"));
    CHECK(native_error==765 && errno==ERANGE && test_process.pr_WindowPtr==(APTR)&main_window);
    reset();debug_enabled=1;test_process.pr_WindowPtr=(APTR)(intptr_t)-1;
    gui_requester_trace(&test_gui,"test.inherited-requester",0,0);
    CHECK(test_process.pr_WindowPtr==(APTR)(intptr_t)-1);
    reset();debug_enabled=1;test_process.pr_Task.tc_Node.ln_Type=1;
    gui_requester_trace(&test_gui,"test.not-a-test_process",0,0);CHECK(!open_calls);
    reset();debug_enabled=1;WindowBase=LayoutBase=ButtonBase=NULL;
    gui_requester_trace(&test_gui,"test.no-libraries",0,0);
    CHECK(strstr(log_data,"classes=window:0.0,layout:0.0,button:0.0"));
    WindowBase=&window_lib;LayoutBase=&layout_lib;ButtonBase=&button_lib;
}

static void test_failures(void)
{
    unsigned stage;
    reset();CHECK(!confirm_question_dialog_for_window(NULL,&main_window,"Question","Note",310));
    CHECK(!window_open_calls);
    reset();CHECK(!confirm_question_dialog_for_window(&test_gui,NULL,"Question","Note",310));
    CHECK(test_gui.confirmation_failure==1 && strstr(last_status,"R1"));
    reset();test_gui.screen=NULL;CHECK(!confirm_delete_dialog(&test_gui));CHECK(test_gui.confirmation_failure==1);
    reset();ButtonBase=NULL;CHECK(!confirm_delete_dialog(&test_gui));
    CHECK(test_gui.confirmation_failure==2 && !live_objects);ButtonBase=&button_lib;
    reset();LayoutBase=NULL;CHECK(!confirm_delete_dialog(&test_gui));
    CHECK(test_gui.confirmation_failure==2 && !live_objects);LayoutBase=&layout_lib;
    reset();WindowBase=NULL;CHECK(!confirm_delete_dialog(&test_gui));
    CHECK(test_gui.confirmation_failure==3 && !live_objects);WindowBase=&window_lib;

    for(stage=2;stage<=5;++stage) {
        reset();fail_stage=(int)stage;
        CHECK(confirm_delete_dialog(&test_gui)==0);CHECK(test_gui.confirmation_failure==stage);
        CHECK(strstr(log_data,"requester.failure") && !wait_calls && !input_calls);
        CHECK(live_objects==0);
        if(stage>=3) CHECK(disposed_window==1);
    }
    reset();fail_stage=4;CHECK(!confirm_delete_dialog(&test_gui));
    fail_stage=0;object_count=0;events[0]=WMHI_GADGETUP|GID_CONFIRM_YES;
    CHECK(confirm_delete_dialog(&test_gui)==1);CHECK(test_gui.confirmation_failure==0);
}

static void test_decisions(void)
{
    const ULONG firsts[]={WMHI_GADGETUP|GID_CONFIRM_NO, WMHI_RAWKEY|GUI_RAWKEY_ESCAPE,
        WMHI_CLOSEWINDOW,WMHI_GADGETUP|GID_CONFIRM_YES,
        WMHI_RAWKEY|GUI_RAWKEY_RETURN,WMHI_RAWKEY|GUI_RAWKEY_NP_ENTER};
    size_t i;
    for(i=0;i<sizeof(firsts)/sizeof(firsts[0]);++i) {
        reset();debug_enabled=1;events[0]=firsts[i];
        events[1]=i<3?(WMHI_GADGETUP|GID_CONFIRM_YES):(WMHI_GADGETUP|GID_CONFIRM_NO);
        event_count=2;
        CHECK(confirm_delete_dialog(&test_gui)==(i>=3));
        CHECK(input_calls==1 && event_index==1 && test_gui.confirmation_failure==0);
        CHECK(disposed_window==1 && strstr(log_data,"requester.result"));
        CHECK(strstr(log_data,"requester.minimum v1=302")!=NULL);
        CHECK(strstr(log_data,"requester.size v1=310")!=NULL);
        CHECK(strstr(log_data,"requester.position v1=205")!=NULL);
    }
    reset();debug_enabled=1;pending=SIGBREAKF_CTRL_C|(1UL<<5);
    events[0]=WMHI_GADGETUP|GID_CONFIRM_YES;
    CHECK(!confirm_delete_dialog(&test_gui));CHECK(!input_calls);
    CHECK(strstr(log_data,"requester.cancel.ctrl-c"));
    reset();events[0]=WMHI_IGNORE;events[1]=WMHI_GADGETUP|GID_CONFIRM_YES;event_count=2;
    CHECK(confirm_delete_dialog(&test_gui)==1 && input_calls==2);
    reset();CHECK(!confirm_empty_trash_dialog(&test_gui));
    reset();CHECK(!confirm_empty_spam_dialog(&test_gui));
}

static void test_native_sizing(void)
{
    int done, submitted;
    /* The main window has a sizing border; the requester has no size gadget. */
    main_window.BorderBottom=24;
    reset();actual_border_bottom=4;
    events[0]=WMHI_GADGETUP|GID_CONFIRM_YES;
    CHECK(confirm_delete_dialog(&test_gui)==1);
    CHECK(confirm_window.Height==68 && !test_gui.confirmation_failure);
    CHECK(!live_objects);
    main_window.BorderBottom=4;
    reset();debug_enabled=1;actual_border_top=28;actual_border_bottom=8;
    actual_border_left=6;actual_border_right=6;
    events[0]=WMHI_GADGETUP|GID_CONFIRM_YES;
    CHECK(confirm_delete_dialog(&test_gui)==1);
    CHECK(confirm_window.Width==314 && confirm_window.Height==88);
    CHECK(confirm_window.LeftEdge==203 && confirm_window.TopEdge==201);
    CHECK(strstr(log_data,"requester.inner-size v1=302")!=NULL);
    CHECK(strstr(log_data,"requester.size v1=314")!=NULL);
    CHECK(!live_objects && !test_gui.confirmation_failure);
    reset();domain_width=700;domain_height=120;actual_border_top=32;
    events[0]=WMHI_GADGETUP|GID_CONFIRM_YES;
    CHECK(confirm_question_result_for_window(&test_gui,&main_window,"Q","N",310)==GUI_CONFIRM_YES);
    CHECK(confirm_window.Width==708 && confirm_window.Height==156);
    CHECK(!live_objects);
    reset();actual_border_top=28;events[0]=WMHI_GADGETUP|GID_CONFIRM_NO;
    test_compose_cancel(&test_gui,&done,&submitted);
    CHECK(done && !submitted && !compose_queue_calls);
    CHECK(!test_gui.confirmation_failure && !live_objects);
}

static void test_text_spacing(void)
{
    UWORD heights[]={8U,12U,16U,24U};size_t h;int with_note;
    for(h=0;h<sizeof(heights)/sizeof(heights[0]);++h) {
        for(with_note=0;with_note<=1;++with_note) {
            Object *layout=NULL,*gap;size_t i,gap_index=with_note?2U:1U;
            reset();text_attr.ta_YSize=heights[h];
            events[0]=WMHI_GADGETUP|GID_CONFIRM_YES;
            CHECK(confirm_question_result_for_window(&test_gui,&main_window,"Q",
                  with_note?"N":NULL,310)==GUI_CONFIRM_YES);
            /* Test objects retain their recorded tags after disposal. */
            for(i=0;i<32U;++i) if(test_objects[i].kind==TEST_VGROUP &&
                test_objects[i].children) layout=&test_objects[i];
            CHECK(layout && layout->children==(with_note?4U:3U));
            CHECK(layout->child_min_height[0]==(ULONG)heights[h]+4UL);
            if(with_note) CHECK(layout->child_min_height[1]==(ULONG)heights[h]+2UL);
            gap=layout->child[gap_index];
            CHECK(gap->kind==TEST_BUTTON && !gap->children);
            CHECK(layout->child_min_height[gap_index]==heights[h]);
            CHECK(layout->child_max_height[gap_index]==heights[h]);
            CHECK(layout->child_weight[gap_index]==0UL);
            CHECK(!live_objects);
        }
    }
}

static void test_delete_safety(void)
{
    AmgError error;
    int stage;
    for(stage=0;stage<=5;++stage) {
        reset();debug_enabled=1;fail_stage=stage;events[0]=WMHI_GADGETUP|GID_CONFIRM_YES;
        if(stage==1) test_gui.screen=NULL;
        delete_selected_messages(&test_gui,&error);
        CHECK(queue_calls==(stage==0?2:0));
        CHECK(strstr(log_data,"delete.begin"));
        if(stage) CHECK(strstr(log_data,"delete.not-confirmed"));
    }
    reset();debug_enabled=1;delete_selected_messages(&test_gui,&error);
    CHECK(!queue_calls && strstr(log_data,"requester.cancel.no"));
    reset();allocation_fail=1;debug_enabled=1;delete_selected_messages(&test_gui,&error);
    CHECK(!queue_calls && !window_open_calls && strstr(log_data,"delete.blocked.allocation"));
    reset();selected_count=0;debug_enabled=1;delete_selected_messages(&test_gui,&error);
    CHECK(!queue_calls && !window_open_calls && strstr(log_data,"delete.blocked.no-selection"));
    reset();test_network.connected=0;debug_enabled=1;delete_selected_messages(&test_gui,&error);
    CHECK(!queue_calls && !window_open_calls && strstr(log_data,"delete.blocked.offline"));
    reset();test_gui.current_mailbox_utf8[0]=0;debug_enabled=1;delete_selected_messages(&test_gui,&error);
    CHECK(!queue_calls && !window_open_calls && strstr(log_data,"delete.blocked.no-folder"));
    reset();strcpy(test_gui.current_mailbox_utf8,"Trash");debug_enabled=1;delete_selected_messages(&test_gui,&error);
    CHECK(!queue_calls && !window_open_calls && strstr(log_data,"delete.blocked.in-trash"));
    reset();events[0]=WMHI_GADGETUP|GID_CONFIRM_YES;queue_failure_at=1;
    delete_selected_messages(&test_gui,&error);CHECK(queue_calls==1 && !strcmp(last_status,"Queue failed"));
}

static void test_draft_safety(void)
{
    int done,submitted,stage;
    for(stage=1;stage<=5;++stage) {
        reset();fail_stage=stage;if(stage==1) test_gui.screen=NULL;
        test_compose_cancel(&test_gui,&done,&submitted);
        CHECK(!done && !submitted && !compose_queue_calls);
        CHECK(strstr(compose_status_text,"Confirmation failed. Draft remains open.") && strstr(compose_status_text,"(R"));
        CHECK(live_objects==0);
    }
    reset();test_compose_cancel(&test_gui,&done,&submitted);CHECK(done && !submitted);
    reset();events[0]=WMHI_RAWKEY|GUI_RAWKEY_ESCAPE;
    test_compose_cancel(&test_gui,&done,&submitted);CHECK(!done && !submitted);
    reset();events[0]=WMHI_CLOSEWINDOW;
    test_compose_cancel(&test_gui,&done,&submitted);CHECK(!done && !submitted);
    reset();test_gui.window=NULL;events[0]=WMHI_GADGETUP|GID_CONFIRM_YES;
    test_compose_cancel(&test_gui,&done,&submitted);CHECK(done && submitted);
    reset();events[0]=WMHI_GADGETUP|GID_CONFIRM_YES;
    test_compose_cancel(&test_gui,&done,&submitted);CHECK(done && submitted && compose_queue_calls==1);
    reset();events[0]=WMHI_GADGETUP|GID_CONFIRM_YES;compose_queue_result=AMG_ERR_IO;
    test_compose_cancel(&test_gui,&done,&submitted);CHECK(!done && !submitted);
}
static void test_repeat_and_lifecycle(void)
{
    int i, j;
    for(i=1;i<=8;++i) {
        reset();fail_allocation=i;
        CHECK(confirm_question_result_for_window(&test_gui,&main_window,"Q","N",310)==GUI_CONFIRM_FAILED);
        CHECK(live_objects==0);
        fail_allocation=0;event_index=0;events[0]=WMHI_GADGETUP|GID_CONFIRM_YES;
        CHECK(confirm_delete_dialog(&test_gui)==1);
        CHECK(live_objects==0);
    }
    reset();
    for(j=0;j<128;++j) {
        event_index=0;wait_calls=0;events[0]=WMHI_GADGETUP|(j&1?GID_CONFIRM_YES:GID_CONFIRM_NO);
        CHECK(confirm_question_result_for_window(&test_gui,&main_window,"Q",j&1?"N":NULL,310)==
              (j&1?GUI_CONFIRM_YES:GUI_CONFIRM_NO));
        CHECK(live_objects==0);
    }
    reset();raw_qualifier=IEQUALIFIER_REPEAT;
    events[0]=WMHI_RAWKEY|GUI_RAWKEY_ESCAPE;events[1]=WMHI_GADGETUP|GID_CONFIRM_YES;event_count=2;
    CHECK(confirm_delete_dialog(&test_gui)==1 && event_index==2 && live_objects==0);
    reset();
    CHECK(compose_escape_request(&test_objects[0],WMHI_RAWKEY|GUI_RAWKEY_ESCAPE));
    raw_qualifier=IEQUALIFIER_REPEAT;
    CHECK(!compose_escape_request(&test_objects[0],WMHI_RAWKEY|GUI_RAWKEY_ESCAPE));
    CHECK(!compose_escape_request(&test_objects[0],WMHI_RAWKEY|GUI_RAWKEY_RETURN));
}
int main(void)
{
    test_log();test_failures();test_decisions();test_text_spacing();test_native_sizing();test_delete_safety();test_draft_safety();test_repeat_and_lifecycle();
    printf("requester diagnostics: %u checks passed (explicit API doubles).\n",checks);
    return 0;
}
