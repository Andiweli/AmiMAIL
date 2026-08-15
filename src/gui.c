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
#define T(de,en) amg_tr((de),(en))
#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <classes/window.h>
#include <dos/dos.h>
#include <devices/timer.h>
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
#include <intuition/intuition.h>
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
int rawkey_is_accept(ULONG result){ULONG key=result&WMHI_KEYMASK;return key==GUI_RAWKEY_KEYPAD_ENTER||key==GUI_RAWKEY_RETURN;}
int rawkey_is_cancel(ULONG result){return (result&WMHI_KEYMASK)==GUI_RAWKEY_ESCAPE;}
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
        set_string(gadget, window, T("Fehler", "Error"));
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
            T("Erforderliche ReAction-Klassen fehlen. AmiMail ben\303\266tigt AmigaOS 3.2.", "Required ReAction classes are missing. AmiMail requires AmigaOS 3.2."));
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
AmgGui *amg_gui_create(AmgAccount *account, AmgError *error){(void)account;amg_error_set(error,AMG_ERR_UNSUPPORTED,T("ReAction ist nur im AmigaOS-Build verf\303\274gbar.", "ReAction is available only in the AmigaOS build."));return NULL;}
int amg_gui_run(AmgGui *gui, AmgMailtoServer *mailto_server,
                const char *startup_mailto, AmgError *error)
{
    (void)gui;
    (void)mailto_server;
    (void)startup_mailto;
    amg_error_set(error, AMG_ERR_UNSUPPORTED,
                  T("ReAction ist nur im AmigaOS-Build verf\303\274gbar.",
                    "ReAction is available only in the AmigaOS build."));
    return AMG_ERR_UNSUPPORTED;
}
void amg_gui_destroy(AmgGui *gui){free(gui);}
#endif
