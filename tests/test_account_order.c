#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "account.h"
typedef unsigned long ULONG;
typedef long LONG;
typedef void *APTR;
struct Hook { void *h_Data; };
struct IntuiMessage { ULONG Class; void *IAddress; short MouseX, MouseY; };
struct Gadget { unsigned short GadgetID; short LeftEdge, TopEdge, Width, Height; };
#define IDCMP_GADGETDOWN 0x00000020UL
#define IDCMP_INACTIVEWINDOW 0x00080000UL
struct Node { struct Node *ln_Succ, *ln_Pred; ULONG number; };
struct List { struct Node *lh_Head; struct Node tail; };
#define TNA_Number 10UL
#define TAG_DONE 0UL
static void NewList(struct List *list)
{
    list->lh_Head = &list->tail;
    list->tail.ln_Succ = NULL;
    list->tail.ln_Pred = NULL;
}
static void AddTail(struct List *list, struct Node *node)
{
    node->ln_Succ = &list->tail;
    node->ln_Pred = list->tail.ln_Pred;
    if (node->ln_Pred) node->ln_Pred->ln_Succ = node;
    else list->lh_Head = node;
    list->tail.ln_Pred = node;
}
static void SetClickTabNodeAttrs(struct Node *node, ULONG tag,
                                 ULONG number, ULONG end)
{
    assert(tag == TNA_Number && end == TAG_DONE);
    node->number = number;
}

/* The runner extracts these exact functions and the enum from gui_dialogs.c.
 * Tests never use a separately maintained copy of the production algorithm. */
#include "account_order_production.inc"

static unsigned long cases;
static void verify_permutation(const size_t *order)
{
    unsigned mask = 0;
    size_t i;
    for (i = 0; i < AMG_MAX_ACCOUNTS; ++i) {
        assert(order[i] < AMG_MAX_ACCOUNTS);
        assert(!(mask & (1U << order[i])));
        mask |= 1U << order[i];
    }
}
static void test_order(const size_t input[AMG_MAX_ACCOUNTS])
{
    unsigned config;
    for (config = 1; config < (1U << AMG_MAX_ACCOUNTS); ++config) {
        int configured[AMG_MAX_ACCOUNTS];
        size_t pos;
        for (pos = 0; pos < AMG_MAX_ACCOUNTS; ++pos)
            configured[pos] = !!(config & (1U << pos));
        for (pos = 0; pos < AMG_MAX_ACCOUNTS; ++pos) {
            int direction;
            if (!configured[input[pos]]) continue;
            for (direction = -1; direction <= 1; direction += 2) {
                size_t order[AMG_MAX_ACCOUNTS], expected[AMG_MAX_ACCOUNTS];
                size_t map[AMG_MAX_ACCOUNTS], count = 0U, j, at;
                struct Node nodes[AMG_MAX_ACCOUNTS], *node;
                struct List list;
                long other = (long)pos + direction;
                int should_move;
                AccountOrderClick click = {0UL, 0U, 0};
                ULONG id = direction < 0 ? GID_ACCOUNT_MOVE_LEFT : GID_ACCOUNT_MOVE_RIGHT;
                NewList(&list);
                memset(nodes, 0, sizeof(nodes));
                for (j = 0; j < AMG_MAX_ACCOUNTS; ++j) {
                    size_t slot = input[j];
                    if (!configured[slot]) continue;
                    nodes[slot].number = count;
                    map[count++] = slot;
                    AddTail(&list, &nodes[slot]);
                }
                memcpy(order, input, sizeof(order));
                memcpy(expected, input, sizeof(expected));
                while (other >= 0 && other < (long)AMG_MAX_ACCOUNTS &&
                       !configured[input[other]]) other += direction;
                should_move = other >= 0 && other < (long)AMG_MAX_ACCOUNTS;
                if (should_move) {
                    expected[pos] = input[other]; expected[other] = input[pos];
                }
                assert(!account_order_click_take(&click, id, input[pos]));
                account_order_click_begin(&click, id, input[pos]);
                assert(account_order_click_take(&click, id, input[pos]));
                assert(account_order_move_configured_slot(order, configured,
                       input[pos], direction) == should_move);
                assert(!memcmp(order, expected, sizeof(order)));
                verify_permutation(order);
                assert(account_config_reorder_nodes(&list, order, configured, map, count));
                node = list.lh_Head;
                at = 0;
                for (j = 0; j < AMG_MAX_ACCOUNTS; ++j) {
                    size_t slot = order[j];
                    if (!configured[slot]) continue;
                    assert(map[at] == slot && node == &nodes[slot]);
                    assert(node->number == at++);
                    node = node->ln_Succ;
                }
                assert(node == &list.tail && node->ln_Succ == NULL && at == count);
                /* Extra GADGETUPs after the same press are discarded. */
                assert(!account_order_click_take(&click, id, input[pos]));
                assert(!account_order_click_take(&click, id, input[pos]));
                assert(!memcmp(order, expected, sizeof(order)));
                if (should_move) {
                    id = direction < 0 ? GID_ACCOUNT_MOVE_RIGHT : GID_ACCOUNT_MOVE_LEFT;
                    account_order_click_begin(&click, id, input[pos]);
                    assert(account_order_click_take(&click, id, input[pos]));
                    assert(account_order_move_configured_slot(order, configured,
                           input[pos], -direction));
                    assert(!memcmp(order, input, sizeof(order)));
                    assert(account_config_reorder_nodes(&list, order, configured, map, count));
                    node = list.lh_Head; at = 0;
                    for (j = 0; j < AMG_MAX_ACCOUNTS; ++j) {
                        if (!configured[input[j]]) continue;
                        assert(node == &nodes[input[j]] && node->number == at);
                        assert(map[at++] == input[j]);
                        node = node->ln_Succ;
                    }
                    assert(node == &list.tail);
                }
                ++cases;
            }
        }
    }
}
static void permute(size_t order[AMG_MAX_ACCOUNTS], size_t at)
{
    size_t i;
    if (at == AMG_MAX_ACCOUNTS) { test_order(order); return; }
    for (i = at; i < AMG_MAX_ACCOUNTS; ++i) {
        size_t swap = order[at]; order[at] = order[i]; order[i] = swap;
        permute(order, at + 1U);
        swap = order[at]; order[at] = order[i]; order[i] = swap;
    }
}
static void test_click_hook(void)
{
    AccountOrderClick click = {0UL, 0U, 0};
    size_t active_slot = 2U;
    struct Gadget arrow = {GID_ACCOUNT_MOVE_RIGHT, 100, 20, 28, 12};
    struct Gadget left = {GID_ACCOUNT_MOVE_LEFT, 68, 20, 28, 12};
    struct Gadget layout = {0, 0, 0, 500, 300};
    struct Gadget *left_pointer = &left, *right_pointer = &arrow;
    AccountOrderHookData data = {
        &click, &active_slot, &left_pointer, &right_pointer
    };
    struct Hook hook = {&data};
    struct IntuiMessage message = {IDCMP_GADGETDOWN, &arrow, 0, 0};
    assert(account_order_idcmp_subentry(NULL, NULL, &message) == 0UL);
    assert(account_order_idcmp_subentry(&hook, NULL, NULL) == 0UL);
    assert(!click.armed);
    account_order_idcmp_subentry(&hook, NULL, &message);
    assert(click.armed && click.slot == active_slot);
    assert(account_order_click_take(&click, GID_ACCOUNT_MOVE_RIGHT, active_slot));
    assert(!account_order_click_take(&click, GID_ACCOUNT_MOVE_RIGHT, active_slot));
    account_order_idcmp_subentry(&hook, NULL, &message);
    message.Class = IDCMP_INACTIVEWINDOW;
    account_order_idcmp_subentry(&hook, NULL, &message);
    assert(!click.armed);
    message.Class = IDCMP_GADGETDOWN;
    account_order_idcmp_subentry(&hook, NULL, &message);
    ++active_slot;
    assert(!account_order_click_take(&click, GID_ACCOUNT_MOVE_RIGHT, active_slot));
    message.IAddress = NULL;
    account_order_idcmp_subentry(&hook, NULL, &message);
    assert(!click.armed);
    message.IAddress = &layout;
    message.MouseX = 101; message.MouseY = 22;
    account_order_idcmp_subentry(&hook, NULL, &message);
    assert(account_order_click_take(&click, GID_ACCOUNT_MOVE_RIGHT, active_slot));
    message.MouseX = 69;
    account_order_idcmp_subentry(&hook, NULL, &message);
    assert(account_order_click_take(&click, GID_ACCOUNT_MOVE_LEFT, active_slot));
    message.MouseX = 96; /* Gap, not the left button's right edge. */
    account_order_idcmp_subentry(&hook, NULL, &message);
    assert(!click.armed);
    message.MouseX = 128; /* Right edge is exclusive as well. */
    account_order_idcmp_subentry(&hook, NULL, &message);
    assert(!click.armed);
    assert(!account_order_button_contains(NULL, 100L, 20L));
}

int main(void)
{
    size_t order[AMG_MAX_ACCOUNTS];
    size_t i;
    AccountOrderClick click = {0UL, 0U, 0};
    for (i = 0; i < AMG_MAX_ACCOUNTS; ++i) order[i] = i;
    test_click_hook();
    permute(order, 0U);
    account_order_click_begin(&click, GID_ACCOUNT_MOVE_RIGHT, 0U);
    assert(!account_order_click_take(&click, GID_ACCOUNT_MOVE_LEFT, 0U));
    account_order_click_begin(&click, GID_ACCOUNT_MOVE_RIGHT, 0U);
    assert(!account_order_click_take(&click, GID_ACCOUNT_MOVE_RIGHT, 1U));
    account_order_click_begin(&click, GID_ACCOUNT_CANCEL, 0U);
    assert(!account_order_click_take(&click, GID_ACCOUNT_MOVE_RIGHT, 0U));
    printf("Account order: %lu permutation/configuration/direction cases passed.\n", cases);
    return 0;
}
