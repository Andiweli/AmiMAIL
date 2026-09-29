#include "gui_internal.h"
#include "gui_icons.h"
#include "contacts.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA

#include <clib/alib_protos.h>
#include <classes/window.h>
#include <dos/dos.h>
#include <devices/inputevent.h>
#include <exec/lists.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/string.h>
#include <intuition/classes.h>
#include <intuition/gadgetclass.h>
#include <intuition/intuition.h>
#include <libraries/asl.h>
#include <proto/asl.h>
#include <proto/button.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <proto/layout.h>
#include <proto/listbrowser.h>
#include <proto/string.h>
#include <proto/utility.h>
#include <proto/window.h>
#include <reaction/reaction.h>
#include <reaction/reaction_macros.h>
#include <utility/tagitem.h>

#ifdef NewObject
#undef NewObject
#endif
#ifdef ButtonObject
#undef ButtonObject
#endif
#define ButtonObject NewObject(NULL, (CONST_STRPTR)"button.gadget"

#define T(id, en) amg_tr((id), (en))

enum ContactGadgetId {
    GID_CONTACTS_LIST = 300,
    GID_CONTACTS_NEW,
    GID_CONTACTS_EDIT,
    GID_CONTACTS_DELETE,
    GID_CONTACTS_IMPORT,
    GID_CONTACTS_CLOSE,
    GID_CONTACT_EDIT_FIRST,
    GID_CONTACT_EDIT_LAST,
    GID_CONTACT_EDIT_COMPANY,
    GID_CONTACT_EDIT_EMAIL,
    GID_CONTACT_EDIT_PHONE,
    GID_CONTACT_EDIT_MOBILE,
    GID_CONTACT_EDIT_WEBSITE,
    GID_CONTACT_EDIT_SAVE,
    GID_CONTACT_EDIT_CANCEL,
    GID_CONTACT_SELECT_LIST,
    GID_CONTACT_SELECT_ACCEPT,
    GID_CONTACT_SELECT_CANCEL
};

static int local_to_utf8_contact(const char *local, char *utf8,
                                 size_t capacity)
{
    const unsigned char *source =
        (const unsigned char *)(local ? local : "");
    size_t used = 0U;
    if (!utf8 || !capacity) return AMG_ERR_ARGUMENT;
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

enum ContactColumn {
    CONTACT_COLUMN_FIRST = 0,
    CONTACT_COLUMN_LAST = 1,
    CONTACT_COLUMN_EMAIL = 2,
    CONTACT_COLUMN_COUNT = 3,
    CONTACT_SORT_COLUMN_COUNT = 2
};

/* Private release codes of our contact-list subclass. Ordinary ListBrowser
 * releases are normalized to zero; the application uses SelectedNode and
 * RelEvent, not the native row number in IntuiMessage.Code. This keeps each
 * completed title click in its own IDCMP message, even during rapid clicks. */
#define CONTACT_SORT_FIRST_CODE 0x7f10U
#define CONTACT_SORT_LAST_CODE  0x7f11U

typedef struct ContactListView ContactListView;

typedef struct ContactColumnRender {
    ContactListView *view;
    ULONG column;
} ContactColumnRender;

struct ContactListView {
    Class *list_class;
    struct Window *window;
    struct Gadget *gadget;
    struct ColumnInfo *columns;
    struct List *list;
    ULONG sort_column;
    ULONG sort_direction;
    LONG cell_left[CONTACT_COLUMN_COUNT];
    LONG cell_right[CONTACT_COLUMN_COUNT];
    LONG body_top;
    ULONG bounds_valid;
    UWORD font_height;
    WORD geometry_left, geometry_top, geometry_width, geometry_height;
    int header_active;
    int native_active;
    LONG pressed_column;
    ContactColumnRender render_data[CONTACT_COLUMN_COUNT];
};

static void init_contact_list_view(ContactListView *view)
{
    memset(view, 0, sizeof(*view));
    view->sort_column = CONTACT_COLUMN_FIRST;
    view->sort_direction = LBMSORT_FORWARD;
    view->pressed_column = -1L;
}

static struct ColumnInfo *contacts_columns(void)
{
    /* There is deliberately NO native sortable column. Merely keeping
     * SortArrow/AutoSort off while enabling Sortable still allowed a native
     * first-column triangle on the reported classic ListBrowser. Title
     * presses are handled by the small subclass below, independently of
     * this flag. All text, frames, fonts and separators remain native.
     * Use real TagItems instead of adding another varargs/macro dependency. */
    struct TagItem tags[] = {
        { LBCIA_Column, CONTACT_COLUMN_FIRST },
        { LBCIA_Title, (ULONG)(uintptr_t)T(MSG_FIRST_NAME, "First name") },
        { LBCIA_Weight, 30UL },
        { LBCIA_Sortable, FALSE },
        { LBCIA_AutoSort, FALSE },
        { LBCIA_SortArrow, FALSE },
        { LBCIA_DraggableSeparator, TRUE },
        { LBCIA_Column, CONTACT_COLUMN_LAST },
        { LBCIA_Title, (ULONG)(uintptr_t)T(MSG_LAST_NAME, "Last name") },
        { LBCIA_Weight, 30UL },
        { LBCIA_Sortable, FALSE },
        { LBCIA_AutoSort, FALSE },
        { LBCIA_SortArrow, FALSE },
        { LBCIA_DraggableSeparator, TRUE },
        { LBCIA_Column, CONTACT_COLUMN_EMAIL },
        { LBCIA_Title, (ULONG)(uintptr_t)T(MSG_EMAIL_ADDRESS, "Email address") },
        { LBCIA_Weight, 40UL },
        { LBCIA_Sortable, FALSE },
        { LBCIA_AutoSort, FALSE },
        { LBCIA_SortArrow, FALSE },
        { LBCIA_DraggableSeparator, TRUE },
        { TAG_DONE, 0UL }
    };
    return AllocLBColumnInfoA(CONTACT_COLUMN_COUNT, tags);
}

static void reset_contact_bounds(ContactListView *view,
                                  const struct Gadget *gadget)
{
    view->bounds_valid = 0UL;
    view->body_top = 0x7fffffffL;
    view->geometry_left = gadget->LeftEdge;
    view->geometry_top = gadget->TopEdge;
    view->geometry_width = gadget->Width;
    view->geometry_height = gadget->Height;
}

static int contact_geometry_matches(const ContactListView *view,
                                     const struct Gadget *gadget)
{
    return view->geometry_left == gadget->LeftEdge &&
           view->geometry_top == gadget->TopEdge &&
           view->geometry_width == gadget->Width &&
           view->geometry_height == gadget->Height;
}

/* A cell's bounds come from the original native row render callback. They
 * include the real column widths after separator dragging, not estimated
 * percentages. Keep the separator hit area itself with ListBrowser. */
static LONG contact_header_column(const ContactListView *view,
                                   const struct Gadget *gadget,
                                   LONG mouse_x, LONG mouse_y)
{
    LONG x, y;
    ULONG column;
    if (!view || !gadget || (gadget->Flags & GFLG_DISABLED) ||
        !view->bounds_valid || !contact_geometry_matches(view, gadget))
        return -1L;
    x = (LONG)gadget->LeftEdge + mouse_x;
    y = (LONG)gadget->TopEdge + mouse_y;
    if (mouse_x < 1L || mouse_x >= (LONG)gadget->Width - 1L ||
        mouse_y < 1L || !view->font_height ||
        mouse_y > (LONG)view->font_height + 3L || y >= view->body_top)
        return -1L;
    for (column = 0UL; column < CONTACT_COLUMN_COUNT; ++column) {
        if (!(view->bounds_valid & (1UL << column))) continue;
        /* The native content rectangle is inset from the separator. Leave
         * its last two pixels alone as well, to favour native resizing. */
        if (x >= view->cell_left[column] &&
            x <= view->cell_right[column] - 2L)
            return (LONG)column;
    }
    return -1L;
}

static void draw_contact_sort_icon(const ContactListView *view,
                                    struct Gadget *gadget,
                                    struct RastPort *rp,
                                    const struct DrawInfo *draw_info)
{
    ULONG column;
    LONG left, right, top, pen;
    if (!view || !gadget || !rp || !contact_geometry_matches(view, gadget))
        return;
    column = view->sort_column;
    if (column >= CONTACT_SORT_COLUMN_COUNT || view->font_height < 4U ||
        !(view->bounds_valid & (1UL << column)))
        return;
    left = view->cell_left[column];
    right = view->cell_right[column];
    if (left < (LONG)gadget->LeftEdge + 1L ||
        right >= (LONG)gadget->LeftEdge + (LONG)gadget->Width - 1L ||
        right - left + 1L < 12L)
        return;
    top = (LONG)gadget->TopEdge + 2L +
          ((LONG)view->font_height - 4L) / 2L;
    if (top + 3L >= view->body_top) return;
    pen = draw_info ? (LONG)draw_info->dri_Pens[TEXTPEN]
                    : (LONG)rp->FgPen;
    /* Only the existing 5x4 mask is drawn. Never repaint any title text,
     * background, bevel or separator, and never change the RastPort font. */
    gui_draw_sort_icon(rp, right - 8L, top,
                       view->sort_direction == LBMSORT_REVERSE, pen);
}

static void contact_icon_after_input(ContactListView *view, Object *object,
                                      struct GadgetInfo *ginfo)
{
    struct RastPort *rp;
    if (!ginfo) return;
    rp = ObtainGIRPort(ginfo);
    if (!rp) return;
    draw_contact_sort_icon(view, (struct Gadget *)object, rp,
                           ginfo->gi_DrInfo);
    ReleaseGIRPort(rp);
}

/* Keep the original ListBrowser implementation for rendering, layout,
 * scrolling, selection and separator dragging. Only mouse activation within
 * the two name headings is ours. In particular, disabling CIF_SORTABLE must
 * NOT again make the headings unclickable: GM_HITTEST/GOACTIVE/HANDLEINPUT
 * implement those clicks without asking the native sorter to do anything.
 * No sorting, allocation, waiting or window operations occur in this hook. */
static ULONG contact_list_dispatcher(struct Hook *hook, APTR object_ptr,
                                      APTR message_ptr)
{
    Class *cl = hook ? (Class *)hook->h_Data : NULL;
    ContactListView *view = cl
        ? (ContactListView *)(uintptr_t)cl->cl_UserData : NULL;
    Object *object = (Object *)object_ptr;
    Msg message = (Msg)message_ptr;
    ULONG result;
    if (!cl || !view || !message) return 0UL;

    if (message->MethodID == GM_HITTEST) {
        struct gpHitTest *hit = (struct gpHitTest *)message_ptr;
        LONG column = contact_header_column(view, (struct Gadget *)object,
                                             hit->gpht_Mouse.X,
                                             hit->gpht_Mouse.Y);
        if (column >= 0L && column < CONTACT_SORT_COLUMN_COUNT)
            return GMR_GADGETHIT;
    } else if (message->MethodID == GM_GOACTIVE ||
               message->MethodID == GM_HANDLEINPUT) {
        struct gpInput *input = (struct gpInput *)message_ptr;
        struct InputEvent *event = input->gpi_IEvent;
        if (!view->header_active && event &&
            event->ie_Class == IECLASS_RAWMOUSE &&
            event->ie_Code == SELECTDOWN) {
            LONG column = contact_header_column(view,
                (struct Gadget *)object, input->gpi_Mouse.X,
                input->gpi_Mouse.Y);
            if (column >= 0L && column < CONTACT_SORT_COLUMN_COUNT) {
                if (message->MethodID == GM_HANDLEINPUT &&
                    view->native_active) {
                    struct gpGoInactive inactive;
                    memset(&inactive, 0, sizeof(inactive));
                    inactive.MethodID = GM_GOINACTIVE;
                    inactive.gpgi_GInfo = input->gpi_GInfo;
                    (void)DoSuperMethodA(cl, object, (Msg)&inactive);
                    view->native_active = 0;
                }
                view->header_active = 1;
                view->pressed_column = column;
                return GMR_MEACTIVE;
            }
            if (column == CONTACT_COLUMN_EMAIL)
                return GMR_NOREUSE;
        }
        if (view->header_active) {
            if (event && event->ie_Class == IECLASS_RAWMOUSE) {
                if (event->ie_Code == SELECTUP) {
                    LONG column = contact_header_column(view,
                        (struct Gadget *)object, input->gpi_Mouse.X,
                        input->gpi_Mouse.Y);
                    if (column == view->pressed_column && column >= 0L &&
                        column < CONTACT_SORT_COLUMN_COUNT &&
                        input->gpi_Termination) {
                        *input->gpi_Termination = column == CONTACT_COLUMN_FIRST
                            ? CONTACT_SORT_FIRST_CODE : CONTACT_SORT_LAST_CODE;
                        return GMR_NOREUSE | GMR_VERIFY;
                    }
                    return GMR_NOREUSE;
                }
                if (event->ie_Code == MENUDOWN)
                    return GMR_REUSE;
            } else if (event && event->ie_Class == IECLASS_RAWKEY &&
                       event->ie_Code == 0x45U) {
                return GMR_NOREUSE; /* Escape cancels a held title click. */
            }
            return GMR_MEACTIVE;
        }
        result = DoSuperMethodA(cl, object, message);
        view->native_active = (result == GMR_MEACTIVE);
        if ((result & GMR_VERIFY) && input->gpi_Termination)
            *input->gpi_Termination = 0L;
        contact_icon_after_input(view, object, input->gpi_GInfo);
        return result;
    } else if (message->MethodID == GM_GOINACTIVE) {
        if (view->header_active) {
            view->header_active = 0;
            view->pressed_column = -1L;
            return 0UL; /* Superclass was not activated for this title press. */
        }
        view->native_active = 0;
        result = DoSuperMethodA(cl, object, message);
        contact_icon_after_input(view, object,
            ((struct gpGoInactive *)message_ptr)->gpgi_GInfo);
        return result;
    } else if (message->MethodID == GM_RENDER) {
        struct gpRender *render = (struct gpRender *)message_ptr;
        struct Gadget *gadget = (struct Gadget *)object;
        view->gadget = gadget;
        if (render->gpr_Redraw == GREDRAW_REDRAW ||
            !contact_geometry_matches(view, gadget))
            reset_contact_bounds(view, gadget);
        result = DoSuperMethodA(cl, object, message);
        draw_contact_sort_icon(view, gadget, render->gpr_RPort,
            render->gpr_GInfo ? render->gpr_GInfo->gi_DrInfo : NULL);
        return result;
    }
    return DoSuperMethodA(cl, object, message);
}

static int open_contact_list_class(ContactListView *view)
{
    Class *cl = MakeClass(NULL, NULL, LISTBROWSER_GetClass(), 0UL, 0UL);
    if (!cl) return 0;
    cl->cl_Dispatcher.h_Entry =
        (__typeof__(cl->cl_Dispatcher.h_Entry))HookEntry;
    cl->cl_Dispatcher.h_SubEntry =
        (__typeof__(cl->cl_Dispatcher.h_SubEntry))contact_list_dispatcher;
    cl->cl_Dispatcher.h_Data = cl;
    cl->cl_UserData = (ULONG)(uintptr_t)view;
    view->list_class = cl;
    return 1;
}

static void close_contact_list_class(ContactListView *view)
{
    if (view->list_class) {
        (void)FreeClass(view->list_class);
        view->list_class = NULL;
    }
}

static ULONG contact_text_render_subentry(struct Hook *hook,
                                          struct Node *node, APTR message)
{
    struct LBDrawMsg *draw = (struct LBDrawMsg *)message;
    struct RastPort *rp;
    ULONG text_value = 0UL;
    ULONG column;
    const char *text;
    LONG text_y;
    UBYTE old_fg, old_mode;

    if (!hook || !node || !draw || draw->lbdm_MethodID != LB_DRAW)
        return LBCB_UNKNOWN;
    rp = draw->lbdm_RastPort;
    if (!rp) return LBCB_OK;

    {
        ContactColumnRender *context = (ContactColumnRender *)hook->h_Data;
        ContactListView *view;
        if (!context || context->column >= CONTACT_COLUMN_COUNT)
            return LBCB_UNKNOWN;
        column = context->column;
        view = context->view;
        if (view) {
            view->cell_left[column] = draw->lbdm_Bounds.MinX;
            view->cell_right[column] = draw->lbdm_Bounds.MaxX;
            view->bounds_valid |= 1UL << column;
            view->font_height = rp->TxHeight;
            if ((LONG)draw->lbdm_Bounds.MinY < view->body_top)
                view->body_top = draw->lbdm_Bounds.MinY;
        }
    }
    GetListBrowserNodeAttrs(
        node,
        LBNA_Column, column,
        LBNCA_Text, (ULONG)(uintptr_t)&text_value,
        TAG_DONE);
    text = (const char *)(uintptr_t)text_value;
    if (!text) text = "";

    old_fg = rp->FgPen;
    old_mode = rp->DrawMode;
    SetDrMd(rp, JAM1);
    if (draw->lbdm_DrawInfo) {
        SetAPen(rp, draw->lbdm_DrawInfo->dri_Pens[
            draw->lbdm_State == LBR_SELECTED ? FILLTEXTPEN : TEXTPEN]);
    }

    text_y = draw->lbdm_Bounds.MinY +
        ((draw->lbdm_Bounds.MaxY - draw->lbdm_Bounds.MinY + 1L -
          (LONG)rp->TxHeight) / 2L) + (LONG)rp->TxBaseline + 1L;
    Move(rp, draw->lbdm_Bounds.MinX, text_y);
    Text(rp, (CONST_STRPTR)text, (ULONG)strlen(text));

    SetAPen(rp, old_fg);
    SetDrMd(rp, old_mode);
    return LBCB_OK;
}

static void init_contact_render_hooks(struct Hook hooks[3],
                                       ContactListView *view)
{
    ULONG i;
    if (!hooks || !view) return;
    for (i = 0UL; i < 3UL; ++i) {
        memset(&hooks[i], 0, sizeof(hooks[i]));
        hooks[i].h_Entry = (__typeof__(hooks[i].h_Entry))HookEntry;
        hooks[i].h_SubEntry =
            (__typeof__(hooks[i].h_SubEntry))contact_text_render_subentry;
        view->render_data[i].view = view;
        view->render_data[i].column = i;
        hooks[i].h_Data = &view->render_data[i];
    }
}

static LONG compare_contact_nodes(struct Node *left, struct Node *right,
                                   ULONG column, ULONG direction)
{
    STRPTR left_text = NULL, right_text = NULL;
    LONG comparison;
    /* Match ListBrowser's priority-first, case-insensitive text ordering.
     * Do not compare email addresses as a hidden secondary sort key. */
    if (left->ln_Pri != right->ln_Pri)
        return left->ln_Pri > right->ln_Pri ? -1L : 1L;
    GetListBrowserNodeAttrs(left,
        LBNA_Column, column,
        LBNCA_Text, (ULONG)(uintptr_t)&left_text,
        TAG_DONE);
    GetListBrowserNodeAttrs(right,
        LBNA_Column, column,
        LBNCA_Text, (ULONG)(uintptr_t)&right_text,
        TAG_DONE);
    comparison = Stricmp((CONST_STRPTR)(left_text ? left_text : (STRPTR)""),
                          (CONST_STRPTR)(right_text ? right_text : (STRPTR)""));
    /* Normalize before reversing, so LONG_MIN cannot overflow. */
    comparison = comparison < 0L ? -1L : (comparison > 0L ? 1L : 0L);
    return direction == LBMSORT_REVERSE ? -comparison : comparison;
}

static void sort_contact_nodes(struct List *list, ULONG column,
                                ULONG direction)
{
    size_t run_length = 1U;
    if (!list || column >= CONTACT_SORT_COLUMN_COUNT) return;

    /* Stable, iterative merge sort: no allocation, no recursion and no
     * quadratic scan on large address books. Only relink existing nodes;
     * their IDs, text, render hooks and multi-selection flags stay intact.
     * The caller MUST detach the list from its gadget first. */
    for (;;) {
        struct List sorted;
        struct Node *left, *right, *node;
        size_t merges = 0U;
        NewList(&sorted);
        left = list->lh_Head;
        while (left && left->ln_Succ) {
            size_t left_count = 0U, right_count = run_length;
            ++merges;
            right = left;
            while (left_count < run_length && right && right->ln_Succ) {
                ++left_count;
                right = right->ln_Succ;
            }
            while (left_count || (right_count && right && right->ln_Succ)) {
                if (!left_count) {
                    node = right;
                    right = right->ln_Succ;
                    --right_count;
                } else if (!right_count || !right || !right->ln_Succ ||
                           compare_contact_nodes(left, right, column,
                                                 direction) <= 0L) {
                    node = left;
                    left = left->ln_Succ;
                    --left_count;
                } else {
                    node = right;
                    right = right->ln_Succ;
                    --right_count;
                }
                Remove(node);
                AddTail(&sorted, node);
            }
            left = right;
        }
        while ((node = RemHead(&sorted)) != NULL)
            AddTail(list, node);
        if (merges <= 1U) return;
        run_length *= 2U;
    }
}

static void sort_contact_column(ContactListView *view, ULONG column)
{
    ULONG top = 0UL, cursor_value = 0UL, cursor_index = (ULONG)~0UL;
    ULONG index = 0UL;
    struct Node *node;
    if (!view || !view->window || !view->gadget || !view->list ||
        column >= CONTACT_SORT_COLUMN_COUNT)
        return;

    /* Repeat on the active heading: toggle A-Z/Z-A. Any change of heading
     * starts at A-Z, never at the direction that column had previously. */
    if (view->sort_column == column)
        view->sort_direction = view->sort_direction == LBMSORT_REVERSE
            ? LBMSORT_FORWARD : LBMSORT_REVERSE;
    else
        view->sort_direction = LBMSORT_FORWARD;
    view->sort_column = column;
    GetAttr(LISTBROWSER_Top, (Object *)view->gadget, &top);
    GetAttr(LISTBROWSER_CursorNode, (Object *)view->gadget, &cursor_value);
    SetGadgetAttrs(view->gadget, view->window, NULL,
                   LISTBROWSER_Labels, (ULONG)~0UL, TAG_DONE);
    sort_contact_nodes(view->list, column, view->sort_direction);
    for (node = view->list->lh_Head; node && node->ln_Succ;
         node = node->ln_Succ, ++index) {
        if (node == (struct Node *)(uintptr_t)cursor_value)
            cursor_index = index;
    }
    SetGadgetAttrs(view->gadget, view->window, NULL,
                   LISTBROWSER_ColumnInfo, (ULONG)(uintptr_t)view->columns,
                   LISTBROWSER_Labels, (ULONG)(uintptr_t)view->list,
                   cursor_index != (ULONG)~0UL
                       ? LISTBROWSER_CursorSelect : TAG_IGNORE, cursor_index,
                   LISTBROWSER_Top, top,
                   TAG_DONE);
    RefreshGList(view->gadget, view->window, NULL, 1);
}

static int handle_contact_sort_event(ContactListView *view, UWORD code)
{
    ULONG release_event = LBRE_NORMAL;
    if (code == CONTACT_SORT_FIRST_CODE || code == CONTACT_SORT_LAST_CODE) {
        sort_contact_column(view, code == CONTACT_SORT_FIRST_CODE
            ? CONTACT_COLUMN_FIRST : CONTACT_COLUMN_LAST);
        return 1;
    }
    GetAttr(LISTBROWSER_RelEvent, (Object *)view->gadget, &release_event);
    if (release_event == LBRE_COLUMNADJUST) {
        RefreshGList(view->gadget, view->window, NULL, 1);
        return 1;
    }
    /* Never treat a stray native title event as a contact double-click. */
    return release_event == LBRE_TITLECLICK;
}

static struct Node *contact_node(const AmgContact *contact,
                                 struct Hook hooks[3], UWORD row_height)
{
    char first[AMG_CONTACT_FIRST_NAME_MAX];
    char last[AMG_CONTACT_LAST_NAME_MAX];
    char email[AMG_CONTACT_EMAIL_MAX];
    if (!contact) return NULL;
    utf8_to_local_copy(contact->first_name, first, sizeof(first));
    utf8_to_local_copy(contact->last_name, last, sizeof(last));
    utf8_to_local_copy(contact->email, email, sizeof(email));
    /* Company-only records from Google Contacts would otherwise be an empty
     * line in the requested three-column overview. Keep the stored name data
     * untouched and use the company only as a display fallback. */
    if (!first[0] && !last[0] && contact->company[0])
        utf8_to_local_copy(contact->company, last, sizeof(last));
    return AllocListBrowserNode(
        3,
        LBNA_UserData, (ULONG)contact->id,
        LBNA_Column, 0,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)first,
        LBNCA_RenderHook, hooks ? (ULONG)(uintptr_t)&hooks[0] : 0UL,
        LBNCA_HookHeight, row_height ? row_height : 10U,
        LBNA_Column, 1,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)last,
        LBNCA_RenderHook, hooks ? (ULONG)(uintptr_t)&hooks[1] : 0UL,
        LBNCA_HookHeight, row_height ? row_height : 10U,
        LBNA_Column, 2,
        LBNCA_CopyText, TRUE,
        LBNCA_Text, (ULONG)(uintptr_t)email,
        LBNCA_RenderHook, hooks ? (ULONG)(uintptr_t)&hooks[2] : 0UL,
        LBNCA_HookHeight, row_height ? row_height : 10U,
        TAG_DONE);
}

static void rebuild_contact_list(struct Gadget *gadget, struct Window *window,
                                 struct List *list,
                                 const AmgContactBook *book,
                                 int email_only,
                                 struct Hook hooks[3], UWORD row_height,
                                 ContactListView *view)
{
    size_t i;
    struct Node *node;
    if (!gadget || !list || !book) return;
    SetGadgetAttrs(gadget, window, NULL,
                   LISTBROWSER_Labels, (ULONG)~0UL, TAG_DONE);
    FreeListBrowserList(list);
    NewList(list);
    for (i = 0U; i < book->count; ++i) {
        if (email_only && !book->items[i].email[0]) continue;
        node = contact_node(&book->items[i], hooks, row_height);
        if (node) AddTail(list, node);
    }
    if (!list->lh_Head->ln_Succ) {
        node = AllocListBrowserNode(
            3,
            LBNA_UserData, 0UL,
            LBNA_Column, 0,
            LBNCA_CopyText, TRUE,
            LBNCA_Text, (ULONG)(uintptr_t)(email_only
                ? T(MSG_NO_CONTACTS_WITH_AN_EMAIL_ADDRESS, "No contacts with an email address.")
                : T(MSG_NO_CONTACTS_AVAILABLE, "No contacts available.")),
            LBNCA_RenderHook, hooks ? (ULONG)(uintptr_t)&hooks[0] : 0UL,
            LBNCA_HookHeight, row_height ? row_height : 10U,
            LBNA_Column, CONTACT_COLUMN_LAST,
            LBNCA_Text, (ULONG)(uintptr_t)"",
            LBNCA_RenderHook, hooks ? (ULONG)(uintptr_t)&hooks[1] : 0UL,
            LBNCA_HookHeight, row_height ? row_height : 10U,
            LBNA_Column, CONTACT_COLUMN_EMAIL,
            LBNCA_Text, (ULONG)(uintptr_t)"",
            LBNCA_RenderHook, hooks ? (ULONG)(uintptr_t)&hooks[2] : 0UL,
            LBNCA_HookHeight, row_height ? row_height : 10U,
            TAG_DONE);
        if (node) AddTail(list, node);
    }
    view->list = list;
    sort_contact_nodes(list, view->sort_column, view->sort_direction);
    SetGadgetAttrs(gadget, window, NULL,
                   LISTBROWSER_ColumnInfo, (ULONG)(uintptr_t)view->columns,
                   LISTBROWSER_Labels, (ULONG)(uintptr_t)list,
                   LISTBROWSER_Selected, (ULONG)~0UL,
                   LISTBROWSER_Top, 0,
                   TAG_DONE);
    RefreshGList(gadget, window, NULL, 1);
}

static int selected_contact_id(struct Gadget *gadget, unsigned long *id)
{
    struct Node *node = NULL;
    ULONG value = 0UL;
    if (id) *id = 0UL;
    if (!gadget || !id) return 0;
    GetAttr(LISTBROWSER_SelectedNode, (Object *)gadget, (ULONG *)&node);
    if (!node) return 0;
    GetListBrowserNodeAttrs(node, LBNA_UserData,
                            (ULONG)(uintptr_t)&value, TAG_DONE);
    if (!value) return 0;
    *id = (unsigned long)value;
    return 1;
}

static size_t selected_contact_ids(const struct List *list,
                                   unsigned long *ids, size_t capacity)
{
    const struct Node *node;
    size_t count = 0U;
    if (!list) return 0U;
    node = list->lh_Head;
    while (node && node->ln_Succ) {
        ULONG selected = FALSE, value = 0UL;
        GetListBrowserNodeAttrs((struct Node *)node,
                                LBNA_Selected,
                                (ULONG)(uintptr_t)&selected,
                                LBNA_UserData,
                                (ULONG)(uintptr_t)&value,
                                TAG_DONE);
        if (selected && value) {
            if (ids && count < capacity) ids[count] = (unsigned long)value;
            ++count;
        }
        node = node->ln_Succ;
    }
    return count;
}

static void set_contact_status(struct Gadget *status, struct Window *window,
                               const char *text)
{
    if (status) set_string(status, window, text ? text : "");
}

static void set_contact_editor_status(struct Gadget *status,
                                      struct Window *window,
                                      const char *text)
{
    if (!status) return;
    SetGadgetAttrs(status, window, NULL,
                   GA_Text, (ULONG)(uintptr_t)(text ? text : ""),
                   TAG_DONE);
}

static int contact_from_editor(AmgContact *contact,
                               struct Gadget *first,
                               struct Gadget *last,
                               struct Gadget *company,
                               struct Gadget *email,
                               struct Gadget *phone,
                               struct Gadget *mobile,
                               struct Gadget *website)
{
    if (!contact) return AMG_ERR_ARGUMENT;
    if (local_to_utf8_contact(string_text(first), contact->first_name,
                              sizeof(contact->first_name)) != AMG_OK ||
        local_to_utf8_contact(string_text(last), contact->last_name,
                              sizeof(contact->last_name)) != AMG_OK ||
        local_to_utf8_contact(string_text(company), contact->company,
                              sizeof(contact->company)) != AMG_OK ||
        local_to_utf8_contact(string_text(email), contact->email,
                              sizeof(contact->email)) != AMG_OK ||
        local_to_utf8_contact(string_text(phone), contact->phone,
                              sizeof(contact->phone)) != AMG_OK ||
        local_to_utf8_contact(string_text(mobile), contact->mobile,
                              sizeof(contact->mobile)) != AMG_OK ||
        local_to_utf8_contact(string_text(website), contact->website,
                              sizeof(contact->website)) != AMG_OK)
        return AMG_ERR_LIMIT;
    amg_contact_trim(contact);
    return AMG_OK;
}

static int contact_editor(AmgGui *gui, AmgContactBook *book,
                          unsigned long contact_id, AmgError *error)
{
    Object *dialog;
    struct Window *window;
    struct Gadget *root_layout = NULL;
    struct Gadget *first_gadget = NULL, *last_gadget = NULL;
    struct Gadget *company_gadget = NULL, *email_gadget = NULL;
    struct Gadget *phone_gadget = NULL, *mobile_gadget = NULL;
    struct Gadget *website_gadget = NULL, *status_gadget = NULL;
    ULONG signal_mask = 0UL;
    AmgContact original, edited;
    char first_local[AMG_CONTACT_FIRST_NAME_MAX];
    char last_local[AMG_CONTACT_LAST_NAME_MAX];
    char company_local[AMG_CONTACT_COMPANY_MAX];
    char email_local[AMG_CONTACT_EMAIL_MAX];
    char phone_local[AMG_CONTACT_PHONE_MAX];
    char mobile_local[AMG_CONTACT_PHONE_MAX];
    char website_local[AMG_CONTACT_WEBSITE_MAX];
    int editing = contact_id != 0UL;
    int done = 0, saved = 0;

    memset(&original, 0, sizeof(original));
    if (editing) {
        const AmgContact *existing = amg_contacts_find(book, contact_id);
        if (!existing) return 0;
        original = *existing;
    }
    edited = original;
    utf8_to_local_copy(original.first_name, first_local, sizeof(first_local));
    utf8_to_local_copy(original.last_name, last_local, sizeof(last_local));
    utf8_to_local_copy(original.company, company_local, sizeof(company_local));
    utf8_to_local_copy(original.email, email_local, sizeof(email_local));
    utf8_to_local_copy(original.phone, phone_local, sizeof(phone_local));
    utf8_to_local_copy(original.mobile, mobile_local, sizeof(mobile_local));
    utf8_to_local_copy(original.website, website_local, sizeof(website_local));

    dialog = WindowObject,
        WA_Title, editing
            ? T(MSG_AMIMAIL_EDIT_CONTACT, "AmiMail - Edit contact")
            : T(MSG_AMIMAIL_NEW_CONTACT, "AmiMail - New contact"),
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                  WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WA_PubScreen, gui->screen,
        WA_Width, 430,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_ParentGroup,
            root_layout = (struct Gadget *)VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, TRUE,
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T(MSG_FIRST_NAME_E581, "First name:")),
                CHILD_MinWidth, 105,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    first_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_CONTACT_EDIT_FIRST,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 63,
                        STRINGA_TextVal, (ULONG)(uintptr_t)first_local,
                    EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T(MSG_LAST_NAME_67EF, "Last name:")),
                CHILD_MinWidth, 105,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    last_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_CONTACT_EDIT_LAST,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 63,
                        STRINGA_TextVal, (ULONG)(uintptr_t)last_local,
                    EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T(MSG_COMPANY, "Company:")),
                CHILD_MinWidth, 105,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    company_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_CONTACT_EDIT_COMPANY,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 95,
                        STRINGA_TextVal, (ULONG)(uintptr_t)company_local,
                    EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T(MSG_EMAIL_ADDRESS_F1D2, "Email address:")),
                CHILD_MinWidth, 105,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    email_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_CONTACT_EDIT_EMAIL,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 255,
                        STRINGA_TextVal, (ULONG)(uintptr_t)email_local,
                    EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T(MSG_PHONE, "Phone:")),
                CHILD_MinWidth, 105,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    phone_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_CONTACT_EDIT_PHONE,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 95,
                        STRINGA_TextVal, (ULONG)(uintptr_t)phone_local,
                    EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T(MSG_MOBILE_PHONE, "Mobile phone:")),
                CHILD_MinWidth, 105,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    mobile_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_CONTACT_EDIT_MOBILE,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 95,
                        STRINGA_TextVal, (ULONG)(uintptr_t)mobile_local,
                    EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, static_text_label(T(MSG_WEBSITE, "Website:")),
                CHILD_MinWidth, 105,
                CHILD_WeightedWidth, 0,
                LAYOUT_AddChild,
                    website_gadget = (struct Gadget *)StringObject,
                        GA_ID, GID_CONTACT_EDIT_WEBSITE,
                        GA_RelVerify, TRUE,
                        GA_TabCycle, TRUE,
                        STRINGA_MaxChars, 383,
                        STRINGA_TextVal, (ULONG)(uintptr_t)website_local,
                    EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
            /* Status/validation text, not an eighth contact field. */
            LAYOUT_AddChild,
                status_gadget = (struct Gadget *)static_text_label(""),
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_CONTACT_EDIT_SAVE,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_SAVE, "_Save"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_CONTACT_EDIT_CANCEL,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_CANCEL, "_Cancel"),
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;
    if (!dialog) return 0;
    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        return 0;
    }
    WindowToFront(window);
    ActivateWindow(window);
    /* String gadgets that live inside layout.gadget must be activated via
     * the layout class.  Direct ActivateGadget() bypasses ReAction's
     * keyboard/tab-cycle bookkeeping and can leave the active string gadget
     * in an inconsistent state on classic AmigaOS when TAB is pressed. */
    if (root_layout && first_gadget)
        ActivateLayoutGadget(root_layout, window, NULL,
                             (ULONG)(uintptr_t)first_gadget);
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);

    while (!done) {
        ULONG signals = Wait(signal_mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) done = 1;
        if (signals & signal_mask) {
            ULONG input;
            UWORD input_code = 0U;
            while ((input = RA_HandleInput(dialog, &input_code)) != WMHI_LASTMSG) {
                switch (input & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        done = 1;
                        break;
                    case WMHI_RAWKEY:
                        if (rawkey_is_cancel(input)) done = 1;
                        else if (rawkey_is_accept(input))
                            input = WMHI_GADGETUP | GID_CONTACT_EDIT_SAVE;
                        else break;
                        /* fall through */
                    case WMHI_GADGETUP:
                        if ((input & WMHI_GADGETMASK) == GID_CONTACT_EDIT_CANCEL) {
                            done = 1;
                        } else if ((input & WMHI_GADGETMASK) == GID_CONTACT_EDIT_SAVE) {
                            unsigned long new_id = 0UL;
                            int result;
                            edited.id = contact_id;
                            result = contact_from_editor(
                                &edited, first_gadget, last_gadget,
                                company_gadget, email_gadget, phone_gadget,
                                mobile_gadget, website_gadget);
                            if (result != AMG_OK) {
                                set_contact_editor_status(status_gadget, window,
                                    T(MSG_AN_INPUT_FIELD_IS_TOO_LONG, "An input field is too long."));
                                break;
                            }
                            if (!amg_contact_has_data(&edited)) {
                                set_contact_editor_status(status_gadget, window,
                                    T(MSG_PLEASE_FILL_AT_LEAST_ONE_FIELD, "Please fill at least one field."));
                                break;
                            }
                            if (amg_contacts_is_duplicate(
                                    book, &edited, contact_id)) {
                                set_contact_editor_status(status_gadget, window,
                                    T(MSG_CONTACT_ALREADY_EXISTS, "Contact already exists."));
                                break;
                            }
                            if (editing)
                                result = amg_contacts_update(book, &edited, error);
                            else
                                result = amg_contacts_add(book, &edited, &new_id, error);
                            if (result != AMG_OK) {
                                set_contact_editor_status(status_gadget, window,
                                    T(MSG_CONTACT_COULD_NOT_BE_SAVED, "Contact could not be saved."));
                                break;
                            }
                            result = amg_contacts_save(
                                AMG_CONTACTS_DEFAULT_PATH, book, error);
                            if (result != AMG_OK) {
                                if (editing) {
                                    AmgContact *restore =
                                        amg_contacts_find_mutable(book, contact_id);
                                    if (restore) *restore = original;
                                } else if (new_id) {
                                    amg_contacts_delete(book, new_id, NULL);
                                }
                                set_contact_editor_status(status_gadget, window,
                                    T(MSG_CONTACT_FILE_COULD_NOT_BE_SAVED, "Contact file could not be saved."));
                                break;
                            }
                            saved = 1;
                            done = 1;
                        }
                        break;
                }
            }
        }
    }
    DisposeObject(dialog);
    return saved;
}

static int choose_contact_import_file(struct Window *window,
                                      char *path, size_t capacity)
{
    struct FileRequester *requester;
    char accept_pattern[64];
    LONG pattern_result;
    if (!path || !capacity) return 0;
    path[0] = 0;
    pattern_result = ParsePatternNoCase(
        (CONST_STRPTR)"#?.(csv|vcf)", (STRPTR)accept_pattern,
        (LONG)sizeof(accept_pattern));
    requester = AllocAslRequestTags(
        ASL_FileRequest,
        ASLFR_TitleText, (ULONG)(uintptr_t)T(MSG_IMPORT_CONTACTS, "Import contacts"),
        ASLFR_Window, (ULONG)(uintptr_t)window,
        ASLFR_SleepWindow, TRUE,
        ASLFR_RejectIcons, TRUE,
        pattern_result >= 0 ? ASLFR_AcceptPattern : TAG_IGNORE,
            (ULONG)(uintptr_t)accept_pattern,
        TAG_DONE);
    if (!requester) return 0;
    if (AslRequest(requester, NULL)) {
        snprintf(path, capacity, "%s",
                 requester->fr_Drawer ? (const char *)requester->fr_Drawer : "");
        if (!requester->fr_File || !requester->fr_File[0] ||
            !AddPart((STRPTR)path, (CONST_STRPTR)requester->fr_File,
                     (LONG)capacity))
            path[0] = 0;
    }
    FreeAslRequest(requester);
    return path[0] != 0;
}

static void import_contacts(AmgGui *gui, struct Window *window,
                            struct Gadget *list_gadget, struct List *list,
                            struct Gadget *status_gadget,
                            AmgContactBook *book, AmgError *error,
                            struct Hook hooks[3], UWORD row_height,
                             ContactListView *view)
{
    char path[768];
    char message[256];
    AmgContactImportResult imported;
    int result;
    if (!choose_contact_import_file(window, path, sizeof(path))) return;
    result = amg_contacts_import_file(path, book, &imported, error);
    if (result != AMG_OK) {
        /* Import works transactionally from the user's point of view: on a
         * parser/memory failure discard any in-memory partial additions and
         * reload the last successfully saved contact book. */
        amg_contacts_free(book);
        amg_contacts_init(book);
        amg_contacts_load(AMG_CONTACTS_DEFAULT_PATH, book, NULL);
        rebuild_contact_list(list_gadget, window, list, book, 0,
                             hooks, row_height, view);
        set_contact_status(status_gadget, window,
            T(MSG_CONTACTS_COULD_NOT_BE_IMPORTED, "Contacts could not be imported."));
        return;
    }
    if (imported.imported &&
        amg_contacts_save(AMG_CONTACTS_DEFAULT_PATH, book, error) != AMG_OK) {
        amg_contacts_free(book);
        amg_contacts_init(book);
        amg_contacts_load(AMG_CONTACTS_DEFAULT_PATH, book, NULL);
        rebuild_contact_list(list_gadget, window, list, book, 0,
                             hooks, row_height, view);
        set_contact_status(status_gadget, window,
            T(MSG_IMPORTED_CONTACTS_COULD_NOT_BE_SAVED, "Imported contacts could not be saved."));
        return;
    }
    rebuild_contact_list(list_gadget, window, list, book, 0,
                             hooks, row_height, view);
    amg_tr_snprintf(message, sizeof(message), MSG_VALUE_IMPORTED_VALUE_DUPLICATES_VALUE_SKIPPED, "%lu imported, %lu duplicates, %lu skipped.", (unsigned long)imported.imported, (unsigned long)imported.duplicates, (unsigned long)imported.skipped);
    set_contact_status(status_gadget, window, message);
    (void)gui;
}

void gui_contacts_dialog(AmgGui *gui, AmgError *error)
{
    Object *dialog;
    struct Window *window;
    struct Gadget *list_gadget = NULL, *status_gadget = NULL;
    struct ColumnInfo *columns;
    struct List list;
    struct Hook render_hooks[3];
    ContactListView view;
    UWORD row_height;
    AmgContactBook book;
    ULONG signal_mask = 0UL;
    int done = 0;

    if (!gui || !gui->screen) return;
    init_contact_list_view(&view);
    init_contact_render_hooks(render_hooks, &view);
    row_height = gui->list_row_hook_height
        ? gui->list_row_hook_height
        : (gui->screen->RastPort.TxHeight
            ? (UWORD)(gui->screen->RastPort.TxHeight + 2U) : 10U);
    amg_contacts_init(&book);
    if (amg_contacts_load(AMG_CONTACTS_DEFAULT_PATH, &book, error) != AMG_OK) {
        status_local(gui, T(MSG_CONTACT_FILE_COULD_NOT_BE_LOADED, "Contact file could not be loaded."));
        amg_contacts_free(&book);
        return;
    }
    NewList(&list);
    columns = contacts_columns();
    if (!columns) {
        amg_contacts_free(&book);
        return;
    }
    view.columns = columns;
    view.list = &list;
    if (!open_contact_list_class(&view)) {
        FreeLBColumnInfo(columns);
        amg_contacts_free(&book);
        return;
    }
    dialog = WindowObject,
        WA_Title, T(MSG_AMIMAIL_CONTACT_MANAGEMENT, "AmiMail - Contact management"),
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                  WFLG_SIZEGADGET | WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WA_PubScreen, gui->screen,
        WA_Width, 600,
        WA_Height, 380,
        WA_MinWidth, 480,
        WA_MinHeight, 260,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, TRUE,
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_CONTACTS_NEW,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_NEW_CONTACT, "_New contact"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_CONTACTS_EDIT,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_EDIT, "_Edit"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_CONTACTS_DELETE,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_DELETE, "_Delete"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_CONTACTS_IMPORT,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_IMPORT, "_Import"),
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild,
                list_gadget = (struct Gadget *)NewObject(
                    view.list_class, NULL,
                    GA_ID, GID_CONTACTS_LIST,
                    GA_RelVerify, TRUE,
                    LISTBROWSER_Labels, (ULONG)(uintptr_t)&list,
                    LISTBROWSER_ColumnInfo, (ULONG)(uintptr_t)columns,
                    LISTBROWSER_ColumnTitles, TRUE,
                    LISTBROWSER_TitleClickable, TRUE,
                    LISTBROWSER_MultiSelect, TRUE,
                    LISTBROWSER_ShowSelected, TRUE,
                    LISTBROWSER_Spacing, 1,
                    TAG_DONE),
            LAYOUT_AddChild,
                status_gadget = (struct Gadget *)StringObject,
                    GA_ReadOnly, TRUE,
                    STRINGA_MaxChars, 255,
                    STRINGA_TextVal, "",
                EndObject,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, ButtonObject,
                GA_ID, GID_CONTACTS_CLOSE,
                GA_RelVerify, TRUE,
                GA_Text, T(MSG_CLOSE, "_Close"),
            EndObject,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;
    if (!dialog) {
        close_contact_list_class(&view);
        FreeLBColumnInfo(columns);
        amg_contacts_free(&book);
        return;
    }
    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        close_contact_list_class(&view);
        FreeLBColumnInfo(columns);
        amg_contacts_free(&book);
        return;
    }
    view.window = window;
    view.gadget = list_gadget;
    WindowToFront(window);
    ActivateWindow(window);
    rebuild_contact_list(list_gadget, window, &list, &book, 0,
                         render_hooks, row_height, &view);
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);
    while (!done) {
        ULONG signals = Wait(signal_mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) done = 1;
        if (signals & signal_mask) {
            ULONG input;
            UWORD input_code = 0U;
            while ((input = RA_HandleInput(dialog, &input_code)) != WMHI_LASTMSG) {
                ULONG gid = input & WMHI_GADGETMASK;
                switch (input & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        done = 1;
                        break;
                    case WMHI_RAWKEY:
                        if (rawkey_is_cancel(input)) done = 1;
                        break;
                    case WMHI_GADGETUP:
                        if (gid == GID_CONTACTS_CLOSE) {
                            done = 1;
                        } else if (gid == GID_CONTACTS_NEW) {
                            if (contact_editor(gui, &book, 0UL, error)) {
                                rebuild_contact_list(list_gadget, window,
                                                     &list, &book, 0,
                                                     render_hooks, row_height, &view);
                                set_contact_status(status_gadget, window,
                                    T(MSG_CONTACT_SAVED, "Contact saved."));
                            }
                        } else if (gid == GID_CONTACTS_EDIT ||
                                   gid == GID_CONTACTS_LIST) {
                            ULONG rel_event = LBRE_NORMAL;
                            unsigned long id = 0UL;
                            if (gid == GID_CONTACTS_LIST) {
                                if (handle_contact_sort_event(&view, input_code))
                                    break;
                                GetAttr(LISTBROWSER_RelEvent,
                                        (Object *)list_gadget, &rel_event);
                                if (rel_event == LBRE_TITLECLICK) break;
                                if (rel_event != LBRE_DOUBLECLICK) break;
                            }
                            if (!selected_contact_id(list_gadget, &id)) {
                                set_contact_status(status_gadget, window,
                                    T(MSG_PLEASE_SELECT_A_CONTACT_FIRST, "Please select a contact first."));
                                break;
                            }
                            if (contact_editor(gui, &book, id, error)) {
                                rebuild_contact_list(list_gadget, window,
                                                     &list, &book, 0,
                                                     render_hooks, row_height, &view);
                                set_contact_status(status_gadget, window,
                                    T(MSG_CONTACT_SAVED, "Contact saved."));
                            }
                        } else if (gid == GID_CONTACTS_DELETE) {
                            size_t selected_count =
                                selected_contact_ids(&list, NULL, 0U);
                            unsigned long *ids = NULL;
                            char question[160];
                            char status_text[160];
                            size_t i;
                            int delete_ok = 1;

                            if (!selected_count) {
                                set_contact_status(status_gadget, window,
                                    T(MSG_PLEASE_SELECT_AT_LEAST_ONE_CONTACT, "Please select at least one contact."));
                                break;
                            }
                            ids = (unsigned long *)malloc(
                                selected_count * sizeof(*ids));
                            if (!ids) {
                                set_contact_status(status_gadget, window,
                                    T(MSG_NOT_ENOUGH_MEMORY, "Not enough memory."));
                                break;
                            }
                            (void)selected_contact_ids(
                                &list, ids, selected_count);

                            if (selected_count == 1U) {
                                snprintf(question, sizeof(question), "%s",
                                         T(MSG_REALLY_DELETE_CONTACT, "Really delete contact?"));
                            } else {
                                amg_tr_snprintf(question, sizeof(question), MSG_REALLY_DELETE_VALUE_SELECTED_CONTACTS, "Really delete %lu selected contacts?", (unsigned long)selected_count);
                            }
                            if (confirm_question_dialog_for_window(
                                    gui, window, question,
                                    T(MSG_THIS_ACTION_CANNOT_BE_UNDONE, "This action cannot be undone."),
                                    330L)) {
                                for (i = 0U; i < selected_count; ++i) {
                                    if (amg_contacts_delete(
                                            &book, ids[i], error) != AMG_OK) {
                                        delete_ok = 0;
                                        break;
                                    }
                                }
                                if (delete_ok &&
                                    amg_contacts_save(AMG_CONTACTS_DEFAULT_PATH,
                                                      &book, error) == AMG_OK) {
                                    rebuild_contact_list(list_gadget, window,
                                                         &list, &book, 0,
                                                         render_hooks, row_height, &view);
                                    if (selected_count == 1U) {
                                        snprintf(status_text,
                                                 sizeof(status_text), "%s",
                                                 T(MSG_CONTACT_DELETED, "Contact deleted."));
                                    } else {
                                        amg_tr_snprintf(status_text, sizeof(status_text), MSG_VALUE_CONTACTS_DELETED, "%lu contacts deleted.", (unsigned long)selected_count);
                                    }
                                    set_contact_status(status_gadget, window,
                                                       status_text);
                                } else {
                                    /* Restore the last durable state if either
                                     * one delete or the transactional save fails. */
                                    amg_contacts_free(&book);
                                    amg_contacts_init(&book);
                                    amg_contacts_load(AMG_CONTACTS_DEFAULT_PATH,
                                                      &book, NULL);
                                    rebuild_contact_list(list_gadget, window,
                                                         &list, &book, 0,
                                                         render_hooks, row_height, &view);
                                    set_contact_status(status_gadget, window,
                                        T(MSG_CONTACTS_COULD_NOT_BE_DELETED, "Contacts could not be deleted."));
                                }
                            }
                            free(ids);
                        } else if (gid == GID_CONTACTS_IMPORT) {
                            import_contacts(gui, window, list_gadget, &list,
                                            status_gadget, &book, error,
                                            render_hooks, row_height, &view);
                        }
                        break;
                }
            }
        }
    }
    SetGadgetAttrs(list_gadget, window, NULL,
                   LISTBROWSER_Labels, (ULONG)~0UL, TAG_DONE);
    DisposeObject(dialog);
    close_contact_list_class(&view);
    FreeListBrowserList(&list);
    FreeLBColumnInfo(columns);
    amg_contacts_free(&book);
}

static int ascii_segment_equal_ci(const char *text, size_t length,
                                  const char *email)
{
    size_t i;
    size_t email_length = strlen(email ? email : "");
    if (length != email_length) return 0;
    for (i = 0U; i < length; ++i) {
        unsigned char a = (unsigned char)text[i];
        unsigned char b = (unsigned char)email[i];
        if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = (unsigned char)(b + ('a' - 'A'));
        if (a != b) return 0;
    }
    return 1;
}

static int recipient_has_email(const char *recipients, const char *email)
{
    const char *p = recipients ? recipients : "";
    if (!email || !*email) return 1;

    while (*p) {
        const char *start;
        const char *end;
        const char *address_start;
        const char *address_end;
        const char *lt = NULL;
        const char *gt = NULL;
        int quoted = 0;

        while (*p == ' ' || *p == '\t' || *p == ',' || *p == ';') ++p;
        if (!*p) break;
        start = p;
        while (*p) {
            if (*p == '"') quoted = !quoted;
            else if (!quoted && *p == '<') lt = p;
            else if (!quoted && *p == '>' && lt) gt = p;
            else if (!quoted && (*p == ',' || *p == ';')) break;
            ++p;
        }
        end = p;
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;

        if (lt && gt && lt < gt && lt >= start && gt <= end) {
            address_start = lt + 1;
            address_end = gt;
        } else {
            address_start = start;
            address_end = end;
        }
        while (address_start < address_end &&
               (*address_start == ' ' || *address_start == '\t' ||
                *address_start == '"')) ++address_start;
        while (address_end > address_start &&
               (address_end[-1] == ' ' || address_end[-1] == '\t' ||
                address_end[-1] == '"')) --address_end;

        if (ascii_segment_equal_ci(address_start,
                                   (size_t)(address_end - address_start),
                                   email)) return 1;
        if (*p) ++p;
    }
    return 0;
}

static int append_recipient(char *recipients, size_t capacity,
                            const char *email)
{
    size_t used, needed;
    if (!recipients || !capacity || !email || !*email) return AMG_OK;
    if (recipient_has_email(recipients, email)) return AMG_OK;
    used = strlen(recipients);
    needed = strlen(email) + (used ? 2U : 0U);
    if (used + needed >= capacity) return AMG_ERR_LIMIT;
    if (used) strcat(recipients, ", ");
    strcat(recipients, email);
    return AMG_OK;
}

int gui_contacts_select_emails(AmgGui *gui, struct Window *parent,
                               struct Gadget *target, AmgError *error)
{
    Object *dialog;
    struct Window *window;
    struct Gadget *list_gadget = NULL, *status_gadget = NULL;
    struct ColumnInfo *columns;
    struct List list;
    struct Hook render_hooks[3];
    ContactListView view;
    UWORD row_height;
    AmgContactBook book;
    ULONG signal_mask = 0UL;
    int done = 0, accepted = 0;
    char recipients[768];

    if (!gui || !parent || !target) return 0;
    init_contact_list_view(&view);
    init_contact_render_hooks(render_hooks, &view);
    row_height = gui->list_row_hook_height
        ? gui->list_row_hook_height
        : (gui->screen && gui->screen->RastPort.TxHeight
            ? (UWORD)(gui->screen->RastPort.TxHeight + 2U) : 10U);
    amg_contacts_init(&book);
    if (amg_contacts_load(AMG_CONTACTS_DEFAULT_PATH, &book, error) != AMG_OK)
        return 0;
    NewList(&list);
    columns = contacts_columns();
    if (!columns) { amg_contacts_free(&book); return 0; }
    view.columns = columns;
    view.list = &list;
    if (!open_contact_list_class(&view)) {
        FreeLBColumnInfo(columns);
        amg_contacts_free(&book);
        return 0;
    }
    dialog = WindowObject,
        WA_Title, T(MSG_AMIMAIL_SELECT_CONTACTS, "AmiMail - Select contacts"),
        WA_Flags, WFLG_CLOSEGADGET | WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                  WFLG_SIZEGADGET | WFLG_ACTIVATE,
        WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_GADGETUP | IDCMP_RAWKEY,
        WA_PubScreen, gui->screen,
        WA_Width, 560,
        WA_Height, 320,
        WA_MinWidth, 440,
        WA_MinHeight, 230,
        WINDOW_Position, WPOS_CENTERSCREEN,
        WINDOW_ParentGroup, VGroupObject,
            LAYOUT_SpaceOuter, TRUE,
            LAYOUT_SpaceInner, TRUE,
            LAYOUT_AddChild,
                list_gadget = (struct Gadget *)NewObject(
                    view.list_class, NULL,
                    GA_ID, GID_CONTACT_SELECT_LIST,
                    GA_RelVerify, TRUE,
                    LISTBROWSER_Labels, (ULONG)(uintptr_t)&list,
                    LISTBROWSER_ColumnInfo, (ULONG)(uintptr_t)columns,
                    LISTBROWSER_ColumnTitles, TRUE,
                    LISTBROWSER_TitleClickable, TRUE,
                    LISTBROWSER_MultiSelect, TRUE,
                    LISTBROWSER_ShowSelected, TRUE,
                    LISTBROWSER_Spacing, 1,
                    TAG_DONE),
            LAYOUT_AddChild,
                status_gadget = (struct Gadget *)StringObject,
                    GA_ReadOnly, TRUE,
                    STRINGA_MaxChars, 255,
                    STRINGA_TextVal, T(MSG_MULTIPLE_SELECTION_IS_SUPPORTED, "Multiple selection is supported."),
                EndObject,
            CHILD_WeightedHeight, 0,
            LAYOUT_AddChild, HGroupObject,
                LAYOUT_EvenSize, TRUE,
                LAYOUT_SpaceInner, TRUE,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_CONTACT_SELECT_ACCEPT,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_USE_SELECTED, "_Use selected"),
                EndObject,
                LAYOUT_AddChild, ButtonObject,
                    GA_ID, GID_CONTACT_SELECT_CANCEL,
                    GA_RelVerify, TRUE,
                    GA_Text, T(MSG_CANCEL, "_Cancel"),
                EndObject,
            EndObject,
            CHILD_WeightedHeight, 0,
        EndObject,
    EndWindow;
    if (!dialog) {
        close_contact_list_class(&view);
        FreeLBColumnInfo(columns); amg_contacts_free(&book); return 0;
    }
    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        close_contact_list_class(&view); FreeLBColumnInfo(columns);
        amg_contacts_free(&book); return 0;
    }
    view.window = window;
    view.gadget = list_gadget;
    WindowToFront(window);
    ActivateWindow(window);
    rebuild_contact_list(list_gadget, window, &list, &book, 1,
                         render_hooks, row_height, &view);
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);
    while (!done) {
        ULONG signals = Wait(signal_mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) done = 1;
        if (signals & signal_mask) {
            ULONG input;
            UWORD input_code = 0U;
            while ((input = RA_HandleInput(dialog, &input_code)) != WMHI_LASTMSG) {
                ULONG gid = input & WMHI_GADGETMASK;
                switch (input & WMHI_CLASSMASK) {
                    case WMHI_CLOSEWINDOW:
                        done = 1; break;
                    case WMHI_RAWKEY:
                        if (rawkey_is_cancel(input)) done = 1;
                        else if (rawkey_is_accept(input))
                            input = WMHI_GADGETUP | GID_CONTACT_SELECT_ACCEPT;
                        else break;
                        /* fall through */
                    case WMHI_GADGETUP:
                        gid = input & WMHI_GADGETMASK;
                        if (gid == GID_CONTACT_SELECT_LIST) {
                            (void)handle_contact_sort_event(&view, input_code);
                            break;
                        }
                        if (gid == GID_CONTACT_SELECT_CANCEL) {
                            done = 1;
                        } else if (gid == GID_CONTACT_SELECT_ACCEPT) {
                            struct Node *node = list.lh_Head;
                            int selected_any = 0;
                            snprintf(recipients, sizeof(recipients), "%s",
                                     string_text(target));
                            while (node && node->ln_Succ) {
                                ULONG selected = FALSE, id = 0UL;
                                GetListBrowserNodeAttrs(
                                    node,
                                    LBNA_Selected, (ULONG)(uintptr_t)&selected,
                                    LBNA_UserData, (ULONG)(uintptr_t)&id,
                                    TAG_DONE);
                                if (selected && id) {
                                    const AmgContact *contact =
                                        amg_contacts_find(&book, id);
                                    if (contact && contact->email[0]) {
                                        char email_local[AMG_CONTACT_EMAIL_MAX];
                                        utf8_to_local_copy(contact->email, email_local,
                                                           sizeof(email_local));
                                        if (append_recipient(
                                                recipients,
                                                sizeof(recipients),
                                                email_local) != AMG_OK) {
                                            set_contact_status(
                                                status_gadget, window,
                                                T(MSG_RECIPIENT_LIST_IS_TOO_LONG, "Recipient list is too long."));
                                            selected_any = -1;
                                            break;
                                        }
                                        selected_any = 1;
                                    }
                                }
                                node = node->ln_Succ;
                            }
                            if (!selected_any) {
                                set_contact_status(status_gadget, window,
                                    T(MSG_PLEASE_SELECT_AT_LEAST_ONE_CONTACT, "Please select at least one contact."));
                            } else if (selected_any > 0) {
                                set_string(target, parent, recipients);
                                accepted = 1;
                                done = 1;
                            }
                        }
                        break;
                }
            }
        }
    }
    SetGadgetAttrs(list_gadget, window, NULL,
                   LISTBROWSER_Labels, (ULONG)~0UL, TAG_DONE);
    DisposeObject(dialog);
    close_contact_list_class(&view);
    FreeListBrowserList(&list);
    FreeLBColumnInfo(columns);
    amg_contacts_free(&book);
    return accepted;
}

#endif /* AMIGMAIL_AMIGA */
