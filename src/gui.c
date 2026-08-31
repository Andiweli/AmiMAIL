#include "gui_internal.h"
#include "banner_data.h"
#include "buffer.h"
#include "codec.h"
#include "imap.h"
#include "imap_parser.h"
#include "mime.h"
#include "network_task.h"
#include "storage.h"
#include "i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define T(id, en) amg_tr((id), (en))
#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <classes/window.h>
#include <dos/dos.h>
#include <devices/timer.h>
#include <devices/inputevent.h>
#include <datatypes/datatypes.h>
#include <exec/io.h>
#include <exec/libraries.h>
#include <exec/lists.h>
#include <exec/memory.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/scroller.h>
#include <gadgets/string.h>
#include <gadgets/texteditor.h>
#include <graphics/gfxbase.h>
#include <graphics/view.h>
#include <intuition/classes.h>
#include <intuition/classusr.h>
#include <intuition/intuition.h>
#include <intuition/icclass.h>
#include <libraries/asl.h>
#include <libraries/gadtools.h>
#include <proto/asl.h>
#include <proto/button.h>
#include <proto/datatypes.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/scroller.h>
#include <proto/string.h>
#include <proto/texteditor.h>
#include <proto/window.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <utility/hooks.h>
#include <utility/tagitem.h>
#ifdef NewObject
#undef NewObject
#endif
#ifdef ButtonObject
#undef ButtonObject
#endif
#define ButtonObject NewObject(NULL,(CONST_STRPTR)"button.gadget"
struct Library *WindowBase=NULL;
struct Library *LayoutBase=NULL;
struct Library *ButtonBase=NULL;
struct Library *ListBrowserBase=NULL;
struct Library *ScrollerBase=NULL;
struct Library *StringBase=NULL;
struct Library *TextEditorBase=NULL;
struct Library *OpenURLBase=NULL;
struct Library *AslBase=NULL;
struct Library *DataTypesBase=NULL;
struct Library *IconBase=NULL;
struct GfxBase *GfxBase=NULL;
#define GUI_RAWKEY_KEYPAD_ENTER 0x43UL
#define GUI_RAWKEY_RETURN 0x44UL
#define GUI_RAWKEY_ESCAPE 0x45UL
#define GUI_RAWKEY_A 0x20UL
#define GUI_RAWKEY_W 0x11UL
#define GUI_RAWKEY_DELETE 0x46UL
#define GUI_RAWKEY_HELP 0x5FUL
int rawkey_is_accept(ULONG result){ULONG key=result&WMHI_KEYMASK;return key==GUI_RAWKEY_KEYPAD_ENTER||key==GUI_RAWKEY_RETURN;}
int rawkey_is_cancel(ULONG result){return (result&WMHI_KEYMASK)==GUI_RAWKEY_ESCAPE;}

int rawkey_is_rcommand_letter(Object *window_object, ULONG result, char letter)
{
    ULONG input_event_value = 0UL;
    struct InputEvent *input_event;
    ULONG raw_key;

    if (!window_object || (result & WMHI_CLASSMASK) != WMHI_RAWKEY)
        return 0;

    switch (letter) {
        case 'A': case 'a': raw_key = GUI_RAWKEY_A; break;
        case 'W': case 'w': raw_key = GUI_RAWKEY_W; break;
        default: return 0;
    }
    if ((result & WMHI_KEYMASK) != raw_key)
        return 0;

    GetAttr(WINDOW_InputEvent, window_object, &input_event_value);
    input_event = (struct InputEvent *)(uintptr_t)input_event_value;
    return input_event &&
           (input_event->ie_Qualifier & IEQUALIFIER_RCOMMAND) != 0;
}

int rawkey_is_delete(ULONG result)
{
    return (result & WMHI_CLASSMASK) == WMHI_RAWKEY &&
           (result & WMHI_KEYMASK) == GUI_RAWKEY_DELETE;
}

int rawkey_is_help(ULONG result)
{
    return (result & WMHI_CLASSMASK) == WMHI_RAWKEY &&
           (result & WMHI_KEYMASK) == GUI_RAWKEY_HELP;
}

int input_event_has_multiselect_qualifier(Object *window_object)
{
    ULONG input_event_value = 0UL;
    struct InputEvent *input_event;
    UWORD qualifiers;

    if (!window_object) return 0;
    GetAttr(WINDOW_InputEvent, window_object, &input_event_value);
    input_event = (struct InputEvent *)(uintptr_t)input_event_value;
    if (!input_event) return 0;

    qualifiers = input_event->ie_Qualifier;
    return (qualifiers & (IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT |
                          IEQUALIFIER_CONTROL)) != 0;
}
Object *static_text_label(const char *text)
{
    return NewObject(NULL, (CONST_STRPTR)"button.gadget",
                     GA_ReadOnly, TRUE,
                     GA_Text, (ULONG)(uintptr_t)(text ? text : ""),
                     BUTTON_BevelStyle, BVS_NONE,
                     BUTTON_Transparent, TRUE,
                     BUTTON_Justification, BCJ_LEFT,
                     TAG_DONE);
}

static int open_classes(void)
{
    WindowBase = OpenLibrary((CONST_STRPTR)"window.class", 44);
    LayoutBase = OpenLibrary((CONST_STRPTR)"gadgets/layout.gadget", 44);
    ButtonBase = OpenLibrary((CONST_STRPTR)"gadgets/button.gadget", 44);
    ListBrowserBase =
        OpenLibrary((CONST_STRPTR)"gadgets/listbrowser.gadget", 44);
    ScrollerBase = OpenLibrary((CONST_STRPTR)"gadgets/scroller.gadget", 44);
    StringBase = OpenLibrary((CONST_STRPTR)"gadgets/string.gadget", 44);
    TextEditorBase =
        OpenLibrary((CONST_STRPTR)"gadgets/texteditor.gadget", 44);
    /* OpenURL ist optional: Ohne Library bleibt AmiMail voll
     * funktionsfaehig, lediglich das Oeffnen erkannter URLs entfaellt. */
    OpenURLBase = OpenLibrary((CONST_STRPTR)"openurl.library", 0);
    AslBase = OpenLibrary((CONST_STRPTR)"asl.library", 37);
    DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 44);
    IconBase = OpenLibrary((CONST_STRPTR)"icon.library", 39);
    GfxBase = (struct GfxBase *)
        OpenLibrary((CONST_STRPTR)"graphics.library", 39);
    return WindowBase && LayoutBase && ButtonBase && ListBrowserBase &&
           ScrollerBase && StringBase && TextEditorBase && AslBase && GfxBase;
}

static void close_classes(void)
{
    if (IconBase) CloseLibrary(IconBase);
    if (DataTypesBase) CloseLibrary(DataTypesBase);
    if (AslBase) CloseLibrary(AslBase);
    if (OpenURLBase) CloseLibrary(OpenURLBase);
    if (TextEditorBase) CloseLibrary(TextEditorBase);
    if (StringBase) CloseLibrary(StringBase);
    if (ScrollerBase) CloseLibrary(ScrollerBase);
    if (ListBrowserBase) CloseLibrary(ListBrowserBase);
    if (ButtonBase) CloseLibrary(ButtonBase);
    if (LayoutBase) CloseLibrary(LayoutBase);
    if (WindowBase) CloseLibrary(WindowBase);
    if (GfxBase) CloseLibrary((struct Library *)GfxBase);
    IconBase = NULL;
    DataTypesBase = NULL;
    AslBase = NULL;
    OpenURLBase = NULL;
    TextEditorBase = NULL;
    GfxBase = NULL;
    StringBase = NULL;
    ScrollerBase = NULL;
    ListBrowserBase = NULL;
    ButtonBase = NULL;
    LayoutBase = NULL;
    WindowBase = NULL;
}

 struct Node *one_column_node(const char *text, ULONG user_data)
{
    return AllocListBrowserNode(
        1,
        LBNA_UserData, user_data,
        LBNA_Column, 0,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)text,
        LBNCA_VertJustify, LRJ_BOTTOM,
        TAG_DONE);
}

 const char *string_text(struct Gadget *gadget)
{
    ULONG value = 0;
    if (gadget) GetAttr(STRINGA_TextVal, (Object *)gadget, &value);
    return value ? (const char *)(uintptr_t)value : "";
}

void set_string(struct Gadget *gadget, struct Window *window,
                const char *text)
{
    if (!gadget) return;
    if (window)
        SetGadgetAttrs(gadget, window, NULL,
                       STRINGA_TextVal, (ULONG)(uintptr_t)(text ? text : ""),
                       TAG_DONE);
    else
        SetAttrs((Object *)gadget,
                 STRINGA_TextVal, (ULONG)(uintptr_t)(text ? text : ""),
                 TAG_DONE);
}

 void status_local(AmgGui *gui, const char *text)
{
    if (gui && gui->status_gadget)
        set_string(gui->status_gadget, gui->window, text);
}

 void set_utf8_string(struct Gadget *gadget, struct Window *window,
                            const char *utf8)
{
    AmgBuffer local;
    amg_buffer_init(&local);
    if (utf8 && amg_utf8_to_local(utf8, &local) == AMG_OK &&
        amg_buffer_terminate(&local) == AMG_OK)
        set_string(gadget, window, (const char *)local.data);
    else
        set_string(gadget, window, T(MSG_ERROR, "Error"));
    amg_buffer_free(&local);
}

 void status_utf8(AmgGui *gui, const char *utf8)
{
    if (gui) set_utf8_string(gui->status_gadget, gui->window, utf8);
}

 int local_to_utf8(const char *local, char *utf8, size_t capacity)
{
    const unsigned char *source = (const unsigned char *)(local ? local : "");
    size_t used = 0;
    while (*source) {
        if (*source < 0x80U) {
            if (used + 1U >= capacity) return AMG_ERR_LIMIT;
            utf8[used++] = (char)*source;
        } else {
            if (used + 2U >= capacity) return AMG_ERR_LIMIT;
            utf8[used++] = (char)(0xC0U | (*source >> 6));
            utf8[used++] = (char)(0x80U | (*source & 0x3FU));
        }
        ++source;
    }
    utf8[used] = 0;
    return AMG_OK;
}

 void utf8_to_local_copy(const char *utf8, char *local, size_t capacity)
{
    AmgBuffer converted;
    size_t length;
    if (!local || !capacity) return;
    local[0] = 0;
    amg_buffer_init(&converted);
    if (!utf8 || amg_utf8_to_local(utf8, &converted) != AMG_OK ||
        amg_buffer_terminate(&converted) != AMG_OK) {
        amg_buffer_free(&converted);
        return;
    }
    length = converted.length;
    if (length >= capacity) length = capacity - 1U;
    memcpy(local, converted.data, length);
    local[length] = 0;
    amg_buffer_free(&converted);
}

 void header_to_local(const char *header, const char *fallback,
                            char *local, size_t capacity)
{
    AmgBuffer decoded;
    amg_buffer_init(&decoded);
    if (header && *header && amg_rfc2047_decode(header, &decoded) == AMG_OK &&
        amg_buffer_terminate(&decoded) == AMG_OK)
        utf8_to_local_copy((const char *)decoded.data, local, capacity);
    else
        utf8_to_local_copy(fallback ? fallback : "", local, capacity);
    amg_buffer_free(&decoded);
}

 void detach_listbrowser(struct Gadget *gadget, struct Window *window)
{
    if (!gadget) return;
    if (window)
        SetGadgetAttrs(gadget, window, NULL,
                       LISTBROWSER_Labels, (ULONG)~0UL,
                       TAG_DONE);
    else
        SetAttrs((Object *)gadget,
                 LISTBROWSER_Labels, (ULONG)~0UL,
                 TAG_DONE);
}

 void attach_listbrowser(struct Gadget *gadget, struct Window *window,
                               struct List *list)
{
    if (!gadget) return;
    if (window)
        SetGadgetAttrs(gadget, window, NULL,
                       LISTBROWSER_Labels, (ULONG)(uintptr_t)list,
                       LISTBROWSER_Selected, (ULONG)~0UL,
                       LISTBROWSER_Top, 0,
                       TAG_DONE);
    else
        SetAttrs((Object *)gadget,
                 LISTBROWSER_Labels, (ULONG)(uintptr_t)list,
                 LISTBROWSER_Selected, (ULONG)~0UL,
                 LISTBROWSER_Top, 0,
                 TAG_DONE);
}

 struct Node *find_node_by_user_data(struct List *list,
                                           ULONG user_data)
{
    struct Node *node;
    if (!list) return NULL;
    node = list->lh_Head;
    while (node && node->ln_Succ) {
        ULONG value = 0;
        GetListBrowserNodeAttrs(
            node, LBNA_UserData, (ULONG)(uintptr_t)&value, TAG_DONE);
        if (value == user_data) return node;
        node = node->ln_Succ;
    }
    return NULL;
}

 struct Gadget *create_vertical_scroller(ULONG gadget_id)
{
    return (struct Gadget *)NewObject(
        SCROLLER_GetClass(), NULL,
        GA_ID, gadget_id,
        GA_RelVerify, TRUE,
        SCROLLER_Orientation, SORIENT_VERT,
        SCROLLER_Arrows, TRUE,
        SCROLLER_ArrowDelta, 1,
        SCROLLER_Top, 0,
        SCROLLER_Total, 1,
        SCROLLER_Visible, 1,
        TAG_DONE);
}


/* texteditor.gadget exposes GA_TEXTEDITOR_Prop_First/Entries/Visible
 * specifically for connecting a scrollbar. Use BOOPSI modelclass/icclass
 * notifications so the editor and ReAction scroller stay synchronized
 * while the user scrolls, rather than only after GADGETUP. */
static struct TagItem texteditor_to_scroller_map[] = {
    { GA_TEXTEDITOR_Prop_First, SCROLLER_Top },
    { GA_TEXTEDITOR_Prop_Entries, SCROLLER_Total },
    { GA_TEXTEDITOR_Prop_Visible, SCROLLER_Visible },
    { TAG_DONE, 0 }
};

static struct TagItem scroller_to_texteditor_map[] = {
    { SCROLLER_Top, GA_TEXTEDITOR_Prop_First },
    { TAG_DONE, 0 }
};

void disconnect_texteditor_scroller(struct Gadget *editor,
                                    struct Gadget *scroller,
                                    TextEditorScrollLink *link)
{
    if (!link) return;

    if (editor)
        SetAttrs((Object *)editor, ICA_TARGET, 0UL, TAG_DONE);
    if (scroller)
        SetAttrs((Object *)scroller, ICA_TARGET, 0UL, TAG_DONE);

    if (link->model && link->editor_to_scroller)
        (void)DoMethod(link->model, OM_REMMEMBER,
                       link->editor_to_scroller);
    if (link->model && link->scroller_to_editor)
        (void)DoMethod(link->model, OM_REMMEMBER,
                       link->scroller_to_editor);

    if (link->editor_to_scroller)
        DisposeObject(link->editor_to_scroller);
    if (link->scroller_to_editor)
        DisposeObject(link->scroller_to_editor);
    if (link->model)
        DisposeObject(link->model);

    memset(link, 0, sizeof(*link));
}

int connect_texteditor_scroller(struct Gadget *editor,
                                struct Gadget *scroller,
                                TextEditorScrollLink *link)
{
    Object *model;
    Object *editor_to_scroller;
    Object *scroller_to_editor;

    if (!editor || !scroller || !link) return 0;
    memset(link, 0, sizeof(*link));

    model = NewObject(NULL, (CONST_STRPTR)MODELCLASS, TAG_DONE);
    if (!model) return 0;

    editor_to_scroller = NewObject(
        NULL, (CONST_STRPTR)ICCLASS,
        ICA_TARGET, (ULONG)(uintptr_t)scroller,
        ICA_MAP, (ULONG)(uintptr_t)texteditor_to_scroller_map,
        TAG_DONE);
    scroller_to_editor = NewObject(
        NULL, (CONST_STRPTR)ICCLASS,
        ICA_TARGET, (ULONG)(uintptr_t)editor,
        ICA_MAP, (ULONG)(uintptr_t)scroller_to_texteditor_map,
        TAG_DONE);
    if (!editor_to_scroller || !scroller_to_editor) {
        if (editor_to_scroller) DisposeObject(editor_to_scroller);
        if (scroller_to_editor) DisposeObject(scroller_to_editor);
        DisposeObject(model);
        return 0;
    }

    (void)DoMethod(model, OM_ADDMEMBER, editor_to_scroller);
    (void)DoMethod(model, OM_ADDMEMBER, scroller_to_editor);

    SetAttrs((Object *)editor,
             ICA_TARGET, (ULONG)(uintptr_t)model,
             TAG_DONE);
    SetAttrs((Object *)scroller,
             ICA_TARGET, (ULONG)(uintptr_t)model,
             TAG_DONE);

    link->model = model;
    link->editor_to_scroller = editor_to_scroller;
    link->scroller_to_editor = scroller_to_editor;
    return 1;
}

static ULONG estimate_listbrowser_visible_nodes(struct Window *window,
                                                struct Gadget *listbrowser)
{
    ULONG row_height = 10U;
    ULONG visible;
    LONG height;
    if (!listbrowser) return 1U;
    if (window && window->RPort && window->RPort->TxHeight > 0)
        row_height = (ULONG)window->RPort->TxHeight + 4U;
    height = (LONG)listbrowser->Height - 6L;
    if (height <= 0L) return 1U;
    visible = (ULONG)height / row_height;
    return visible ? visible : 1U;
}

 void set_scroller_full(struct Window *window, struct Gadget *scroller)
{
    if (!window || !scroller) return;
    SetGadgetAttrs(scroller, window, NULL,
                   SCROLLER_Top, 0,
                   SCROLLER_Total, 1,
                   SCROLLER_Visible, 1,
                   TAG_DONE);
    RefreshGList(scroller, window, NULL, 1);
}

 void sync_listbrowser_scroller(struct Window *window,
                                      struct Gadget *listbrowser,
                                      struct Gadget *scroller)
{
    ULONG top = 0, total = 1, visible = 1;
    ULONG total_nodes = 0, estimated_visible;
    if (!window || !listbrowser || !scroller) return;

    estimated_visible = estimate_listbrowser_visible_nodes(window, listbrowser);
    GetAttr(LISTBROWSER_TotalVisibleNodes, (Object *)listbrowser, &total_nodes);

    /* Bei komplett sichtbarem Inhalt muss der Prop-Knopf die gesamte Bahn
     * ausfuellen. Insbesondere aeltere ReAction-Versionen liefern bei einem
     * externen Scroller und LISTBROWSER_VerticalProp=FALSE teils unbrauchbare
     * VProp-Verhaeltnisse, obwohl gar nichts zu scrollen ist. */
    if (total_nodes == 0U || total_nodes <= estimated_visible) {
        SetGadgetAttrs(listbrowser, window, NULL,
                       LISTBROWSER_VPropTop, 0,
                       TAG_DONE);
        set_scroller_full(window, scroller);
        return;
    }

    GetAttr(LISTBROWSER_VPropTop, (Object *)listbrowser, &top);
    GetAttr(LISTBROWSER_VPropTotal, (Object *)listbrowser, &total);
    GetAttr(LISTBROWSER_VPropVisible, (Object *)listbrowser, &visible);
    if (total < 1U) total = total_nodes ? total_nodes : 1U;
    if (visible < 1U) visible = 1U;
    if (visible > total) visible = total;
    if (top > total - visible) top = total - visible;
    SetGadgetAttrs(scroller, window, NULL,
                   SCROLLER_Top, top,
                   SCROLLER_Total, total,
                   SCROLLER_Visible, visible,
                   TAG_DONE);
    RefreshGList(scroller, window, NULL, 1);
}

 void handle_listbrowser_scroller(struct Window *window,
                                        struct Gadget *listbrowser,
                                        struct Gadget *scroller)
{
    ULONG top = 0;
    if (!window || !listbrowser || !scroller) return;
    GetAttr(SCROLLER_Top, (Object *)scroller, &top);
    SetGadgetAttrs(listbrowser, window, NULL,
                   LISTBROWSER_VPropTop, top,
                   TAG_DONE);
    RefreshGList(listbrowser, window, NULL, 1);
    sync_listbrowser_scroller(window, listbrowser, scroller);
}

/* Label-Scrollbar: Der hierarchische Ordnerbaum benutzt weiterhin einen
 * externen Scroller. Die genaue Uebersetzung zwischen LISTBROWSER_Top (Position
 * in der angehaengten Node-Liste) und der sichtbaren Zeilenposition erfolgt in
 * gui_folders.c. Das ist wichtig, weil eingeklappte Kinder in der Node-Liste
 * verbleiben und beide Koordinaten danach nicht mehr identisch sind. */

static ULONG estimate_texteditor_visible_lines(struct Window *window,
                                               struct Gadget *editor)
{
    ULONG line_height = 8;
    ULONG visible;
    LONG height;
    if (!editor) return 1;
    if (window && window->RPort && window->RPort->TxHeight > 0)
        line_height = (ULONG)window->RPort->TxHeight;
    height = (LONG)editor->Height;
    if (height > 6L) height -= 6L;
    if (height <= 0L) return 1;
    visible = (ULONG)height / line_height;
    return visible ? visible : 1;
}

 void sync_texteditor_scroller(struct Window *window,
                                     struct Gadget *editor,
                                     struct Gadget *scroller,
                                     ULONG fallback_entries,
                                     int reset_top)
{
    ULONG first = 0, entries = 1, visible = 1;
    ULONG estimated_visible;
    if (!window || !editor || !scroller) return;
    estimated_visible = estimate_texteditor_visible_lines(window, editor);
    GetAttr(GA_TEXTEDITOR_Prop_First, (Object *)editor, &first);
    GetAttr(GA_TEXTEDITOR_Prop_Entries, (Object *)editor, &entries);
    GetAttr(GA_TEXTEDITOR_Prop_Visible, (Object *)editor, &visible);
    if (fallback_entries > entries) entries = fallback_entries;
    if (entries < 1U) entries = 1U;
    if (estimated_visible < 1U) estimated_visible = 1U;

    /* Ist der komplette Text sichtbar, immer eine volle Scrollbar anzeigen.
     * Damit sehen leere/kurze Textfelder nicht wie sehr lange Dokumente aus. */
    if (entries <= estimated_visible ||
        (fallback_entries > 0U && fallback_entries <= estimated_visible)) {
        if (reset_top || first != 0U)
            SetGadgetAttrs(editor, window, NULL,
                           GA_TEXTEDITOR_Prop_First, 0,
                           TAG_DONE);
        set_scroller_full(window, scroller);
        return;
    }

    if (visible <= 1U && estimated_visible > 1U) visible = estimated_visible;
    if (visible < 1U) visible = 1U;
    if (visible > entries) visible = entries;
    if (reset_top) first = 0U;
    if (first > entries - visible) first = entries - visible;
    SetGadgetAttrs(scroller, window, NULL,
                   SCROLLER_Top, first,
                   SCROLLER_Total, entries,
                   SCROLLER_Visible, visible,
                   TAG_DONE);
    RefreshGList(scroller, window, NULL, 1);
}

 void handle_texteditor_scroller(struct Window *window,
                                       struct Gadget *editor,
                                       struct Gadget *scroller,
                                       ULONG fallback_entries)
{
    ULONG top = 0;
    if (!window || !editor || !scroller) return;
    GetAttr(SCROLLER_Top, (Object *)scroller, &top);
    SetGadgetAttrs(editor, window, NULL,
                   GA_TEXTEDITOR_Prop_First, top,
                   TAG_DONE);
    RefreshGList(editor, window, NULL, 1);
    sync_texteditor_scroller(window, editor, scroller,
                             fallback_entries, 0);
}

AmgGui *amg_gui_create(AmgAccount *account, AmgError *error)
{
    AmgGui *gui;
    if (!account) return NULL;
    if (!open_classes()) {
        close_classes();
        amg_error_set(
            error, AMG_ERR_UNSUPPORTED,
            T(MSG_REQUIRED_REACTION_CLASSES_ARE_MISSING_AMIMAIL_REQUIRES_AMIGAOS, "Required ReAction classes are missing. AmiMail requires AmigaOS 3.2."));
        return NULL;
    }
    gui = (AmgGui *)calloc(1, sizeof(*gui));
    if (!gui) {
        close_classes();
        return NULL;
    }
    gui->account = account;
    gui->notification_sound_signal_bit = -1;
    gui->preview_url_signal_bit = -1;
    gui_state_set_mail_status_active();
    gui_state_load_inbox_notification(gui);
    NewList(&gui->system_labels_list);
    NewList(&gui->labels_list);
    NewList(&gui->messages_list);
    default_labels(gui);
    default_messages(gui);
    gui->network = amg_network_create();
    if (!gui->network || create_window(gui, error) != AMG_OK) {
        amg_gui_destroy(gui);
        return NULL;
    }
    return gui;
}

void amg_gui_destroy(AmgGui *gui)
{
    size_t i;
    if (!gui) return;
    periodic_timer_cleanup(gui);
    gui_notify_cleanup(gui);
    free(gui->current_message_payload);
    gui->current_message_payload = NULL;
    disconnect_texteditor_scroller(gui->preview_gadget,
                                   gui->preview_scroller,
                                   &gui->preview_scroll_link);
    if (gui->window_object) DisposeObject(gui->window_object);
    gui->window_object = NULL;
    if (gui->icon_iconified) FreeDiskObject(gui->icon_iconified);
    gui->icon_iconified = NULL;
    if (gui->app_port) {
        struct Message *message;
        while ((message = GetMsg(gui->app_port)) != NULL)
            ReplyMsg(message);
        DeleteMsgPort(gui->app_port);
        gui->app_port = NULL;
    }
    dispose_label_tree_images(gui);
    if (gui->screen) {
        if (gui->update_pen_owned && gui->update_pen >= 0)
            ReleasePen(gui->screen->ViewPort.ColorMap, gui->update_pen);
        if (gui->unread_pen_owned && gui->unread_pen >= 0)
            ReleasePen(gui->screen->ViewPort.ColorMap, gui->unread_pen);
        for (i = 0; i < BANNER_COLOR_COUNT; ++i) {
            if (gui->banner_pen_owned[i] && gui->banner_pens[i] >= 0)
                ReleasePen(gui->screen->ViewPort.ColorMap,
                           gui->banner_pens[i]);
        }
        UnlockPubScreen(NULL, gui->screen);
        gui->screen = NULL;
    }
    if (gui->columns) FreeLBColumnInfo(gui->columns);
    FreeListBrowserList(&gui->system_labels_list);
    FreeListBrowserList(&gui->labels_list);
    FreeListBrowserList(&gui->messages_list);
    amg_network_destroy(gui->network);
    gui_state_set_mail_status_inactive();
    free(gui);
    close_classes();
}

#else
struct AmgGui { int unavailable; };
AmgGui *amg_gui_create(AmgAccount *account, AmgError *error){(void)account;amg_error_set(error,AMG_ERR_UNSUPPORTED,T(MSG_REACTION_IS_AVAILABLE_ONLY_IN_THE_AMIGAOS_BUILD, "ReAction is available only in the AmigaOS build."));return NULL;}
int amg_gui_run(AmgGui *gui, AmgMailtoServer *mailto_server,
                const char *startup_mailto, AmgError *error)
{
    (void)gui;
    (void)mailto_server;
    (void)startup_mailto;
    amg_error_set(error, AMG_ERR_UNSUPPORTED,
                  T(MSG_REACTION_IS_AVAILABLE_ONLY_IN_THE_AMIGAOS_BUILD, "ReAction is available only in the AmigaOS build."));
    return AMG_ERR_UNSUPPORTED;
}
void amg_gui_destroy(AmgGui *gui){free(gui);}
#endif
