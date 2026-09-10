#include "gui_internal.h"
#include "buffer.h"
#include "codec.h"
#include "imap_parser.h"
#include "mime.h"
#include "network_task.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA

#include <clib/alib_protos.h>
#include <classes/window.h>
#include <devices/inputevent.h>
#include <dos/dos.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/scroller.h>
#include <gadgets/string.h>
#include <gadgets/texteditor.h>
#include <intuition/classes.h>
#include <intuition/gadgetclass.h>
#include <intuition/intuition.h>
#include <intuition/icclass.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/button.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/scroller.h>
#include <proto/string.h>
#include <proto/texteditor.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <utility/tagitem.h>

/* Same classic-GCC/ReAction varargs setup as the original gui.c code. */
#ifdef NewObject
#undef NewObject
#endif

#ifdef ButtonObject
#undef ButtonObject
#endif
#define ButtonObject NewObject(NULL, (CONST_STRPTR)"button.gadget"

#define T(id, en) amg_tr((id), (en))

#define COMPOSE_WINDOW_STATE_DRAWER "ENVARC:AmiMail"
#define COMPOSE_WINDOW_STATE_PATH "ENVARC:AmiMail/compose-window.state"
#define COMPOSE_WINDOW_STATE_TEMP "ENVARC:AmiMail/compose-window.state.new"
#define COMPOSE_WINDOW_STATE_HEADER "AMIMAIL-COMPOSE-WINDOW-1"

#define COMPOSE_RAWKEY_A 0x20U
#define COMPOSE_RAWKEY_X 0x32U
#define COMPOSE_RAWKEY_C 0x33U
#define COMPOSE_RAWKEY_V 0x34U

/* The editable compose TextEditor is deliberately not connected to the
 * generic modelclass link used by the read-only preview.  Instead, let the
 * scroller notify the editor directly and route the editor's own property
 * notifications through IDCMP_IDCMPUPDATE.  texteditor.gadget documents
 * Prop_First/Entries/Visible specifically as the scrollbar interface.  This
 * keeps updates event-driven without replacing the editor's mouse handling. */
static struct TagItem compose_scroller_to_texteditor_map[] = {
    { SCROLLER_Top, GA_TEXTEDITOR_Prop_First },
    { TAG_DONE, 0 }
};

typedef struct ComposeScrollerSyncState {
    ULONG first;
    ULONG entries;
    WORD editor_height;
    int valid;
} ComposeScrollerSyncState;

typedef struct ComposeInputHookState {
    int scroller_sync_pending;
} ComposeInputHookState;

static int compose_connect_texteditor_scroller(struct Gadget *editor,
                                                struct Gadget *scroller)
{
    if (!editor || !scroller) return 0;

    /* Editor -> application: property changes arrive as IDCMP_IDCMPUPDATE
     * and are filtered by compose_idcmp_subentry(). */
    SetAttrs((Object *)editor,
             ICA_TARGET, ICTARGET_IDCMP,
             TAG_DONE);

    /* Scroller -> editor: moving the prop changes only the first line. */
    SetAttrs((Object *)scroller,
             ICA_TARGET, (ULONG)(uintptr_t)editor,
             ICA_MAP, (ULONG)(uintptr_t)compose_scroller_to_texteditor_map,
             TAG_DONE);
    return 1;
}

static void compose_disconnect_texteditor_scroller(struct Gadget *editor,
                                                    struct Gadget *scroller)
{
    if (editor)
        SetAttrs((Object *)editor, ICA_TARGET, 0UL, TAG_DONE);
    if (scroller)
        SetAttrs((Object *)scroller,
                 ICA_TARGET, 0UL,
                 ICA_MAP, 0UL,
                 TAG_DONE);
}

static int compose_update_contains_scroll_property(struct TagItem *tags)
{
    struct TagItem *state = tags;
    struct TagItem *tag;
    if (!tags) return 0;
    while ((tag = NextTagItem(&state)) != NULL) {
        if (tag->ti_Tag == GA_TEXTEDITOR_Prop_First ||
            tag->ti_Tag == GA_TEXTEDITOR_Prop_Entries ||
            tag->ti_Tag == GA_TEXTEDITOR_Prop_Visible)
            return 1;
    }
    return 0;
}

static ULONG compose_idcmp_subentry(struct Hook *hook, APTR object,
                                    APTR message_ptr)
{
    ComposeInputHookState *state =
        hook ? (ComposeInputHookState *)hook->h_Data : NULL;
    struct IntuiMessage *message = (struct IntuiMessage *)message_ptr;
    (void)object;

    if (!state || !message) return 0UL;

    if (message->Class == IDCMP_IDCMPUPDATE &&
        compose_update_contains_scroll_property(
            (struct TagItem *)message->IAddress))
        state->scroller_sync_pending = 1;

    return 0UL;
}

static void compose_init_input_hook(struct Hook *hook,
                                    ComposeInputHookState *state)
{
    if (!hook || !state) return;
    memset(state, 0, sizeof(*state));
    memset(hook, 0, sizeof(*hook));
    hook->h_Entry = (__typeof__(hook->h_Entry))HookEntry;
    hook->h_SubEntry =
        (__typeof__(hook->h_SubEntry))compose_idcmp_subentry;
    hook->h_Data = state;
}

static void compose_text_end_position(const unsigned char *text,
                                      ULONG *end_x, ULONG *end_y)
{
    ULONG x = 0U, y = 0U;
    const unsigned char *p =
        text ? text : (const unsigned char *)"";
    while (*p) {
        if (*p == '\r') {
            if (p[1] == '\n') ++p;
            ++y;
            x = 0U;
        } else if (*p == '\n') {
            ++y;
            x = 0U;
        } else {
            ++x;
        }
        ++p;
    }
    if (end_x) *end_x = x;
    if (end_y) *end_y = y;
}

static ULONG compose_texteditor_super_arexx(Class *cl, Object *object,
                                             struct GadgetInfo *ginfo,
                                             const char *command)
{
    struct GP_TEXTEDITOR_ARexxCmd message;
    if (!cl || !object || !command) return 0UL;
    memset(&message, 0, sizeof(message));
    message.MethodID = GM_TEXTEDITOR_ARexxCmd;
    message.GInfo = ginfo;
    message.command = (STRPTR)command;
    return DoSuperMethodA(cl, object, (Msg)&message);
}

static ULONG compose_texteditor_super_mark_all(Class *cl, Object *object,
                                                struct GadgetInfo *ginfo)
{
    struct GP_TEXTEDITOR_ExportText export_message;
    struct GP_TEXTEDITOR_MarkText mark_message;
    STRPTR text;
    ULONG end_x = 0U, end_y = 0U;
    ULONG result;

    if (!cl || !object) return 0UL;
    memset(&export_message, 0, sizeof(export_message));
    export_message.MethodID = GM_TEXTEDITOR_ExportText;
    export_message.GInfo = ginfo;
    text = (STRPTR)(uintptr_t)DoSuperMethodA(
        cl, object, (Msg)&export_message);
    if (!text) return 0UL;

    compose_text_end_position(text, &end_x, &end_y);
    memset(&mark_message, 0, sizeof(mark_message));
    mark_message.MethodID = GM_TEXTEDITOR_MarkText;
    mark_message.GInfo = ginfo;
    mark_message.start_crsr_x = 0U;
    mark_message.start_crsr_y = 0U;
    mark_message.stop_crsr_x = end_x;
    mark_message.stop_crsr_y = end_y;
    result = DoSuperMethodA(cl, object, (Msg)&mark_message);
    FreeVec(text);
    return result;
}

static int compose_texteditor_arexx(struct Gadget *editor,
                                    struct Window *window,
                                    const char *command)
{
    struct GP_TEXTEDITOR_ARexxCmd message;
    if (!editor || !window || !command) return 0;
    memset(&message, 0, sizeof(message));
    message.MethodID = GM_TEXTEDITOR_ARexxCmd;
    message.GInfo = NULL;
    message.command = (STRPTR)command;
    return DoGadgetMethodA(editor, window, NULL, (Msg)&message) != 0UL;
}

static int compose_texteditor_mark_all(struct Gadget *editor,
                                       struct Window *window)
{
    struct GP_TEXTEDITOR_MarkText mark_message;
    STRPTR text;
    ULONG end_x = 0U, end_y = 0U;
    ULONG result;

    if (!editor || !window) return 0;
    text = (STRPTR)(uintptr_t)DoGadgetMethod(
        editor, window, NULL, GM_TEXTEDITOR_ExportText, 0UL);
    if (!text) return 0;

    compose_text_end_position(text, &end_x, &end_y);
    memset(&mark_message, 0, sizeof(mark_message));
    mark_message.MethodID = GM_TEXTEDITOR_MarkText;
    mark_message.GInfo = NULL;
    mark_message.start_crsr_x = 0U;
    mark_message.start_crsr_y = 0U;
    mark_message.stop_crsr_x = end_x;
    mark_message.stop_crsr_y = end_y;
    result = DoGadgetMethodA(editor, window, NULL, (Msg)&mark_message);
    FreeVec(text);
    return result != 0UL;
}

static int compose_handle_edit_menu(ULONG menu_code,
                                    struct Window *window,
                                    struct Gadget *body_gadget)
{
    switch (menu_code) {
        case MENU_EDIT_COPY:
            return compose_texteditor_arexx(body_gadget, window, "COPY");
        case MENU_EDIT_CUT:
            return compose_texteditor_arexx(body_gadget, window, "CUT");
        case MENU_EDIT_PASTE:
            return compose_texteditor_arexx(body_gadget, window, "PASTE");
        case MENU_EDIT_SELECT_ALL:
            return compose_texteditor_mark_all(body_gadget, window);
        default:
            return 0;
    }
}

/*
 * ReAction texteditor.gadget V44/V47 deliberately ignores auto-repeat for
 * Return/Enter on some classic systems.  Handling this in window.class does
 * not work reliably because the active child receives GM_HANDLEINPUT before
 * a RAWKEY message ever reaches the window IDCMP.
 *
 * Use a private subclass of texteditor.gadget instead.  The subclass sees
 * exactly the InputEvents that the active editor receives.  For repeated
 * Return/Enter only, insert one newline and consume that repeat event.
 * Ordinary Return/Enter and every other input event are still handled by the
 * original texteditor.gadget unchanged.
 */
static ULONG compose_texteditor_dispatcher(struct Hook *hook, APTR object_ptr,
                                           APTR message_ptr)
{
    Class *cl = hook ? (Class *)hook->h_Data : NULL;
    Object *object = (Object *)object_ptr;
    Msg message = (Msg)message_ptr;

    if (!cl || !object || !message) return 0UL;

    if (message->MethodID == GM_HANDLEINPUT) {
        struct gpInput *input = (struct gpInput *)message_ptr;
        struct InputEvent *event = input->gpi_IEvent;

        if (event && event->ie_Class == IECLASS_RAWKEY &&
            (event->ie_Qualifier & IEQUALIFIER_RCOMMAND) &&
            !(event->ie_Qualifier & IEQUALIFIER_REPEAT) &&
            !(event->ie_Code & IECODE_UP_PREFIX)) {
            UWORD code = event->ie_Code & (UWORD)~IECODE_UP_PREFIX;
            switch (code) {
                case COMPOSE_RAWKEY_C:
                    (void)compose_texteditor_super_arexx(
                        cl, object, input->gpi_GInfo, "COPY");
                    return GMR_MEACTIVE;
                case COMPOSE_RAWKEY_X:
                    (void)compose_texteditor_super_arexx(
                        cl, object, input->gpi_GInfo, "CUT");
                    return GMR_MEACTIVE;
                case COMPOSE_RAWKEY_V:
                    (void)compose_texteditor_super_arexx(
                        cl, object, input->gpi_GInfo, "PASTE");
                    return GMR_MEACTIVE;
                case COMPOSE_RAWKEY_A:
                    (void)compose_texteditor_super_mark_all(
                        cl, object, input->gpi_GInfo);
                    return GMR_MEACTIVE;
            }
        }

        if (event && event->ie_Class == IECLASS_RAWKEY &&
            (event->ie_Qualifier & IEQUALIFIER_REPEAT)) {
            UWORD code = event->ie_Code;

            if (!(code & IECODE_UP_PREFIX)) {
                code &= (UWORD)~IECODE_UP_PREFIX;
                if (code == 0x43U || code == 0x44U) {
                    struct GP_TEXTEDITOR_InsertText insert;

                    insert.MethodID = GM_TEXTEDITOR_InsertText;
                    insert.GInfo = input->gpi_GInfo;
                    insert.text = (STRPTR)"\n";
                    insert.pos = (LONG)GV_TEXTEDITOR_InsertText_Cursor;
                    (void)DoSuperMethodA(cl, object, (Msg)&insert);
                    return GMR_MEACTIVE;
                }
            }
        }
    }

    return DoSuperMethodA(cl, object, message);
}

static Class *compose_make_texteditor_class(void)
{
    Class *cl = MakeClass(NULL, NULL, TEXTEDITOR_GetClass(), 0UL, 0UL);
    if (!cl) return NULL;

    cl->cl_Dispatcher.h_Entry =
        (__typeof__(cl->cl_Dispatcher.h_Entry))HookEntry;
    cl->cl_Dispatcher.h_SubEntry =
        (__typeof__(cl->cl_Dispatcher.h_SubEntry))
            compose_texteditor_dispatcher;
    cl->cl_Dispatcher.h_Data = cl;
    return cl;
}

/* Keep the external compose scrollbar in sync without periodic polling.
 * The call is made only after a real TextEditor property notification or a
 * window resize.  The line count and editor height are cached so unchanged
 * values never redraw the scroller. */
static void compose_sync_texteditor_scroller(
    struct Window *window, struct Gadget *editor, struct Gadget *scroller,
    ComposeScrollerSyncState *state, int force)
{
    ULONG first = 0U, entries = 1U;
    WORD editor_height;

    if (!window || !editor || !scroller || !state) return;
    GetAttr(GA_TEXTEDITOR_Prop_First, (Object *)editor, &first);
    GetAttr(GA_TEXTEDITOR_Prop_Entries, (Object *)editor, &entries);
    if (entries < 1U) entries = 1U;
    editor_height = editor->Height;

    if (!force && state->valid &&
        state->first == first && state->entries == entries &&
        state->editor_height == editor_height)
        return;

    sync_texteditor_scroller(window, editor, scroller, 0U, 0);

    GetAttr(GA_TEXTEDITOR_Prop_First, (Object *)editor, &state->first);
    GetAttr(GA_TEXTEDITOR_Prop_Entries, (Object *)editor, &state->entries);
    if (state->entries < 1U) state->entries = 1U;
    state->editor_height = editor->Height;
    state->valid = 1;
}

static void compose_state_ensure_drawer(void)
{
    BPTR lock = Lock((CONST_STRPTR)COMPOSE_WINDOW_STATE_DRAWER, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return;
    }
    lock = CreateDir((CONST_STRPTR)COMPOSE_WINDOW_STATE_DRAWER);
    if (lock) UnLock(lock);
}

static ULONG compose_state_clamp(ULONG value, ULONG minimum, ULONG maximum)
{
    if (maximum < minimum) minimum = maximum;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static int compose_state_load(const struct Screen *screen, ULONG *left,
                              ULONG *top, ULONG *width, ULONG *height)
{
    FILE *file;
    char header[64];
    unsigned long saved_left, saved_top, saved_width, saved_height;
    ULONG max_width, max_height, max_left, max_top;
    if (!screen || !left || !top || !width || !height) return 0;
    file = fopen(COMPOSE_WINDOW_STATE_PATH, "rb");
    if (!file) return 0;
    if (!fgets(header, sizeof(header), file) ||
        strncmp(header, COMPOSE_WINDOW_STATE_HEADER,
                strlen(COMPOSE_WINDOW_STATE_HEADER)) != 0 ||
        fscanf(file, "left=%lu\ntop=%lu\nwidth=%lu\nheight=%lu\n",
               &saved_left, &saved_top, &saved_width, &saved_height) != 4) {
        fclose(file);
        return 0;
    }
    fclose(file);

    max_width = (ULONG)screen->Width;
    max_height = (ULONG)screen->Height;
    if (max_width > 20UL) max_width -= 20UL;
    if (max_height > 20UL) max_height -= 20UL;
    *width = compose_state_clamp((ULONG)saved_width, 440UL, max_width);
    *height = compose_state_clamp((ULONG)saved_height, 300UL, max_height);
    max_left = (ULONG)screen->Width > *width
        ? (ULONG)screen->Width - *width : 0UL;
    max_top = (ULONG)screen->Height > *height
        ? (ULONG)screen->Height - *height : 0UL;
    *left = compose_state_clamp((ULONG)saved_left, 0UL, max_left);
    *top = compose_state_clamp((ULONG)saved_top, 0UL, max_top);
    return 1;
}

static void compose_state_save(const struct Window *window)
{
    FILE *file;
    int failed = 0;
    if (!window) return;
    compose_state_ensure_drawer();
    file = fopen(COMPOSE_WINDOW_STATE_TEMP, "wb");
    if (!file) return;
    if (fprintf(file, "%s\nleft=%lu\ntop=%lu\nwidth=%lu\nheight=%lu\n",
                COMPOSE_WINDOW_STATE_HEADER,
                (unsigned long)(window->LeftEdge < 0 ? 0 : window->LeftEdge),
                (unsigned long)(window->TopEdge < 0 ? 0 : window->TopEdge),
                (unsigned long)window->Width,
                (unsigned long)window->Height) < 0)
        failed = 1;
    if (fclose(file) != 0) failed = 1;
    if (failed) {
        DeleteFile((CONST_STRPTR)COMPOSE_WINDOW_STATE_TEMP);
        return;
    }
    DeleteFile((CONST_STRPTR)COMPOSE_WINDOW_STATE_PATH);
    if (!Rename((CONST_STRPTR)COMPOSE_WINDOW_STATE_TEMP,
                (CONST_STRPTR)COMPOSE_WINDOW_STATE_PATH))
        DeleteFile((CONST_STRPTR)COMPOSE_WINDOW_STATE_TEMP);
}

enum ComposeGadgetId {
    GID_COMPOSE_TO = 200,
    GID_COMPOSE_CC,
    GID_COMPOSE_BCC,
    GID_COMPOSE_TO_CONTACTS,
    GID_COMPOSE_CC_CONTACTS,
    GID_COMPOSE_BCC_CONTACTS,
    GID_COMPOSE_SUBJECT,
    GID_COMPOSE_BODY,
    GID_COMPOSE_BODY_SCROLL,
    GID_COMPOSE_ATTACHMENTS,
    GID_COMPOSE_ATTACHMENTS_SCROLL,
    GID_COMPOSE_ADD_ATTACHMENT,
    GID_COMPOSE_REMOVE_ATTACHMENT,
    GID_COMPOSE_STATUS,
    GID_COMPOSE_SEND,
    GID_COMPOSE_CANCEL
};


static int build_from_header(const AmgAccount *account,
                             char *destination, size_t capacity,
                             AmgError *error)
{
    char name_utf8[256];
    AmgBuffer encoded;
    int result;
    int length;

    if (!account || !destination || capacity == 0U)
        return AMG_ERR_ARGUMENT;

    if (!account->display_name[0]) {
        length = snprintf(destination, capacity, "%s", account->email);
        if (length < 0 || (size_t)length >= capacity) {
            amg_error_set(error, AMG_ERR_LIMIT,
                          T(MSG_A_HEADER_LINE_IS_TOO_LONG,
                            "A header line is too long."));
            return AMG_ERR_LIMIT;
        }
        return AMG_OK;
    }

    result = local_to_utf8(account->display_name, name_utf8,
                           sizeof(name_utf8));
    if (result != AMG_OK) {
        amg_error_set(error, AMG_ERR_LIMIT,
                      T(MSG_A_HEADER_LINE_IS_TOO_LONG,
                        "A header line is too long."));
        return result;
    }

    amg_buffer_init(&encoded);
    result = amg_base64_encode((const unsigned char *)name_utf8,
                               strlen(name_utf8), &encoded);
    if (result == AMG_OK)
        result = amg_buffer_terminate(&encoded);
    if (result != AMG_OK) {
        amg_buffer_free(&encoded);
        amg_error_set(error, result,
                      T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
        return result;
    }

    length = snprintf(destination, capacity, "=?UTF-8?B?%s?= <%s>",
                      (const char *)encoded.data, account->email);
    amg_buffer_free(&encoded);
    if (length < 0 || (size_t)length >= capacity) {
        amg_error_set(error, AMG_ERR_LIMIT,
                      T(MSG_A_HEADER_LINE_IS_TOO_LONG,
                        "A header line is too long."));
        return AMG_ERR_LIMIT;
    }
    return AMG_OK;
}

static void append_local_limited(char *destination, size_t capacity,
                                 const char *source)
{
    size_t used, available, length;
    if (!destination || !capacity || !source) return;
    used = strlen(destination);
    if (used >= capacity - 1U) return;
    available = capacity - used - 1U;
    length = strlen(source);
    if (length > available) length = available;
    memcpy(destination + used, source, length);
    destination[used + length] = 0;
}

static void delete_compose_temp_file(const char *path)
{
    if (path && *path) DeleteFile((CONST_STRPTR)path);
}

static void cleanup_compose_attachments(ComposeAttachment *attachments,
                                        size_t count)
{
    size_t i;
    if (!attachments) return;
    for (i = 0U; i < count; ++i) {
        if (attachments[i].temporary && attachments[i].path[0])
            delete_compose_temp_file(attachments[i].path);
        attachments[i].temporary = 0;
    }
}

static int write_compose_attachment_temp(ComposeAttachment *attachment,
                                       unsigned long uid, size_t index,
                                       const char *name_utf8,
                                       const unsigned char *data,
                                       size_t length, AmgError *error)
{
    static unsigned long sequence = 0UL;
    struct DateStamp stamp;
    AmgBuffer name_local;
    FILE *file;
    int result = AMG_OK;

    if (!attachment || (!data && length)) return AMG_ERR_ARGUMENT;
    memset(attachment, 0, sizeof(*attachment));
    DateStamp(&stamp);
    ++sequence;
    snprintf(attachment->path, sizeof(attachment->path),
             "T:AmiMail-compose-%08lx-%08lx-%08lx-%04lx-%02lu.tmp",
             uid, (unsigned long)stamp.ds_Days,
             (unsigned long)stamp.ds_Tick,
             (unsigned long)(sequence & 0xffffUL),
             (unsigned long)index);

    file = fopen(attachment->path, "wb");
    if (!file) {
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_TEMPORARY_ATTACHMENT_COULD_NOT_BE_CREATED, "Temporary attachment could not be created."));
        return AMG_ERR_IO;
    }
    if (length && fwrite(data, 1U, length, file) != length)
        result = AMG_ERR_IO;
    if (fclose(file) != 0 && result == AMG_OK) result = AMG_ERR_IO;
    if (result != AMG_OK) {
        delete_compose_temp_file(attachment->path);
        attachment->path[0] = 0;
        amg_error_set(error, result,
                      T(MSG_TEMPORARY_ATTACHMENT_COULD_NOT_BE_WRITTEN, "Temporary attachment could not be written."));
        return result;
    }

    strncpy(attachment->name_utf8,
            name_utf8 && *name_utf8 ? name_utf8 : "attachment.bin",
            sizeof(attachment->name_utf8) - 1U);
    attachment->name_utf8[sizeof(attachment->name_utf8) - 1U] = 0;
    amg_buffer_init(&name_local);
    if (amg_utf8_to_local(attachment->name_utf8, &name_local) == AMG_OK &&
        amg_buffer_terminate(&name_local) == AMG_OK && name_local.length) {
        strncpy(attachment->name_local, (const char *)name_local.data,
                sizeof(attachment->name_local) - 1U);
        attachment->name_local[sizeof(attachment->name_local) - 1U] = 0;
    } else {
        strcpy(attachment->name_local, "attachment.bin");
    }
    amg_buffer_free(&name_local);
    attachment->size = (unsigned long)length;
    attachment->temporary = 1;
    amg_error_set(error, AMG_OK, "");
    return AMG_OK;
}

void cleanup_draft_edit_files(DraftEditData *seed)
{
    if (!seed) return;
    cleanup_compose_attachments(seed->attachments, seed->attachment_count);
    seed->attachment_count = 0U;
}

int prepare_draft_edit_payload(AmgGui *gui, const unsigned char *payload,
                                 size_t payload_length, unsigned long uid,
                                 const char *mailbox_utf8,
                                 DraftEditData *seed, AmgError *error)
{
    AmgImapFetchRecord record;
    AmgMailHeaders headers;
    AmgBuffer body_utf8, body_local;
    size_t position = 0U;
    size_t attachment_count = 0U;
    size_t i;
    unsigned long attachment_total_bytes = 0UL;
    int result;

    if (!gui || !seed || !uid) return AMG_ERR_ARGUMENT;
    memset(seed, 0, sizeof(*seed));
    seed->uid = uid;
    strncpy(seed->mailbox_utf8, mailbox_utf8 ? mailbox_utf8 : "",
            sizeof(seed->mailbox_utf8) - 1U);
    seed->mailbox_utf8[sizeof(seed->mailbox_utf8) - 1U] = 0;

    result = amg_imap_fetch_record_next(payload, payload_length,
                                        &position, &record);
    if (result <= 0) {
        result = result < 0 ? result : AMG_ERR_PARSE;
        amg_error_set(error, result,
                      T(MSG_DRAFT_COULD_NOT_BE_PARSED, "Draft could not be parsed."));
        return result;
    }

    amg_mail_headers_init(&headers);
    amg_buffer_init(&body_utf8);
    amg_buffer_init(&body_local);
    result = amg_mail_headers_parse((const char *)record.literal,
                                    record.literal_length, &headers, NULL);
    if (result != AMG_OK) goto done;

    header_to_local(amg_mail_header_get(&headers, "To"), "",
                    seed->to_local, sizeof(seed->to_local));
    header_to_local(amg_mail_header_get(&headers, "Cc"), "",
                    seed->cc_local, sizeof(seed->cc_local));
    header_to_local(amg_mail_header_get(&headers, "Bcc"), "",
                    seed->bcc_local, sizeof(seed->bcc_local));
    header_to_local(amg_mail_header_get(&headers, "Subject"), "",
                    seed->subject_local, sizeof(seed->subject_local));
    snprintf(seed->message_id_utf8, sizeof(seed->message_id_utf8), "%s",
             amg_mail_header_get(&headers, "Message-ID")
                 ? amg_mail_header_get(&headers, "Message-ID") : "");
    snprintf(seed->in_reply_to_utf8, sizeof(seed->in_reply_to_utf8), "%s",
             amg_mail_header_get(&headers, "In-Reply-To")
                 ? amg_mail_header_get(&headers, "In-Reply-To") : "");
    snprintf(seed->references_utf8, sizeof(seed->references_utf8), "%s",
             amg_mail_header_get(&headers, "References")
                 ? amg_mail_header_get(&headers, "References") : "");

    result = amg_mime_extract_text((const char *)record.literal,
                                   record.literal_length, &body_utf8, error);
    if (result != AMG_OK) goto done;
    result = amg_buffer_terminate(&body_utf8);
    if (result == AMG_OK)
        result = amg_utf8_to_local((const char *)body_utf8.data, &body_local);
    if (result == AMG_OK) result = amg_buffer_terminate(&body_local);
    if (result != AMG_OK) goto done;
    if (body_local.length >= sizeof(seed->body_local)) {
        result = AMG_ERR_LIMIT;
        amg_error_set(error, result,
                      T(MSG_DRAFT_BODY_IS_TOO_LARGE_TO_EDIT, "Draft body is too large to edit."));
        goto done;
    }
    memcpy(seed->body_local, body_local.data, body_local.length + 1U);

    result = amg_mime_attachment_count((const char *)record.literal,
                                       record.literal_length,
                                       &attachment_count, error);
    if (result != AMG_OK) goto done;
    if (attachment_count > AMG_MAIL_MAX_ATTACHMENTS) {
        result = AMG_ERR_LIMIT;
        amg_error_set(error, result,
                      T(MSG_THE_DRAFT_CONTAINS_MORE_THAN_8_ATTACHMENTS, "The draft contains more than 8 attachments."));
        goto done;
    }

    for (i = 0U; i < attachment_count; ++i) {
        AmgBuffer name_utf8, data;
        amg_buffer_init(&name_utf8);
        amg_buffer_init(&data);
        result = amg_mime_extract_attachment(
            (const char *)record.literal, record.literal_length, i,
            &name_utf8, &data, error);
        if (result == AMG_OK) result = amg_buffer_terminate(&name_utf8);
        if (result == AMG_OK &&
            data.length > AMG_MAIL_MAX_ATTACHMENT_TOTAL -
                          attachment_total_bytes) {
            result = AMG_ERR_LIMIT;
            amg_error_set(error, result,
                          T(MSG_DRAFT_ATTACHMENTS_TOTAL_MORE_THAN_10_MB, "Draft attachments total more than 10 MB."));
        }
        if (result == AMG_OK)
            result = write_compose_attachment_temp(
                &seed->attachments[seed->attachment_count], uid, i,
                name_utf8.length ? (const char *)name_utf8.data
                                 : "attachment.bin",
                data.data, data.length, error);
        if (result == AMG_OK) {
            attachment_total_bytes += (unsigned long)data.length;
            ++seed->attachment_count;
        }
        amg_buffer_free(&name_utf8);
        amg_buffer_free(&data);
        if (result != AMG_OK) goto done;
    }

    amg_error_set(error, AMG_OK, "");

done:
    amg_mail_headers_free(&headers);
    amg_buffer_free(&body_utf8);
    amg_buffer_free(&body_local);
    if (result != AMG_OK) cleanup_draft_edit_files(seed);
    return result;
}

int prepare_reply_payload(AmgGui *gui, const unsigned char *payload,
                                 size_t payload_length, AmgError *error)
{
    AmgImapFetchRecord record;
    AmgMailHeaders headers;
    AmgBuffer body_utf8, body_local;
    const char *reply_to, *subject, *date, *message_id, *references;
    char from_local[768], subject_local[512], date_local[192];
    const char *cursor;
    size_t position = 0;
    int result;
    if (!gui) return AMG_ERR_ARGUMENT;
    result = amg_imap_fetch_record_next(payload, payload_length,
                                        &position, &record);
    if (result <= 0) {
        amg_error_set(error, result < 0 ? result : AMG_ERR_PARSE,
                      T(MSG_REPLY_DATA_COULD_NOT_BE_PARSED, "Reply data could not be parsed."));
        return result < 0 ? result : AMG_ERR_PARSE;
    }
    amg_mail_headers_init(&headers);
    amg_buffer_init(&body_utf8);
    amg_buffer_init(&body_local);
    result = amg_mail_headers_parse((const char *)record.literal,
                                    record.literal_length, &headers, NULL);
    if (result != AMG_OK) goto done;
    reply_to = amg_mail_header_get(&headers, "Reply-To");
    if (!reply_to || !*reply_to)
        reply_to = amg_mail_header_get(&headers, "From");
    subject = amg_mail_header_get(&headers, "Subject");
    date = amg_mail_header_get(&headers, "Date");
    message_id = amg_mail_header_get(&headers, "Message-ID");
    references = amg_mail_header_get(&headers, "References");
    header_to_local(reply_to, "", gui->reply_to_local,
                    sizeof(gui->reply_to_local));
    gui->reply_cc_local[0] = 0;
    header_to_local(amg_mail_header_get(&headers, "From"),
                    T(MSG_SENDER, "sender"), from_local, sizeof(from_local));
    header_to_local(subject, "", subject_local, sizeof(subject_local));
    header_to_local(date, "", date_local, sizeof(date_local));
    if (!subject_local[0])
        strcpy(gui->reply_subject_local, "Re:");
    else if ((subject_local[0] == 'R' || subject_local[0] == 'r') &&
             (subject_local[1] == 'E' || subject_local[1] == 'e') &&
             subject_local[2] == ':')
        snprintf(gui->reply_subject_local,
                 sizeof(gui->reply_subject_local), "%s", subject_local);
    else
        snprintf(gui->reply_subject_local,
                 sizeof(gui->reply_subject_local), "Re: %.507s", subject_local);

    snprintf(gui->reply_in_reply_to_utf8,
             sizeof(gui->reply_in_reply_to_utf8), "%s",
             message_id ? message_id : "");
    gui->reply_references_utf8[0] = 0;
    if (references && *references) {
        snprintf(gui->reply_references_utf8,
                 sizeof(gui->reply_references_utf8), "%s", references);
    }
    if (message_id && *message_id) {
        if (gui->reply_references_utf8[0])
            append_local_limited(gui->reply_references_utf8,
                                 sizeof(gui->reply_references_utf8), " ");
        append_local_limited(gui->reply_references_utf8,
                             sizeof(gui->reply_references_utf8), message_id);
    }

    result = amg_mime_extract_text((const char *)record.literal,
                                   record.literal_length, &body_utf8, error);
    if (result != AMG_OK) goto done;
    result = amg_buffer_terminate(&body_utf8);
    if (result == AMG_OK)
        result = amg_utf8_to_local((const char *)body_utf8.data, &body_local);
    if (result == AMG_OK) result = amg_buffer_terminate(&body_local);
    if (result != AMG_OK) goto done;

    gui->reply_body_local[0] = 0;
    amg_tr_snprintf(gui->reply_body_local, sizeof(gui->reply_body_local), MSG_ON_VALUE_VALUE_WROTE, "\n\nOn %s, %s wrote:\n", date_local[0] ? date_local :
                        T(MSG_AN_UNKNOWN_DATE, "an unknown date"), from_local);
    cursor = (const char *)body_local.data;
    while (*cursor && strlen(gui->reply_body_local) + 4U <
                         sizeof(gui->reply_body_local)) {
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), "> ");
        while (*cursor && *cursor != '\r' && *cursor != '\n') {
            char character[2];
            character[0] = *cursor++;
            character[1] = 0;
            append_local_limited(gui->reply_body_local,
                                 sizeof(gui->reply_body_local), character);
        }
        if (*cursor == '\r') ++cursor;
        if (*cursor == '\n') ++cursor;
        append_local_limited(gui->reply_body_local,
                             sizeof(gui->reply_body_local), "\n");
    }
    amg_error_set(error, AMG_OK, "");

done:
    if (result != AMG_OK && error && !error->message[0])
        amg_error_set(error, result,
                      T(MSG_REPLY_COULD_NOT_BE_PREPARED, "Reply could not be prepared."));
    amg_mail_headers_free(&headers);
    amg_buffer_free(&body_utf8);
    amg_buffer_free(&body_local);
    return result;
}

static unsigned char recipient_ascii_lower(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return (unsigned char)(c + ('a' - 'A'));
    return c;
}

static int recipient_email_equal(const char *left, const char *right)
{
    if (!left || !right) return 0;
    while (*left && *right) {
        if (recipient_ascii_lower((unsigned char)*left++) !=
            recipient_ascii_lower((unsigned char)*right++))
            return 0;
    }
    return *left == 0 && *right == 0;
}

static void recipient_trim_copy(const char *start, size_t length,
                                char *output, size_t capacity)
{
    while (length && (*start == ' ' || *start == '\t')) {
        ++start;
        --length;
    }
    while (length && (start[length - 1U] == ' ' ||
                      start[length - 1U] == '\t'))
        --length;
    if (!capacity) return;
    if (length >= capacity) length = capacity - 1U;
    memcpy(output, start, length);
    output[length] = 0;
}

static void recipient_identity(const char *token, char *identity,
                               size_t capacity)
{
    const char *left, *right, *start, *end;
    size_t length;
    if (!identity || !capacity) return;
    identity[0] = 0;
    if (!token) return;
    left = strchr(token, '<');
    right = left ? strchr(left + 1, '>') : NULL;
    if (left && right && right > left + 1) {
        start = left + 1;
        end = right;
    } else {
        start = token;
        end = token + strlen(token);
    }
    while (start < end && (*start == ' ' || *start == '\t' ||
                           *start == '"'))
        ++start;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' ||
                           end[-1] == '"'))
        --end;
    length = (size_t)(end - start);
    if (length >= capacity) length = capacity - 1U;
    memcpy(identity, start, length);
    identity[length] = 0;
}

typedef struct ReplyRecipientSet {
    char seen[32][256];
    size_t seen_count;
    char to[768];
    char cc[768];
    const char *own_email;
} ReplyRecipientSet;

static int reply_recipient_seen(const ReplyRecipientSet *set,
                                const char *identity)
{
    size_t i;
    for (i = 0U; i < set->seen_count; ++i)
        if (recipient_email_equal(set->seen[i], identity)) return 1;
    return 0;
}

static void reply_recipient_add(ReplyRecipientSet *set, const char *token)
{
    char trimmed[768];
    char identity[256];
    char *destination;
    size_t length;
    if (!set || !token) return;
    recipient_trim_copy(token, strlen(token), trimmed, sizeof(trimmed));
    if (!trimmed[0]) return;
    recipient_identity(trimmed, identity, sizeof(identity));
    if (!identity[0]) return;
    if (set->own_email && *set->own_email &&
        recipient_email_equal(identity, set->own_email))
        return;
    if (reply_recipient_seen(set, identity)) return;
    if (set->seen_count >= sizeof(set->seen) / sizeof(set->seen[0]))
        return;
    strncpy(set->seen[set->seen_count], identity,
            sizeof(set->seen[set->seen_count]) - 1U);
    set->seen[set->seen_count][sizeof(set->seen[set->seen_count]) - 1U] = 0;
    ++set->seen_count;
    destination = set->to[0] ? set->cc : set->to;
    length = strlen(destination);
    if (length && length + 2U < 768U) {
        destination[length++] = ',';
        destination[length++] = ' ';
        destination[length] = 0;
    }
    append_local_limited(destination, 768U, trimmed);
}

static void reply_recipient_add_list(ReplyRecipientSet *set,
                                     const char *list)
{
    const char *start, *cursor;
    int quoted = 0;
    int angle_depth = 0;
    if (!set || !list) return;
    start = cursor = list;
    for (;;) {
        char c = *cursor;
        if (c == '"' && (cursor == list || cursor[-1] != '\\'))
            quoted = !quoted;
        else if (!quoted && c == '<')
            ++angle_depth;
        else if (!quoted && c == '>' && angle_depth > 0)
            --angle_depth;
        if ((c == ',' && !quoted && angle_depth == 0) || c == 0) {
            char token[768];
            recipient_trim_copy(start, (size_t)(cursor - start),
                                token, sizeof(token));
            reply_recipient_add(set, token);
            if (!c) break;
            start = cursor + 1;
        }
        ++cursor;
    }
}

int prepare_reply_all_payload(AmgGui *gui, const unsigned char *payload,
                              size_t payload_length, AmgError *error)
{
    AmgImapFetchRecord record;
    AmgMailHeaders headers;
    ReplyRecipientSet recipients;
    char normal_reply_to[768];
    char reply_local[1536];
    char cc_local[1536];
    const char *reply_to;
    size_t position = 0U;
    int result;

    if (!gui) return AMG_ERR_ARGUMENT;
    result = prepare_reply_payload(gui, payload, payload_length, error);
    if (result != AMG_OK) return result;

    /* Keep the proven normal-reply target as a safe fallback. Reply All must
     * never fail merely because there is only one person to answer. */
    snprintf(normal_reply_to, sizeof(normal_reply_to), "%s",
             gui->reply_to_local);

    result = amg_imap_fetch_record_next(payload, payload_length,
                                        &position, &record);
    if (result <= 0) {
        result = result < 0 ? result : AMG_ERR_PARSE;
        amg_error_set(error, result,
                      T(MSG_REPLY_TO_ALL_DATA_COULD_NOT_BE_PARSED, "Reply-to-all data could not be parsed."));
        return result;
    }
    amg_mail_headers_init(&headers);
    result = amg_mail_headers_parse((const char *)record.literal,
                                    record.literal_length, &headers, NULL);
    if (result != AMG_OK) {
        amg_mail_headers_free(&headers);
        if (error && !error->message[0])
            amg_error_set(error, result,
                          T(MSG_REPLY_TO_ALL_COULD_NOT_BE_PREPARED, "Reply to all could not be prepared."));
        return result;
    }

    memset(&recipients, 0, sizeof(recipients));
    recipients.own_email = gui->account ? gui->account->email : "";
    reply_to = amg_mail_header_get(&headers, "Reply-To");
    if (!reply_to || !*reply_to)
        reply_to = amg_mail_header_get(&headers, "From");
    header_to_local(reply_to, "", reply_local, sizeof(reply_local));
    header_to_local(amg_mail_header_get(&headers, "Cc"), "",
                    cc_local, sizeof(cc_local));

    /* Reply All follows the requested simple rule: answer the sender
     * (Reply-To, otherwise From) and everybody in Cc, excluding our own
     * account and duplicate addresses. With no additional Cc recipient, it
     * therefore behaves exactly like a normal Reply. */
    reply_recipient_add_list(&recipients, reply_local);
    reply_recipient_add_list(&recipients, cc_local);
    if (recipients.to[0] || recipients.cc[0]) {
        snprintf(gui->reply_to_local, sizeof(gui->reply_to_local), "%s",
                 recipients.to);
        snprintf(gui->reply_cc_local, sizeof(gui->reply_cc_local), "%s",
                 recipients.cc);
    } else {
        snprintf(gui->reply_to_local, sizeof(gui->reply_to_local), "%s",
                 normal_reply_to);
        gui->reply_cc_local[0] = 0;
    }
    amg_mail_headers_free(&headers);
    amg_error_set(error, AMG_OK, "");
    return AMG_OK;
}

static int subject_has_forward_prefix(const char *subject)
{
    size_t length;
    if (!subject) return 0;
    while (*subject == ' ' || *subject == '\t') ++subject;
    length = strlen(subject);
    if (length >= 3U &&
        recipient_ascii_lower((unsigned char)subject[0]) == 'f' &&
        recipient_ascii_lower((unsigned char)subject[1]) == 'w') {
        if (length >= 4U &&
            recipient_ascii_lower((unsigned char)subject[2]) == 'd' &&
            subject[3] == ':')
            return 1;
        if (subject[2] == ':') return 1;
    }
    return 0;
}

int prepare_forward_payload(AmgGui *gui, const unsigned char *payload,
                            size_t payload_length, unsigned long uid,
                            DraftEditData *seed, AmgError *error)
{
    AmgImapFetchRecord record;
    AmgMailHeaders headers;
    AmgBuffer body_utf8, body_local;
    char from_local[768], date_local[192], subject_local[512];
    char to_local[1536], cc_local[1536];
    size_t position = 0U;
    size_t attachment_count = 0U;
    size_t i;
    unsigned long attachment_total_bytes = 0UL;
    int result;

    if (!gui || !seed || !uid) return AMG_ERR_ARGUMENT;
    memset(seed, 0, sizeof(*seed));
    result = amg_imap_fetch_record_next(payload, payload_length,
                                        &position, &record);
    if (result <= 0) {
        result = result < 0 ? result : AMG_ERR_PARSE;
        amg_error_set(error, result,
                      T(MSG_FORWARD_DATA_COULD_NOT_BE_PARSED, "Forward data could not be parsed."));
        return result;
    }

    amg_mail_headers_init(&headers);
    amg_buffer_init(&body_utf8);
    amg_buffer_init(&body_local);
    result = amg_mail_headers_parse((const char *)record.literal,
                                    record.literal_length, &headers, NULL);
    if (result != AMG_OK) goto done;

    header_to_local(amg_mail_header_get(&headers, "From"),
                    T(MSG_UNKNOWN_SENDER, "Unknown sender"),
                    from_local, sizeof(from_local));
    header_to_local(amg_mail_header_get(&headers, "Date"), "",
                    date_local, sizeof(date_local));
    header_to_local(amg_mail_header_get(&headers, "Subject"), "",
                    subject_local, sizeof(subject_local));
    header_to_local(amg_mail_header_get(&headers, "To"), "",
                    to_local, sizeof(to_local));
    header_to_local(amg_mail_header_get(&headers, "Cc"), "",
                    cc_local, sizeof(cc_local));

    if (!subject_local[0])
        strcpy(seed->subject_local, "Fwd:");
    else if (subject_has_forward_prefix(subject_local))
        snprintf(seed->subject_local, sizeof(seed->subject_local), "%s",
                 subject_local);
    else
        snprintf(seed->subject_local, sizeof(seed->subject_local),
                 "Fwd: %.506s", subject_local);

    result = amg_mime_extract_text((const char *)record.literal,
                                   record.literal_length, &body_utf8, error);
    if (result != AMG_OK) goto done;
    result = amg_buffer_terminate(&body_utf8);
    if (result == AMG_OK)
        result = amg_utf8_to_local((const char *)body_utf8.data, &body_local);
    if (result == AMG_OK) result = amg_buffer_terminate(&body_local);
    if (result != AMG_OK) goto done;

    seed->body_local[0] = 0;
    append_local_limited(seed->body_local, sizeof(seed->body_local),
        T(MSG_FORWARDED_MESSAGE, "---------- Forwarded message ----------\n"));
    append_local_limited(seed->body_local, sizeof(seed->body_local),
                         T(MSG_FROM, "From: "));
    append_local_limited(seed->body_local, sizeof(seed->body_local), from_local);
    append_local_limited(seed->body_local, sizeof(seed->body_local), "\n");
    if (date_local[0]) {
        append_local_limited(seed->body_local, sizeof(seed->body_local),
                             T(MSG_DATE, "Date: "));
        append_local_limited(seed->body_local, sizeof(seed->body_local), date_local);
        append_local_limited(seed->body_local, sizeof(seed->body_local), "\n");
    }
    if (subject_local[0]) {
        append_local_limited(seed->body_local, sizeof(seed->body_local),
                             T(MSG_SUBJECT, "Subject: "));
        append_local_limited(seed->body_local, sizeof(seed->body_local), subject_local);
        append_local_limited(seed->body_local, sizeof(seed->body_local), "\n");
    }
    if (to_local[0]) {
        append_local_limited(seed->body_local, sizeof(seed->body_local),
                             T(MSG_TO, "To: "));
        append_local_limited(seed->body_local, sizeof(seed->body_local), to_local);
        append_local_limited(seed->body_local, sizeof(seed->body_local), "\n");
    }
    if (cc_local[0]) {
        append_local_limited(seed->body_local, sizeof(seed->body_local), "Cc: ");
        append_local_limited(seed->body_local, sizeof(seed->body_local), cc_local);
        append_local_limited(seed->body_local, sizeof(seed->body_local), "\n");
    }
    append_local_limited(seed->body_local, sizeof(seed->body_local), "\n");
    append_local_limited(seed->body_local, sizeof(seed->body_local),
                         (const char *)body_local.data);

    result = amg_mime_attachment_count((const char *)record.literal,
                                       record.literal_length,
                                       &attachment_count, error);
    if (result != AMG_OK) goto done;
    if (attachment_count > AMG_MAIL_MAX_ATTACHMENTS) {
        result = AMG_ERR_LIMIT;
        amg_error_set(error, result,
                      T(MSG_THE_MESSAGE_CONTAINS_MORE_THAN_8_ATTACHMENTS, "The message contains more than 8 attachments."));
        goto done;
    }
    for (i = 0U; i < attachment_count; ++i) {
        AmgBuffer name_utf8, data;
        amg_buffer_init(&name_utf8);
        amg_buffer_init(&data);
        result = amg_mime_extract_attachment(
            (const char *)record.literal, record.literal_length, i,
            &name_utf8, &data, error);
        if (result == AMG_OK) result = amg_buffer_terminate(&name_utf8);
        if (result == AMG_OK &&
            data.length > AMG_MAIL_MAX_ATTACHMENT_TOTAL -
                          attachment_total_bytes) {
            result = AMG_ERR_LIMIT;
            amg_error_set(error, result,
                          T(MSG_ATTACHMENTS_TOTAL_MORE_THAN_10_MB, "Attachments total more than 10 MB."));
        }
        if (result == AMG_OK)
            result = write_compose_attachment_temp(
                &seed->attachments[seed->attachment_count], uid, i,
                name_utf8.length ? (const char *)name_utf8.data
                                 : "attachment.bin",
                data.data, data.length, error);
        if (result == AMG_OK) {
            attachment_total_bytes += (unsigned long)data.length;
            ++seed->attachment_count;
        }
        amg_buffer_free(&name_utf8);
        amg_buffer_free(&data);
        if (result != AMG_OK) goto done;
    }
    amg_error_set(error, AMG_OK, "");

done:
    amg_mail_headers_free(&headers);
    amg_buffer_free(&body_utf8);
    amg_buffer_free(&body_local);
    if (result != AMG_OK) cleanup_draft_edit_files(seed);
    return result;
}

static int selected_file_size(const char *path, unsigned long *size)
{
    FILE *file = fopen(path, "rb");
    long length;
    if (!file) return AMG_ERR_IO;
    if (fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) < 0) {
        fclose(file);
        return AMG_ERR_IO;
    }
    fclose(file);
    *size = (unsigned long)length;
    return AMG_OK;
}

static unsigned long attachment_total(const ComposeAttachment *attachments,
                                      size_t count)
{
    size_t i;
    unsigned long total = 0;
    for (i = 0; i < count; ++i) total += attachments[i].size;
    return total;
}

static void rebuild_attachment_list(struct Gadget *list_gadget,
                                    struct Window *window, struct List *list,
                                    const ComposeAttachment *attachments,
                                    size_t count)
{
    size_t i;
    SetGadgetAttrs(list_gadget, window, NULL,
                   LISTBROWSER_Labels, (ULONG)~0UL,
                   TAG_DONE);
    FreeListBrowserList(list);
    NewList(list);
    for (i = 0; i < count; ++i) {
        char line[384];
        struct Node *node;
        snprintf(line, sizeof(line), "%s (%lu KB)",
                 attachments[i].name_local,
                 (attachments[i].size + 1023UL) / 1024UL);
        node = one_column_node(line, (ULONG)i);
        if (node) AddTail(list, node);
    }
    SetGadgetAttrs(list_gadget, window, NULL,
                   LISTBROWSER_Labels, (ULONG)(uintptr_t)list,
                   TAG_DONE);
}

static void update_compose_status(struct Gadget *status_gadget,
                                  struct Window *window,
                                  const ComposeAttachment *attachments,
                                  size_t count)
{
    char text[160];
    unsigned long total = attachment_total(attachments, count);
    amg_tr_snprintf(text, sizeof(text), MSG_VALUE_ATTACHMENT_S_VALUE_KB_OF_10240_KB, "%lu attachment(s), %lu KB of 10240 KB", (unsigned long)count, (total + 1023UL) / 1024UL);
    set_string(status_gadget, window, text);
}

static int add_attachment(struct Window *window, struct Gadget *list_gadget,
                          struct Gadget *status_gadget, struct List *list,
                          ComposeAttachment *attachments, size_t *count)
{
    struct FileRequester *request;
    char path[COMPOSE_PATH_MAX];
    unsigned long size, total;
    size_t i;
    if (*count >= AMG_MAIL_MAX_ATTACHMENTS) {
        set_string(status_gadget, window,
                   T(MSG_A_MAXIMUM_OF_8_ATTACHMENTS_IS_ALLOWED, "A maximum of 8 attachments is allowed."));
        return 0;
    }
    request = (struct FileRequester *)AllocAslRequestTags(
        ASL_FileRequest,
        ASLFR_TitleText, (ULONG)(uintptr_t)T(MSG_SELECT_ATTACHMENT, "Select attachment"),
        ASLFR_Window, (ULONG)(uintptr_t)window,
        ASLFR_SleepWindow, TRUE,
        ASLFR_RejectIcons, TRUE,
        TAG_DONE);
    if (!request) {
        set_string(status_gadget, window,
                   T(MSG_FILE_REQUESTER_COULD_NOT_BE_OPENED, "File requester could not be opened."));
        return 0;
    }
    if (!AslRequest(request, NULL)) {
        FreeAslRequest(request);
        return 0;
    }
    strncpy(path, request->rf_Dir ? (const char *)request->rf_Dir : "",
            sizeof(path) - 1U);
    path[sizeof(path) - 1U] = 0;
    if (!AddPart((STRPTR)path, request->rf_File,
                 (LONG)sizeof(path))) {
        FreeAslRequest(request);
        set_string(status_gadget, window, T(MSG_FILE_PATH_IS_TOO_LONG, "File path is too long."));
        return 0;
    }
    for (i = 0; i < *count; ++i) {
        if (!strcmp(attachments[i].path, path)) {
            FreeAslRequest(request);
            set_string(status_gadget, window,
                       T(MSG_THIS_FILE_IS_ALREADY_ATTACHED, "This file is already attached."));
            return 0;
        }
    }
    if (selected_file_size(path, &size) != AMG_OK) {
        FreeAslRequest(request);
        set_string(status_gadget, window,
                   T(MSG_FILE_COULD_NOT_BE_READ, "File could not be read."));
        return 0;
    }
    total = attachment_total(attachments, *count);
    if (size > AMG_MAIL_MAX_ATTACHMENT_TOTAL - total) {
        FreeAslRequest(request);
        set_string(status_gadget, window,
                   T(MSG_ATTACHMENTS_MAY_TOTAL_NO_MORE_THAN_10_MB_LOCAL, "Attachments may total no more than 10 MB."));
        return 0;
    }
    strncpy(attachments[*count].path, path,
            sizeof(attachments[*count].path) - 1U);
    attachments[*count].path[sizeof(attachments[*count].path) - 1U] = 0;
    strncpy(attachments[*count].name_local,
            request->rf_File ? (const char *)request->rf_File : "attachment.bin",
            sizeof(attachments[*count].name_local) - 1U);
    attachments[*count].name_local[
        sizeof(attachments[*count].name_local) - 1U] = 0;
    if (local_to_utf8(attachments[*count].name_local,
                      attachments[*count].name_utf8,
                      sizeof(attachments[*count].name_utf8)) != AMG_OK)
        strcpy(attachments[*count].name_utf8, "attachment.bin");
    attachments[*count].size = size;
    attachments[*count].temporary = 0;
    ++*count;
    FreeAslRequest(request);
    rebuild_attachment_list(list_gadget, window, list, attachments, *count);
    update_compose_status(status_gadget, window, attachments, *count);
    return 1;
}

static void remove_attachment(struct Window *window, struct Gadget *list_gadget,
                              struct Gadget *status_gadget, struct List *list,
                              ComposeAttachment *attachments, size_t *count)
{
    ULONG selected = (ULONG)~0UL;
    size_t i;
    GetAttr(LISTBROWSER_Selected, (Object *)list_gadget, &selected);
    if (selected == (ULONG)~0UL || selected >= *count) {
        set_string(status_gadget, window,
                   T(MSG_PLEASE_SELECT_AN_ATTACHMENT_FIRST, "Please select an attachment first."));
        return;
    }
    if (attachments[selected].temporary && attachments[selected].path[0])
        delete_compose_temp_file(attachments[selected].path);
    for (i = (size_t)selected; i + 1U < *count; ++i)
        attachments[i] = attachments[i + 1U];
    --*count;
    memset(&attachments[*count], 0, sizeof(attachments[*count]));
    rebuild_attachment_list(list_gadget, window, list, attachments, *count);
    update_compose_status(status_gadget, window, attachments, *count);
}

static int is_leap_year(unsigned long year)
{
    return (year % 4UL == 0UL && year % 100UL != 0UL) ||
           year % 400UL == 0UL;
}

static unsigned long days_in_year(unsigned long year)
{
    return is_leap_year(year) ? 366UL : 365UL;
}

static int make_date_and_message_id(char date[96], char message_id[256])
{
    static const char *weekdays[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    static const unsigned char month_lengths[] = {
        31U, 28U, 31U, 30U, 31U, 30U,
        31U, 31U, 30U, 31U, 30U, 31U
    };
    static unsigned long sequence = 0UL;
    struct DateStamp stamp;
    unsigned long remaining_days, year, month, day;
    unsigned long hour, minute, second, weekday;
    int length;

    DateStamp(&stamp);
    if (stamp.ds_Days < 0 || stamp.ds_Minute < 0 || stamp.ds_Tick < 0)
        return AMG_ERR_IO;

    remaining_days = (unsigned long)stamp.ds_Days;
    weekday = remaining_days % 7UL; /* 1 January 1978 was a Sunday. */
    year = 1978UL;
    while (remaining_days >= days_in_year(year)) {
        remaining_days -= days_in_year(year);
        ++year;
    }
    month = 0UL;
    while (month < 11UL) {
        unsigned long length_of_month = month_lengths[month];
        if (month == 1UL && is_leap_year(year)) ++length_of_month;
        if (remaining_days < length_of_month) break;
        remaining_days -= length_of_month;
        ++month;
    }
    day = remaining_days + 1UL;
    hour = (unsigned long)stamp.ds_Minute / 60UL;
    minute = (unsigned long)stamp.ds_Minute % 60UL;
    second = (unsigned long)stamp.ds_Tick / 50UL;
    if (second > 59UL) second = 59UL;

    /* DateStamp contains local time. -0000 denotes an unknown local offset. */
    length = snprintf(date, 96U,
                      "%s, %02lu %s %04lu %02lu:%02lu:%02lu -0000",
                      weekdays[weekday], day, months[month], year,
                      hour, minute, second);
    if (length < 0 || length >= 96) return AMG_ERR_IO;
    ++sequence;
    length = snprintf(message_id, 256U, "<%08lx.%04lx.%04lx.%04lx@amimail.local>",
                      (unsigned long)stamp.ds_Days,
                      (unsigned long)stamp.ds_Minute,
                      (unsigned long)stamp.ds_Tick, sequence);
    if (length < 0 || length >= 256) return AMG_ERR_IO;
    return AMG_OK;
}

static int queue_composed_mail(AmgGui *gui, struct Window *window,
                               struct Gadget *to_gadget,
                               struct Gadget *cc_gadget,
                               struct Gadget *bcc_gadget,
                               struct Gadget *subject_gadget,
                               struct Gadget *body_gadget,
                               const ComposeAttachment *attachments,
                               size_t attachment_count, ComposeMode mode,
                               const DraftEditData *seed,
                               int save_as_draft, AmgError *error)
{
    char to_utf8[1536], cc_utf8[1536], bcc_utf8[1536];
    char subject_utf8[1024];
    char from_header[768];
    char date[96], message_id[256];
    AmgBuffer body_utf8;
    AmgAttachmentInput inputs[AMG_MAIL_MAX_ATTACHMENTS];
    AmgMailDraft draft;
    const unsigned char *p;
    STRPTR body_local = NULL;
    size_t i;
    int result = AMG_OK;
    if (!save_as_draft && !*string_text(to_gadget) &&
        !*string_text(cc_gadget) && !*string_text(bcc_gadget)) {
        amg_error_set(error, AMG_ERR_ARGUMENT,
                      T(MSG_AT_LEAST_ONE_RECIPIENT_IS_REQUIRED, "At least one recipient is required."));
        return AMG_ERR_ARGUMENT;
    }
    if (local_to_utf8(string_text(to_gadget), to_utf8,
                      sizeof(to_utf8)) != AMG_OK ||
        local_to_utf8(string_text(cc_gadget), cc_utf8,
                      sizeof(cc_utf8)) != AMG_OK ||
        local_to_utf8(string_text(bcc_gadget), bcc_utf8,
                      sizeof(bcc_utf8)) != AMG_OK ||
        local_to_utf8(string_text(subject_gadget), subject_utf8,
                      sizeof(subject_utf8)) != AMG_OK) {
        amg_error_set(error, AMG_ERR_LIMIT, T(MSG_A_HEADER_LINE_IS_TOO_LONG, "A header line is too long."));
        return AMG_ERR_LIMIT;
    }
    if (build_from_header(gui->account, from_header, sizeof(from_header),
                          error) != AMG_OK)
        return error ? error->code : AMG_ERR_LIMIT;

    body_local = (STRPTR)(uintptr_t)DoGadgetMethod(
        body_gadget, window, NULL, GM_TEXTEDITOR_ExportText, 0UL);
    if (!body_local) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_MESSAGE_TEXT_COULD_NOT_BE_READ, "Message text could not be read."));
        return AMG_ERR_MEMORY;
    }
    amg_buffer_init(&body_utf8);
    p = (const unsigned char *)body_local;
    while (*p && result == AMG_OK) {
        unsigned char bytes[2];
        if (*p < 0x80U) {
            result = amg_buffer_append_char(&body_utf8, *p);
        } else {
            bytes[0] = (unsigned char)(0xC0U | (*p >> 6));
            bytes[1] = (unsigned char)(0x80U | (*p & 0x3FU));
            result = amg_buffer_append(&body_utf8, bytes, 2U);
        }
        ++p;
    }
    if (result == AMG_OK) result = amg_buffer_terminate(&body_utf8);
    if (result != AMG_OK) {
        amg_buffer_free(&body_utf8);
        FreeVec(body_local);
        amg_error_set(error, result, T(MSG_MAIL_TEXT_IS_TOO_LARGE, "Mail text is too large."));
        return result;
    }
    for (i = 0; i < attachment_count; ++i) {
        inputs[i].path = attachments[i].path;
        inputs[i].name_utf8 = attachments[i].name_utf8;
        inputs[i].size = attachments[i].size;
        inputs[i].delete_after_use = attachments[i].temporary;
    }
    if (make_date_and_message_id(date, message_id) != AMG_OK) {
        amg_buffer_free(&body_utf8);
        FreeVec(body_local);
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_MESSAGE_DATE_COULD_NOT_BE_GENERATED, "Message date could not be generated."));
        return AMG_ERR_IO;
    }
    if (mode == COMPOSE_MODE_EDIT_DRAFT && seed &&
        seed->message_id_utf8[0]) {
        strncpy(message_id, seed->message_id_utf8, sizeof(message_id) - 1U);
        message_id[sizeof(message_id) - 1U] = 0;
    }
    draft.from = from_header;
    draft.to = to_utf8;
    draft.cc = cc_utf8;
    draft.bcc = bcc_utf8;
    draft.subject = subject_utf8;
    draft.body_utf8 = (const char *)body_utf8.data;
    draft.date_rfc2822 = date;
    draft.message_id = message_id;
    if (mode == COMPOSE_MODE_REPLY) {
        draft.in_reply_to = gui->reply_in_reply_to_utf8;
        draft.references = gui->reply_references_utf8;
    } else if (mode == COMPOSE_MODE_EDIT_DRAFT && seed) {
        draft.in_reply_to = seed->in_reply_to_utf8;
        draft.references = seed->references_utf8;
    } else {
        draft.in_reply_to = NULL;
        draft.references = NULL;
    }
    draft.attachments = inputs;
    draft.attachment_count = attachment_count;

    if (!amg_network_is_running(gui->network)) {
        result = amg_network_start(gui->network, gui->account, error);
        if (result == AMG_OK)
            result = amg_network_request(gui->network, AMG_NET_CONNECT, 0,
                                         NULL, NULL, error);
    } else if (save_as_draft &&
               !amg_network_is_connected(gui->network)) {
        result = amg_network_request(gui->network, AMG_NET_CONNECT, 0,
                                     NULL, NULL, error);
    }
    if (result == AMG_OK) {
        if (save_as_draft) {
            const char *draft_mailbox = gui->labels[3U].available
                ? gui->labels[3U].server_mailbox_utf8 : "\\Drafts";
            if (mode == COMPOSE_MODE_EDIT_DRAFT && seed &&
                seed->uid && seed->mailbox_utf8[0])
                result = amg_network_request_draft_update(
                    gui->network, &draft, draft_mailbox,
                    seed->uid, seed->mailbox_utf8, error);
            else
                result = amg_network_request_draft(
                    gui->network, &draft, draft_mailbox, error);
        } else if (mode == COMPOSE_MODE_EDIT_DRAFT && seed &&
                   seed->uid && seed->mailbox_utf8[0]) {
            result = amg_network_request_mail_from_draft(
                gui->network, &draft, seed->uid, seed->mailbox_utf8, error);
        } else {
            result = amg_network_request_mail(gui->network, &draft, error);
        }
    }
    amg_buffer_free(&body_utf8);
    FreeVec(body_local);
    return result;
}



int compose_dialog(AmgGui *gui, ComposeMode mode,
                          DraftEditData *draft_seed, AmgError *error)
{
    Object *dialog;
    struct Window *window;
    struct Gadget *to_gadget, *cc_gadget, *bcc_gadget, *subject_gadget;
    struct Gadget *body_gadget, *body_scroller;
    struct Gadget *attachments_gadget, *attachments_scroller;
    struct Gadget *compose_status;
    struct List attachment_list;
    ComposeAttachment attachments[AMG_MAIL_MAX_ATTACHMENTS];
    size_t attachment_count = 0;
    ULONG signal_mask, compose_width = 600UL, compose_height = 400UL;
    ULONG compose_left = 0UL, compose_top = 0UL;
    const char *initial_to = "";
    const char *initial_cc = "";
    const char *initial_bcc = "";
    const char *initial_subject = "";
    const char *initial_body = "";
    char *initial_body_with_signature = NULL;
    int reply_mode = mode == COMPOSE_MODE_REPLY;
    int edit_draft = mode == COMPOSE_MODE_EDIT_DRAFT;
    int forward_mode = mode == COMPOSE_MODE_FORWARD;
    int done = 0, submitted = 0, sent_queued = 0;
    int select_all_pending = 0;
    ComposeScrollerSyncState body_scroll_state;
    ComposeInputHookState compose_input_state;
    struct Hook compose_idcmp_hook;
    Class *compose_texteditor_class = NULL;

    if (gui->compose_open) {
        if (gui->compose_window) {
            WindowToFront(gui->compose_window);
            ActivateWindow(gui->compose_window);
        }
        return 0;
    }

    memset(attachments, 0, sizeof(attachments));
    memset(&body_scroll_state, 0, sizeof(body_scroll_state));
    memset(&compose_input_state, 0, sizeof(compose_input_state));
    memset(&compose_idcmp_hook, 0, sizeof(compose_idcmp_hook));
    if (reply_mode) {
        initial_to = gui->reply_to_local;
        initial_cc = gui->reply_cc_local;
        initial_subject = gui->reply_subject_local;
        initial_body = gui->reply_body_local;
    } else if (forward_mode && draft_seed) {
        initial_to = draft_seed->to_local;
        initial_cc = draft_seed->cc_local;
        initial_bcc = draft_seed->bcc_local;
        initial_subject = draft_seed->subject_local;
        initial_body = draft_seed->body_local;
        attachment_count = draft_seed->attachment_count;
        if (attachment_count > AMG_MAIL_MAX_ATTACHMENTS)
            attachment_count = AMG_MAIL_MAX_ATTACHMENTS;
        if (attachment_count)
            memcpy(attachments, draft_seed->attachments,
                   attachment_count * sizeof(attachments[0]));
        memset(draft_seed->attachments, 0, sizeof(draft_seed->attachments));
        draft_seed->attachment_count = 0U;
    } else if (!edit_draft && draft_seed) {
        /* COMPOSE_MODE_NEW may carry an initial seed from a mailto: URL.
         * Draft-specific metadata and attachment ownership are deliberately
         * not used in this mode. */
        initial_to = draft_seed->to_local;
        initial_cc = draft_seed->cc_local;
        initial_bcc = draft_seed->bcc_local;
        initial_subject = draft_seed->subject_local;
        initial_body = draft_seed->body_local;
    } else if (edit_draft && draft_seed) {
        initial_to = draft_seed->to_local;
        initial_cc = draft_seed->cc_local;
        initial_bcc = draft_seed->bcc_local;
        initial_subject = draft_seed->subject_local;
        initial_body = draft_seed->body_local;
        attachment_count = draft_seed->attachment_count;
        if (attachment_count > AMG_MAIL_MAX_ATTACHMENTS)
            attachment_count = AMG_MAIL_MAX_ATTACHMENTS;
        if (attachment_count)
            memcpy(attachments, draft_seed->attachments,
                   attachment_count * sizeof(attachments[0]));
        memset(draft_seed->attachments, 0, sizeof(draft_seed->attachments));
        draft_seed->attachment_count = 0U;
    }
    NewList(&attachment_list);
    to_gadget = cc_gadget = bcc_gadget = subject_gadget = NULL;
    body_gadget = body_scroller = NULL;
    attachments_gadget = attachments_scroller = compose_status = NULL;
    if (gui->screen && (ULONG)gui->screen->Width > 40UL) {
        ULONG available_width = (ULONG)gui->screen->Width - 20UL;
        if (compose_width > available_width) compose_width = available_width;
    }
    if (compose_width < 440UL) compose_width = 440UL;
    if (gui->screen && (ULONG)gui->screen->Height > 40UL) {
        ULONG available_height = (ULONG)gui->screen->Height - 20UL;
        if (compose_height > available_height) compose_height = available_height;
    }
    if (compose_height < 300UL) compose_height = 300UL;
    if (gui->screen) {
        if ((ULONG)gui->screen->Width > compose_width)
            compose_left = ((ULONG)gui->screen->Width - compose_width) / 2UL;
        if ((ULONG)gui->screen->Height > compose_height)
            compose_top = ((ULONG)gui->screen->Height - compose_height) / 2UL;
        (void)compose_state_load(gui->screen, &compose_left, &compose_top,
                                 &compose_width, &compose_height);
    }

    body_scroller = create_vertical_scroller(GID_COMPOSE_BODY_SCROLL);
    attachments_scroller =
        create_vertical_scroller(GID_COMPOSE_ATTACHMENTS_SCROLL);
    if (!body_scroller || !attachments_scroller) {
        if (attachments_scroller)
            DisposeObject((Object *)attachments_scroller);
        if (body_scroller) DisposeObject((Object *)body_scroller);
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_SCROLLBAR_COULD_NOT_BE_CREATED, "Scrollbar could not be created."));
        cleanup_compose_attachments(attachments, attachment_count);
        return 0;
    }

    compose_texteditor_class = compose_make_texteditor_class();
    if (!compose_texteditor_class) {
        DisposeObject((Object *)attachments_scroller);
        DisposeObject((Object *)body_scroller);
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_NEW_MAIL_WINDOW_COULD_NOT_BE_CREATED,
                        "New mail window could not be created."));
        cleanup_compose_attachments(attachments, attachment_count);
        return 0;
    }

    /* Apply the saved signature to newly created messages. Drafts already
     * contain exactly what the user saved and must not receive it twice.
     * In replies/forwards the signature sits between the typing area and the
     * quoted/forwarded original. For a mailto: body it follows that body. */
    if (!edit_draft) {
        char signature_local[GUI_SIGNATURE_MAX];
        gui_signature_load(gui, signature_local, sizeof(signature_local));
        if (signature_local[0]) {
            initial_body_with_signature =
                (char *)calloc(1U, GUI_REPLY_BODY_MAX);
            if (initial_body_with_signature) {
                if (reply_mode || forward_mode) {
                    append_local_limited(initial_body_with_signature,
                                         GUI_REPLY_BODY_MAX, "\n\n");
                    append_local_limited(initial_body_with_signature,
                                         GUI_REPLY_BODY_MAX, signature_local);
                    append_local_limited(initial_body_with_signature,
                                         GUI_REPLY_BODY_MAX, initial_body);
                } else {
                    append_local_limited(initial_body_with_signature,
                                         GUI_REPLY_BODY_MAX, initial_body);
                    append_local_limited(initial_body_with_signature,
                                         GUI_REPLY_BODY_MAX, "\n\n");
                    append_local_limited(initial_body_with_signature,
                                         GUI_REPLY_BODY_MAX, signature_local);
                }
                initial_body = initial_body_with_signature;
            }
        }
    }

    compose_init_input_hook(&compose_idcmp_hook, &compose_input_state);

    dialog = WindowObject,
        WA_Title, edit_draft
            ? T(MSG_AMIMAIL_EDIT_DRAFT, "AmiMail - Edit draft")
            : (reply_mode
                ? T(MSG_AMIMAIL_REPLY, "AmiMail - Reply")
                : (forward_mode
                    ? T(MSG_AMIMAIL_FORWARD, "AmiMail - Forward")
                    : T(MSG_AMIMAIL_NEW_MAIL, "AmiMail - New mail"))),
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                      WFLG_SIZEGADGET | WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_MENUPICK |
                  IDCMP_RAWKEY | IDCMP_IDCMPUPDATE | IDCMP_NEWSIZE,
        WINDOW_IDCMPHook, &compose_idcmp_hook,
        WINDOW_IDCMPHookBits, IDCMP_IDCMPUPDATE,
        WA_PubScreen, gui->screen,
        WA_Left, compose_left,
        WA_Top, compose_top,
        WA_Width, compose_width,
        WA_Height, compose_height,
        WA_MinWidth, 440,
        WA_MinHeight, 300,
        WINDOW_NewMenu, gui_compose_menu_definition(),
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, TRUE,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T(MSG_TO_6B77, "To:")),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    to_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_COMPOSE_TO,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 767,
                        STRINGA_TextVal, (ULONG)(uintptr_t)initial_to,
                    EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_COMPOSE_TO_CONTACTS,
                    GA_RelVerify, TRUE,
                    GA_Text, "...",
                EndObject,
                CHILD_MinWidth, 30,
                CHILD_MaxWidth, 30,
                CHILD_WeightedWidth, 0,
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label("CC:"),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    cc_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_COMPOSE_CC,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 767,
                        STRINGA_TextVal, (ULONG)(uintptr_t)initial_cc,
                    EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_COMPOSE_CC_CONTACTS,
                    GA_RelVerify, TRUE,
                    GA_Text, "...",
                EndObject,
                CHILD_MinWidth, 30,
                CHILD_MaxWidth, 30,
                CHILD_WeightedWidth, 0,
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label("BCC:"),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    bcc_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_COMPOSE_BCC,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 767,
                        STRINGA_TextVal, (ULONG)(uintptr_t)initial_bcc,
                    EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_COMPOSE_BCC_CONTACTS,
                    GA_RelVerify, TRUE,
                    GA_Text, "...",
                EndObject,
                CHILD_MinWidth, 30,
                CHILD_MaxWidth, 30,
                CHILD_WeightedWidth, 0,
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T(MSG_SUBJECT_DF0D, "Subject:")),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    subject_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_COMPOSE_SUBJECT,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 511,
                        STRINGA_TextVal, (ULONG)(uintptr_t)initial_subject,
                    EndObject,
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T(MSG_MESSAGE, "Message:")),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_SpaceOuter, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                    LAYOUT_AddChild,
                        body_gadget = (struct Gadget *)NewObject(
                            compose_texteditor_class, NULL,
                            GA_ID, GID_COMPOSE_BODY,
                            GA_TabCycle, TRUE,
                            GA_TEXTEDITOR_Contents,
                                (ULONG)(uintptr_t)initial_body,
                            GA_TEXTEDITOR_TabSize, 4,
                            GA_TEXTEDITOR_IndentWidth, 4,
                            GA_TEXTEDITOR_TabKeyPolicy,
                                GV_TEXTEDITOR_TabKey_IndentsAfter,
                            TAG_DONE),
                    LAYOUT_AddChild, body_scroller,
                    CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                    CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                    CHILD_WeightedWidth, 0,
                EndObject,
                CHILD_MinHeight, 100,
                CHILD_WeightedHeight, 100,
            EndObject,
            CHILD_WeightedHeight, 65,
            CHILD_MinHeight, 100,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T(MSG_ATTACHMENTS, "Attachments:")),
                CHILD_MinWidth, 70,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild, HGroupObject,
                    LAYOUT_AddChild, HGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, FALSE,
                        LAYOUT_AddChild,
                            attachments_gadget =
                                (struct Gadget *)ListBrowserObject,
                                GA_ID, GID_COMPOSE_ATTACHMENTS,
                                GA_RelVerify, TRUE,
                                LISTBROWSER_Labels, &attachment_list,
                                LISTBROWSER_ShowSelected, TRUE,
                                LISTBROWSER_VerticalProp, FALSE,
                            EndObject,
                        LAYOUT_AddChild, attachments_scroller,
                        CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                        CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                        CHILD_WeightedWidth, 0,
                    EndObject,
                    CHILD_WeightedWidth, 75,
                    CHILD_MinHeight, 45,

                    LAYOUT_AddChild, VGroupObject,
                        LAYOUT_AddChild, ButtonObject,
                            GA_ID, GID_COMPOSE_ADD_ATTACHMENT,
                            GA_RelVerify, TRUE,
                            GA_Text, T(MSG_ATTACHMENT, "Attach_ment..."),
                        EndObject,
                        LAYOUT_AddChild, ButtonObject,
                            GA_ID, GID_COMPOSE_REMOVE_ATTACHMENT,
                            GA_RelVerify, TRUE,
                            GA_Text, T(MSG_REMOVE, "_Remove"),
                        EndObject,
                    EndObject,
                    CHILD_WeightedWidth, 25,
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 25,

            LAYOUT_AddChild,
                compose_status = (struct Gadget *)StringObject,
                    GA_ID, GID_COMPOSE_STATUS,
                    GA_ReadOnly, TRUE,
                    STRINGA_TextVal, T(MSG_0_ATTACHMENTS_0_KB_OF_10240_KB, "0 attachments, 0 KB of 10240 KB"),
                EndObject,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_COMPOSE_SEND,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_SEND, "_Send"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_COMPOSE_CANCEL,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_CANCEL, "_Cancel"),
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;

    if (!dialog) {
        FreeClass(compose_texteditor_class);
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_NEW_MAIL_WINDOW_COULD_NOT_BE_CREATED, "New mail window could not be created."));
        cleanup_compose_attachments(attachments, attachment_count);
        free(initial_body_with_signature);
        return 0;
    }
    if (!compose_connect_texteditor_scroller(body_gadget, body_scroller)) {
        DisposeObject(dialog);
        FreeClass(compose_texteditor_class);
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_NEW_MAIL_WINDOW_COULD_NOT_BE_CREATED,
                        "New mail window could not be created."));
        cleanup_compose_attachments(attachments, attachment_count);
        free(initial_body_with_signature);
        return 0;
    }

    window = RA_OpenWindow(dialog);
    if (!window) {
        compose_disconnect_texteditor_scroller(body_gadget, body_scroller);
        DisposeObject(dialog);
        FreeClass(compose_texteditor_class);
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_NEW_MAIL_WINDOW_COULD_NOT_BE_OPENED, "New mail window could not be opened."));
        cleanup_compose_attachments(attachments, attachment_count);
        free(initial_body_with_signature);
        return 0;
    }
    /* Keep the compose window independent from the main window.  Both
     * window.class objects are serviced by the same task while compose is
     * open, so the message list, preview and network refresh remain live. */
    gui->compose_open = 1;
    gui->compose_window = window;
    WindowToFront(window);
    ActivateWindow(window);
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);
    if (attachment_count) {
        rebuild_attachment_list(attachments_gadget, window, &attachment_list,
                                attachments, attachment_count);
        update_compose_status(compose_status, window, attachments,
                              attachment_count);
    }
    compose_sync_texteditor_scroller(
        window, body_gadget, body_scroller, &body_scroll_state, 1);
    sync_listbrowser_scroller(window, attachments_gadget,
                              attachments_scroller);

    while (!done) {
        ULONG runtime_signal = gui_runtime_signal_mask(gui);
        ULONG signals = Wait(signal_mask | runtime_signal |
                             SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) done = 1;

        if (runtime_signal && (signals & runtime_signal)) {
            gui_runtime_process_signals(gui, signals & runtime_signal, error);
            if (!gui->running) done = 1;
        }

        if (!done && (signals & signal_mask)) {
            ULONG result;
            while ((result = RA_HandleInput(dialog, NULL)) != WMHI_LASTMSG) {
                switch (result & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        done = 1;
                        break;

                    case WMHI_RAWKEY:
                        if (rawkey_is_help(result)) {
                            about_dialog(gui);
                        } else if (rawkey_is_cancel(result)) {
                            done = 1;
                        }
                        break;

                    case WMHI_MENUPICK:
                    {
                        ULONG menu_code = result & 0xffffUL;
                        if (menu_code == MENU_EDIT_SELECT_ALL) {
                            /* Defer selection until all menu mouse events
                             * have been drained. Otherwise the mouse-up that
                             * closes the menu can immediately clear the mark. */
                            select_all_pending = 1;
                        } else if (menu_code == MENU_EDIT_COPY ||
                                   menu_code == MENU_EDIT_CUT ||
                                   menu_code == MENU_EDIT_PASTE) {
                            (void)compose_handle_edit_menu(
                                menu_code, window, body_gadget);
                        } else {
                            handle_menu(gui, menu_code, error);
                            if (!gui->running) done = 1;
                        }
                        break;
                    }

                    case WMHI_GADGETUP:
                        switch (result & WMHI_GADGETMASK) {
                            case GID_COMPOSE_CANCEL:
                                /* Keep all destructive/save Yes/No
                                 * requesters centered over the main AmiMail
                                 * window for a consistent UI position. */
                                if (confirm_question_dialog_for_window(
                                        gui, gui->window,
                                        T(MSG_DO_YOU_WANT_TO_SAVE_THE_DRAFT, "Do you want to save the draft?"),
                                        NULL, 310L)) {
                                    if (queue_composed_mail(
                                            gui, window, to_gadget, cc_gadget,
                                            bcc_gadget, subject_gadget,
                                            body_gadget, attachments,
                                            attachment_count, mode, draft_seed,
                                            1, error) == AMG_OK) {
                                        submitted = 1;
                                        done = 1;
                                    } else {
                                        set_utf8_string(compose_status, window,
                                                        error->message);
                                    }
                                } else {
                                    done = 1;
                                }
                                break;

                            case GID_COMPOSE_TO_CONTACTS:
                                gui_contacts_select_emails(
                                    gui, window, to_gadget, error);
                                break;

                            case GID_COMPOSE_CC_CONTACTS:
                                gui_contacts_select_emails(
                                    gui, window, cc_gadget, error);
                                break;

                            case GID_COMPOSE_BCC_CONTACTS:
                                gui_contacts_select_emails(
                                    gui, window, bcc_gadget, error);
                                break;

                            case GID_COMPOSE_ATTACHMENTS_SCROLL:
                                handle_listbrowser_scroller(
                                    window, attachments_gadget,
                                    attachments_scroller);
                                break;

                            case GID_COMPOSE_ADD_ATTACHMENT:
                                add_attachment(
                                    window, attachments_gadget, compose_status,
                                    &attachment_list, attachments,
                                    &attachment_count);
                                break;

                            case GID_COMPOSE_REMOVE_ATTACHMENT:
                                remove_attachment(
                                    window, attachments_gadget, compose_status,
                                    &attachment_list, attachments,
                                    &attachment_count);
                                break;

                            case GID_COMPOSE_SEND:
                                if (queue_composed_mail(
                                        gui, window, to_gadget, cc_gadget,
                                        bcc_gadget, subject_gadget, body_gadget,
                                        attachments, attachment_count, mode,
                                        draft_seed, 0, error) == AMG_OK) {
                                    submitted = 1;
                                    sent_queued = 1;
                                    done = 1;
                                } else {
                                    set_utf8_string(compose_status, window,
                                                    error->message);
                                }
                                break;
                        }
                        break;
                }
            }
            if (!done) {
                if (select_all_pending) {
                    /* Match the reliable RAmiga+A path after the menu has
                     * fully closed: reactivate the editor, then mark it. */
                    ActivateGadget(body_gadget, window, NULL);
                    (void)compose_texteditor_mark_all(body_gadget, window);
                    select_all_pending = 0;
                }
                if (compose_input_state.scroller_sync_pending ||
                    body_scroll_state.editor_height != body_gadget->Height) {
                    compose_sync_texteditor_scroller(
                        window, body_gadget, body_scroller,
                        &body_scroll_state, 0);
                    compose_input_state.scroller_sync_pending = 0;
                }
                sync_listbrowser_scroller(
                    window, attachments_gadget, attachments_scroller);
            }
        }
    }
    compose_state_save(window);
    gui->compose_window = NULL;
    gui->compose_open = 0;
    compose_disconnect_texteditor_scroller(body_gadget, body_scroller);
    DisposeObject(dialog);
    FreeClass(compose_texteditor_class);
    FreeListBrowserList(&attachment_list);
    if (!submitted)
        cleanup_compose_attachments(attachments, attachment_count);
    free(initial_body_with_signature);
    if (sent_queued)
        status_local(gui, T(MSG_SENDING_MAIL, "Sending mail..."));
    else if (submitted)
        status_local(gui, T(MSG_SAVING_DRAFT, "Saving draft..."));
    return submitted;
}

#endif /* AMIGMAIL_AMIGA */
