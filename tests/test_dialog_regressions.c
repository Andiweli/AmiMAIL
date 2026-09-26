/* Host control-flow tests with API doubles; NOT an Amiga SDK or GUI emulator.
 * Public class names deliberately do not resolve. The opened class pointers
 * do. Constructors can fail before or after accepting their children.
 */
#include "native_api.h"
#include "gui_internal.h"
#include "i18n.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This test double always uses its English printf format verbatim. */
int amg_tr_snprintf(char *output, size_t capacity, long id,
                    const char *format, ...)
    __attribute__((format(printf, 4, 5)));

/* Native vararg tag APIs are modelled as typed tag arrays, like NDK inlines. */
static struct ColumnInfo *test_columns(ULONG count, const ULONG *tags);
static struct Node *test_node(ULONG count, const ULONG *tags);
static ULONG test_node_get(struct Node *node, const ULONG *tags);
static ULONG test_node_set(struct Node *node, const ULONG *tags);
static ULONG test_attrs(Object *object, const ULONG *tags);
static ULONG test_gattrs(struct Gadget *gadget, struct Window *window,
                          void *requester, const ULONG *tags);
static void *test_asl(ULONG type, const ULONG *tags);
#define AllocLBColumnInfo(n, ...) test_columns((n), (ULONG[]){__VA_ARGS__})
#define AllocListBrowserNode(n, ...) test_node((n), (ULONG[]){__VA_ARGS__})
#define GetListBrowserNodeAttrs(n, ...) test_node_get((n), (ULONG[]){__VA_ARGS__})
#define SetListBrowserNodeAttrs(n, ...) test_node_set((n), (ULONG[]){__VA_ARGS__})
#define SetAttrs(o, ...) test_attrs((o), (ULONG[]){__VA_ARGS__})
#define SetGadgetAttrs(g,w,r,...) test_gattrs((g),(w),(r),(ULONG[]){__VA_ARGS__})
#define AllocAslRequestTags(t,...) test_asl((t),(ULONG[]){__VA_ARGS__})

#include "../src/gui_transfer.c"
#include "../src/gui_attachments.c"

struct IClass { int kind; };
static Class window_class = {1}, layout_class = {2}, button_class = {3};
static Class string_class = {4}, browser_class = {5}, gauge_class = {6};
struct Library { int unused; };
static struct Library gauge_library;
struct Library *FuelGaugeBase = &gauge_library;
struct AmgNetwork { int slot; };
struct Object {
    Class *cl;
    Object *children[8];
    int borrowed[8];
    size_t count, current_child;
    ULONG min_height[8], max_height[8];
    ULONG id, top, disabled, level, percent;
    char label[16];
    struct List *list;
    struct Window window;
    int opened;
};
struct TestNode { struct Node node; ULONG index, selected; };
static int live_objects, live_columns, live_nodes, blocked_count;
static int constructor_calls, fail_constructor, fail_after_tags;
static int node_calls, fail_node, fail_columns, fail_open, fail_request;
static int public_lookups, signal_break, manual_mask;
static ULONG events[32];
static size_t event_count, event_next;
static unsigned saved_mask;
static char last_status[256];
static AmgTransferProgress network_states[AMG_MAX_ACCOUNTS];
static unsigned network_queries[AMG_MAX_ACCOUNTS];
static int cancel_slot = -1;
static unsigned long cancel_serial;
static unsigned gauge_updates, resize_calls;
static ULONG status_test_height = 14UL;

Class *FUELGAUGE_GetClass(void) { return &gauge_class; }
Class *WINDOW_GetClass(void) { return &window_class; }
Class *LAYOUT_GetClass(void) { return &layout_class; }
Class *BUTTON_GetClass(void) { return &button_class; }
Class *STRING_GetClass(void) { return &string_class; }
Class *LISTBROWSER_GetClass(void) { return &browser_class; }

static void apply_tag(Object *o, ULONG key, ULONG value)
{
    if (key == LAYOUT_AddChild || key == WINDOW_Layout) {
        assert(o->count < 8U);
        o->current_child = o->count;
        o->children[o->count++] = (Object *)(uintptr_t)value;
    } else if (key == LAYOUT_ModifyChild) {
        size_t i;
        for (i = 0U; i < o->count; ++i)
            if (o->children[i] == (Object *)(uintptr_t)value) break;
        assert(i < o->count);
        o->current_child = i;
    } else if (key == CHILD_NoDispose) {
        assert(o->count > 0U);
        o->borrowed[o->current_child] = value != 0UL;
    } else if (key == CHILD_MinHeight) {
        assert(o->count > 0U); o->min_height[o->current_child] = value;
    } else if (key == CHILD_MaxHeight) {
        assert(o->count > 0U); o->max_height[o->current_child] = value;
    } else if (key == GA_ID) o->id = value;
    else if (key == LISTBROWSER_Labels) o->list = (struct List *)(uintptr_t)value;
    else if (key == GA_Disabled) o->disabled = value;
    else if (key == FUELGAUGE_Level) { o->level = value; ++gauge_updates; }
    else if (key == FUELGAUGE_Percent) o->percent = value;
    else if (key == GA_Text) snprintf(o->label, sizeof(o->label), "%.15s", (const char *)(uintptr_t)value);
    else if (key == LISTBROWSER_Top) o->top = value;
    else if (key == WA_Left) o->window.LeftEdge = (WORD)value;
    else if (key == WA_Top) o->window.TopEdge = (WORD)value;
    else if (key == WA_Width) o->window.Width = (WORD)value;
    else if (key == WA_Height) o->window.Height = (WORD)value;
}

Object *NewObjectA(Class *cl, CONST_STRPTR name, struct TagItem *tags)
{
    Object *o;
    size_t i;
    int failing;
    if (!cl || name) { ++public_lookups; return NULL; }
    failing = ++constructor_calls == fail_constructor;
    if (failing && !fail_after_tags) return NULL;
    o = calloc(1U, sizeof(*o)); assert(o); ++live_objects; o->cl = cl;
    for (i = 0U; tags[i].ti_Tag != TAG_DONE; ++i) {
        assert(i < 64U);
        apply_tag(o, tags[i].ti_Tag, tags[i].ti_Data);
    }
    if (failing) { DisposeObject(o); return NULL; }
    return o;
}

Object *NewObject(Class *cl, CONST_STRPTR name, ULONG tag, ...)
{
    (void)cl; (void)name; (void)tag;
    /* No production constructor should use this name-based path. */
    ++public_lookups;
    return NULL;
}

void DisposeObject(Object *o)
{
    size_t i;
    assert(o && live_objects > 0);
    for (i = 0U; i < o->count; ++i)
        if (!o->borrowed[i]) DisposeObject(o->children[i]);
    --live_objects;
    free(o);
}

static ULONG test_attrs(Object *o, const ULONG *tags)
{
    size_t i;
    for (i = 0U; tags[i] != TAG_DONE; i += 2U) {
        assert(i < 128U);
        apply_tag(o, tags[i], tags[i + 1U]);
    }
    return 1UL;
}

static ULONG test_gattrs(struct Gadget *g, struct Window *w, void *r,
                          const ULONG *tags)
{
    (void)w; (void)r;
    return test_attrs((Object *)g, tags);
}

ULONG GetAttr(ULONG attr, Object *o, ULONG *value)
{
    if (attr == WINDOW_SigMask) *value = 1UL;
    else if (attr == GA_Disabled) *value = o->disabled;
    else if (attr == LISTBROWSER_Top) *value = o->top;
    else *value = 0UL;
    return 1UL;
}

void LayoutLimits(struct Gadget *g, struct LayoutLimits *limits,
                    struct TextFont *f, struct Screen *s)
{
    Object *object = (Object *)g;
    (void)f; (void)s;
    limits->MinWidth = 300U;
    limits->MinHeight = object->cl == &string_class ? (UWORD)status_test_height : 100U;
}

struct Window *RA_OpenWindow(Object *o)
{
    if (fail_open) return NULL;
    assert(o->cl == &window_class && o->count == 1U);
    assert(o->children[0]->cl == &layout_class);
    o->opened = 1;
    return &o->window;
}

static void mark_browser(Object *o, unsigned mask)
{
    size_t i;
    if (o->cl == &browser_class && o->list) {
        struct Node *n;
        for (n = o->list->lh_Head; n->ln_Succ; n = n->ln_Succ) {
            struct TestNode *t = (struct TestNode *)n;
            t->selected = (mask >> t->index) & 1U;
        }
    }
    for (i = 0U; i < o->count; ++i) mark_browser(o->children[i], mask);
}

ULONG RA_HandleInput(Object *o, UWORD *code)
{
    ULONG event;
    *code = 0U;
    if (event_next == event_count) return WMHI_LASTMSG;
    event = events[event_next++];
    if ((event & WMHI_GADGETMASK) == ATT_SAVE && manual_mask >= 0)
        mark_browser(o, (unsigned)manual_mask);
    return event;
}

ULONG Wait(ULONG mask)
{
    assert(event_next < event_count || signal_break);
    return signal_break ? SIGBREAKF_CTRL_C : mask & ~SIGBREAKF_CTRL_C;
}
ULONG SetSignal(ULONG bits, ULONG mask)
{ (void)bits; (void)mask; return signal_break ? SIGBREAKF_CTRL_C : 0UL; }
void InitRequester(struct Requester *r) { memset(r, 0, sizeof(*r)); }
BOOL Request(struct Requester *r, struct Window *w)
{ (void)r; assert(w); if (fail_request) return FALSE; ++blocked_count; return TRUE; }
void EndRequest(struct Requester *r, struct Window *w)
{ (void)r; assert(w && blocked_count > 0); --blocked_count; }
void RefreshGList(struct Gadget *g, struct Window *w, void *r, LONG n)
{ (void)g; (void)w; (void)r; (void)n; }

void gui_mail_split_update_limits(AmgGui *gui, int relayout)
{ (void)gui; (void)relayout; ++resize_calls; }
void draw_window_overlays(AmgGui *gui) { (void)gui; }

void NewList(struct List *l)
{
    memset(l, 0, sizeof(*l));
    l->lh_Head = (struct Node *)&l->lh_Tail;
    l->lh_TailPred = (struct Node *)&l->lh_Head;
}
void AddTail(struct List *l, struct Node *n)
{
    n->ln_Succ = (struct Node *)&l->lh_Tail;
    n->ln_Pred = l->lh_TailPred;
    l->lh_TailPred->ln_Succ = n;
    l->lh_TailPred = n;
}
static struct ColumnInfo *test_columns(ULONG count, const ULONG *tags)
{
    struct ColumnInfo *c;
    (void)tags; assert(count == 3UL);
    if (fail_columns) return NULL;
    c = calloc(1U, sizeof(*c)); assert(c); ++live_columns; return c;
}
void FreeLBColumnInfo(struct ColumnInfo *c)
{ assert(c && live_columns > 0); --live_columns; free(c); }
static ULONG test_node_set(struct Node *n, const ULONG *tags)
{
    struct TestNode *t = (struct TestNode *)n;
    size_t i;
    for (i = 0U; tags[i]; i += 2U) {
        if (tags[i] == LBNA_UserData) t->index = tags[i + 1U];
        if (tags[i] == LBNA_Selected) t->selected = tags[i + 1U];
    }
    return 1UL;
}
static struct Node *test_node(ULONG count, const ULONG *tags)
{
    struct TestNode *n;
    assert(count == 3UL);
    if (++node_calls == fail_node) return NULL;
    n = calloc(1U, sizeof(*n)); assert(n); ++live_nodes;
    (void)test_node_set(&n->node, tags);
    return &n->node;
}
static ULONG test_node_get(struct Node *n, const ULONG *tags)
{
    struct TestNode *t = (struct TestNode *)n;
    size_t i;
    for (i = 0U; tags[i]; i += 2U) {
        ULONG *p = (ULONG *)(uintptr_t)tags[i + 1U];
        if (tags[i] == LBNA_UserData) *p = t->index;
        if (tags[i] == LBNA_Selected) *p = t->selected;
    }
    return 1UL;
}
void FreeListBrowserList(struct List *list)
{
    struct Node *n = list->lh_Head;
    while (n->ln_Succ) {
        struct Node *next = n->ln_Succ;
        assert(live_nodes > 0); --live_nodes; free(n); n = next;
    }
    NewList(list);
}
void detach_listbrowser(struct Gadget *g, struct Window *w)
{ (void)w; ((Object *)g)->list = NULL; }

static void *test_asl(ULONG kind, const ULONG *tags)
{
    struct FileRequester *r;
    (void)kind; (void)tags;
    r = calloc(1U, sizeof(*r)); assert(r);
    r->rf_Dir = (STRPTR)"RAM:test";
    return r;
}
BOOL AslRequest(void *r, struct TagItem *t) { (void)r; (void)t; return TRUE; }
void FreeAslRequest(void *r) { free(r); }
const char *amg_tr(long id, const char *en) { (void)id; return en; }
int amg_tr_snprintf(char *out, size_t cap, long id, const char *format, ...)
{
    va_list args; int result; (void)id;
    va_start(args, format); result = vsnprintf(out, cap, format, args); va_end(args);
    return result;
}
void set_string(struct Gadget *g, struct Window *w, const char *text)
{ (void)g; (void)w; (void)text; }
void status_local(AmgGui *gui, const char *text)
{ (void)gui; snprintf(last_status, sizeof(last_status), "%s", text); }
void status_utf8(AmgGui *gui, const char *text) { status_local(gui, text); }
int rawkey_is_cancel(ULONG result) { return (result & WMHI_GADGETMASK) == 0x45U; }
void sanitize_attachment_name(const char *name, char *out, size_t cap)
{ snprintf(out, cap, "%s", name); }
int gui_prepare_attachment_file(AmgGui *gui, AmgError *error)
{ (void)gui; (void)error; return AMG_OK; }
const AmgMailFilePart *amg_mailfile_attachment(const AmgMailFile *mail, size_t i)
{ return i < mail->attachment_count ? &mail->parts[i] : NULL; }
int amg_mailfile_save_attachment(const AmgMailFile *mail, size_t i,
    const char *dir, const char *name, char dest[AMG_SPOOL_PATH_MAX],
    AmgTransfer *transfer, AmgError *error)
{
    (void)mail; (void)dir; (void)name; (void)dest; (void)error;
    if (transfer && !transfer->report(transfer->context, AMG_TRANSFER_EXPORT, 0U, 100U))
        return AMG_ERR_CANCELLED;
    saved_mask |= 1U << i;
    return AMG_OK;
}
int amg_network_transfer_progress(AmgNetwork *net, AmgTransferProgress *p)
{
    if (!net) { memset(p, 0, sizeof(*p)); return 0; }
    assert(net->slot >= 0 && net->slot < (int)AMG_MAX_ACCOUNTS);
    ++network_queries[net->slot];
    *p = network_states[net->slot];
    return p->active;
}
int amg_network_cancel_transfer(AmgNetwork *net, unsigned long serial)
{
    assert(net);
    cancel_slot = net->slot; cancel_serial = serial;
    network_states[net->slot].cancellable = 0;
    return 1;
}

static void reset(void)
{
    assert(live_objects == 0 && live_nodes == 0 && live_columns == 0);
    assert(blocked_count == 0);
    constructor_calls = fail_constructor = fail_after_tags = 0;
    node_calls = fail_node = fail_columns = fail_open = fail_request = 0;
    public_lookups = signal_break = 0; manual_mask = -1;
    memset(network_states, 0, sizeof(network_states));
    memset(network_queries, 0, sizeof(network_queries));
    cancel_slot = -1; cancel_serial = 0UL;
    gauge_updates = resize_calls = 0U;
    event_count = event_next = 0U; saved_mask = 0U; last_status[0] = 0;
}
static void push(ULONG action) { events[event_count++] = WMHI_GADGETUP | action; }
static void clean(void)
{
    assert(live_objects == 0 && live_nodes == 0 && live_columns == 0);
    assert(blocked_count == 0 && public_lookups == 0);
}
static void test_selection(AmgGui *gui)
{
    unsigned char selected[3];
    const ULONG action[] = { 0UL, ATT_ALL, ATT_REGULAR, ATT_GRAPHICS };
    const unsigned masks[] = { 5U, 7U, 5U, 2U };
    size_t i, j;
    for (i = 0U; i < 4U; ++i) {
        reset(); if (action[i]) push(action[i]); push(ATT_SAVE);
        assert(choose_attachments(gui, selected) == 1);
        for (j = 0U; j < 3U; ++j) assert(selected[j] == ((masks[i] >> j) & 1U));
        clean();
    }
    reset(); manual_mask = 4; push(ATT_SAVE);
    assert(choose_attachments(gui, selected) == 1);
    assert(selected[0] == 0U && selected[1] == 0U && selected[2] == 1U); clean();
    reset(); push(ATT_NONE); push(ATT_SAVE); push(ATT_CANCEL);
    assert(choose_attachments(gui, selected) == 0); clean();
    reset(); events[event_count++] = WMHI_CLOSEWINDOW;
    assert(choose_attachments(gui, selected) == 0); clean();
    reset(); signal_break = 1;
    assert(choose_attachments(gui, selected) == 0); clean();
}
static void test_failures(AmgGui *gui)
{
    int n, after;
    unsigned char selected[3];
    const int steps[] = { 3,4,5,5,5,5,5,5,6,7,8,9 };
    for (after = 0; after <= 1; ++after) {
        for (n = 1; n <= 12; ++n) {
            reset(); fail_constructor = n; fail_after_tags = after;
            assert(choose_attachments(gui, selected) == -steps[n - 1]); clean();
        }
    }
    reset(); fail_columns = 1; assert(choose_attachments(gui, selected) == -1); clean();
    for (n = 1; n <= 3; ++n) {
        reset(); fail_node = n; assert(choose_attachments(gui, selected) == -2); clean();
    }
    reset(); fail_open = 1; assert(choose_attachments(gui, selected) == -10); clean();
    reset(); fail_request = 1; assert(choose_attachments(gui, selected) == -11); clean();
    reset(); fail_constructor = 12; save_current_attachments(gui);
    assert(strstr(last_status, "Select attachments to save failed (code 9).")); clean();
}
static void test_export_and_progress(AmgGui *gui)
{
    GuiFileProgress *p;
    reset(); push(ATT_SAVE); save_current_attachments(gui);
    assert(saved_mask == 5U); assert(strstr(last_status, "2 saved, 0 failed.")); clean();
    reset(); push(ATT_ALL); push(ATT_SAVE); push(GID_TRANSFER_CANCEL); save_current_attachments(gui);
    assert(saved_mask == 0U); assert(strstr(last_status, "Cancelled; 0 file(s)")); clean();
    reset(); p = gui_file_progress_open(gui, "Export"); assert(p);
    assert(gui_file_progress_callback(p)->report(p, AMG_TRANSFER_EXPORT, 0U, 10U));
    gui_file_progress_item(p, "test.pdf");
    assert(gui_file_progress_callback(p)->report(p, AMG_TRANSFER_EXPORT, 10U, 10U));
    gui_file_progress_close(p); clean();
}

static void test_percent_math(void)
{
    size_t total, done;
    assert(progress_percent(0U, 0U) == 0U);
    assert(progress_percent(100U, 0U) == 0U);
    for (total = 1U; total < 1000U; ++total)
        for (done = 0U; done <= total; ++done)
            assert(progress_percent(done, total) == (unsigned)((done * 100U) / total));
    assert(progress_percent(1U, 3U) == 33U);
    assert(progress_percent(2U, 3U) == 66U);
    assert(progress_percent((size_t)-2, (size_t)-1) == 99U);
    assert(progress_percent((size_t)-1, (size_t)-1) == 100U);
    assert(progress_percent(2147483647UL, 4294967295UL) == 49U);
    assert(progress_percent(33554432U, 33554432U) == 100U);
    assert(progress_percent(11U, 10U) == 100U);
}

static void test_status_progress(AmgGui *gui)
{
    AmgAccountSet accounts;
    AmgNetwork networks[AMG_MAX_ACCOUNTS];
    Object gauge, cancel, navigation;
    AmgTransferProgress *p;
    unsigned i, updates;
    GuiFileProgress *file;
    reset();
    memset(&accounts, 0, sizeof(accounts));
    memset(&gauge, 0, sizeof(gauge));
    memset(&cancel, 0, sizeof(cancel));
    memset(&navigation, 0, sizeof(navigation));
    gui->transfer_gadget = (struct Gadget *)&gauge;
    gui->transfer_cancel_gadget = (struct Gadget *)&cancel;
    gui->account_tabs_gadget = (struct Gadget *)&navigation;
    gui->transfer_native_gauge = 1;
    gui->transfer_initialized = 0;
    gui->account_set = &accounts;
    gui->active_account = 1U; gui->running = 1;
    for (i = 0U; i < AMG_MAX_ACCOUNTS; ++i) {
        networks[i].slot = (int)i; gui->networks[i] = &networks[i];
        accounts.accounts[i].enabled = 1;
        network_states[i].active = network_states[i].cancellable = 1;
        network_states[i].type = AMG_NET_FETCH_MESSAGE;
        network_states[i].serial = 20UL + i;
        network_states[i].uid = 41UL;
        network_states[i].done = 500U; network_states[i].total = 1000U;
        strcpy(network_states[i].mailbox, "INBOX");
    }
    strcpy(last_status, "Message loaded.");
    gui_transfer_update(gui);
    assert(gui->transfer_visible && gauge.level == 50UL && gauge.percent);
    assert(!cancel.disabled && gui->transfer_account == 1U);
    assert(strcmp(last_status, "Message loaded.") == 0);
    for (i = 0U; i < AMG_MAX_ACCOUNTS; ++i)
        assert(network_queries[i] == (i == 1U ? 1U : 0U));
    assert(constructor_calls == 0 && blocked_count == 0);
    updates = gauge_updates;
    gui_transfer_update(gui); assert(gauge_updates == updates);
    p = &network_states[1];
    p->done = 509U; gui_transfer_update(gui); assert(gauge_updates == updates);
    p->done = 510U; gui_transfer_update(gui); assert(gauge.level == 51UL);

    /* Same UID in a different folder/account must not leak across views. */
    strcpy(p->mailbox, "Sent"); gui_transfer_update(gui);
    assert(!gui->transfer_visible && gauge.level == 0UL && !gauge.percent && cancel.disabled);
    strcpy(p->mailbox, "INBOX"); p->uid = 42UL; gui_transfer_update(gui);
    assert(!gui->transfer_visible);
    p->uid = 41UL; p->active = 0; gui_transfer_update(gui);
    assert(!gui->transfer_visible);
    p->active = 1; gui_transfer_update(gui);
    gui->active_account = 2U;
    gui_transfer_cancel(gui); assert(cancel_slot == -1); /* old button */
    assert(gui->transfer_account == 2U);
    gui_transfer_cancel(gui); assert(cancel_slot == 2 && cancel_serial == 22UL);
    assert(cancel.disabled);
    gui->active_account = 1U; gui_transfer_update(gui);
    cancel_slot = -1; ++p->serial;
    gui_transfer_cancel(gui); assert(cancel_slot == -1); /* finished/new job */
    gui_transfer_cancel(gui); assert(cancel_slot == 1 && cancel_serial == p->serial);

    /* Unknown total is NOT fake 0%/100%; commit cannot be cancelled. */
    p->total = 0U; p->done = 100U; p->cancellable = 0; p->phase = AMG_TRANSFER_COMMIT;
    gui_transfer_update(gui); assert(!gauge.percent && cancel.disabled);
    gui->iconified = 1; gui_transfer_update(gui); assert(gui->transfer_serial == 0);
    gui->iconified = 0; gui_transfer_update(gui); assert(gui->transfer_visible);
    accounts.accounts[1].enabled = 0; gui_transfer_update(gui); assert(!gui->transfer_visible);
    accounts.accounts[1].enabled = 1;

    /* Scoped submit from an empty folder; unknown/unscoped jobs stay quiet. */
    p->type = AMG_NET_SEND_MAIL; p->uid = gui->active_message_uid = 0UL;
    gui_transfer_update(gui); assert(gui->transfer_visible);
    p->mailbox[0] = 0; gui_transfer_update(gui); assert(!gui->transfer_visible);
    strcpy(p->mailbox, "INBOX"); p->type = AMG_NET_CHECK_INBOX;
    gui_transfer_update(gui); assert(!gui->transfer_visible);
    p->type = AMG_NET_FETCH_MESSAGE;
    gui_transfer_update(gui); assert(!gui->transfer_visible); /* UID zero */
    /* Folder-list progress is mailbox-scoped, independent of a selected UID.
     * Other accounts and CHECK_INBOX notifications remain excluded. */
    p->type = AMG_NET_FETCH_INBOX; p->uid = 0UL;
    p->done = 125U; p->total = 250U; p->cancellable = 1;
    gui->active_message_uid = 41UL;
    gui_transfer_update(gui);
    assert(gui->transfer_visible && gauge.level == 50UL && !cancel.disabled);
    gui->active_message_uid = 99UL;
    gui_transfer_update(gui); assert(gui->transfer_visible);
    strcpy(gui->current_mailbox_utf8, "Archive");
    cancel_slot = -1; gui_transfer_cancel(gui);
    assert(!gui->transfer_visible && cancel_slot == -1);
    strcpy(gui->current_mailbox_utf8, "INBOX");
    gui_transfer_update(gui); assert(gui->transfer_visible);
    p->uid = 55UL; gui_transfer_update(gui); assert(!gui->transfer_visible);
    p->uid = 0UL; p->type = AMG_NET_CHECK_INBOX;
    gui_transfer_update(gui); assert(!gui->transfer_visible);
    p->type = AMG_NET_FETCH_MESSAGE;
    p->uid = gui->active_message_uid = 41UL;

    /* Only the selected label's canonical folder alias is accepted. */
    gui->label_count = 1U;
    strcpy(gui->labels[0].mailbox_utf8, "Trash");
    strcpy(gui->labels[0].server_mailbox_utf8, "Deleted Items");
    strcpy(gui->current_mailbox_utf8, "Trash");
    assert(gui_transfer_message_matches(gui, 41UL, "Deleted Items"));
    assert(gui_transfer_mailbox_matches(gui, "Trash"));
    assert(!gui_transfer_message_matches(gui, 41UL, "INBOX"));
    assert(!gui_transfer_message_matches(gui, 42UL, "Deleted Items"));
    assert(!gui_transfer_message_matches(gui, 0UL, "Deleted Items"));
    assert(!gui_transfer_message_matches(gui, 41UL, "deleted items"));
    strcpy(gui->current_mailbox_utf8, "INBOX"); gui->label_count = 0U;

    /* Local export owns only the same status strip, never a new window. */
    gui->transfer_native_gauge = 1;
    file = gui_file_progress_open(gui, "Saving attachment"); assert(file);
    assert(navigation.disabled && !cancel.disabled);
    assert(blocked_count == 0 && constructor_calls == 0);
    assert(gui_file_progress_callback(file)->report(file, AMG_TRANSFER_EXPORT, 3U, 4U));
    assert(gauge.level == 75UL && gauge.percent);
    gui_transfer_update(gui); assert(gauge.level == 75UL); /* background suppressed */
    events[event_count++] = WMHI_NEWSIZE;
    assert(file_progress_report(file, AMG_TRANSFER_EXPORT, 4U, 4U));
    assert(resize_calls == 1 && gauge.level == 100UL);
    push(GID_TRANSFER_CANCEL);
    assert(!file_progress_report(file, AMG_TRANSFER_EXPORT, 4U, 4U));
    gui_file_progress_close(file);
    assert(!navigation.disabled && !gui->file_progress && cancel.disabled);
    navigation.disabled = 1;
    file = gui_file_progress_open(gui, "Export"); assert(file);
    gui_file_progress_close(file); assert(navigation.disabled); /* original state */
    navigation.disabled = 0;
    file = gui_file_progress_open(gui, "Export"); assert(file);
    gui->active_message_uid = 42UL;
    assert(!file_progress_report(file, AMG_TRANSFER_EXPORT, 0U, 10U));
    gui_file_progress_close(file); gui->active_message_uid = 41UL;

    file = gui_file_progress_open(gui, "Export"); assert(file);
    events[event_count++] = WMHI_CLOSEWINDOW;
    assert(!file_progress_report(file, AMG_TRANSFER_EXPORT, 0U, 10U));
    gui_file_progress_close(file); assert(!gui->running);
    gui->running = 1;

    /* Fallback label retains its storage, and percent/empty-state updates. */
    gui->transfer_native_gauge = 0; gui->transfer_initialized = 0;
    show_progress(gui, 1, 1U, 3U, 1); assert(!strcmp(gauge.label, "33%"));
    show_progress(gui, 1, 0U, 0U, 1); assert(!strcmp(gauge.label, "..."));
    show_progress(gui, 0, 0U, 0U, 0); assert(!strcmp(gauge.label, ""));
    gui->account_tabs_gadget = NULL;
    gui->transfer_gadget = gui->transfer_cancel_gadget = NULL;
    gui->account_set = NULL; gui->active_account = 0U;
    memset(gui->networks, 0, sizeof(gui->networks));
    clean();
}

static void test_gauge_construction(AmgGui *gui)
{
    Object *object;
    int after;
    reset(); FuelGaugeBase = &gauge_library;
    object = gui_transfer_create_gauge(gui); assert(object && object->cl == &gauge_class);
    assert(gui->transfer_native_gauge); DisposeObject(object); clean();
    reset(); FuelGaugeBase = NULL;
    object = gui_transfer_create_gauge(gui); assert(object && object->cl == &button_class);
    assert(!gui->transfer_native_gauge); DisposeObject(object); clean();
    for (after = 0; after <= 1; ++after) {
        reset(); FuelGaugeBase = &gauge_library;
        fail_constructor = 1; fail_after_tags = after;
        object = gui_transfer_create_gauge(gui); assert(object && object->cl == &button_class);
        assert(!gui->transfer_native_gauge); DisposeObject(object); clean();
        reset(); FuelGaugeBase = NULL; fail_constructor = 1; fail_after_tags = after;
        assert(gui_transfer_create_gauge(gui) == NULL); clean();
    }
    FuelGaugeBase = &gauge_library;
}

static void test_status_row(AmgGui *gui)
{
    Object *row;
    unsigned height, i;
    int native, after, failure;
    for (native = 0; native <= 1; ++native) {
        FuelGaugeBase = native ? &gauge_library : NULL;
        for (height = 10U; height <= 30U; height += 2U) {
            reset(); status_test_height = height;
            row = gui_transfer_create_status_row(gui);
            assert(row && row->count == 3U && row->cl == &layout_class);
            assert(row->children[0] == (Object *)gui->status_gadget);
            assert(row->children[1] == (Object *)gui->transfer_gadget);
            assert(row->children[2] == (Object *)gui->transfer_cancel_gadget);
            for (i = 0U; i < 3U; ++i) {
                assert(row->min_height[i] == height);
                assert(row->max_height[i] == height);
                assert(!row->borrowed[i]);
            }
            assert(gui->transfer_native_gauge == native);
            DisposeObject(row); clean();
        }
        /* Failure before and after constructor tags: the borrowed-child
         * construction must never leak or dispose a child twice. */
        for (after = 0; after <= 1; ++after) {
            for (failure = 1; failure <= 5; ++failure) {
                reset(); status_test_height = 14UL;
                fail_constructor = failure; fail_after_tags = after;
                row = gui_transfer_create_status_row(gui);
                if (row) DisposeObject(row);
                else assert(!gui->status_gadget && !gui->transfer_gadget &&
                            !gui->transfer_cancel_gadget);
                clean();
            }
        }
    }
    gui->status_gadget = gui->transfer_gadget = gui->transfer_cancel_gadget = NULL;
    FuelGaugeBase = &gauge_library; status_test_height = 14UL;
}

int main(void)
{
    AmgGui gui;
    AmgMailFile mail;
    AmgMailFilePart parts[3];
    struct Screen screen;
    struct Window window;
    size_t i;
    memset(&gui, 0, sizeof(gui)); memset(&mail, 0, sizeof(mail));
    memset(&screen, 0, sizeof(screen)); memset(&window, 0, sizeof(window));
    memset(parts, 0, sizeof(parts));
    for (i = 0U; i < 3U; ++i) { parts[i].name_utf8 = "test.bin"; parts[i].end = 100U; }
    parts[1].embedded = 1;
    mail.parts = parts; mail.attachment_count = 3U;
    screen.Width = 1024; screen.Height = 768;
    window.Width = 720; window.Height = 480;
    window.BorderLeft = window.BorderRight = 4;
    window.BorderTop = 11; window.BorderBottom = 10;
    gui.screen = &screen; gui.window = &window;
    gui.window_object = (Object *)(uintptr_t)1; /* no event dereferences it */
    gui.running = 1;
    gui.active_message_uid = 41UL;
    strcpy(gui.current_mailbox_utf8, "INBOX");
    gui.current_mail_file = &mail; gui.current_attachment_count = 3U;
    test_selection(&gui); test_failures(&gui); test_export_and_progress(&gui);
    test_percent_math(); test_status_progress(&gui); test_gauge_construction(&gui);
    test_status_row(&gui);
    puts("Dialog/export regressions, scoped status progress, cancellation and overflow tests: passed.");
    puts("API doubles only: no real ReAction, m68k ABI or screen rendering tested.");
    return 0;
}
