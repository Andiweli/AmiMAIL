/* Production Herald transport and GUI bridge, executed against explicit
 * Exec/rexxsyslib/timer doubles. No Amiga or Herald server is emulated. */
#include "herald_native_double.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"%s:%d: %s\n", __FILE__,__LINE__,#x); exit(1); } } while (0)
static struct RxsLib mock_rexx;
static struct Task mock_gui_task;
static struct MsgPort server;
static struct Node server_tail, tails[8];
static struct MsgPort *ports[8];
static struct timerequest *timer_io;
static unsigned live_libs, live_ports, live_ios, live_devices, live_msgs, live_args;
static unsigned native_calls, fail_call, forbid_depth, wait_calls;
static ULONG allocated_signals;
static unsigned long long now_us, deadline_us;
static int server_running, return_during_wait;
static struct RexxMsg *held;
static unsigned sent_count;
static char sent[100][AMG_HERALD_COMMAND_MAX];
static int german;
#include "herald_translations.inc"

static int allocation_fails(void) { return ++native_calls == fail_call; }
static void list_init(struct MsgPort *p, struct Node *tail)
{
    memset(tail, 0, sizeof(*tail));
    p->mp_MsgList.lh_Head = tail;
    p->mp_MsgList.lh_Tail = NULL;
    p->mp_MsgList.lh_TailPred = NULL;
}
static void unlink_from(struct MsgPort *p, struct Node *n)
{
    if (n->ln_Pred) n->ln_Pred->ln_Succ = n->ln_Succ;
    else p->mp_MsgList.lh_Head = n->ln_Succ;
    if (n->ln_Succ->ln_Succ) n->ln_Succ->ln_Pred = n->ln_Pred;
    else p->mp_MsgList.lh_TailPred = n->ln_Pred;
    n->ln_Pred = n->ln_Succ = NULL;
}
static int contains(struct MsgPort *p, struct Node *n)
{
    struct Node *it;
    if (!p) return 0;
    for (it=p->mp_MsgList.lh_Head; it && it->ln_Succ; it=it->ln_Succ)
        if (it==n) return 1;
    return 0;
}
void Forbid(void) { ++forbid_depth; }
void Permit(void) { CHECK(forbid_depth > 0U); --forbid_depth; }
ULONG SetSignal(ULONG value, ULONG mask)
{
    ULONG old=mock_gui_task.signals;
    mock_gui_task.signals=(old & ~mask) | (value & mask);
    return old;
}
void FreeSignal(LONG bit)
{
    CHECK(bit>=0 && bit<31); CHECK(allocated_signals & (1UL<<(ULONG)bit));
    allocated_signals &= ~(1UL<<(ULONG)bit);
}
struct MsgPort *FindPort(CONST_STRPTR name)
{
    CHECK(forbid_depth>0U); CHECK(!strcmp((const char *)name,"HERALD"));
    return server_running ? &server : NULL;
}
struct MsgPort *CreateMsgPort(void)
{
    unsigned i,bit;
    struct MsgPort *p;
    if (allocation_fails()) return NULL;
    for(i=0;i<8U && ports[i];++i) {}
    CHECK(i<8U);
    for(bit=1; allocated_signals & (1UL<<bit);++bit) {}
    CHECK(bit<31U);
    p=(struct MsgPort *)calloc(1,sizeof(*p)); CHECK(p!=NULL);
    p->mp_Flags=PA_SIGNAL; p->mp_SigBit=(UBYTE)bit;
    p->mp_SigTask=&mock_gui_task;
    allocated_signals |= 1UL<<bit;
    list_init(p,&tails[i]); ports[i]=p; ++live_ports;
    return p;
}
static void free_port_memory(struct MsgPort *p)
{
    unsigned i;
    CHECK(!p->mp_MsgList.lh_Head->ln_Succ);
    for(i=0;i<8U && ports[i]!=p;++i) {}
    CHECK(i<8U); ports[i]=NULL; --live_ports; free(p);
}
void DeleteMsgPort(struct MsgPort *p)
{
    CHECK(p!=NULL); FreeSignal(p->mp_SigBit); free_port_memory(p);
}
void PutMsg(struct MsgPort *p, struct Message *m)
{
    struct Node *n=&m->mn_Node;
    CHECK(p && m && !n->ln_Succ && !n->ln_Pred);
    if(p==&server) {
        CHECK(forbid_depth>0U); CHECK(sent_count<100U);
        CHECK(!strcmp((const char *)n->ln_Name,"REXX"));
        CHECK(((struct RexxMsg *)m)->rm_Action==(RXCOMM|RXFF_RESULT));
        snprintf(sent[sent_count++],sizeof(sent[0]),"%s",
                 (const char *)((struct RexxMsg *)m)->rm_Args[0]);
    }
    n->ln_Pred=p->mp_MsgList.lh_TailPred;
    if(n->ln_Pred) {
        n->ln_Succ=n->ln_Pred->ln_Succ; n->ln_Pred->ln_Succ=n;
    } else { n->ln_Succ=p->mp_MsgList.lh_Head; p->mp_MsgList.lh_Head=n; }
    p->mp_MsgList.lh_TailPred=n;
    if(p->mp_Flags==PA_SIGNAL && p->mp_SigTask) {
        CHECK(p->mp_SigTask==&mock_gui_task);
        p->mp_SigTask->signals |= 1UL<<p->mp_SigBit;
    }
}
struct Message *GetMsg(struct MsgPort *p)
{
    struct Node *n=p->mp_MsgList.lh_Head;
    if(!n || !n->ln_Succ) return NULL;
    unlink_from(p,n); return (struct Message *)n;
}
void ReplyMsg(struct Message *m)
{
    m->mn_Node.ln_Type=NT_REPLYMSG; PutMsg(m->mn_ReplyPort,m);
}
void Remove(struct Node *n)
{
    unsigned i;
    CHECK(forbid_depth>0U);
    if(contains(&server,n)) { unlink_from(&server,n); return; }
    for(i=0;i<8U;++i) if(contains(ports[i],n)) { unlink_from(ports[i],n); return; }
    CHECK(0);
}
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version)
{
    CHECK(!strcmp((const char *)name,"rexxsyslib.library")); CHECK(version==36UL);
    if(allocation_fails()) return NULL;
    ++live_libs; return (struct Library *)&mock_rexx;
}
void CloseLibrary(struct Library *base)
{
    CHECK(base==(struct Library *)&mock_rexx && live_libs); --live_libs;
}
struct IORequest *CreateIORequest(struct MsgPort *port, ULONG size)
{
    if(allocation_fails()) return NULL;
    CHECK(size==sizeof(struct timerequest)); CHECK(!timer_io);
    timer_io=(struct timerequest *)calloc(1,size); CHECK(timer_io);
    timer_io->tr_node.io_Message.mn_ReplyPort=port; ++live_ios;
    return &timer_io->tr_node;
}
void DeleteIORequest(struct IORequest *io)
{
    CHECK(io==&timer_io->tr_node && !io->pending);
    free(timer_io); timer_io=NULL; --live_ios;
}
BYTE OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *io, ULONG flags)
{
    CHECK(!strcmp((const char *)name,TIMERNAME) && unit==UNIT_VBLANK);
    CHECK(io && !flags);
    if(allocation_fails()) return -1;
    ++live_devices; return 0;
}
void CloseDevice(struct IORequest *io) { CHECK(io && live_devices); --live_devices; }
void SendIO(struct IORequest *io)
{
    CHECK(io && !io->pending && live_devices);
    io->pending=1; io->complete=0;
    deadline_us=now_us+timer_io->tr_time.tv_secs*1000000ULL+timer_io->tr_time.tv_micro;
}
static void advance_us(unsigned long long amount)
{
    now_us+=amount;
    if(timer_io && timer_io->tr_node.pending && now_us>=deadline_us) {
        timer_io->tr_node.complete=1;
        mock_gui_task.signals |= 1UL<<timer_io->tr_node.io_Message.mn_ReplyPort->mp_SigBit;
    }
}
struct IORequest *CheckIO(struct IORequest *io) { return io->complete ? io : NULL; }
LONG AbortIO(struct IORequest *io) { CHECK(io->pending); io->complete=1; return 0; }
LONG WaitIO(struct IORequest *io)
{ CHECK(io->pending && io->complete); io->pending=0; return 0; }
void test_rexx_base(const struct RxsLib *base)
{ CHECK(base==&mock_rexx && live_libs); }
struct RexxMsg *test_CreateRexxMsg(struct MsgPort *reply, STRPTR extension, STRPTR host)
{
    struct RexxMsg *m;
    CHECK(reply && !extension && !host);
    if(allocation_fails()) return NULL;
    m=(struct RexxMsg *)calloc(1,sizeof(*m)); CHECK(m);
    m->rm_Node.mn_ReplyPort=reply; m->rm_Node.mn_Node.ln_Type=NT_MESSAGE;
    ++live_msgs; return m;
}
void test_DeleteRexxMsg(struct RexxMsg *m)
{ CHECK(m && !m->rm_Node.mn_Node.ln_Succ && live_msgs); --live_msgs; free(m); }
STRPTR test_CreateArgstring(STRPTR text, ULONG length)
{
    char *s;
    if(allocation_fails()) return NULL;
    s=(char *)malloc(length+1U); CHECK(s);
    memcpy(s,text,length); s[length]=0; ++live_args; return (STRPTR)s;
}
void test_DeleteArgstring(UBYTE *s) { CHECK(s && live_args); --live_args; free(s); }
static void reply_held(const char *result, LONG rc)
{
    CHECK(held);
    held->rm_Result1=rc;
    held->rm_Result2=rc==RC_OK && result
        ? (LONG)(uintptr_t)test_CreateArgstring((STRPTR)result,strlen(result)) : 42L;
    ReplyMsg(&held->rm_Node); held=NULL;
}
static void take_request(void)
{
    CHECK(!held); held=(struct RexxMsg *)GetMsg(&server); CHECK(held);
}
static void answer(const char *result)
{ take_request(); reply_held(result,RC_OK); }
ULONG Wait(ULONG mask)
{
    ULONG result;
    CHECK(!forbid_depth); ++wait_calls;
    if(return_during_wait && held) { return_during_wait=0; reply_held("OK, MSG QUEUED",RC_OK); }
    result=mock_gui_task.signals & mask;
    if(!result) {
        CHECK(timer_io && timer_io->tr_node.pending);
        if(deadline_us>now_us) advance_us(deadline_us-now_us);
        else advance_us(0);
        result=mock_gui_task.signals & mask;
    }
    CHECK(result); mock_gui_task.signals &= ~result;
    return result;
}
const char *amg_tr(long id,const char *english)
{
    size_t i;
    if(german) for(i=0;i<sizeof(translations)/sizeof(translations[0]);++i)
        if(translations[i].id==id) return translations[i].de;
    return english;
}
/* The real catalog validator verifies translated format arguments. Clang
 * cannot prove that relationship inside this test-only formatting double. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#endif
int amg_tr_snprintf(char *out,size_t cap,long id,const char *english,...)
{
    int result; va_list ap;
    va_start(ap,english); result=vsnprintf(out,cap,amg_tr(id,english),ap); va_end(ap);
    return result;
}
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include "../src/herald.c"
#include "../src/gui_herald.c"

static void reset(void)
{
    CHECK(!live_libs && !live_ports && !live_ios && !live_devices && !live_msgs && !live_args);
    CHECK(!allocated_signals && !forbid_depth && !held);
    memset(&server,0,sizeof(server)); list_init(&server,&server_tail);
    memset(&mock_gui_task,0,sizeof(mock_gui_task));
    native_calls=fail_call=sent_count=wait_calls=0U;
    now_us=deadline_us=0ULL; server_running=1; return_during_wait=german=0;
}
static void ack_registration(AmgHerald *h)
{
    CHECK(strstr(sent[sent_count-1U],"REGISTERAPP APP=AmiMAIL")!=NULL);
    answer("OK, APP REGISTERED"); amg_herald_poll(h);
    CHECK(strstr(sent[sent_count-1U],"NOTIFY APP=AmiMAIL")!=NULL);
}
static void ack_notification(AmgHerald *h)
{ answer("OK, MSG QUEUED"); amg_herald_poll(h); }
static void cooldown(AmgHerald *h) { advance_us(2000000ULL); amg_herald_poll(h); }
static void simple_and_failure_tests(void)
{
    AmgHerald *h; unsigned fail;
    reset(); h=amg_herald_create(); CHECK(h); CHECK(amg_herald_signal_mask(h));
    CHECK(amg_herald_notify(h,0,"Hello",1)==AMG_HERALD_QUEUED);
    CHECK(!wait_calls && live_msgs==1 && live_args==2);
    ack_registration(h); ack_notification(h);
    CHECK(amg_herald_test_result(h)==AMG_HERALD_ACCEPTED);
    CHECK(!live_msgs && !live_args && !wait_calls);
    amg_herald_destroy(h); reset();
    h=amg_herald_create(); CHECK(h);
    CHECK(amg_herald_notify(h,4,"Again",1)==AMG_HERALD_QUEUED);
    answer("ERROR: APP ALREADY REGISTERED"); amg_herald_poll(h);
    CHECK(strstr(sent[1],"ID=test4 GROUP=test4 UPDATE")!=NULL);
    ack_notification(h); amg_herald_destroy(h); reset();
    /* Every native allocation point in initialization or first dispatch. */
    for(fail=1U;fail<=8U;++fail) {
        fail_call=fail; h=amg_herald_create();
        if(h) {
            CHECK(amg_herald_notify(h,0,"Test",1)==AMG_HERALD_UNAVAILABLE);
            amg_herald_destroy(h);
        }
        reset();
    }
    h=amg_herald_create(); CHECK(h); server_running=0;
    CHECK(amg_herald_notify(h,0,"Test",1)==AMG_HERALD_NOT_RUNNING);
    CHECK(!sent_count && !live_msgs); server_running=1;
    CHECK(amg_herald_notify(h,0,"Test",1)==AMG_HERALD_QUEUED);
    answer("ERROR: APP NOT ALLOWED"); amg_herald_poll(h);
    CHECK(sent_count==1 && amg_herald_test_result(h)==AMG_HERALD_REJECTED);
    cooldown(h); CHECK(amg_herald_notify(h,0,"Test",1)==AMG_HERALD_QUEUED);
    ack_registration(h); answer("ERROR: OUT OF MEMORY"); amg_herald_poll(h);
    CHECK(amg_herald_test_result(h)==AMG_HERALD_REJECTED);
    cooldown(h); CHECK(amg_herald_notify(h,0,"Test",1)==AMG_HERALD_QUEUED);
    take_request(); reply_held(NULL,10L); amg_herald_poll(h);
    CHECK(amg_herald_test_result(h)==AMG_HERALD_REJECTED);
    amg_herald_destroy(h); reset();
}
static void restart_timeout_shutdown_tests(void)
{
    AmgHerald *h; unsigned before;
    struct RexxMsg *orphan; struct MsgPort *orphan_port;
    reset(); h=amg_herald_create(); CHECK(h);
    CHECK(amg_herald_notify(h,1,"Test",1)==AMG_HERALD_QUEUED);
    ack_registration(h);
    /* Herald restarted after registration; one bounded retry, not a loop. */
    answer("ERROR: APP NOT REGISTERED"); amg_herald_poll(h);
    ack_registration(h); answer("ERROR: APP NOT REGISTERED"); amg_herald_poll(h);
    CHECK(sent_count==4 && amg_herald_test_result(h)==AMG_HERALD_REJECTED);
    amg_herald_destroy(h); reset();
    h=amg_herald_create(); CHECK(h);
    CHECK(amg_herald_notify(h,0,"Not taken",1)==AMG_HERALD_QUEUED);
    advance_us(5000000ULL); amg_herald_poll(h);
    CHECK(amg_herald_test_result(h)==AMG_HERALD_TIMEOUT);
    CHECK(!live_msgs && !server.mp_MsgList.lh_Head->ln_Succ && !wait_calls);
    amg_herald_destroy(h); reset();
    h=amg_herald_create(); CHECK(h);
    CHECK(amg_herald_notify(h,0,"Held",1)==AMG_HERALD_QUEUED); take_request();
    advance_us(5000000ULL); amg_herald_poll(h);
    CHECK(amg_herald_test_result(h)==AMG_HERALD_TIMEOUT && live_msgs==1);
    before=sent_count;
    CHECK(amg_herald_notify(h,1,"Do not accumulate",0)==AMG_HERALD_BUSY);
    CHECK(sent_count==before && live_msgs==1 && !wait_calls);
    reply_held("OK, APP REGISTERED",RC_OK); amg_herald_poll(h);
    CHECK(sent_count==before && !live_msgs); /* no stale NOTIFY after timeout */
    cooldown(h); CHECK(amg_herald_notify(h,1,"Recovered",1)==AMG_HERALD_QUEUED);
    ack_registration(h); ack_notification(h); amg_herald_destroy(h); reset();
    /* Quit while the request is still queued: reclaim without waiting. */
    h=amg_herald_create(); CHECK(h);
    (void)amg_herald_notify(h,0,"Queued at Quit",1);
    amg_herald_destroy(h); CHECK(!wait_calls); reset();
    /* Quit races a healthy reply: consume it within the 250 ms grace period. */
    h=amg_herald_create(); CHECK(h); (void)amg_herald_notify(h,0,"Quitting",1);
    take_request(); return_during_wait=1;
    amg_herald_destroy(h); CHECK(wait_calls<=2U && now_us<=250000ULL); reset();
    /* Receiver never replies. Never free its borrowed message or signal a
     * destroyed GUI task. Explicitly reap the simulated quarantine afterwards
     * so LeakSanitizer can still check all other paths. */
    h=amg_herald_create(); CHECK(h); (void)amg_herald_notify(h,0,"Hung",1);
    take_request(); orphan=held; orphan_port=held->rm_Node.mn_ReplyPort;
    amg_herald_destroy(h);
    CHECK(now_us==250000ULL && live_msgs==1 && live_ports==1 && live_libs==1);
    CHECK(!allocated_signals && !live_ios && !live_devices);
    CHECK(orphan_port->mp_Flags==PA_IGNORE && !orphan_port->mp_SigTask);
    CHECK(!strcmp((const char *)orphan->rm_Node.mn_Node.ln_Name,"REXX"));
    mock_gui_task.signals=0;
    reply_held("OK, APP REGISTERED",RC_OK);
    CHECK(!mock_gui_task.signals && GetMsg(orphan_port)==&orphan->rm_Node);
    test_DeleteArgstring((UBYTE *)(uintptr_t)orphan->rm_Result2);
    test_DeleteArgstring(orphan->rm_Args[0]);
    test_DeleteArgstring((UBYTE *)orphan->rm_Node.mn_Node.ln_Name);
    test_DeleteRexxMsg(orphan); free_port_memory(orphan_port);
    CloseLibrary((struct Library *)&mock_rexx); reset();
}
static void queue_tests(void)
{
    AmgHerald *h; size_t slot; unsigned before;
    reset(); h=amg_herald_create(); CHECK(h);
    for(slot=0;slot<AMG_MAX_ACCOUNTS;++slot)
        CHECK(amg_herald_notify(h,slot,"Batch",0)==AMG_HERALD_QUEUED);
    CHECK(sent_count==1 && live_msgs==1);
    for(slot=0;slot<AMG_MAX_ACCOUNTS;++slot) {
        char id[64];
        ack_registration(h);
        snprintf(id,sizeof(id),"ID=mail%lu GROUP=mail%lu",(unsigned long)slot,(unsigned long)slot);
        CHECK(strstr(sent[sent_count-1U],id)!=NULL);
        before=sent_count; ack_notification(h);
        CHECK(sent_count==before); cooldown(h);
    }
    CHECK(sent_count==10U && !live_msgs && !wait_calls);
    amg_herald_destroy(h); reset();
    h=amg_herald_create(); CHECK(h);
    (void)amg_herald_notify(h,0,"First",0);
    (void)amg_herald_notify(h,1,"Old unsent batch",0);
    (void)amg_herald_notify(h,1,"Latest batch",0);
    amg_herald_discard_account(h,0); answer("OK, APP REGISTERED"); amg_herald_poll(h);
    CHECK(sent_count==1U); cooldown(h); ack_registration(h);
    CHECK(strstr(sent[sent_count-1U],"Latest batch")!=NULL);
    ack_notification(h); cooldown(h);
    CHECK(amg_herald_notify(h,2,"No server after register",1)==AMG_HERALD_QUEUED);
    take_request(); server_running=0; reply_held("OK, APP REGISTERED",RC_OK); amg_herald_poll(h);
    CHECK(amg_herald_test_result(h)==AMG_HERALD_NOT_RUNNING && !live_msgs);
    amg_herald_destroy(h); reset();
}
static void gui_tests(void)
{
    AmgAccountSet accounts; AmgGui gui; size_t i;
    reset(); memset(&accounts,0,sizeof(accounts)); memset(&gui,0,sizeof(gui)); gui.account_set=&accounts;
    for(i=0;i<AMG_MAX_ACCOUNTS;++i) { accounts.accounts[i].enabled=1; accounts.order[i]=i; }
    strcpy(accounts.accounts[0].account_name,"Privat");
    gui_herald_new_mail(&gui,0,1,NULL,0U,0UL); CHECK(!gui.herald && !sent_count);
    accounts.accounts[0].herald_notifications=1;
    server_running=0; gui_herald_new_mail(&gui,0,1,NULL,0U,0UL); CHECK(!gui.herald && !sent_count);
    server_running=1; gui_herald_new_mail(&gui,0,0,NULL,0U,0UL); CHECK(!gui.herald);
    /* Herald remains independent with notification_sound=0. */
    german=1; gui_herald_new_mail(&gui,0,3,NULL,0U,0UL); CHECK(gui.herald);
    ack_registration(gui.herald); CHECK(strstr(sent[1],"3 neue Nachrichten - Privat")!=NULL);
    CHECK(strstr(sent[1],"NOSOUND")!=NULL);
    CHECK(strstr(sent[1],"\\nNeueste: (Kein Betreff)")!=NULL);
    ack_notification(gui.herald); cooldown(gui.herald);
    accounts.order[0]=4; accounts.order[4]=0;
    gui_herald_new_mail(&gui,0,1,NULL,0U,0UL); ack_registration(gui.herald);
    CHECK(strstr(sent[3],"ID=mail0 GROUP=mail0") && strstr(sent[3],"1 neue Nachricht - Privat"));
    CHECK(strstr(sent[3],"\\nBetreff: (Kein Betreff)")!=NULL);
    ack_notification(gui.herald); cooldown(gui.herald);
    german=0; CHECK(gui_herald_test(&gui,0,"The \"Boss\" *") == AMG_HERALD_QUEUED);
    CHECK(gui_herald_test(&gui,1,"Other")==AMG_HERALD_BUSY);
    ack_registration(gui.herald);
    CHECK(strstr(sent[5],"ID=test0 GROUP=test0") && strstr(sent[5],"The *\"Boss*\" **"));
    ack_notification(gui.herald); cooldown(gui.herald);
    accounts.accounts[1].herald_notifications=1; accounts.accounts[1].enabled=0;
    gui_herald_new_mail(&gui,1,1,NULL,0U,0UL); CHECK(sent_count==6U);
    CHECK(!strcmp(gui_herald_result_text(AMG_HERALD_ACCEPTED),"Herald accepted the test notification."));
    german=1; CHECK(!strcmp(gui_herald_result_text(AMG_HERALD_NOT_RUNNING),"Herald ist nicht gestartet."));
    amg_herald_destroy(gui.herald); reset();
}
/* Decode the command's quoted TEXT field as Herald does before handling
 * its literal backslash+n line separator. Exercise the actual GUI formatter,
 * MIME header decoder and transport together, not just the helper in isolation. */
static void notification_text(char output[AMG_HERALD_TEXT_MAX + 1U])
{
    const char *p = strstr(sent[sent_count - 1U], "TEXT=\"");
    size_t used = 0U;
    CHECK(p != NULL);
    p += 6U;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '*') {
            CHECK(*p == '*' || *p == '"');
            c = *p++;
        }
        CHECK(used < AMG_HERALD_TEXT_MAX);
        output[used++] = c;
    }
    CHECK(*p == '"' && p[1] == 0);
    output[used] = 0;
}

static void gui_subject_tests(void)
{
    AmgAccountSet accounts;
    AmgGui gui;
    char payload[4096], body[1600], content[AMG_HERALD_TEXT_MAX + 1U];
    const char *older = "Subject: old message\r\n\r\n";
    const char *newest =
        "Date: Wed, 01 Jan 2020 00:00:00 +0000\r\n"
        "Subject: =?UTF-8?Q?Gr=C3=BC=C3=9Fe?= \"Boss\" *\r\n\r\n";
    const char *deleted = "Subject: deleted message\r\n\r\n";
    int written;
    unsigned language;
    size_t used, i;
    reset();
    memset(&accounts, 0, sizeof(accounts));
    memset(&gui, 0, sizeof(gui));
    gui.account_set = &accounts;
    accounts.accounts[2].enabled = 1;
    accounts.accounts[2].herald_notifications = 1;
    strcpy(accounts.accounts[2].account_name, "Privat");
    written = snprintf(payload, sizeof(payload),
        "* 1 FETCH (UID 103 FLAGS () BODY[HEADER.FIELDS (SUBJECT DATE)] {%lu}\r\n%s)\r\n"
        "* 2 FETCH (UID 104 FLAGS (\\Deleted) BODY[HEADER.FIELDS (SUBJECT)] {%lu}\r\n%s)\r\n"
        "* 3 FETCH (UID 102 FLAGS () BODY[HEADER.FIELDS (SUBJECT)] {%lu}\r\n%s)\r\n",
        (unsigned long)strlen(newest), newest,
        (unsigned long)strlen(deleted), deleted,
        (unsigned long)strlen(older), older);
    CHECK(written > 0 && (size_t)written < sizeof(payload));
    german = 1;
    gui_herald_new_mail(&gui, 2U, 2UL, (const unsigned char *)payload,
                        (size_t)written, 101UL);
    CHECK(gui.herald != NULL);
    ack_registration(gui.herald);
    notification_text(content);
    CHECK(!strcmp(content, "2 neue Nachrichten - Privat\\nNeueste: Gr\374\337e \"Boss\" *"));
    CHECK(strstr(sent[sent_count - 1U], "ID=mail2 GROUP=mail2 UPDATE") != NULL);
    CHECK(strstr(sent[sent_count - 1U], "NOSOUND") != NULL);
    ack_notification(gui.herald); cooldown(gui.herald);
    german = 0;
    gui_herald_new_mail(&gui, 2U, 1UL, (const unsigned char *)payload,
                        (size_t)written, 102UL);
    ack_registration(gui.herald);
    notification_text(content);
    CHECK(!strcmp(content, "1 new message - Privat\\nSubject: Gr\374\337e \"Boss\" *"));
    ack_notification(gui.herald); cooldown(gui.herald);

    /* Deliberately hostile/long header contents cannot inject another line or
     * command. Literal quotes/stars remain round-trip safe after truncation. */
    memset(accounts.accounts[2].account_name, 'A',
           sizeof(accounts.accounts[2].account_name) - 1U);
    accounts.accounts[2].account_name[sizeof(accounts.accounts[2].account_name) - 1U] = 0;
    strcpy(body, "Subject: NEXT\\nPRI=10 \"Q\" * ");
    used = strlen(body);
    for (i = 0U; i < 800U; ++i) body[used++] = (i % 2U) ? '*' : '"';
    memcpy(body + used, "\r\n\r\n", 5U);
    written = snprintf(payload, sizeof(payload),
        "* 1 FETCH (UID 200 FLAGS () BODY[HEADER.FIELDS (SUBJECT)] {%lu}\r\n%s)\r\n",
        (unsigned long)strlen(body), body);
    CHECK(written > 0 && (size_t)written < sizeof(payload));
    for (language = 0U; language < 2U; ++language) {
        const char *separator;
        german = (int)language;
        gui_herald_new_mail(&gui, 2U, 4294967295UL, (const unsigned char *)payload,
                            (size_t)written, 199UL);
        ack_registration(gui.herald);
        notification_text(content);
        CHECK(strlen(content) == AMG_HERALD_TEXT_MAX);
        CHECK(!strcmp(content + AMG_HERALD_TEXT_MAX - 3U, "..."));
        CHECK(strstr(content, "NEXT nPRI=10 \"Q\" *") != NULL);
        separator = strstr(content, "\\n");
        CHECK(separator != NULL && strstr(separator + 2U, "\\n") == NULL);
        CHECK(strstr(separator, language ? "\\nNeueste: " : "\\nLatest: ") != NULL);
        CHECK(strlen(sent[sent_count - 1U]) < AMG_HERALD_COMMAND_MAX);
        ack_notification(gui.herald); cooldown(gui.herald);
    }
    amg_herald_destroy(gui.herald); reset();
}

static void quoted_roundtrip(const char *input)
{
    char command[AMG_HERALD_COMMAND_MAX], output[AMG_HERALD_TEXT_MAX+1U];
    const char *p; size_t n=0U,i;
    CHECK(amg_herald_build_notify(4,0,input,command,sizeof(command))==AMG_OK);
    p=strstr(command,"TEXT=\""); CHECK(p); p+=6;
    while(*p) {
        char c=*p++;
        if(c=='"') { CHECK(!*p); break; }
        if(c=='*') { CHECK(*p=='*' || *p=='"'); c=*p++; }
        CHECK(n<AMG_HERALD_TEXT_MAX); output[n++]=c;
    }
    output[n]=0; CHECK(n==strlen(input));
    for(i=0;i<n;++i) {
        unsigned char c=(unsigned char)input[i];
        if(c<32U || c==127U)c=' ';
        CHECK((unsigned char)output[i]==c);
    }
}
static void command_tests(void)
{
    char text[AMG_HERALD_TEXT_MAX+2U], command[AMG_HERALD_COMMAND_MAX];
    unsigned i,j; unsigned long seed=17;
    quoted_roundtrip(""); quoted_roundtrip("The \"Boss\" **\r\nNEXT PRI=10 ");
    memset(text,'*',AMG_HERALD_TEXT_MAX); text[AMG_HERALD_TEXT_MAX]=0; quoted_roundtrip(text);
    text[AMG_HERALD_TEXT_MAX]='x';text[AMG_HERALD_TEXT_MAX+1U]=0;
    CHECK(amg_herald_build_notify(0,0,text,command,sizeof(command))==AMG_ERR_ARGUMENT);
    CHECK(!command[0]);
    CHECK(amg_herald_build_notify(5,0,"x",command,sizeof(command))==AMG_ERR_ARGUMENT);
    CHECK(amg_herald_build_notify(0,0,"xx",command,sizeof(command))==AMG_OK);
    { size_t required = strlen(command) + 1U;
      for(i=1;i<required;++i) {
        memset(command,1,sizeof(command));
        CHECK(amg_herald_build_notify(0,0,"xx",command,i)!=AMG_OK);
        CHECK(!command[0] && command[i]==1);
      }
      CHECK(amg_herald_build_notify(0,0,"xx",command,required)==AMG_OK);
    }
    for(i=0;i<500U;++i) {
        unsigned len=i%(AMG_HERALD_TEXT_MAX+1U);
        for(j=0;j<len;++j) { seed=(seed*1664525UL+1013904223UL)&0xffffffffUL; text[j]=(char)(1U+(seed%255U)); }
        text[len]=0; quoted_roundtrip(text);
    }
}
int main(void)
{
    command_tests(); simple_and_failure_tests(); restart_timeout_shutdown_tests(); queue_tests(); gui_tests(); gui_subject_tests();
    printf("Herald native/API-double + GUI bridge: %u checks passed.\n",checks);
    return 0;
}
