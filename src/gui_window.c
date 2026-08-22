#include "gui_internal.h"
#include "banner_data.h"
#include "iconified_data.h"
#include "i18n.h"

#include <string.h>

#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <classes/window.h>
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
#include <libraries/gadtools.h>
#include <proto/button.h>
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
#include <workbench/workbench.h>

static int window_current_mailbox_is_drafts(const AmgGui *gui)
{
    return gui && gui->label_count > 3U && gui->labels[3U].available &&
        (!strcmp(gui->current_mailbox_utf8, gui->labels[3U].mailbox_utf8) ||
         !strcmp(gui->current_mailbox_utf8,
                 gui->labels[3U].server_mailbox_utf8));
}

static struct DiskObject *load_embedded_disk_object(
    const unsigned char *data, size_t size,
    const char *base_name, const char *info_name)
{
    struct DiskObject *icon = NULL;
    BPTR file;
    if (!IconBase || !data || !size || !base_name || !info_name) return NULL;

    /* icon.library has no public memory-source GetDiskObject() on classic
     * AmigaOS. Materialise the complete embedded .info briefly in T:, let
     * icon.library build the native DiskObject, then delete the file again. */
    file = Open((CONST_STRPTR)info_name, MODE_NEWFILE);
    if (file) {
        LONG written = Write(file, (APTR)data, (LONG)size);
        Close(file);
        if (written == (LONG)size)
            icon = GetDiskObject((CONST_STRPTR)base_name);
        DeleteFile((CONST_STRPTR)info_name);
    }
    return icon;
}

static void load_iconify_disk_object(AmgGui *gui)
{
    if (!gui) return;
    gui->icon_iconified = load_embedded_disk_object(
        amg_icon_iconified_info, amg_icon_iconified_info_size,
        "T:AmiMail-Iconified-Embedded",
        "T:AmiMail-Iconified-Embedded.info");

    /* Safety fallback for unusual systems where T: or icon.library rejects
     * the embedded icon. The installed program icon remains usable. */
    if (!gui->icon_iconified && IconBase)
        gui->icon_iconified = GetDiskObject((CONST_STRPTR)"PROGDIR:AmiMail");
}

/*
 * WindowObject/EndWindow spans several expressions.  Keep the same classic
 * GCC/ReAction NewObject handling as the previously tested gui.c.
 */
#ifdef NewObject
#undef NewObject
#endif

#ifdef ButtonObject
#undef ButtonObject
#endif
#define ButtonObject NewObject(NULL, (CONST_STRPTR)"button.gadget"

#define GUI_MESSAGE_FLAG_COLUMN_WIDTH 16
#define T(id, en) amg_tr((id), (en))

/* LAYOUT_FillPattern uses the classic two-row, 16-bit area pattern. */
static UWORD solid_fill_pattern[2] = {0xffffU, 0xffffU};

static struct NewMenu menus[] = {
    {NM_TITLE, (STRPTR)"File", NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Account settings...", (STRPTR)"E", 0, 0, NULL},
    {NM_ITEM, (STRPTR)"About AmiMail...", NULL, 0, 0, NULL},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Quit", (STRPTR)"Q", 0, 0, NULL},
    {NM_TITLE, (STRPTR)"Edit", NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Contact management", (STRPTR)"C", 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Signature", NULL, 0, 0, NULL},
    {NM_ITEM, NM_BARLABEL, NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Empty Trash...", NULL, 0, 0, NULL},
    {NM_ITEM, (STRPTR)"Empty Spam...", NULL, 0, 0, NULL},
    {NM_END, NULL, NULL, 0, 0, NULL}
};
static void localize_menus(void)
{
    menus[0].nm_Label=(STRPTR)T(MSG_FILE, "File");
    menus[1].nm_Label=(STRPTR)T(MSG_ACCOUNT_SETTINGS, "Account settings...");
    menus[2].nm_Label=(STRPTR)T(MSG_ABOUT_AMIMAIL_38B2, "About AmiMail...");
    menus[4].nm_Label=(STRPTR)T(MSG_QUIT, "Quit");
    menus[5].nm_Label=(STRPTR)T(MSG_EDIT_A8FE, "Edit");
    menus[6].nm_Label=(STRPTR)T(MSG_CONTACT_MANAGEMENT, "Contact management");
    menus[7].nm_Label=(STRPTR)T(MSG_SIGNATURE, "Signature");
    menus[9].nm_Label=(STRPTR)T(MSG_EMPTY_TRASH, "Empty Trash...");
    menus[10].nm_Label=(STRPTR)T(MSG_EMPTY_SPAM, "Empty Spam...");
}

static ULONG compact_text_render_subentry(struct Hook *hook,
                                          struct Node *node, APTR message)
{
    struct LBDrawMsg *draw = (struct LBDrawMsg *)message;
    struct RastPort *rp;
    ULONG text_value = 0UL, flags = 0UL, fg_pen = 0UL;
    const char *text;
    LONG text_x, text_y, width;
    UBYTE old_fg, old_mode;

    if (!hook || !node || !draw || draw->lbdm_MethodID != LB_DRAW)
        return LBCB_UNKNOWN;
    rp = draw->lbdm_RastPort;
    if (!rp) return LBCB_OK;

    GetListBrowserNodeAttrs(
        node,
        LBNA_Flags, (ULONG)(uintptr_t)&flags,
        LBNA_Column, 0,
        LBNCA_Text, (ULONG)(uintptr_t)&text_value,
        LBNCA_FGPen, (ULONG)(uintptr_t)&fg_pen,
        TAG_DONE);
    text = (const char *)(uintptr_t)text_value;
    if (!text) text = "";

    old_fg = rp->FgPen;
    old_mode = rp->DrawMode;
    SetDrMd(rp, JAM1);
    if (draw->lbdm_DrawInfo) {
        if (draw->lbdm_State == LBR_SELECTED)
            SetAPen(rp, draw->lbdm_DrawInfo->dri_Pens[FILLTEXTPEN]);
        else if (flags & LBFLG_CUSTOMPENS)
            SetAPen(rp, fg_pen);
        else
            SetAPen(rp, draw->lbdm_DrawInfo->dri_Pens[TEXTPEN]);
    }

    text_x = draw->lbdm_Bounds.MinX;
    text_y = draw->lbdm_Bounds.MinY +
        ((draw->lbdm_Bounds.MaxY - draw->lbdm_Bounds.MinY + 1L -
          (LONG)rp->TxHeight) / 2L) + (LONG)rp->TxBaseline + 1L;

    /* h_Data != NULL is used for the narrow flag column, which is centred. */
    if (hook->h_Data) {
        width = TextLength(rp, (CONST_STRPTR)text, (ULONG)strlen(text));
        text_x += ((draw->lbdm_Bounds.MaxX - draw->lbdm_Bounds.MinX + 1L) -
                   width) / 2L;
    }
    Move(rp, text_x, text_y);
    Text(rp, (CONST_STRPTR)text, (ULONG)strlen(text));

    SetAPen(rp, old_fg);
    SetDrMd(rp, old_mode);
    return LBCB_OK;
}

static void init_compact_list_render_hooks(AmgGui *gui)
{
    if (!gui) return;
    gui->list_row_hook_height = gui->screen && gui->screen->RastPort.TxHeight
        ? (UWORD)(gui->screen->RastPort.TxHeight + 2U) : 10U;

    memset(&gui->system_label_render_hook, 0,
           sizeof(gui->system_label_render_hook));
    gui->system_label_render_hook.h_Entry = (__typeof__(gui->system_label_render_hook.h_Entry))HookEntry;
    gui->system_label_render_hook.h_SubEntry =
        (__typeof__(gui->system_label_render_hook.h_SubEntry))compact_text_render_subentry;
    gui->system_label_render_hook.h_Data = NULL;

    memset(&gui->message_flag_render_hook, 0,
           sizeof(gui->message_flag_render_hook));
    gui->message_flag_render_hook.h_Entry = (__typeof__(gui->message_flag_render_hook.h_Entry))HookEntry;
    gui->message_flag_render_hook.h_SubEntry =
        (__typeof__(gui->message_flag_render_hook.h_SubEntry))compact_text_render_subentry;
    gui->message_flag_render_hook.h_Data = gui; /* centre flag column */
}


static unsigned short banner_be16(const unsigned char *data)
{
    return (unsigned short)(((unsigned)data[0] << 8) | data[1]);
}

static unsigned long banner_be32(const unsigned char *data)
{
    return ((unsigned long)data[0] << 24) |
           ((unsigned long)data[1] << 16) |
           ((unsigned long)data[2] << 8) | data[3];
}

static const unsigned char *banner_chunk(const char id[4], size_t *length)
{
    size_t position = 12U;
    if (amg_banner_iff_size < position ||
        memcmp(amg_banner_iff, "FORM", 4U) ||
        memcmp(amg_banner_iff + 8U, "ILBM", 4U))
        return NULL;
    while (position + 8U <= amg_banner_iff_size) {
        size_t chunk_length =
            (size_t)banner_be32(amg_banner_iff + position + 4U);
        size_t data_position = position + 8U;
        if (chunk_length > amg_banner_iff_size - data_position) return NULL;
        if (!memcmp(amg_banner_iff + position, id, 4U)) {
            *length = chunk_length;
            return amg_banner_iff + data_position;
        }
        position = data_position + chunk_length + (chunk_length & 1U);
    }
    return NULL;
}

static void prepare_banner_pens(AmgGui *gui)
{
    const unsigned char *palette;
    size_t palette_length;
    size_t i;
    palette = banner_chunk("CMAP", &palette_length);
    for (i = 0; i < BANNER_COLOR_COUNT; ++i) {
        unsigned char red = 0x88U, green = 0x88U, blue = 0x88U;
        LONG pen;
        if (palette && i * 3U + 2U < palette_length) {
            red = palette[i * 3U];
            green = palette[i * 3U + 1U];
            blue = palette[i * 3U + 2U];
        }
        pen = ObtainBestPenA(
            gui->screen->ViewPort.ColorMap,
            (ULONG)red * 0x01010101UL,
            (ULONG)green * 0x01010101UL,
            (ULONG)blue * 0x01010101UL, NULL);
        if (pen >= 0) {
            gui->banner_pens[i] = pen;
            gui->banner_pen_owned[i] = 1U;
        } else {
            pen = FindColor(
                gui->screen->ViewPort.ColorMap,
                (ULONG)red * 0x01010101UL,
                (ULONG)green * 0x01010101UL,
                (ULONG)blue * 0x01010101UL, -1L);
            gui->banner_pens[i] =
                pen >= 0 ? pen : (LONG)gui->screen->BlockPen;
        }
    }
}

static void prepare_unread_pen(AmgGui *gui)
{
    LONG pen;
    if (!gui || !gui->screen) return;
    pen = ObtainBestPenA(
        gui->screen->ViewPort.ColorMap,
        0x00000000UL, 0x33333333UL, 0xffffffffUL, NULL);
    if (pen >= 0) {
        gui->unread_pen = pen;
        gui->unread_pen_owned = 1U;
        return;
    }
    pen = FindColor(gui->screen->ViewPort.ColorMap,
                    0x00000000UL, 0x33333333UL, 0xffffffffUL, -1L);
    gui->unread_pen = pen >= 0 ? pen : (LONG)gui->screen->DetailPen;
}

static void prepare_text_pen(AmgGui *gui)
{
    struct DrawInfo *draw_info;
    if (!gui || !gui->screen) return;
    gui->text_pen = (LONG)gui->screen->DetailPen;
    draw_info = GetScreenDrawInfo(gui->screen);
    if (draw_info) {
        gui->text_pen = (LONG)draw_info->dri_Pens[TEXTPEN];
        FreeScreenDrawInfo(gui->screen, draw_info);
    }
}

static void prepare_update_pen(AmgGui *gui)
{
    LONG pen;
    if (!gui || !gui->screen) return;
    /* Deep blue (#003366) stays readable on the banner's #888888 grey
     * without the visual harshness of the previous full-intensity red. */
    pen = ObtainBestPenA(gui->screen->ViewPort.ColorMap,
                         0x00000000UL, 0x33333333UL, 0x66666666UL, NULL);
    if (pen >= 0) {
        gui->update_pen = pen;
        gui->update_pen_owned = 1U;
        return;
    }
    pen = FindColor(gui->screen->ViewPort.ColorMap,
                    0x00000000UL, 0x33333333UL, 0x66666666UL, -1L);
    gui->update_pen = pen >= 0 ? pen : (LONG)gui->screen->DetailPen;
}

void center_window_on_screen(struct Window *window)
{
    LONG left, top;
    if (!window || !window->WScreen) return;
    left = ((LONG)window->WScreen->Width - (LONG)window->Width) / 2L;
    top = ((LONG)window->WScreen->Height - (LONG)window->Height) / 2L;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    MoveWindow(window, left - window->LeftEdge, top - window->TopEdge);
}

void draw_embedded_banner_at(AmgGui *gui, struct Window *window,
                                    LONG left, LONG top,
                                    LONG available_width, LONG available_height)
{
    const unsigned char *header, *body;
    size_t header_length, body_length;
    unsigned width, height, planes, row_bytes, draw_width, draw_height;
    unsigned x, y;
    struct RastPort *rastport;

    if (!gui || !window || available_width <= 0L || available_height <= 0L)
        return;
    rastport = window->RPort;
    if (!rastport) return;

    header = banner_chunk("BMHD", &header_length);
    body = banner_chunk("BODY", &body_length);
    if (!header || header_length < 20U || !body) return;
    width = banner_be16(header);
    height = banner_be16(header + 2U);
    planes = header[8U];
    if (!width || !height || planes != 3U || header[10U] != 0U) return;
    row_bytes = ((width + 15U) / 16U) * 2U;
    if ((size_t)row_bytes * planes * height > body_length) return;

    draw_width = width;
    if ((LONG)draw_width > available_width)
        draw_width = (unsigned)available_width;
    draw_height = height;
    if ((LONG)draw_height > available_height)
        draw_height = (unsigned)available_height;

    for (y = 0; y < draw_height; ++y) {
        const unsigned char *row = body + (size_t)y * row_bytes * planes;
        x = 0;
        while (x < draw_width) {
            unsigned run_start = x;
            unsigned color = 0;
            unsigned next_color;
            unsigned plane;
            for (plane = 0; plane < planes; ++plane)
                color |= ((row[plane * row_bytes + (x >> 3)] >>
                           (7U - (x & 7U))) & 1U) << plane;
            do {
                ++x;
                if (x >= draw_width) break;
                next_color = 0;
                for (plane = 0; plane < planes; ++plane)
                    next_color |= ((row[plane * row_bytes + (x >> 3)] >>
                                    (7U - (x & 7U))) & 1U) << plane;
            } while (next_color == color);
            SetAPen(rastport, (ULONG)gui->banner_pens[color]);
            RectFill(rastport, left + (LONG)run_start, top + (LONG)y,
                     left + (LONG)x - 1L, top + (LONG)y);
        }
    }
}

static void draw_banner(AmgGui *gui)
{
    LONG left, logo_left, top, right, bottom;
    struct RastPort *rastport;
    if (!gui || !gui->window) return;
    rastport = gui->window->RPort;
    if (!rastport) return;
    left = gui->window->BorderLeft;
    top = gui->window->BorderTop;
    right = (LONG)gui->window->Width - gui->window->BorderRight - 1L;
    bottom = top + 27L;
    if (right < left ||
        bottom >= (LONG)gui->window->Height - gui->window->BorderBottom)
        return;
    SetAPen(rastport, (ULONG)gui->banner_pens[2]);
    RectFill(rastport, left, top, right, bottom);

    /* Match the logo edge to the first toolbar button after layout. */
    logo_left = left;
    if (gui->new_mail_gadget) {
        LONG gadget_left = (LONG)gui->new_mail_gadget->LeftEdge;
        if (gadget_left >= left && gadget_left <= right)
            logo_left = gadget_left;
    }

    draw_embedded_banner_at(gui, gui->window, logo_left, top,
                            right - logo_left + 1L, 28L);
}

static void draw_version_text(AmgGui *gui)
{
    static const char version_text[] = "Version " AMIGMAIL_VERSION;
    struct RastPort *rp;
    struct TextFont *old_font;
    LONG header_top, status_top, right, text_width, text_x, text_y;
    LONG available_height;
    UBYTE old_fg, old_mode;

    if (!gui || !gui->window || !gui->update_gadget || !gui->screen) return;
    rp = gui->window->RPort;
    if (!rp || !gui->screen->RastPort.Font) return;

    header_top = gui->window->BorderTop;
    status_top = (LONG)gui->update_gadget->TopEdge;
    right = (LONG)gui->update_gadget->LeftEdge +
            (LONG)gui->update_gadget->Width - 1L;
    available_height = status_top - header_top;
    if (available_height <= 0L) return;

    old_font = rp->Font;
    SetFont(rp, gui->screen->RastPort.Font);
    text_width = TextLength(rp, (CONST_STRPTR)version_text,
                            (ULONG)(sizeof(version_text) - 1U));
    text_x = right - text_width + 1L;
    text_y = header_top +
             (available_height - (LONG)rp->TxHeight) / 2L +
             (LONG)rp->TxBaseline;

    old_fg = rp->FgPen;
    old_mode = rp->DrawMode;
    SetDrMd(rp, JAM1);
    SetAPen(rp, (ULONG)(gui->text_pen >= 0 ? gui->text_pen : old_fg));
    Move(rp, text_x, text_y);
    Text(rp, (CONST_STRPTR)version_text,
         (ULONG)(sizeof(version_text) - 1U));
    SetAPen(rp, old_fg);
    SetDrMd(rp, old_mode);
    if (old_font) SetFont(rp, old_font);
}

static void draw_message_flag_header(AmgGui *gui)
{
    struct RastPort *rp;
    struct TextFont *old_font;
    LONG gadget_left, gadget_top;
    LONG title_height, text_width, text_x, text_y;
    UBYTE old_fg, old_mode;
    static const char marker[] = "!";

    if (!gui || !gui->window || !gui->messages_gadget || !gui->screen) return;
    rp = gui->window->RPort;
    if (!rp || !gui->screen->RastPort.Font) return;

    gadget_left = (LONG)gui->messages_gadget->LeftEdge;
    gadget_top = (LONG)gui->messages_gadget->TopEdge;

    /* listbrowser.gadget uses the public screen font by default.  Temporarily
     * use that exact font here too, so the exclamation mark keeps precisely
     * the same normal glyph weight as the other native column titles.  Only
     * its x coordinate uses the native visual center of the first column. */
    old_font = rp->Font;
    SetFont(rp, gui->screen->RastPort.Font);

    title_height = (LONG)rp->TxHeight + 2L;
    text_width = TextLength(rp, (CONST_STRPTR)marker, 1UL);
    text_x = gadget_left + 2L +
        ((LONG)GUI_MESSAGE_FLAG_COLUMN_WIDTH - text_width) / 2L;
    text_y = gadget_top + 2L +
        (title_height - (LONG)rp->TxHeight) / 2L + (LONG)rp->TxBaseline;

    old_fg = rp->FgPen;
    old_mode = rp->DrawMode;
    SetDrMd(rp, JAM1);
    SetAPen(rp, (ULONG)(gui->text_pen >= 0 ? gui->text_pen : old_fg));
    Move(rp, text_x, text_y);
    Text(rp, (CONST_STRPTR)marker, 1UL);
    SetAPen(rp, old_fg);
    SetDrMd(rp, old_mode);
    if (old_font) SetFont(rp, old_font);
}

void draw_window_overlays(AmgGui *gui)
{
    draw_banner(gui);
    gui_update_refresh_gadget(gui);
    draw_version_text(gui);
    sync_labels_scroller(gui);
    sync_messages_scroller(gui);
    sync_preview_scroller(gui, 0);
    draw_message_flag_header(gui);
}

int create_window(AmgGui *gui, AmgError *error)
{
    Object *banner_row;

    if (!gui->app_port) {
        gui->app_port = CreateMsgPort();
        if (!gui->app_port) {
            amg_error_set(error, AMG_ERR_MEMORY,
                          T(MSG_WORKBENCH_APPPORT_COULD_NOT_BE_CREATED, "Workbench AppPort could not be created."));
            return AMG_ERR_MEMORY;
        }
    }

    gui->screen = LockPubScreen(NULL);
    if (!gui->screen) {
        amg_error_set(error, AMG_ERR_IO,
                      T(MSG_WORKBENCH_SCREEN_COULD_NOT_BE_LOCKED, "Workbench screen could not be locked."));
        return AMG_ERR_IO;
    }
    gui_state_prepare_window(gui);
    prepare_banner_pens(gui);
    prepare_unread_pen(gui);
    prepare_text_pen(gui);
    prepare_update_pen(gui);
    init_preview_url_hook(gui);
    init_label_tree_render_hook(gui);
    init_compact_list_render_hooks(gui);
    /* default_labels() wird vor dem LockPubScreen() aufgebaut. Jetzt sind
     * Font und RenderHook bekannt, daher erzeugen wir nur die Label-Nodes
     * erneut; die Hierarchie- und Persistenzdaten selbst bleiben erhalten. */
    rebuild_label_lists(gui);
    if (!create_label_tree_images(gui)) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_LABEL_SYMBOLS_COULD_NOT_BE_CREATED, "Label symbols could not be created."));
        return AMG_ERR_MEMORY;
    }

    banner_row = HGroupObject,
        LAYOUT_SpaceOuter, FALSE,
        LAYOUT_SpaceInner, FALSE,
        LAYOUT_AddChild, HGroupObject,
            LAYOUT_SpaceOuter, FALSE,
            LAYOUT_SpaceInner, FALSE,
        EndObject,
        LAYOUT_AddChild, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, FALSE,
            /* Reserve a small first line for the version text.  The text
             * itself is drawn as a JAM1 overlay in draw_version_text(), so
             * no gadget background can cover the header artwork. */
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceOuter, FALSE,
                LAYOUT_SpaceInner, FALSE,
            EndObject,
            CHILD_MinHeight, 9,
            CHILD_MaxHeight, 9,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                gui->update_gadget = (struct Gadget *)ButtonObject,
                GA_ID, GID_UPDATE,
                GA_RelVerify, TRUE,
                GA_ReadOnly, TRUE,
                GA_Text, T(MSG_UP_TO_DATE, "Up to date"),
                BUTTON_DomainString, T(MSG_UP_TO_DATE, "Up to date"),
                BUTTON_BevelStyle, BVS_THIN,
                BUTTON_Transparent, FALSE,
                BUTTON_Justification, BCJ_CENTER,
                BUTTON_TextPen, gui->text_pen,
                BUTTON_BackgroundPen, gui->banner_pens[2],
                BUTTON_FillTextPen, gui->text_pen,
                BUTTON_FillPen, gui->banner_pens[2],
            EndObject,
            CHILD_WeightedHeight, 0,
        EndObject,
        CHILD_WeightedWidth, 0,
    EndObject;
    if (!banner_row) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_BANNER_ROW_COULD_NOT_BE_CREATED, "Banner row could not be created."));
        return AMG_ERR_MEMORY;
    }

    gui->columns = AllocLBColumnInfo(
        5,
        LBCIA_Column, 0,
        LBCIA_Title, (ULONG)(uintptr_t)"",
        LBCIA_Width, GUI_MESSAGE_FLAG_COLUMN_WIDTH,
        LBCIA_Flags, CIF_FIXED | CIF_CENTER,
        LBCIA_Column, 1,
        LBCIA_Title, (ULONG)(uintptr_t)T(MSG_SENDER_F4D4, "Sender"),
        LBCIA_Weight, 27,
        LBCIA_AutoSort, TRUE,
        LBCIA_SortArrow, TRUE,
        LBCIA_DraggableSeparator, TRUE,
        LBCIA_Column, 2,
        LBCIA_Title, (ULONG)(uintptr_t)T(MSG_SUBJECT_5C24, "Subject"),
        LBCIA_Weight, 37,
        LBCIA_AutoSort, TRUE,
        LBCIA_SortArrow, TRUE,
        LBCIA_DraggableSeparator, TRUE,
        LBCIA_Column, 3,
        LBCIA_Title, (ULONG)(uintptr_t)T(MSG_DATE_B264, "Date"),
        LBCIA_Weight, 23,
        LBCIA_AutoSort, TRUE,
        LBCIA_SortArrow, TRUE,
        LBCIA_SortDirection, LBMSORT_REVERSE,
        LBCIA_DraggableSeparator, TRUE,
        LBCIA_Column, 4,
        LBCIA_Title, (ULONG)(uintptr_t)T(MSG_SIZE, "Size"),
        LBCIA_Weight, 13,
        LBCIA_AutoSort, TRUE,
        LBCIA_SortArrow, TRUE,
        LBCIA_DraggableSeparator, TRUE,
        TAG_DONE);
    if (!gui->columns) {
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_COLUMNS_COULD_NOT_BE_CREATED, "Columns could not be created."));
        return AMG_ERR_MEMORY;
    }

    gui->labels_scroller = create_vertical_scroller(GID_LABELS_SCROLL);
    gui->messages_scroller = create_vertical_scroller(GID_MESSAGES_SCROLL);
    gui->preview_scroller = create_vertical_scroller(GID_PREVIEW_SCROLL);
    if (!gui->labels_scroller || !gui->messages_scroller ||
        !gui->preview_scroller) {
        if (gui->preview_scroller) DisposeObject((Object *)gui->preview_scroller);
        if (gui->messages_scroller) DisposeObject((Object *)gui->messages_scroller);
        if (gui->labels_scroller) DisposeObject((Object *)gui->labels_scroller);
        gui->preview_scroller = NULL;
        gui->messages_scroller = NULL;
        gui->labels_scroller = NULL;
        FreeLBColumnInfo(gui->columns);
        gui->columns = NULL;
        amg_error_set(error, AMG_ERR_MEMORY,
                      T(MSG_SCROLLBAR_COULD_NOT_BE_CREATED, "Scrollbar could not be created."));
        return AMG_ERR_MEMORY;
    }

    load_iconify_disk_object(gui);

    localize_menus();

    gui->window_object = WindowObject,
        WA_Title, "AmiMail",
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                      WFLG_SIZEGADGET | WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_MENUPICK |
                      IDCMP_RAWKEY | IDCMP_NEWSIZE | IDCMP_REFRESHWINDOW,
        gui->window_state_valid ? WA_Left : TAG_IGNORE,
            (ULONG)gui->saved_window_left,
        gui->window_state_valid ? WA_Top : TAG_IGNORE,
            (ULONG)gui->saved_window_top,
        WA_InnerWidth, gui->window_state_valid
            ? (ULONG)gui->saved_window_width
            : (ULONG)GUI_MAIN_DEFAULT_WIDTH,
        WA_InnerHeight, gui->window_state_valid
            ? (ULONG)gui->saved_window_height
            : (ULONG)GUI_MAIN_DEFAULT_HEIGHT,
        WA_MinWidth, GUI_MAIN_MIN_WIDTH,
        WA_MinHeight, GUI_MAIN_MIN_HEIGHT,
        WA_PubScreen, gui->screen,
        WINDOW_AppPort, gui->app_port,
        WINDOW_IconTitle, "AmiMail",
        gui->icon_iconified ? WINDOW_Icon : TAG_IGNORE,
            (ULONG)(uintptr_t)gui->icon_iconified,
        WINDOW_IconNoDispose, TRUE,
        WINDOW_IconifyGadget, TRUE,
        gui->window_state_valid ? TAG_IGNORE : WINDOW_Position,
            WPOS_CENTERSCREEN,
        WINDOW_NewMenu, menus,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, FALSE,
            LAYOUT_SpaceInner, FALSE,
            LAYOUT_FillPen, gui->banner_pens[2],
            LAYOUT_FillPattern, (ULONG)(uintptr_t)solid_fill_pattern,

            LAYOUT_AddChild, banner_row,
            CHILD_MinHeight, 28,
            CHILD_MaxHeight, 28,
            CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, VGroupObject,
                LAYOUT_SpaceOuter, TRUE,
                LAYOUT_SpaceInner, TRUE,

                LAYOUT_AddChild, HGroupObject,
                    /* Keep the six main action buttons equally weighted.
                     * Spacing is explicit so the arrow can sit directly
                     * against Reply without changing the other gaps. */
                    LAYOUT_EvenSize, FALSE,
                    LAYOUT_SpaceInner, FALSE,
                    LAYOUT_AddChild,
                        gui->new_mail_gadget = (struct Gadget *)ButtonObject,
                        GA_ID, GID_NEW_MAIL,
                        GA_RelVerify, TRUE,
                        GA_Text, T(MSG_NEW_MAIL, "_New mail"),
                    EndObject,
                    CHILD_MinWidth, 92,
                    CHILD_WeightedWidth, 100,
                    LAYOUT_AddChild, HGroupObject, EndObject,
                    CHILD_MinWidth, 4,
                    CHILD_MaxWidth, 4,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, ButtonObject,
                        GA_ID, GID_FETCH,
                        GA_RelVerify, TRUE,
                        GA_Text, T(MSG_FETCH, "_Fetch"),
                    EndObject,
                    CHILD_MinWidth, 92,
                    CHILD_WeightedWidth, 100,
                    LAYOUT_AddChild, HGroupObject, EndObject,
                    CHILD_MinWidth, 4,
                    CHILD_MaxWidth, 4,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild,
                        gui->reply_gadget = (struct Gadget *)ButtonObject,
                        GA_ID, GID_REPLY,
                        GA_RelVerify, TRUE,
                        GA_Text, window_current_mailbox_is_drafts(gui)
                            ? T(MSG_EDIT, "_Edit")
                            : T(MSG_REPLY, "_Reply"),
                    EndObject,
                    CHILD_MinWidth, 92,
                    CHILD_WeightedWidth, 100,
                    LAYOUT_AddChild,
                        gui->reply_menu_gadget =
                            (struct Gadget *)ButtonObject,
                        GA_ID, GID_REPLY_MENU,
                        GA_RelVerify, TRUE,
                        GA_Disabled, window_current_mailbox_is_drafts(gui)
                            ? TRUE : FALSE,
                        GA_Text, "v",
                    EndObject,
                    CHILD_MinWidth, 18,
                    CHILD_MaxWidth, 18,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, HGroupObject, EndObject,
                    CHILD_MinWidth, 4,
                    CHILD_MaxWidth, 4,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, ButtonObject,
                        GA_ID, GID_DELETE,
                        GA_RelVerify, TRUE,
                        GA_Text, T(MSG_DELETE, "_Delete"),
                    EndObject,
                    CHILD_MinWidth, 92,
                    CHILD_WeightedWidth, 100,
                    LAYOUT_AddChild, HGroupObject, EndObject,
                    CHILD_MinWidth, 4,
                    CHILD_MaxWidth, 4,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, ButtonObject,
                        GA_ID, GID_MOVE,
                        GA_RelVerify, TRUE,
                        GA_Text, T(MSG_MOVE, "_Move"),
                    EndObject,
                    CHILD_MinWidth, 92,
                    CHILD_WeightedWidth, 100,
                    LAYOUT_AddChild, HGroupObject, EndObject,
                    CHILD_MinWidth, 4,
                    CHILD_MaxWidth, 4,
                    CHILD_WeightedWidth, 0,
                    LAYOUT_AddChild, ButtonObject,
                        GA_ID, GID_SEEN,
                        GA_RelVerify, TRUE,
                        GA_Text, T(MSG_READ_UNREAD, "_Read/Unread"),
                    EndObject,
                    CHILD_MinWidth, 92,
                    CHILD_WeightedWidth, 100,
                EndObject,
                CHILD_WeightedHeight, 0,

            LAYOUT_AddChild, HGroupObject,
                LAYOUT_AddChild,
                    VGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, FALSE,

                        LAYOUT_AddChild, HGroupObject,
                            LAYOUT_SpaceOuter, FALSE,
                            LAYOUT_SpaceInner, FALSE,
                            LAYOUT_AddChild,
                                gui->system_labels_gadget =
                                    (struct Gadget *)ListBrowserObject,
                                    GA_ID, GID_SYSTEM_LABELS,
                                    GA_RelVerify, TRUE,
                                    LISTBROWSER_Labels,
                                        &gui->system_labels_list,
                                    LISTBROWSER_ShowSelected, TRUE,
                                    LISTBROWSER_VerticalProp, FALSE,
                                    LISTBROWSER_Spacing, 1,
                                    LISTBROWSER_MinVisible,
                                        GUI_SYSTEM_LABEL_VISIBLE_COUNT,
                                EndObject,
                            LAYOUT_AddChild, HGroupObject,
                                LAYOUT_SpaceOuter, FALSE,
                                LAYOUT_SpaceInner, FALSE,
                            EndObject,
                            CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                            CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                            CHILD_WeightedWidth, 0,
                        EndObject,
                        CHILD_WeightedHeight, 0,

                        LAYOUT_AddChild, HGroupObject,
                            LAYOUT_SpaceOuter, FALSE,
                            LAYOUT_SpaceInner, FALSE,
                        EndObject,
                        CHILD_MinHeight, 3,
                        CHILD_MaxHeight, 3,
                        CHILD_WeightedHeight, 0,

                        LAYOUT_AddChild, HGroupObject,
                            LAYOUT_SpaceOuter, FALSE,
                            LAYOUT_SpaceInner, FALSE,
                            LAYOUT_AddChild,
                                gui->labels_gadget =
                                    (struct Gadget *)ListBrowserObject,
                                    GA_ID, GID_LABELS,
                                    GA_RelVerify, TRUE,
                                    LISTBROWSER_Labels, &gui->labels_list,
                                    LISTBROWSER_ShowSelected, TRUE,
                                    LISTBROWSER_Hierarchical, TRUE,
                                    /* Kleines [+] = geschlossen /
                                     * aufklappen, kleines [-] = offen /
                                     * zuklappen. Eigene klassische Images
                                     * verhindern die grossen nativen Pfeile. */
                                    LISTBROWSER_ShowImage,
                                        &gui->label_show_image,
                                    LISTBROWSER_HideImage,
                                        &gui->label_hide_image,
                                    LISTBROWSER_VerticalProp, FALSE,
                                    LISTBROWSER_Spacing, 1,
                                EndObject,
                            LAYOUT_AddChild, gui->labels_scroller,
                            CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                            CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                            CHILD_WeightedWidth, 0,
                        EndObject,
                        CHILD_WeightedHeight, 100,
                    EndObject,
                CHILD_WeightedWidth, 25,
                CHILD_MinWidth, 120,

                LAYOUT_AddChild, VGroupObject,
                    LAYOUT_AddChild, HGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, FALSE,
                        LAYOUT_AddChild,
                            gui->messages_gadget =
                                (struct Gadget *)ListBrowserObject,
                                GA_ID, GID_MESSAGES,
                                GA_RelVerify, TRUE,
                                LISTBROWSER_Labels, &gui->messages_list,
                                LISTBROWSER_ColumnInfo, gui->columns,
                                LISTBROWSER_ColumnTitles, TRUE,
                                LISTBROWSER_TitleClickable, TRUE,
                                LISTBROWSER_SortColumn, 3,
                                LISTBROWSER_MultiSelect, TRUE,
                                LISTBROWSER_ShowSelected, TRUE,
                                LISTBROWSER_VerticalProp, FALSE,
                                LISTBROWSER_WrapText, TRUE,
                                LISTBROWSER_Spacing, 1,
                                /* Kein ScrollRaster-Optimierungsweg: auf
                                 * einigen klassischen Intuition/Layer-Setups
                                 * bleiben sonst beim Scrollen einzelne
                                 * Pixelzeilen des vorherigen Inhalts stehen. */
                                LISTBROWSER_ScrollRaster, FALSE,
                            EndObject,
                        LAYOUT_AddChild, gui->messages_scroller,
                        CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                        CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                        CHILD_WeightedWidth, 0,
                    EndObject,
                    CHILD_WeightedHeight, 55,
                    CHILD_MinWidth, 250,

                    LAYOUT_AddChild, VGroupObject,
                        LAYOUT_SpaceOuter, FALSE,
                        LAYOUT_SpaceInner, TRUE,
                        LAYOUT_AddChild, HGroupObject,
                            LAYOUT_SpaceOuter, FALSE,
                            LAYOUT_SpaceInner, FALSE,
                            LAYOUT_AddChild,
                                gui->preview_gadget =
                                    (struct Gadget *)TextEditorObject,
                                    GA_ID, GID_PREVIEW,
                                    GA_ReadOnly, TRUE,
                                    GA_TEXTEDITOR_ReadOnly, TRUE,
                                    GA_TEXTEDITOR_DoubleClickHook,
                                        &gui->preview_url_hook,
                                    GA_TEXTEDITOR_Contents,
                                        T(MSG_SELECT_A_MESSAGE_AFTER_FETCHING, "Select a message after fetching."),
                                EndObject,
                            LAYOUT_AddChild, gui->preview_scroller,
                            CHILD_MinWidth, GUI_SCROLLBAR_WIDTH,
                            CHILD_MaxWidth, GUI_SCROLLBAR_WIDTH,
                            CHILD_WeightedWidth, 0,
                        EndObject,
                        CHILD_WeightedHeight, 100,
                        LAYOUT_AddChild, HGroupObject,
                            LAYOUT_SpaceOuter, FALSE,
                            LAYOUT_AddChild, HGroupObject,
                                LAYOUT_SpaceOuter, FALSE,
                                LAYOUT_SpaceInner, FALSE,
                            EndObject,
                            CHILD_WeightedWidth, 100,
                            LAYOUT_AddChild,
                                gui->save_attachments_gadget =
                                    (struct Gadget *)ButtonObject,
                                    GA_ID, GID_SAVE_ATTACHMENTS,
                                    GA_RelVerify, TRUE,
                                    GA_Disabled, TRUE,
                                    GA_Text, T(MSG_SAVE_ATTACHMENTS_08AF, "Save _attachments..."),
                                EndObject,
                            CHILD_WeightedWidth, 0,
                        EndObject,
                        CHILD_WeightedHeight, 0,
                    EndObject,
                    CHILD_WeightedHeight, 45,
                    CHILD_MinWidth, 250,
                EndObject,
                CHILD_WeightedWidth, 75,
                CHILD_MinWidth, 250,
            EndObject,

                LAYOUT_AddChild,
                    gui->status_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_STATUS,
                        GA_ReadOnly, TRUE,
                        STRINGA_TextVal, T(MSG_READY, "Ready"),
                    EndObject,
                CHILD_WeightedHeight, 0,
            EndObject,
        EndObject,
    EndWindow;

    if (!gui->window_object) {
        if (gui->icon_iconified) FreeDiskObject(gui->icon_iconified);
        gui->icon_iconified = NULL;
        FreeLBColumnInfo(gui->columns);
        gui->columns = NULL;
        gui->labels_scroller = NULL;
        gui->messages_scroller = NULL;
        gui->preview_scroller = NULL;
        amg_error_set(error, AMG_ERR_MEMORY,
                      "ReAction-Fenster konnte nicht erzeugt werden.");
        return AMG_ERR_MEMORY;
    }
    return AMG_OK;
}

#endif
