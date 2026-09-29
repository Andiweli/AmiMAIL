#ifndef AMIMAIL_RETRIEVAL_NATIVE_DOUBLE_H
#define AMIMAIL_RETRIEVAL_NATIVE_DOUBLE_H
/* Explicit ReAction/Exec/timer doubles, not an NDK or pixel-layout emulator. */
#include "account.h"
#include "periodic.h"
#include "network_task.h"
#include "i18n.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned long ULONG;
typedef long LONG;
typedef unsigned short UWORD;
typedef unsigned char UBYTE;
typedef signed char BYTE;
typedef void *APTR;
typedef UBYTE *STRPTR;
typedef const UBYTE *CONST_STRPTR;
struct Library { int unused; };
struct TagItem { ULONG ti_Tag, ti_Data; };
struct Device { int opened; };
struct MsgPort { UBYTE mp_SigBit; };
struct IORequest { struct Device *io_Device; UWORD io_Command; int pending, complete; };
struct timerequest { struct IORequest tr_node; struct { ULONG tv_secs, tv_micro; } tr_time; };
struct EClockVal { ULONG ev_hi, ev_lo; };
struct Window { int dummy; };
typedef struct Class { const char *name; } Class;
typedef struct Gadget Object;
struct Gadget {
    const Class *cl;
    char text[512];
    ULONG id, selected, disabled, chosen, popup, relverify;
    ULONG min_width, max_width, weight;
    char choices[AMG_PERIODIC_INTERVAL_COUNT][16];
    size_t choice_count;
    struct Gadget *children[16];
    size_t child_count;
};
typedef struct GuiAccountRuntime {
    unsigned long inbox_latest_uid, inbox_uid_validity;
    int inbox_baseline_ready, periodic_check_pending;
} GuiAccountRuntime;
struct AmgNetwork { int running, connected, fail; unsigned calls; AmgNetCommandType last; };
typedef struct AmgGui {
    AmgAccountSet *account_set;
    AmgAccount *account;
    struct MsgPort *periodic_timer_port;
    struct timerequest *periodic_timer_request;
    int periodic_timer_device_open, periodic_timer_pending;
    AmgPeriodicSchedule periodic_schedule;
    AmgNetwork *networks[AMG_MAX_ACCOUNTS];
    GuiAccountRuntime account_runtime[AMG_MAX_ACCOUNTS];
    size_t active_account;
    int periodic_check_pending, inbox_baseline_ready;
    unsigned long inbox_latest_uid, inbox_uid_validity;
} AmgGui;
#define TRUE 1UL
#define FALSE 0UL
#define TAG_DONE 0UL
#define NOSUB 31UL
#define FULLMENUNUM(m,i,s) (((m)<<16)|((i)<<5)|(s))
#define TIMERNAME "timer.device"
#define UNIT_VBLANK 1UL
#define TR_ADDREQUEST 9U
#define GUI_ACCOUNT_SMALL_BUTTON_WIDTH 32
#define T(id,en) amg_tr((id),(en))
/* Distinct identities for construction tests, not fabricated SDK aliases. */
#define GA_ID 0x1000UL
#define GA_Selected 0x1001UL
/* Real classic SDK alias, not a newly invented checkbox tag. */
#define CHECKBOX_Checked GA_Selected
#define GA_Disabled 0x1002UL
#define GA_Text 0x1003UL
#define GA_RelVerify 0x1004UL
#define GA_TabCycle 0x1005UL
#define GA_ReadOnly 0x1006UL
#define BUTTON_AutoButton 0x1100UL
#define BUTTON_PushButton 0x1101UL
#define BAG_CHECKBOX 1UL
#define STRINGA_MaxChars 0x1200UL
#define STRINGA_TextVal 0x1201UL
#define CHOOSER_PopUp 0x1300UL
#define CHOOSER_LabelArray 0x1301UL
#define CHOOSER_Selected 0x1302UL
#define CHOOSER_AutoFit 0x1303UL
#define LAYOUT_AddChild 0x1400UL
#define LAYOUT_SpaceInner 0x1401UL
#define LAYOUT_SpaceOuter 0x1402UL
#define CHILD_MinWidth 0x1500UL
#define CHILD_MaxWidth 0x1501UL
#define CHILD_WeightedWidth 0x1502UL
#define CHILD_WeightedHeight 0x1503UL
#define HGroupObject NewObject(&layout_class, NULL
#define VGroupObject HGroupObject
#define ButtonObject NewObject(&button_class, NULL
#define StringObject NewObject(&string_class, NULL
#define EndObject TAG_DONE)

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static Class layout_class={"layout"},button_class={"button"},
             string_class={"string"},chooser_class={"chooser"},label_class={"label"},
             test_checkbox_class={"checkbox"};
static struct Library checkbox_library;
static struct Library *CheckBoxBase=&checkbox_library;
static int checkbox_class_missing, checkbox_allocation_failure;
static unsigned checkbox_class_calls, checkbox_object_calls;
static unsigned live_objects,live_ports,live_requests,send_count,abort_count;
static unsigned native_call,fail_native;
static struct Device device;
static uint64_t eclock;

static Object *new_gadget(const Class *cl)
{
    Object *o=calloc(1,sizeof(*o));CHECK(o);o->cl=cl;++live_objects;return o;
}
static void apply_tags(Object *o,ULONG first,va_list args)
{
    ULONG tag=first;
    Object *child=NULL;
    while (tag!=TAG_DONE) {
        ULONG value=va_arg(args,ULONG);
        switch(tag) {
            case GA_ID:o->id=value;break;
            case GA_Selected:o->selected=value;break;
            case GA_RelVerify:o->relverify=value;break;
            case GA_Disabled:o->disabled=value;break;
            case GA_Text:case STRINGA_TextVal:
                snprintf(o->text,sizeof(o->text),"%s",(const char *)(uintptr_t)value);break;
            case CHOOSER_Selected:o->chosen=value;break;
            case CHOOSER_PopUp:o->popup=value;break;
            case CHOOSER_LabelArray: {
                STRPTR *p=(STRPTR *)(uintptr_t)value;
                o->choice_count=0U;
                while(*p) {
                    CHECK(o->choice_count<AMG_PERIODIC_INTERVAL_COUNT);
                    snprintf(o->choices[o->choice_count++],16U,"%s",(const char *)*p++);
                }
                break;
            }
            case LAYOUT_AddChild:
                CHECK(o->child_count<16U);
                child=(Object *)(uintptr_t)value;CHECK(child);
                o->children[o->child_count++]=child;break;
            case CHILD_MinWidth:CHECK(child);child->min_width=value;break;
            case CHILD_MaxWidth:CHECK(child);child->max_width=value;break;
            case CHILD_WeightedWidth:CHECK(child);child->weight=value;break;
            default:break;
        }
        tag=va_arg(args,ULONG);
    }
}
static Object *NewObject(Class *cl,CONST_STRPTR name,ULONG first,...)
{
    Object *o=new_gadget(cl);va_list ap;(void)name;
    va_start(ap,first);apply_tags(o,first,ap);va_end(ap);return o;
}
/* Only models the public constructor contract. It does not render a
 * checkmark or emulate Intuition mouse tracking. */
static Class *CHECKBOX_GetClass(void)
{
    ++checkbox_class_calls;
    CHECK(CheckBoxBase == &checkbox_library);
    return checkbox_class_missing ? NULL : &test_checkbox_class;
}
static Object *NewObjectA(Class *cl, CONST_STRPTR name, struct TagItem *tags)
{
    Object *o;
    ++checkbox_object_calls;
    CHECK(cl == &test_checkbox_class && !name && tags);
    CHECK(tags[0].ti_Tag == GA_ID);
    CHECK(tags[1].ti_Tag == GA_RelVerify && tags[1].ti_Data == TRUE);
    CHECK(tags[2].ti_Tag == CHECKBOX_Checked);
    CHECK(tags[2].ti_Data == TRUE || tags[2].ti_Data == FALSE);
    CHECK(tags[3].ti_Tag == TAG_DONE && tags[3].ti_Data == 0UL);
    if (checkbox_allocation_failure) return NULL;
    o = new_gadget(cl);
    o->id = tags[0].ti_Data;
    o->relverify = tags[1].ti_Data;
    o->selected = tags[2].ti_Data;
    return o;
}
static ULONG SetGadgetAttrs(struct Gadget *g,struct Window *w,APTR requester,ULONG first,...)
{
    va_list ap;(void)w;(void)requester;CHECK(g);
    va_start(ap,first);apply_tags(g,first,ap);va_end(ap);return 1UL;
}
static ULONG GetAttr(ULONG attr,Object *o,ULONG *result)
{
    CHECK(o && result);
    if(attr==GA_Selected)*result=o->selected;
    else if(attr==CHOOSER_Selected)*result=o->chosen;
    else {CHECK(0);return 0UL;}
    return 1UL;
}
static Class *CHOOSER_GetClass(void) { return &chooser_class; }
static void DisposeObject(Object *o)
{
    size_t i;if(!o)return;
    for(i=0U;i<o->child_count;++i)DisposeObject(o->children[i]);
    CHECK(live_objects);--live_objects;free(o);
}
static Object *static_text_label(const char *s)
{ return NewObject(&label_class,NULL,GA_Text,(ULONG)(uintptr_t)s,TAG_DONE); }
static const char *string_text(struct Gadget *g) {CHECK(g);return g->text;}
static void set_string(struct Gadget *g,struct Window *w,const char *s)
{(void)w;CHECK(g);snprintf(g->text,sizeof(g->text),"%s",s?s:"");}
static int local_to_utf8(const char *s,char *out,size_t cap)
{
    size_t n=strlen(s);if(n>=cap)return AMG_ERR_LIMIT;memcpy(out,s,n+1U);return AMG_OK;
}
static void utf8_to_local_copy(const char *s,char *out,size_t cap)
{snprintf(out,cap,"%s",s?s:"");}
static int account_is_locked(const AmgAccount *account)
{ return account->auth_mode==AMG_AUTH_OAUTH2_GOOGLE; }
static void status_local(AmgGui *gui,const char *text) {(void)gui;(void)text;}
static void status_utf8(AmgGui *gui,const char *text) {(void)gui;(void)text;}

static struct MsgPort *CreateMsgPort(void)
{
    struct MsgPort *p;if(++native_call==fail_native)return NULL;
    p=calloc(1,sizeof(*p));CHECK(p);p->mp_SigBit=4;++live_ports;return p;
}
static void DeleteMsgPort(struct MsgPort *p){CHECK(p && live_ports);--live_ports;free(p);}
static struct IORequest *CreateIORequest(struct MsgPort *p,ULONG size)
{
    struct IORequest *r;CHECK(p);
    if(++native_call==fail_native)return NULL;
    r=calloc(1,size);CHECK(r);++live_requests;return r;
}
static void DeleteIORequest(struct IORequest *r)
{CHECK(r && live_requests && !r->pending);--live_requests;free(r);}
static BYTE OpenDevice(CONST_STRPTR name,ULONG unit,struct IORequest *r,ULONG flags)
{
    CHECK(!strcmp((const char *)name,TIMERNAME) && unit==UNIT_VBLANK && !flags);
    if(++native_call==fail_native)return 1;
    r->io_Device=&device;device.opened=1;return 0;
}
static void CloseDevice(struct IORequest *r)
{CHECK(r && r->io_Device==&device && device.opened);device.opened=0;}
static ULONG SetSignal(ULONG value,ULONG mask){(void)value;(void)mask;return 0UL;}
static void SendIO(struct IORequest *r)
{
    CHECK(r && r->io_Device==&device && device.opened && !r->pending);
    CHECK(r->io_Command==TR_ADDREQUEST);r->pending=1;r->complete=0;++send_count;
}
static struct IORequest *CheckIO(struct IORequest *r){return r->complete?r:NULL;}
static LONG AbortIO(struct IORequest *r){CHECK(r->pending);r->complete=1;++abort_count;return 0L;}
static LONG WaitIO(struct IORequest *r){CHECK(r->pending && r->complete);r->pending=0;return 0L;}
static ULONG test_ReadEClock(struct Device *base,struct EClockVal *v)
{
    CHECK(base==&device && base->opened);
    v->ev_hi=(ULONG)(eclock>>32);v->ev_lo=(ULONG)(eclock & UINT32_MAX);return 1000000UL;
}
#define ReadEClock(v) test_ReadEClock(TimerBase,(v))
void periodic_timer_restart(AmgGui *gui);
void periodic_timer_cleanup(AmgGui *gui);
void periodic_fetch_mail(AmgGui *gui,unsigned long due_accounts,AmgError *error);

#endif
