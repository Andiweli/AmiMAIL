#include "gui_internal.h"
#include "contacts.h"
#include "i18n.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA

#include <clib/alib_protos.h>
#include <classes/window.h>
#include <dos/dos.h>
#include <exec/lists.h>
#include <gadgets/button.h>
#include <gadgets/layout.h>
#include <gadgets/listbrowser.h>
#include <gadgets/string.h>
#include <intuition/classes.h>
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

static struct ColumnInfo *contacts_columns(void)
{
    return AllocLBColumnInfo(
        3,
        LBCIA_Column, 0,
        LBCIA_Title, (ULONG)(uintptr_t)T(MSG_FIRST_NAME, "First name"),
        LBCIA_Weight, 30,
        LBCIA_AutoSort, TRUE,
        LBCIA_SortArrow, TRUE,
        LBCIA_DraggableSeparator, TRUE,
        LBCIA_Column, 1,
        LBCIA_Title, (ULONG)(uintptr_t)T(MSG_LAST_NAME, "Last name"),
        LBCIA_Weight, 30,
        LBCIA_AutoSort, TRUE,
        LBCIA_SortArrow, TRUE,
        LBCIA_SortDirection, LBMSORT_FORWARD,
        LBCIA_DraggableSeparator, TRUE,
        LBCIA_Column, 2,
        LBCIA_Title, (ULONG)(uintptr_t)T(MSG_EMAIL_ADDRESS, "Email address"),
        LBCIA_Weight, 40,
        LBCIA_DraggableSeparator, TRUE,
        TAG_DONE);
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

    column = (ULONG)(uintptr_t)hook->h_Data;
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

static void init_contact_render_hooks(struct Hook hooks[3])
{
    ULONG i;
    if (!hooks) return;
    for (i = 0UL; i < 3UL; ++i) {
        memset(&hooks[i], 0, sizeof(hooks[i]));
        hooks[i].h_Entry = (__typeof__(hooks[i].h_Entry))HookEntry;
        hooks[i].h_SubEntry =
            (__typeof__(hooks[i].h_SubEntry))contact_text_render_subentry;
        hooks[i].h_Data = (APTR)(uintptr_t)i;
    }
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
                                 struct Hook hooks[3], UWORD row_height)
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
            TAG_DONE);
        if (node) AddTail(list, node);
    }
    SetGadgetAttrs(gadget, window, NULL,
                   LISTBROWSER_Labels, (ULONG)(uintptr_t)list,
                   LISTBROWSER_Selected, (ULONG)~0UL,
                   LISTBROWSER_Top, 0,
                   LISTBROWSER_SortColumn, 1,
                   TAG_DONE);
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
                            struct Hook hooks[3], UWORD row_height)
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
                             hooks, row_height);
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
                             hooks, row_height);
        set_contact_status(status_gadget, window,
            T(MSG_IMPORTED_CONTACTS_COULD_NOT_BE_SAVED, "Imported contacts could not be saved."));
        return;
    }
    rebuild_contact_list(list_gadget, window, list, book, 0,
                             hooks, row_height);
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
    UWORD row_height;
    AmgContactBook book;
    ULONG signal_mask = 0UL;
    int done = 0;

    if (!gui || !gui->screen) return;
    init_contact_render_hooks(render_hooks);
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
                list_gadget = (struct Gadget *)ListBrowserObject,
                    GA_ID, GID_CONTACTS_LIST,
                    GA_RelVerify, TRUE,
                    LISTBROWSER_Labels, (ULONG)(uintptr_t)&list,
                    LISTBROWSER_ColumnInfo, (ULONG)(uintptr_t)columns,
                    LISTBROWSER_ColumnTitles, TRUE,
                    LISTBROWSER_TitleClickable, TRUE,
                    LISTBROWSER_MultiSelect, TRUE,
                    LISTBROWSER_ShowSelected, TRUE,
                    LISTBROWSER_Spacing, 1,
                    LISTBROWSER_SortColumn, 1,
                EndObject,
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
        FreeLBColumnInfo(columns);
        amg_contacts_free(&book);
        return;
    }
    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog);
        FreeLBColumnInfo(columns);
        amg_contacts_free(&book);
        return;
    }
    WindowToFront(window);
    ActivateWindow(window);
    rebuild_contact_list(list_gadget, window, &list, &book, 0,
                         render_hooks, row_height);
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);
    while (!done) {
        ULONG signals = Wait(signal_mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) done = 1;
        if (signals & signal_mask) {
            ULONG input;
            while ((input = RA_HandleInput(dialog, NULL)) != WMHI_LASTMSG) {
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
                                                     render_hooks, row_height);
                                set_contact_status(status_gadget, window,
                                    T(MSG_CONTACT_SAVED, "Contact saved."));
                            }
                        } else if (gid == GID_CONTACTS_EDIT ||
                                   gid == GID_CONTACTS_LIST) {
                            ULONG rel_event = LBRE_NORMAL;
                            unsigned long id = 0UL;
                            if (gid == GID_CONTACTS_LIST) {
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
                                                     render_hooks, row_height);
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
                                                         render_hooks, row_height);
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
                                                         render_hooks, row_height);
                                    set_contact_status(status_gadget, window,
                                        T(MSG_CONTACTS_COULD_NOT_BE_DELETED, "Contacts could not be deleted."));
                                }
                            }
                            free(ids);
                        } else if (gid == GID_CONTACTS_IMPORT) {
                            import_contacts(gui, window, list_gadget, &list,
                                            status_gadget, &book, error,
                                            render_hooks, row_height);
                        }
                        break;
                }
            }
        }
    }
    SetGadgetAttrs(list_gadget, window, NULL,
                   LISTBROWSER_Labels, (ULONG)~0UL, TAG_DONE);
    DisposeObject(dialog);
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
    UWORD row_height;
    AmgContactBook book;
    ULONG signal_mask = 0UL;
    int done = 0, accepted = 0;
    char recipients[768];

    if (!gui || !parent || !target) return 0;
    init_contact_render_hooks(render_hooks);
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
                list_gadget = (struct Gadget *)ListBrowserObject,
                    GA_ID, GID_CONTACT_SELECT_LIST,
                    GA_RelVerify, TRUE,
                    LISTBROWSER_Labels, (ULONG)(uintptr_t)&list,
                    LISTBROWSER_ColumnInfo, (ULONG)(uintptr_t)columns,
                    LISTBROWSER_ColumnTitles, TRUE,
                    LISTBROWSER_TitleClickable, TRUE,
                    LISTBROWSER_MultiSelect, TRUE,
                    LISTBROWSER_ShowSelected, TRUE,
                    LISTBROWSER_Spacing, 1,
                    LISTBROWSER_SortColumn, 1,
                EndObject,
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
        FreeLBColumnInfo(columns); amg_contacts_free(&book); return 0;
    }
    window = RA_OpenWindow(dialog);
    if (!window) {
        DisposeObject(dialog); FreeLBColumnInfo(columns);
        amg_contacts_free(&book); return 0;
    }
    WindowToFront(window);
    ActivateWindow(window);
    rebuild_contact_list(list_gadget, window, &list, &book, 1,
                         render_hooks, row_height);
    GetAttr(WINDOW_SigMask, dialog, &signal_mask);
    while (!done) {
        ULONG signals = Wait(signal_mask | SIGBREAKF_CTRL_C);
        if (signals & SIGBREAKF_CTRL_C) done = 1;
        if (signals & signal_mask) {
            ULONG input;
            while ((input = RA_HandleInput(dialog, NULL)) != WMHI_LASTMSG) {
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
    FreeListBrowserList(&list);
    FreeLBColumnInfo(columns);
    amg_contacts_free(&book);
    return accepted;
}

#endif /* AMIGMAIL_AMIGA */
