/* Actual native worker startup/port/exit helpers with per-task Exec doubles.
 * Each task has its own 16 user bits. No socket or Amiga scheduler is run. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/network_task.c"

static unsigned checks, wake_count, wait_count, forbid_depth, live_ports;
typedef struct TestTask { struct Task task; ULONG allocated; } TestTask;
static TestTask gui_task, workers[AMG_MAX_ACCOUNTS];
static TestTask *current;
static AmgNetwork *active_network;
static AmgNetwork next_start;
static int fail_create, start_on_wait;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); abort(); } } while (0)

void Forbid(void) { ++forbid_depth; }
void Permit(void) { CHECK(forbid_depth > 0U); --forbid_depth; }
struct Task *FindTask(CONST_STRPTR name) { CHECK(!name); return &current->task; }
struct MsgPort *CreateMsgPort(void)
{
    struct MsgPort *port;
    int bit;
    if (fail_create) return NULL;
    for (bit = 31; bit >= 16 && (current->allocated & (1UL << bit)); --bit) {}
    if (bit < 16) return NULL;
    port = calloc(1U, sizeof(*port)); CHECK(port);
    port->mp_SigTask = &current->task;
    port->mp_SigBit = (UBYTE)bit;
    current->allocated |= 1UL << bit;
    ++live_ports;
    return port;
}
void DeleteMsgPort(struct MsgPort *port)
{
    CHECK(port && port->mp_SigTask == &current->task);
    CHECK(current->allocated & (1UL << port->mp_SigBit));
    current->allocated &= ~(1UL << port->mp_SigBit);
    --live_ports;
    free(port);
}
void Signal(struct Task *task, ULONG mask)
{
    CHECK(task == &gui_task.task);
    CHECK(mask == (1UL << active_network->responses->mp_SigBit));
    CHECK(starting_network == NULL);
    if (active_network->commands) {
        CHECK(active_network->worker_ready == 1 && !forbid_depth);
        CHECK(active_network->commands->mp_SigTask == &current->task);
    } else CHECK(active_network->worker_ready == -1 && forbid_depth == 1U);
    ++wake_count;
    /* Model the parent immediately preparing the NEXT worker at this wakeup.
     * The current worker must not clear that new handoff after Signal(). */
    starting_network = &next_start;
}
ULONG Wait(ULONG mask)
{
    TestTask *parent = current;
    CHECK(current == &gui_task && start_on_wait);
    CHECK(mask == (1UL << active_network->responses->mp_SigBit));
    ++wait_count;
    current = &workers[0];
    CHECK(network_worker_open_commands(active_network));
    current = parent;
    return mask;
}
void ReplyMsg(struct Message *message)
{
    CHECK(message && forbid_depth == 1U);
    CHECK(!active_network->commands && !active_network->running);
    CHECK(!active_network->worker_ready);
}
const char *amg_tr(long id, const char *english) { (void)id; return english; }

static void worker_returned_to_dos(void) { forbid_depth = 0U; current = &gui_task; }

int main(void)
{
    AmgNetwork networks[AMG_MAX_ACCOUNTS];
    AmgError error = {0};
    ULONG gui_bits;
    size_t index;
    memset(networks, 0, sizeof(networks));
    current = &gui_task;
    for (index = 0U; index < AMG_MAX_ACCOUNTS; ++index)
        networks[index].responses = CreateMsgPort();
    gui_bits = gui_task.allocated;
    for (index = 0U; index < AMG_MAX_ACCOUNTS; ++index) {
        active_network = &networks[index];
        starting_network = active_network;
        current = &workers[index];
        CHECK(network_worker_open_commands(active_network));
        CHECK(starting_network == &next_start);
        CHECK(gui_task.allocated == gui_bits);
        current = &gui_task;
        CHECK(network_wait_for_worker(active_network, &error) == AMG_OK);
        CHECK(active_network->running);
    }
    CHECK(live_ports == 2U * AMG_MAX_ACCOUNTS && wake_count == AMG_MAX_ACCOUNTS);
    for (index = 0U; index < AMG_MAX_ACCOUNTS; ++index) {
        struct Message stop = {0};
        active_network = &networks[index];
        current = &workers[index];
        network_worker_close_commands(active_network);
        CHECK(!workers[index].allocated && gui_task.allocated == gui_bits);
        network_worker_finish(active_network, &stop);
        CHECK(forbid_depth == 1U);
        worker_returned_to_dos();
    }
    /* Worker signal exhaustion and allocation failure: notify failure safely,
     * clear the stale process pointer in the parent, and allow a retry. */
    active_network = &networks[0];
    for (index = 0U; index < 2U; ++index) {
        current = &workers[0];
        workers[0].allocated = index ? 0UL : 0xffff0000UL;
        fail_create = (int)index;
        active_network->process = (struct Process *)&workers[0];
        starting_network = active_network;
        CHECK(!network_worker_open_commands(active_network));
        CHECK(starting_network == &next_start && forbid_depth == 1U);
        worker_returned_to_dos();
        CHECK(network_wait_for_worker(active_network, &error) == AMG_ERR_MEMORY);
        CHECK(error.code == AMG_ERR_MEMORY && !active_network->process);
        CHECK(!active_network->worker_ready && !active_network->commands);
        CHECK(gui_task.allocated == gui_bits);
    }
    fail_create = 0; workers[0].allocated = 0UL;
    starting_network = active_network; start_on_wait = 1;
    CHECK(network_wait_for_worker(active_network, &error) == AMG_OK);
    CHECK(wait_count == 1U && active_network->running);
    current = &workers[0]; network_worker_close_commands(active_network);
    current = &gui_task;
    for (index = 0U; index < AMG_MAX_ACCOUNTS; ++index)
        DeleteMsgPort(networks[index].responses);
    CHECK(!live_ports && !gui_task.allocated && !workers[0].allocated);
    printf("Network port ownership/startup: %u checks passed (per-task Exec doubles).\n", checks);
    return 0;
}
