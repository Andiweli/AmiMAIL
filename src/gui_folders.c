#include "gui_internal.h"
#include "charset.h"
#include "i18n.h"
#include "imap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if AMIGMAIL_AMIGA
#include <clib/alib_protos.h>
#include <exec/memory.h>
#include <gadgets/listbrowser.h>
#include <gadgets/scroller.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/listbrowser.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <utility/tagitem.h>
#define LABEL_STATE_PATH "ENVARC:AmiMail/folders.state"
#define GUI_TREE_IMAGE_WIDTH 9
#define GUI_TREE_IMAGE_HEIGHT 7
#define GUI_TREE_LEVEL_WIDTH GUI_TREE_IMAGE_WIDTH
#define GUI_TREE_IMAGE_TRAILING_GAP 1
#define GUI_TREE_AXIS_LEFT_SHIFT ((GUI_TREE_IMAGE_WIDTH - 1) / 2)
#define GUI_TREE_CHILD_LEFT_SHIFT (GUI_TREE_AXIS_LEFT_SHIFT + 1)
#define GUI_TREE_DOT_STEP 2
#define T(id, en) amg_tr((id), (en))

typedef struct SystemLabelDefinition {
    long string_id;
    const char *display_en;
    unsigned long special_use;
} SystemLabelDefinition;

static const SystemLabelDefinition system_labels[GUI_SYSTEM_LABEL_COUNT] = {
    {MSG_INBOX, "Inbox", AMG_LABEL_INBOX},
    {MSG_SYSTEM_STARRED, "Starred", AMG_LABEL_FLAGGED},
    {MSG_SYSTEM_SENT, "Sent", AMG_LABEL_SENT},
    {MSG_SYSTEM_DRAFTS, "Drafts", AMG_LABEL_DRAFTS},
    {MSG_SYSTEM_ALL_MAIL, "All Mail", AMG_LABEL_ALL},
    {MSG_SYSTEM_SPAM, "Spam", AMG_LABEL_SPAM},
    {MSG_SYSTEM_TRASH, "Trash", AMG_LABEL_TRASH}
};

static const char *system_special_aliases[GUI_SYSTEM_LABEL_COUNT] = {
    "\\Inbox",
    "\\Flagged",
    "\\Sent",
    "\\Drafts",
    "\\All",
    "\\Junk",
    "\\Trash"
};

static void load_label_expansion_state(AmgGui *gui);
static void save_label_expansion_state(const AmgGui *gui);
static void apply_label_expansion_state(AmgGui *gui);
static const UWORD label_plus_pattern[GUI_TREE_IMAGE_HEIGHT] = {0xff80U,0x8080U,0x8880U,0xbe80U,0x8880U,0x8080U,0xff80U};
static const UWORD label_minus_pattern[GUI_TREE_IMAGE_HEIGHT] = {0xff80U,0x8080U,0x8080U,0xbe80U,0x8080U,0x8080U,0xff80U};
static int build_label_tree_image(AmgGui *gui, struct Image *image,
                                  UWORD **storage,
                                  const UWORD *pattern,
                                  ULONG *allocated_bytes)
{
    struct DrawInfo *draw_info;
    ULONG foreground, background, depth_mask;
    ULONG plane_bytes, total_bytes;
    UBYTE screen_depth, bit, plane_count, plane_index;
    UBYTE plane_pick, plane_on_off;
    UWORD visible_mask;
    UWORD *data;
    ULONG y;

    if (!gui || !gui->screen || !image || !storage || !pattern ||
        !allocated_bytes)
        return 0;

    foreground = gui->text_pen >= 0 ? (ULONG)gui->text_pen : 1UL;
    background = 0UL;
    draw_info = GetScreenDrawInfo(gui->screen);
    if (draw_info) {
        background = (ULONG)draw_info->dri_Pens[BACKGROUNDPEN];
        if (foreground == background)
            foreground = (ULONG)draw_info->dri_Pens[SHADOWPEN];
        FreeScreenDrawInfo(gui->screen, draw_info);
    }
    if (foreground == background)
        foreground = background ? 0UL : 1UL;

    screen_depth = gui->screen->RastPort.BitMap ?
        gui->screen->RastPort.BitMap->Depth : 2U;
    if (screen_depth == 0U) screen_depth = 1U;
    if (screen_depth > 8U) screen_depth = 8U;
    depth_mask = (1UL << screen_depth) - 1UL;
    foreground &= depth_mask;
    background &= depth_mask;

    plane_pick = 0U;
    plane_on_off = 0U;
    plane_count = 0U;
    for (bit = 0U; bit < screen_depth; ++bit) {
        ULONG mask = 1UL << bit;
        if ((foreground & mask) != (background & mask)) {
            plane_pick |= (UBYTE)mask;
            ++plane_count;
        } else if (background & mask) {
            plane_on_off |= (UBYTE)mask;
        }
    }
    if (plane_count == 0U) return 0;

    plane_bytes = (ULONG)GUI_TREE_IMAGE_HEIGHT * (ULONG)sizeof(UWORD);
    total_bytes = plane_bytes * (ULONG)plane_count;
    data = (UWORD *)AllocMem(total_bytes, MEMF_CHIP | MEMF_CLEAR);
    if (!data) return 0;

    visible_mask =
        (UWORD)(0xffffU << (16U - (unsigned)GUI_TREE_IMAGE_WIDTH));
    plane_index = 0U;
    for (bit = 0U; bit < screen_depth; ++bit) {
        ULONG mask = 1UL << bit;
        if ((foreground & mask) != (background & mask)) {
            int foreground_bit = (foreground & mask) != 0UL;
            for (y = 0UL; y < (ULONG)GUI_TREE_IMAGE_HEIGHT; ++y) {
                UWORD value = pattern[y] & visible_mask;
                if (!foreground_bit)
                    value = (UWORD)((~value) & visible_mask);
                data[(ULONG)plane_index * GUI_TREE_IMAGE_HEIGHT + y] = value;
            }
            ++plane_index;
        }
    }

    memset(image, 0, sizeof(*image));
    image->LeftEdge = 0;
    image->TopEdge = -1;
    image->Width = GUI_TREE_IMAGE_WIDTH;
    image->Height = GUI_TREE_IMAGE_HEIGHT;
    image->Depth = plane_count;
    image->ImageData = data;
    image->PlanePick = plane_pick;
    image->PlaneOnOff = plane_on_off;
    image->NextImage = NULL;

    *storage = data;
    *allocated_bytes = total_bytes;
    return 1;
}

 int create_label_tree_images(AmgGui *gui)
{
    ULONG show_bytes = 0UL, hide_bytes = 0UL;
    if (!gui) return 0;

    memset(&gui->label_show_image, 0, sizeof(gui->label_show_image));
    memset(&gui->label_hide_image, 0, sizeof(gui->label_hide_image));
    gui->label_show_image_data = NULL;
    gui->label_hide_image_data = NULL;
    gui->label_image_data_bytes = 0UL;

    if (!build_label_tree_image(gui, &gui->label_show_image,
                                &gui->label_show_image_data,
                                label_plus_pattern, &show_bytes))
        return 0;
    if (!build_label_tree_image(gui, &gui->label_hide_image,
                                &gui->label_hide_image_data,
                                label_minus_pattern, &hide_bytes)) {
        FreeMem(gui->label_show_image_data, show_bytes);
        gui->label_show_image_data = NULL;
        memset(&gui->label_show_image, 0, sizeof(gui->label_show_image));
        return 0;
    }

    if (show_bytes != hide_bytes) {
        FreeMem(gui->label_show_image_data, show_bytes);
        FreeMem(gui->label_hide_image_data, hide_bytes);
        gui->label_show_image_data = NULL;
        gui->label_hide_image_data = NULL;
        memset(&gui->label_show_image, 0, sizeof(gui->label_show_image));
        memset(&gui->label_hide_image, 0, sizeof(gui->label_hide_image));
        return 0;
    }
    gui->label_image_data_bytes = show_bytes;

    return 1;
}

 void dispose_label_tree_images(AmgGui *gui)
{
    if (!gui) return;
    if (gui->label_show_image_data && gui->label_image_data_bytes)
        FreeMem(gui->label_show_image_data, gui->label_image_data_bytes);
    if (gui->label_hide_image_data && gui->label_image_data_bytes)
        FreeMem(gui->label_hide_image_data, gui->label_image_data_bytes);
    gui->label_show_image_data = NULL;
    gui->label_hide_image_data = NULL;
    gui->label_image_data_bytes = 0UL;
    memset(&gui->label_show_image, 0, sizeof(gui->label_show_image));
    memset(&gui->label_hide_image, 0, sizeof(gui->label_hide_image));
}

static void draw_tree_dotted_vertical(struct RastPort *rp, LONG x,
                                      LONG y1, LONG y2)
{
    LONG y;
    if (!rp) return;
    if (y2 < y1) {
        LONG swap = y1;
        y1 = y2;
        y2 = swap;
    }
    for (y = y1; y <= y2; y += GUI_TREE_DOT_STEP)
        WritePixel(rp, x, y);
}

static void draw_tree_dotted_horizontal(struct RastPort *rp, LONG x1,
                                        LONG x2, LONG y)
{
    LONG x;
    if (!rp) return;
    if (x2 < x1) {
        LONG swap = x1;
        x1 = x2;
        x2 = swap;
    }
    for (x = x1; x <= x2; x += GUI_TREE_DOT_STEP)
        WritePixel(rp, x, y);
}

static ULONG label_tree_render_subentry(struct Hook *hook,
                                        struct Node *node, APTR message)
{
    AmgGui *gui = hook ? (AmgGui *)hook->h_Data : NULL;
    struct LBDrawMsg *draw = (struct LBDrawMsg *)message;
    struct RastPort *rp;
    ULONG label_index = (ULONG)~0UL;
    const GuiLabel *label;
    LONG row_top, row_bottom, row_center, text_x, text_y;
    LONG current_slot_x, current_glyph_center;
    LONG line_pen, text_pen;
    UBYTE old_fg, old_mode;
    size_t child_index, parent_index;
    unsigned level_up = 1U;

    if (!gui || !node || !draw || draw->lbdm_MethodID != LB_DRAW)
        return LBCB_UNKNOWN;

    GetListBrowserNodeAttrs(
        node, LBNA_UserData, (ULONG)(uintptr_t)&label_index, TAG_DONE);
    if (label_index >= gui->label_count)
        return LBCB_OK;

    label = &gui->labels[label_index];
    rp = draw->lbdm_RastPort;
    if (!rp) return LBCB_OK;

    row_top = draw->lbdm_Bounds.MinY;
    row_bottom = draw->lbdm_Bounds.MaxY;
    row_center = row_top + (row_bottom - row_top) / 2L;

    /* In hierarchical mode ListBrowser places the custom show/hide image
     * immediately before the column renderer. Leaf nodes have no image, so
     * they retain the one-space optical compensation used by the previous
     * native-text implementation. */
    current_slot_x = draw->lbdm_Bounds.MinX;
    if (label->has_children)
        current_slot_x -= GUI_TREE_IMAGE_WIDTH + GUI_TREE_IMAGE_TRAILING_GAP;
    current_glyph_center = current_slot_x +
        (GUI_TREE_IMAGE_WIDTH - 1L) / 2L - GUI_TREE_AXIS_LEFT_SHIFT;

    old_fg = rp->FgPen;
    old_mode = rp->DrawMode;
    SetDrMd(rp, JAM1);

    if (draw->lbdm_DrawInfo) {
        line_pen = draw->lbdm_State == LBR_SELECTED
            ? draw->lbdm_DrawInfo->dri_Pens[FILLTEXTPEN]
            : draw->lbdm_DrawInfo->dri_Pens[SHADOWPEN];
        text_pen = draw->lbdm_State == LBR_SELECTED
            ? draw->lbdm_DrawInfo->dri_Pens[FILLTEXTPEN]
            : draw->lbdm_DrawInfo->dri_Pens[TEXTPEN];
    } else {
        line_pen = gui->text_pen >= 0 ? gui->text_pen : old_fg;
        text_pen = gui->text_pen >= 0 ? gui->text_pen : old_fg;
    }

    SetAPen(rp, (ULONG)line_pen);

    /* Classic tree continuation: for the direct parent the vertical line
     * always reaches the current branch. It continues through the complete
     * row only when another sibling follows. For higher ancestors a line is
     * needed only while the child on the current path has a later sibling. */
    child_index = (size_t)label_index;
    parent_index = label->parent_index;
    while (parent_index != (size_t)-1 && parent_index < gui->label_count) {
        LONG ancestor_x = current_slot_x -
            (LONG)level_up * GUI_TREE_LEVEL_WIDTH +
            GUI_TREE_IMAGE_WIDTH / 2L - GUI_TREE_AXIS_LEFT_SHIFT;
        int continues = gui->labels[child_index].has_next_sibling;

        if (level_up == 1U) {
            draw_tree_dotted_vertical(
                rp, ancestor_x, row_top,
                continues ? row_bottom : row_center);
        } else if (continues) {
            draw_tree_dotted_vertical(rp, ancestor_x, row_top, row_bottom);
        }

        child_index = parent_index;
        parent_index = gui->labels[parent_index].parent_index;
        ++level_up;
    }

    if (label->generation > 1U && label->parent_index != (size_t)-1) {
        LONG parent_x = current_slot_x - GUI_TREE_LEVEL_WIDTH +
                        GUI_TREE_IMAGE_WIDTH / 2L -
                        GUI_TREE_AXIS_LEFT_SHIFT;
        LONG branch_end;
        if (label->has_children) {
            /* current_slot_x entspricht auf den getesteten klassischen
             * ListBrowser-Versionen der horizontalen Boxmitte. Der Ast soll
             * kurz vor der linken Boxkante enden und nicht bis in die Box
             * hinein verlaengert werden. */
            branch_end = current_slot_x -
                GUI_TREE_IMAGE_WIDTH / 2L - 2L;
        } else {
            LONG space_width = TextLength(rp, (CONST_STRPTR)" ", 1);
            branch_end = draw->lbdm_Bounds.MinX + space_width - 2L -
                         GUI_TREE_CHILD_LEFT_SHIFT;
        }
        if ((long long)branch_end - (long long)parent_x >= 0LL)
            draw_tree_dotted_horizontal(rp, parent_x, branch_end, row_center);
    }

    /* An expanded branch starts its own vertical continuation immediately
     * below the [-] box. Hidden children therefore never leave stray lines. */
    if (label->has_children && label->expanded) {
        LONG glyph_bottom = row_center + GUI_TREE_IMAGE_HEIGHT / 2L;
        /* Leave a clean one-pixel gap below the [-] box.  On classic
         * ListBrowser versions the first dotted trunk pixel otherwise
         * touches the lower-left edge of the hierarchy image and looks like
         * a stray bitmap pixel. */
        if (glyph_bottom + 2L < row_bottom)
            draw_tree_dotted_vertical(rp, current_glyph_center,
                                      glyph_bottom + 3L, row_bottom);
    }

    SetAPen(rp, (ULONG)text_pen);
    text_x = draw->lbdm_Bounds.MinX;
    if (!label->has_children) {
        text_x += TextLength(rp, (CONST_STRPTR)" ", 1);
        /* Untergeordnete Blatt-Labels ruecken mit dem Ast nach links. So
         * bleibt die Querlinie kurz und der Text steht optisch unter dem
         * darueberliegenden Hauptordner, statt weit nach rechts zu wandern. */
        if (label->generation > 1U)
            text_x -= GUI_TREE_CHILD_LEFT_SHIFT;
    }
    text_y = row_top +
        ((row_bottom - row_top + 1L - (LONG)rp->TxHeight) / 2L) +
        (LONG)rp->TxBaseline + 1L;
    Move(rp, text_x, text_y);
    Text(rp, (CONST_STRPTR)label->display_local,
         (ULONG)strlen(label->display_local));

    SetAPen(rp, old_fg);
    SetDrMd(rp, old_mode);
    return LBCB_OK;
}

 void init_label_tree_render_hook(AmgGui *gui)
{
    if (!gui) return;
    memset(&gui->label_tree_render_hook, 0,
           sizeof(gui->label_tree_render_hook));
    gui->label_tree_render_hook.h_Entry =
        (__typeof__(gui->label_tree_render_hook.h_Entry))HookEntry;
    gui->label_tree_render_hook.h_SubEntry =
        (__typeof__(gui->label_tree_render_hook.h_SubEntry))label_tree_render_subentry;
    gui->label_tree_render_hook.h_Data = gui;
    gui->label_tree_hook_height = gui->screen && gui->screen->RastPort.TxHeight
        ? (UWORD)(gui->screen->RastPort.TxHeight + 2U) : 10U;
}

static struct Node *system_label_node(AmgGui *gui,
                                      const char *text, ULONG user_data)
{
    if (gui && gui->system_label_render_hook.h_Entry &&
        gui->list_row_hook_height) {
        return AllocListBrowserNode(
            1,
            LBNA_UserData, user_data,
            LBNA_Column, 0,
            LBNCA_CopyText, TRUE,
            LBNCA_Text, (ULONG)(uintptr_t)text,
            LBNCA_RenderHook, (ULONG)(uintptr_t)&gui->system_label_render_hook,
            LBNCA_HookHeight, gui->list_row_hook_height,
            TAG_DONE);
    }
    return one_column_node(text, user_data);
}

static struct Node *hierarchical_label_node(AmgGui *gui,
                                            const GuiLabel *label,
                                            ULONG user_data)
{
    ULONG flags = label && label->has_children ? LBFLG_HASCHILDREN : 0UL;

    if (gui && gui->label_tree_render_hook.h_Entry &&
        gui->label_tree_hook_height) {
        return AllocListBrowserNode(
            1,
            LBNA_UserData, user_data,
            LBNA_Generation, label ? label->generation : 1U,
            LBNA_Flags, flags,
            LBNA_Column, 0,
            LBNCA_RenderHook, (ULONG)(uintptr_t)&gui->label_tree_render_hook,
            LBNCA_HookHeight, gui->label_tree_hook_height,
            TAG_DONE);
    }

    /* Fallback vor Initialisierung des Workbench-Screens. Diese Nodes werden
     * in create_window() nach Initialisierung des RenderHooks neu aufgebaut. */
    {
        char display[160];
        const char *text = label ? label->display_local : "";
        if (label && !label->has_children) {
            snprintf(display, sizeof(display), " %s", text);
            text = display;
        }
        return AllocListBrowserNode(
            1,
            LBNA_UserData, user_data,
            LBNA_Generation, label ? label->generation : 1U,
            LBNA_Flags, flags,
            LBNA_Column, 0,
            LBNCA_CopyText, TRUE,
            LBNCA_Text, (ULONG)(uintptr_t)text,
            TAG_DONE);
    }
}

static void initialize_system_label_map(AmgGui *gui)
{
    size_t i;
    gui->label_count = GUI_SYSTEM_LABEL_COUNT;
    for (i = 0; i < GUI_SYSTEM_LABEL_COUNT; ++i) {
        memset(&gui->labels[i], 0, sizeof(gui->labels[i]));
        {
            const char *display = T(system_labels[i].string_id,
                                    system_labels[i].display_en);
            strncpy(gui->labels[i].display_local, display,
                    sizeof(gui->labels[i].display_local) - 1U);
            strncpy(gui->labels[i].path_local, display,
                    sizeof(gui->labels[i].path_local) - 1U);
        }
        gui->labels[i].delimiter = '/';
        gui->labels[i].generation = 1U;
        gui->labels[i].parent_index = (size_t)-1;
        gui->labels[i].special_use = system_labels[i].special_use;
        strncpy(gui->labels[i].server_mailbox_utf8, system_special_aliases[i],
                sizeof(gui->labels[i].server_mailbox_utf8) - 1U);
    }
    strcpy(gui->labels[0].mailbox_utf8, "INBOX");
    gui->labels[0].available = 1;
    gui->labels[0].selectable = 1;
}

 void rebuild_label_lists(AmgGui *gui)
{
    size_t i;
    detach_listbrowser(gui->system_labels_gadget, gui->window);
    detach_listbrowser(gui->labels_gadget, gui->window);
    FreeListBrowserList(&gui->system_labels_list);
    FreeListBrowserList(&gui->labels_list);
    NewList(&gui->system_labels_list);
    NewList(&gui->labels_list);
    for (i = 0; i < GUI_SYSTEM_LABEL_COUNT; ++i) {
        if (i == GUI_SYSTEM_LABEL_HIDDEN_INDEX) continue;
        struct Node *node = system_label_node(
            gui, gui->labels[i].display_local, (ULONG)i);
        if (node) AddTail(&gui->system_labels_list, node);
    }
    for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i) {
        struct Node *node = hierarchical_label_node(
            gui, &gui->labels[i], (ULONG)i);
        if (node) AddTail(&gui->labels_list, node);
    }
    HideAllListBrowserChildren(&gui->labels_list);
    apply_label_expansion_state(gui);
    attach_listbrowser(gui->system_labels_gadget, gui->window,
                       &gui->system_labels_list);
    attach_listbrowser(gui->labels_gadget, gui->window, &gui->labels_list);
    if (gui->window) sync_labels_scroller(gui);
}

 void default_labels(AmgGui *gui)
{
    initialize_system_label_map(gui);
    strcpy(gui->current_mailbox_utf8, "INBOX");
    strcpy(gui->current_label_local, T(MSG_INBOX, "Inbox"));
    rebuild_label_lists(gui);
}

static int label_sort_compare(const GuiLabel *left, const GuiLabel *right)
{
    unsigned char a, b;
    const char *left_text = left->mailbox_utf8;
    const char *right_text = right->mailbox_utf8;
    while (*left_text && *right_text) {
        a = (unsigned char)*left_text++;
        b = (unsigned char)*right_text++;
        if (left->delimiter && a == (unsigned char)left->delimiter) a = 1U;
        if (right->delimiter && b == (unsigned char)right->delimiter) b = 1U;
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return a < b ? -1 : 1;
    }
    if (*left_text) return 1;
    if (*right_text) return -1;
    return 0;
}

static void prepare_custom_label_tree(AmgGui *gui)
{
    size_t i, j;
    for (i = GUI_SYSTEM_LABEL_COUNT + 1U; i < gui->label_count; ++i) {
        GuiLabel current = gui->labels[i];
        j = i;
        while (j > GUI_SYSTEM_LABEL_COUNT &&
               label_sort_compare(&gui->labels[j - 1U], &current) > 0) {
            gui->labels[j] = gui->labels[j - 1U];
            --j;
        }
        gui->labels[j] = current;
    }

    for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i) {
        GuiLabel *label = &gui->labels[i];
        const char *leaf = label->mailbox_utf8;
        const char *cursor;
        unsigned short generation = 1U;
        size_t parent_index = (size_t)-1;
        if (label->delimiter) {
            cursor = label->mailbox_utf8;
            while (*cursor) {
                if (*cursor == label->delimiter) {
                    leaf = cursor + 1;
                }
                ++cursor;
            }
        }
        for (j = GUI_SYSTEM_LABEL_COUNT; j < i; ++j) {
            const GuiLabel *possible_parent = &gui->labels[j];
            size_t prefix_length = strlen(possible_parent->mailbox_utf8);
            if (label->delimiter &&
                possible_parent->delimiter == label->delimiter &&
                !strncmp(label->mailbox_utf8,
                         possible_parent->mailbox_utf8, prefix_length) &&
                label->mailbox_utf8[prefix_length] == label->delimiter &&
                possible_parent->generation + 1U > generation) {
                generation = possible_parent->generation + 1U;
                parent_index = j;
            }
        }
        label->generation = generation;
        label->parent_index = parent_index;
        label->has_next_sibling = 0;
        utf8_to_local_copy(label->mailbox_utf8, label->path_local,
                           sizeof(label->path_local));
        utf8_to_local_copy(leaf, label->display_local,
                           sizeof(label->display_local));
        label->has_children = 0;
        if (i + 1U < gui->label_count && label->delimiter) {
            size_t prefix_length = strlen(label->mailbox_utf8);
            const GuiLabel *next = &gui->labels[i + 1U];
            if (!strncmp(next->mailbox_utf8, label->mailbox_utf8,
                         prefix_length) &&
                next->mailbox_utf8[prefix_length] == label->delimiter)
                label->has_children = 1;
        }
    }

    /* Fuer klassische TreeView-Verbindungslinien muss jede sichtbare Zeile
     * wissen, ob auf derselben Ebene noch ein Geschwister folgt. Die Suche
     * endet sofort beim Verlassen des aktuellen Eltern-Teilbaums. */
    for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i) {
        GuiLabel *label = &gui->labels[i];
        for (j = i + 1U; j < gui->label_count; ++j) {
            unsigned short next_generation = gui->labels[j].generation;
            if (next_generation < label->generation) break;
            if (next_generation == label->generation) {
                label->has_next_sibling = 1;
                break;
            }
        }
    }
}

 size_t update_labels_from_payload(AmgGui *gui,
                                         const unsigned char *payload,
                                         size_t length)
{
    size_t position = 0, folder_count = 0;
    initialize_system_label_map(gui);

    while (payload && position < length) {
        size_t line_end = position;
        size_t first_tab, second_tab, third_tab, name_start, name_length;
        unsigned long special = 0;
        int selectable = 1;
        char delimiter = 0;
        char name_utf8[513], name_local[513];
        size_t system_index = GUI_SYSTEM_LABEL_COUNT;
        size_t i;

        while (line_end < length && payload[line_end] != '\n') ++line_end;
        if (line_end > position && payload[line_end - 1U] == '\r') --line_end;

        first_tab = position;
        while (first_tab < line_end && payload[first_tab] != '\t') {
            if (payload[first_tab] >= '0' && payload[first_tab] <= '9')
                special = special * 10UL +
                          (unsigned long)(payload[first_tab] - '0');
            ++first_tab;
        }

        second_tab = first_tab < line_end ? first_tab + 1U : line_end;
        {
            size_t cursor = second_tab;
            selectable = 0;
            while (cursor < line_end && payload[cursor] != '\t') {
                if (payload[cursor] == '1') selectable = 1;
                ++cursor;
            }
            second_tab = cursor;
        }

        third_tab = second_tab < line_end ? second_tab + 1U : line_end;
        {
            size_t cursor = third_tab;
            while (cursor < line_end && payload[cursor] != '\t') ++cursor;
            if (third_tab < cursor && payload[third_tab] != ' ')
                delimiter = (char)payload[third_tab];
            third_tab = cursor;
        }

        name_start = third_tab < line_end ? third_tab + 1U : line_end;
        name_length = line_end - name_start;
        if (name_length >= sizeof(name_utf8))
            name_length = sizeof(name_utf8) - 1U;
        if (name_length) memcpy(name_utf8, payload + name_start, name_length);
        name_utf8[name_length] = 0;

        utf8_to_local_copy(name_utf8, name_local, sizeof(name_local));
        for (i = 0; i < GUI_SYSTEM_LABEL_COUNT; ++i) {
            if (special & system_labels[i].special_use) {
                system_index = i;
                break;
            }
        }

        if (system_index < GUI_SYSTEM_LABEL_COUNT) {
            strncpy(gui->labels[system_index].mailbox_utf8, name_utf8,
                    sizeof(gui->labels[system_index].mailbox_utf8) - 1U);
            gui->labels[system_index].mailbox_utf8[
                sizeof(gui->labels[system_index].mailbox_utf8) - 1U] = 0;
            /* For SELECT, move and delete operations always keep the exact
             * mailbox name returned by the server, independent of the local
             * display name or Special-Use role. */
            strncpy(gui->labels[system_index].server_mailbox_utf8, name_utf8,
                    sizeof(gui->labels[system_index].server_mailbox_utf8) - 1U);
            gui->labels[system_index].server_mailbox_utf8[
                sizeof(gui->labels[system_index].server_mailbox_utf8) - 1U] = 0;
            gui->labels[system_index].available = 1;
            gui->labels[system_index].selectable = selectable;
            ++folder_count;
        } else if (name_local[0] &&
                   gui->label_count < AMIGMAIL_MAX_LABELS) {
            GuiLabel *label = &gui->labels[gui->label_count++];
            memset(label, 0, sizeof(*label));
            strncpy(label->mailbox_utf8, name_utf8,
                    sizeof(label->mailbox_utf8) - 1U);
            strncpy(label->server_mailbox_utf8, name_utf8,
                    sizeof(label->server_mailbox_utf8) - 1U);
            label->delimiter = delimiter;
            label->special_use = special;
            label->available = 1;
            label->selectable = selectable;
            ++folder_count;
        }
        position = line_end;
        while (position < length &&
               (payload[position] == '\r' || payload[position] == '\n'))
            ++position;
    }
    prepare_custom_label_tree(gui);
    load_label_expansion_state(gui);
    rebuild_label_lists(gui);
    return folder_count;
}

static void load_label_expansion_state(AmgGui *gui)
{
    FILE *file;
    char line[1024];
    size_t i;
    if (!gui) return;
    for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i)
        gui->labels[i].expanded = 0;

    file = fopen(LABEL_STATE_PATH, "rb");
    if (!file) return;
    while (fgets(line, sizeof(line), file)) {
        size_t length = strlen(line);
        while (length && (line[length - 1U] == '\n' ||
                          line[length - 1U] == '\r'))
            line[--length] = 0;
        if (!length) continue;
        for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i) {
            if (gui->labels[i].has_children &&
                !strcmp(gui->labels[i].mailbox_utf8, line)) {
                gui->labels[i].expanded = 1;
                break;
            }
        }
    }
    fclose(file);
}

static void save_label_expansion_state(const AmgGui *gui)
{
    FILE *file;
    size_t i;
    if (!gui) return;
    file = fopen(LABEL_STATE_PATH, "wb");
    if (!file) return;
    for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i) {
        const GuiLabel *label = &gui->labels[i];
        if (label->has_children && label->expanded &&
            label->mailbox_utf8[0])
            fprintf(file, "%s\n", label->mailbox_utf8);
    }
    fclose(file);
}

static void apply_label_expansion_state(AmgGui *gui)
{
    size_t i;
    if (!gui) return;
    /* Eltern stehen nach prepare_custom_label_tree() immer vor ihren
     * Kindern. So werden verschachtelte, ebenfalls gespeicherte Zweige
     * in der richtigen Reihenfolge wieder sichtbar gemacht. */
    for (i = GUI_SYSTEM_LABEL_COUNT; i < gui->label_count; ++i) {
        struct Node *node;
        if (!gui->labels[i].has_children || !gui->labels[i].expanded)
            continue;
        node = find_node_by_user_data(&gui->labels_list, (ULONG)i);
        if (node) ShowListBrowserNodeChildren(node, 1);
    }
}


int handle_label_tree_event(AmgGui *gui)
{
    ULONG release_event = LBRE_NORMAL;
    ULONG top = 0;
    struct Node *node = NULL;
    if (!gui || !gui->labels_gadget) return 0;
    GetAttr(LISTBROWSER_RelEvent, (Object *)gui->labels_gadget,
            &release_event);
    if (release_event != LBRE_SHOWCHILDREN &&
        release_event != LBRE_HIDECHILDREN)
        return 0;
    GetAttr(LISTBROWSER_CursorNode, (Object *)gui->labels_gadget,
            (ULONG *)&node);
    if (!node)
        GetAttr(LISTBROWSER_SelectedNode, (Object *)gui->labels_gadget,
                (ULONG *)&node);
    if (!node) return 1;
    {
        ULONG label_index = (ULONG)~0UL;
        GetListBrowserNodeAttrs(
            node, LBNA_UserData, (ULONG)(uintptr_t)&label_index, TAG_DONE);
        if (label_index < gui->label_count &&
            gui->labels[label_index].has_children)
            gui->labels[label_index].expanded =
                release_event == LBRE_SHOWCHILDREN ? 1 : 0;
    }
    GetAttr(LISTBROWSER_Top, (Object *)gui->labels_gadget, &top);
    detach_listbrowser(gui->labels_gadget, gui->window);
    if (release_event == LBRE_SHOWCHILDREN)
        ShowListBrowserNodeChildren(node, 1);
    else
        HideListBrowserNodeChildren(node);
    save_label_expansion_state(gui);
    SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                   LISTBROWSER_Labels,
                       (ULONG)(uintptr_t)&gui->labels_list,
                   LISTBROWSER_SelectedNode, (ULONG)(uintptr_t)node,
                   LISTBROWSER_Top, top,
                   TAG_DONE);
    sync_labels_scroller(gui);
    return 1;
}

 void select_label_index(AmgGui *gui, size_t index)
{
    struct Node *node;
    if (!gui) return;
    if (gui->system_labels_gadget) {
        if (gui->window)
            SetGadgetAttrs(gui->system_labels_gadget, gui->window, NULL,
                           LISTBROWSER_Selected, (ULONG)~0UL, TAG_DONE);
        else
            SetAttrs((Object *)gui->system_labels_gadget,
                     LISTBROWSER_Selected, (ULONG)~0UL, TAG_DONE);
    }
    if (gui->labels_gadget) {
        if (gui->window)
            SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                           LISTBROWSER_Selected, (ULONG)~0UL, TAG_DONE);
        else
            SetAttrs((Object *)gui->labels_gadget,
                     LISTBROWSER_Selected, (ULONG)~0UL, TAG_DONE);
    }
    if (index < GUI_SYSTEM_LABEL_COUNT) {
        if (index == GUI_SYSTEM_LABEL_HIDDEN_INDEX) return;
        node = find_node_by_user_data(&gui->system_labels_list,
                                      (ULONG)index);
        if (node && gui->system_labels_gadget) {
            if (gui->window)
                SetGadgetAttrs(gui->system_labels_gadget, gui->window, NULL,
                               LISTBROWSER_SelectedNode,
                                   (ULONG)(uintptr_t)node, TAG_DONE);
            else
                SetAttrs((Object *)gui->system_labels_gadget,
                         LISTBROWSER_SelectedNode, (ULONG)(uintptr_t)node,
                         TAG_DONE);
        }
        return;
    }
    node = find_node_by_user_data(&gui->labels_list, (ULONG)index);
    if (node && gui->labels_gadget) {
        if (gui->window)
            SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                           LISTBROWSER_SelectedNode, (ULONG)(uintptr_t)node,
                           TAG_DONE);
        else
            SetAttrs((Object *)gui->labels_gadget,
                     LISTBROWSER_SelectedNode, (ULONG)(uintptr_t)node,
                     TAG_DONE);
    }
}

static void label_scroll_geometry(AmgGui *gui, ULONG *top_out,
                                  ULONG *total_out, ULONG *visible_out)
{
    ULONG current = 0, max_top = 0, total = 0, visible = 1;
    if (!gui || !gui->window || !gui->labels_gadget) {
        if (top_out) *top_out = 0;
        if (total_out) *total_out = 1;
        if (visible_out) *visible_out = 1;
        return;
    }

    GetAttr(LISTBROWSER_TotalVisibleNodes,
            (Object *)gui->labels_gadget, &total);
    if (total < 1U) total = 1U;
    GetAttr(LISTBROWSER_Top, (Object *)gui->labels_gadget, &current);

    SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                   LISTBROWSER_Top, 0x7fffffffUL,
                   TAG_DONE);
    GetAttr(LISTBROWSER_Top, (Object *)gui->labels_gadget, &max_top);
    if (current > max_top) current = max_top;
    SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                   LISTBROWSER_Top, current,
                   TAG_DONE);

    if (max_top == 0U || total <= max_top) {
        visible = total;
        current = 0U;
    } else {
        visible = total - max_top;
        if (visible < 1U) visible = 1U;
        if (visible > total) visible = total;
    }

    if (top_out) *top_out = current;
    if (total_out) *total_out = total;
    if (visible_out) *visible_out = visible;
}

 void sync_labels_scroller(AmgGui *gui)
{
    ULONG top = 0, total = 1, visible = 1;
    if (!gui || !gui->window || !gui->labels_gadget ||
        !gui->labels_scroller)
        return;

    label_scroll_geometry(gui, &top, &total, &visible);
    if (total <= visible) {
        SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                       LISTBROWSER_Top, 0,
                       TAG_DONE);
        set_scroller_full(gui->window, gui->labels_scroller);
        return;
    }
    if (top > total - visible) top = total - visible;
    SetGadgetAttrs(gui->labels_scroller, gui->window, NULL,
                   SCROLLER_Top, top,
                   SCROLLER_Total, total,
                   SCROLLER_Visible, visible,
                   TAG_DONE);
    RefreshGList(gui->labels_scroller, gui->window, NULL, 1);
}

 void handle_labels_scroller(AmgGui *gui)
{
    ULONG top = 0, total = 1, visible = 1;
    if (!gui || !gui->window || !gui->labels_gadget ||
        !gui->labels_scroller)
        return;

    label_scroll_geometry(gui, NULL, &total, &visible);
    GetAttr(SCROLLER_Top, (Object *)gui->labels_scroller, &top);
    if (total <= visible) top = 0U;
    else if (top > total - visible) top = total - visible;

    SetGadgetAttrs(gui->labels_gadget, gui->window, NULL,
                   LISTBROWSER_Top, top,
                   TAG_DONE);
    RefreshGList(gui->labels_gadget, gui->window, NULL, 1);
    SetGadgetAttrs(gui->labels_scroller, gui->window, NULL,
                   SCROLLER_Top, top,
                   SCROLLER_Total, total,
                   SCROLLER_Visible, visible,
                   TAG_DONE);
    RefreshGList(gui->labels_scroller, gui->window, NULL, 1);
}

#endif /* AMIGMAIL_AMIGA */
