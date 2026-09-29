/* Window/input API doubles for the actual popup + icon dispatcher code. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned char UBYTE;
typedef short WORD;
typedef void *APTR;
typedef const char *CONST_STRPTR;
struct Hook { ULONG (*h_Entry)(struct Hook *,APTR,APTR); ULONG (*h_SubEntry)(struct Hook *,APTR,APTR); APTR h_Data; };
typedef struct Class { struct Hook cl_Dispatcher; } Class;
typedef struct Object { int kind, disposed; } Object;
typedef struct Message { ULONG MethodID; } *Msg;
struct Task { int alive; };
struct RastPort { UBYTE FgPen; };
struct DrawInfo { ULONG dri_Pens[12]; };
struct GadgetInfo { struct DrawInfo *gi_DrInfo; };
struct Gadget { WORD LeftEdge, TopEdge, Width, Height; ULONG Flags; };
struct Window { LONG LeftEdge, TopEdge, Width, Height, MouseX, MouseY; ULONG Flags; };
struct Screen { LONG Width,Height,MouseX,MouseY; };
struct gpInput { ULONG MethodID; struct GadgetInfo *gpi_GInfo; };
struct gpGoInactive { ULONG MethodID; struct GadgetInfo *gpgi_GInfo; };
struct gpRender { ULONG MethodID; struct GadgetInfo *gpr_GInfo; struct RastPort *gpr_RPort; };
typedef struct AmgError { int code; char message[256]; } AmgError;
typedef struct GuiLabel {
    char mailbox_utf8[512], server_mailbox_utf8[512], path_local[512], display_local[128];
    int available;
} GuiLabel;
typedef struct AmgNetwork { int id; } AmgNetwork;
typedef struct TestEvent { int type; ULONG uid; char argument1[768],argument2[768],message[256]; } TestEvent;
typedef struct AmgGui {
    struct Window *window; Object *window_object; struct Screen *screen;
    struct Gadget *reply_gadget,*reply_menu_gadget;
    char current_mailbox_utf8[512];
    GuiLabel labels[7]; size_t label_count;
    AmgNetwork *network;
} AmgGui;
#define AMIGMAIL_AMIGA 1
#define AMIGMAIL_MAX_LABELS 256U
#define AMG_OK 0
#define AMG_NET_DELETE 100
#define AMG_NET_MOVE 101
static void mock_tr_snprintf(char *buffer,size_t size,const char *format,...)
    __attribute__((format(printf,3,4),noinline));
static void mock_tr_snprintf(char *buffer,size_t size,const char *format,...)
{
    va_list args;va_start(args,format);vsnprintf(buffer,size,format,args);va_end(args);
}
#define amg_tr_snprintf(buffer,size,id,...) mock_tr_snprintf(buffer,size,__VA_ARGS__)
#define TRUE 1
#define FALSE 0
#define TAG_DONE 0UL
#define GMR_MEACTIVE 0UL
#define GFLG_SELECTED 1UL
#define GFLG_DISABLED 2UL
#define TEXTPEN 0
#define SHADOWPEN 1
#define WFLG_WINDOWACTIVE 1UL
#define WFLG_ACTIVATE 2UL
#define WFLG_RMBTRAP 4UL
#define SIGBREAKF_CTRL_C (1UL<<12)
#define IDCMP_GADGETUP 1UL
#define IDCMP_RAWKEY 2UL
#define IDCMP_INACTIVEWINDOW 4UL
#define WMHI_CLASSMASK 0xffff0000UL
#define WMHI_GADGETMASK 0xffffUL
#define WMHI_LASTMSG 0UL
#define WMHI_INACTIVE (3UL<<16)
#define WMHI_GADGETUP (2UL<<16)
#define WMHI_RAWKEY (11UL<<16)
#define ESC_KEY 0x45UL
#define GID_REPLY_MENU 17UL
#define GID_REPLY_MENU_REPLY_ALL 400UL
#define GID_REPLY_MENU_FORWARD 401UL
#define MESSAGE_ACTION_REPLY_ALL 4
#define MESSAGE_ACTION_FORWARD 5
#define GM_RENDER 1UL
#define GM_GOACTIVE 2UL
#define GM_HANDLEINPUT 3UL
#define GM_GOINACTIVE 4UL
#define T(id,en) (en)
enum { WA_Left=10,WA_Top,WA_Width,WA_Height,WA_Borderless,WA_Flags,WA_IDCMP,
WA_PubScreen,WINDOW_ParentGroup,LAYOUT_SpaceOuter,LAYOUT_SpaceInner,
LAYOUT_AddChild,GA_ID,GA_RelVerify,GA_Text,CHILD_MinWidth,CHILD_MaxWidth,
CHILD_WeightedWidth,CHILD_MinHeight,CHILD_MaxHeight,CHILD_WeightedHeight,WINDOW_SigMask };

static unsigned checks,failures,dispose_calls,signal_calls,input_calls,request_calls;
static int input_context,forbid_depth,mode,handled,waited,create_fail,open_fail,no_signal;
static Object objects[24];static size_t used_objects;
static struct Window main_window,popup_window;
static struct Screen screen;
static struct Gadget reply,arrow;
static struct Task gui_task={1};static Class test_class;
static AmgGui *active_gui;
#define CHECK(x) do { ++checks;if(!(x)){++failures;fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);} } while(0)
enum { HOLD,FAST_RELEASE,CANCEL_DRAG,OUTSIDE,ESCAPE,REPLY_ALL,FORWARD,BREAK };

static ULONG HookEntry(struct Hook *h,APTR o,APTR m) { (void)h;(void)o;(void)m;return 0; }
static void Forbid(void){++forbid_depth;}
static void Permit(void){CHECK(forbid_depth>0);--forbid_depth;}
static void Signal(struct Task *task,ULONG mask){CHECK(task==&gui_task&&task->alive);CHECK(mask==2UL);++signal_calls;}
static struct Task *FindTask(const char *name){(void)name;return &gui_task;}
static Class *BUTTON_GetClass(void){return &test_class;}
static Class *MakeClass(const char *id,const char *super,Class *cl,ULONG size,ULONG flags)
{(void)id;(void)super;(void)cl;(void)size;(void)flags;return &test_class;}
static int FreeClass(Class *cl){CHECK(cl==&test_class);return 1;}
static ULONG DoSuperMethodA(Class *cl,Object *object,Msg msg)
{(void)cl;(void)object;(void)msg;++input_calls;return GMR_MEACTIVE;}
static void SetAPen(struct RastPort *rp,ULONG pen){rp->FgPen=(UBYTE)pen;}
static void WritePixel(struct RastPort *rp,LONG x,LONG y){(void)rp;(void)x;(void)y;}
static struct RastPort *ObtainGIRPort(struct GadgetInfo *ginfo){(void)ginfo;return NULL;}
static void ReleaseGIRPort(struct RastPort *rp){(void)rp;}
static void RefreshGList(struct Gadget *g,struct Window *w,void *r,LONG n)
{(void)g;(void)w;(void)r;(void)n;}
static Object *NewObject(void *cl,const char *name,...)
{
    Object *o;(void)cl;CHECK(used_objects<24U);
    if(used_objects>=24U)return NULL;
    if(!strcmp(name,"window.class")&&create_fail)return NULL;
    o=&objects[used_objects++];o->kind=!strcmp(name,"window.class")?1:2;o->disposed=0;
    return o;
}
#define WindowObject NewObject(NULL,"window.class"
#define VGroupObject NewObject(NULL,"layout.gadget"
#define ButtonObject NewObject(NULL,"button.gadget"
#define EndObject TAG_DONE)
#define EndWindow TAG_DONE)
static struct Window *RA_OpenWindow(Object *o){CHECK(o&&o->kind==1);return open_fail?NULL:&popup_window;}
static void DisposeObject(Object *o){CHECK(!input_context);CHECK(o&&!o->disposed);if(o)o->disposed=1;++dispose_calls;}
static void ActivateWindow(struct Window *w){w->Flags|=WFLG_WINDOWACTIVE;}
static void WindowToFront(struct Window *w){(void)w;}
static void GetAttr(ULONG tag,Object *o,ULONG *out){CHECK(tag==WINDOW_SigMask);*out=o==active_gui->window_object?2UL:(no_signal?0UL:1UL);}
static int current_mailbox_is_drafts(const AmgGui *gui){(void)gui;return 0;}
static int rawkey_is_cancel(ULONG result){return (result&0xffUL)==ESC_KEY;}
static int focus_open_compose_window(AmgGui *gui){(void)gui;return 0;}
static void request_message(AmgGui *gui,int action,AmgError *error){(void)gui;(void)error;CHECK(action==MESSAGE_ACTION_REPLY_ALL||action==MESSAGE_ACTION_FORWARD);++request_calls;}
static ULONG Wait(ULONG mask);
static ULONG RA_HandleInput(Object *o,void *code);

static unsigned removed,queued;
static long unread_delta;
static ULONG removed_uid;
static char last_status[768],queued_source[768],queued_target[768];
static AmgNetwork networks[2]={{1},{2}};
static AmgNetwork *queued_network;
static int confirm_mode;
static void status_local(AmgGui *gui,const char *text){(void)gui;snprintf(last_status,sizeof(last_status),"%s",text);}
static void status_utf8(AmgGui *gui,const char *text){status_local(gui,text);}
static int message_is_seen(AmgGui *gui,ULONG uid){(void)gui;(void)uid;return 0;}
static void gui_state_adjust_inbox_unseen(AmgGui *gui,long delta){(void)gui;unread_delta+=delta;}
static void remove_message_uid(AmgGui *gui,ULONG uid){(void)gui;++removed;removed_uid=uid;}
static ULONG *selected_message_uids_alloc(AmgGui *gui,size_t *count)
{ ULONG *uids=(ULONG*)malloc(2U*sizeof(*uids));(void)gui;*count=2U;if(uids){uids[0]=42UL;uids[1]=43UL;}return uids; }
static int amg_network_is_connected(AmgNetwork *n){return n!=NULL;}
static int amg_network_request(AmgNetwork *n,int type,ULONG uid,const char *target,const char *source,AmgError *error)
{ (void)error;CHECK(type==AMG_NET_DELETE);CHECK(uid==42UL||uid==43UL);++queued;queued_network=n;
  snprintf(queued_source,sizeof(queued_source),"%s",source);snprintf(queued_target,sizeof(queued_target),"%s",target);return AMG_OK; }
static int confirm_delete_dialog(AmgGui *gui)
{
    if(confirm_mode==2){
        strcpy(gui->current_mailbox_utf8,"Archive");
        strcpy(gui->labels[0].server_mailbox_utf8,"Changed");
        strcpy(gui->labels[6].server_mailbox_utf8,"WrongTrash");
        gui->network=&networks[1];
    }
    return confirm_mode!=0;
}
void gui_reply_popup_close(AmgGui *gui);
void gui_reply_popup_finish_input(AmgGui *gui);
const char *gui_transfer_mailbox(const AmgGui *gui);
int gui_transfer_mailbox_matches(const AmgGui *gui,const char *mailbox);

#include "reply_popup_production.inc"

static void native_event(ULONG id)
{
    struct gpInput msg;
    msg.MethodID=id;msg.gpi_GInfo=NULL;
    input_context=1;
    (void)reply_arrow_dispatcher(&test_class.cl_Dispatcher,(APTR)&arrow,(APTR)&msg);
    input_context=0;
}
static ULONG Wait(ULONG mask)
{
    CHECK(!waited);waited=1;CHECK(mask&1UL);
    if(mode==BREAK)return SIGBREAKF_CTRL_C;
    if(mode==HOLD||mode==FAST_RELEASE||mode==CANCEL_DRAG){
        main_window.Flags|=WFLG_WINDOWACTIVE;
        native_event(GM_GOACTIVE);
        if(mode==FAST_RELEASE){native_event(GM_GOINACTIVE);screen.MouseX=0;screen.MouseY=0;}
    }else if(mode==OUTSIDE){main_window.Flags|=WFLG_WINDOWACTIVE;screen.MouseX=0;screen.MouseY=0;}
    return 1UL;
}
static ULONG RA_HandleInput(Object *o,void *code)
{
    (void)o;(void)code;
    if(handled++)return WMHI_LASTMSG;
    switch(mode){
        case ESCAPE:return WMHI_RAWKEY|ESC_KEY;
        case REPLY_ALL:return WMHI_GADGETUP|GID_REPLY_MENU_REPLY_ALL;
        case FORWARD:return WMHI_GADGETUP|GID_REPLY_MENU_FORWARD;
        default:return WMHI_INACTIVE;
    }
}
static void setup(AmgGui *gui,int choice)
{
    memset(gui,0,sizeof(*gui));memset(&main_window,0,sizeof(main_window));memset(objects,0,sizeof(objects));
    mode=choice;handled=waited=create_fail=open_fail=no_signal=0;
    dispose_calls=signal_calls=request_calls=0;used_objects=0;
    main_window.LeftEdge=10;main_window.TopEdge=20;
    screen.Width=640;screen.Height=480;
    reply.LeftEdge=150;reply.TopEdge=60;reply.Width=80;reply.Height=18;
    arrow.LeftEdge=230;arrow.TopEdge=60;arrow.Width=18;arrow.Height=18;
    screen.MouseX=main_window.LeftEdge+arrow.LeftEdge+5;screen.MouseY=main_window.TopEdge+arrow.TopEdge+5;
    gui->window=&main_window;gui->screen=&screen;gui->reply_gadget=&reply;gui->reply_menu_gadget=&arrow;
    gui->window_object=&objects[23];active_gui=gui;
    CHECK(gui_icons_init());gui_set_reply_arrow_expanded(0);
}
static void test_hold_release(int choice)
{
    AmgGui gui;unsigned i;
    setup(&gui,choice);
    CHECK(reply_action_popup(&gui)==0);
    CHECK(deferred_reply_popup!=NULL&&deferred_reply_popup_owner==&gui);
    CHECK(dispose_calls==0U&&reply_arrow_expanded==1);
    if(choice!=FAST_RELEASE){
        for(i=0;i<8U;++i){gui_reply_popup_finish_input(&gui);CHECK(dispose_calls==0U);}
        CHECK(gui_reply_arrow_was_pressed()&&!gui_reply_arrow_was_released());
        native_event(GM_GOINACTIVE);
    }
    CHECK(gui_reply_arrow_was_released()&&signal_calls==1U);
    CHECK(dispose_calls==0U); /* The input dispatcher never disposes windows. */
    if(choice==CANCEL_DRAG)gui_reply_popup_finish_input(&gui);
    else dispatch_main_arrow(&gui); /* consume closing release, not reopen */
    CHECK(dispose_calls==1U&&deferred_reply_popup==NULL&&reply_arrow_expanded==0);
    gui_reply_popup_finish_input(&gui);CHECK(dispose_calls==1U&&request_calls==0U);
    CHECK(reply_release_task==NULL);
    gui_icons_cleanup();
}
static void test_other_paths(void)
{
    int choice;
    for(choice=OUTSIDE;choice<=BREAK;++choice){
        AmgGui gui;int action;setup(&gui,choice);
        action=reply_action_popup(&gui);
        CHECK(action==(choice==REPLY_ALL?MESSAGE_ACTION_REPLY_ALL:choice==FORWARD?MESSAGE_ACTION_FORWARD:0));
        CHECK(dispose_calls==1U&&deferred_reply_popup==NULL&&reply_arrow_expanded==0);
        CHECK(reply_release_task==NULL);gui_icons_cleanup();
    }
    for(choice=0;choice<3;++choice){
        AmgGui gui;setup(&gui,HOLD);
        create_fail=choice==0;open_fail=choice==1;no_signal=choice==2;
        CHECK(reply_action_popup(&gui)==0);
        CHECK(dispose_calls==(choice==0?0U:1U));
        CHECK(deferred_reply_popup==NULL&&reply_release_task==NULL);
        gui_icons_cleanup();
    }
    {
        AmgGui gui,other;setup(&gui,HOLD);memset(&other,0,sizeof(other));
        CHECK(reply_action_popup(&gui)==0);
        gui_reply_popup_close(&other);CHECK(dispose_calls==0U);
        gui_reply_popup_close(&gui);CHECK(dispose_calls==1U&&reply_release_task==NULL);
        native_event(GM_GOINACTIVE);CHECK(signal_calls==0U);
        gui_reply_popup_close(&gui);CHECK(dispose_calls==1U);
        gui_icons_cleanup();
    }
}
static void mutation_setup(AmgGui *gui,const char *folder)
{
    memset(gui,0,sizeof(*gui));
    strcpy(gui->current_mailbox_utf8,folder);gui->label_count=7U;gui->network=&networks[0];
    strcpy(gui->labels[0].mailbox_utf8,"INBOX");strcpy(gui->labels[0].server_mailbox_utf8,"INBOX");gui->labels[0].available=1;
    strcpy(gui->labels[1].mailbox_utf8,"Archive");strcpy(gui->labels[1].server_mailbox_utf8,"Archive");gui->labels[1].available=1;
    strcpy(gui->labels[6].mailbox_utf8,"Trash");strcpy(gui->labels[6].server_mailbox_utf8,"Trash");gui->labels[6].available=1;
    removed=queued=0U;unread_delta=0;removed_uid=0UL;strcpy(last_status,"unchanged");
}
static void test_mutation_context(void)
{
    AmgGui gui;TestEvent event;AmgError error={0};
    memset(&event,0,sizeof(event));event.type=AMG_NET_DELETE;event.uid=42UL;
    strcpy(event.argument1,"Trash");strcpy(event.argument2,"INBOX");
    mutation_setup(&gui,"Archive");dispatch_mutation(&gui,event);
    CHECK(removed==0U&&unread_delta==0L&&!strcmp(last_status,"unchanged"));
    mutation_setup(&gui,"INBOX");dispatch_mutation(&gui,event);
    CHECK(removed==1U&&removed_uid==42UL&&unread_delta==-1L);
    CHECK(!strcmp(last_status,"Message moved to Trash."));
    strcpy(event.message,"Copied to destination; original only marked deleted (server lacks UIDPLUS).");
    mutation_setup(&gui,"INBOX");dispatch_mutation(&gui,event);
    CHECK(removed==1U&&!strcmp(last_status,event.message));
    event.type=AMG_NET_MOVE;event.message[0]=0;
    strcpy(event.argument1,"INBOX");strcpy(event.argument2,"Archive");
    mutation_setup(&gui,"Archive");dispatch_mutation(&gui,event);
    CHECK(removed==0U&&unread_delta==0L);
    mutation_setup(&gui,"INBOX");dispatch_mutation(&gui,event);
    CHECK(removed==1U&&unread_delta==-1L);
    for(confirm_mode=0;confirm_mode<3;++confirm_mode){
        mutation_setup(&gui,"INBOX");delete_selected_messages(&gui,&error);
        CHECK(queued==(confirm_mode?2U:0U));
        if(confirm_mode){
            CHECK(queued_network==&networks[0]);
            CHECK(!strcmp(queued_source,"INBOX")&&!strcmp(queued_target,"Trash"));
        }
    }
}

int main(void)
{
    test_hold_release(HOLD);test_hold_release(FAST_RELEASE);test_hold_release(CANCEL_DRAG);test_other_paths();
    test_mutation_context();
    CHECK(forbid_depth==0);
    printf("reply-popup: %u checks, %u failures\n",checks,failures);
    return failures?1:0;
}
