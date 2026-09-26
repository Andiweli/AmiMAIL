/* Exercise the real native network queue/progress code with OS API doubles.
 * No socket, real task scheduler or Amiga ABI is simulated by this test. */
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/network_task.c"

static unsigned checks, wake_count;
static int forbid_depth;
static struct MsgPort commands, responses;
static struct Task gui_task;
static AmgNetMessage *queued, *response;
#define CHECK(expr) do { ++checks; if (!(expr)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expr); abort(); \
} } while (0)

void Forbid(void) { ++forbid_depth; }
void Permit(void) { CHECK(forbid_depth > 0); --forbid_depth; }
void Signal(struct Task *task, ULONG mask)
{
    CHECK(forbid_depth == 0);
    CHECK(task == &gui_task);
    CHECK(mask == (1UL << responses.mp_SigBit));
    ++wake_count;
}
void PutMsg(struct MsgPort *port, struct Message *message)
{
    CHECK(port == &commands && queued == NULL);
    queued = (AmgNetMessage *)message;
}
struct Message *GetMsg(struct MsgPort *port)
{
    AmgNetMessage *message = response;
    CHECK(port == &responses);
    response = NULL;
    return (struct Message *)message;
}
const char *amg_tr(long id, const char *english) { (void)id; return english; }
void amg_mailfile_close(AmgMailFile *file) { CHECK(file == NULL); }

static AmgNetMessage *take_queued(void)
{
    AmgNetMessage *message = queued;
    CHECK(message != NULL);
    queued = NULL;
    CHECK(message->message.mn_ReplyPort == &responses);
    CHECK(message->message.mn_Length == sizeof(*message));
    return message;
}

static void init_network(AmgNetwork *network)
{
    memset(network, 0, sizeof(*network));
    network->running = 1;
    network->commands = &commands;
    network->responses = &responses;
}

static void check_origin(AmgNetwork *network, unsigned long uid,
                          const char *mailbox, int active)
{
    AmgTransferProgress snapshot;
    memset(&snapshot, 0, sizeof(snapshot));
    CHECK(amg_network_transfer_progress(network, &snapshot) == active);
    CHECK(snapshot.active == active);
    CHECK(snapshot.uid == uid);
    CHECK(!strcmp(snapshot.mailbox, mailbox));
}

static void test_receive(AmgNetwork *network)
{
    AmgNetMessage *message;
    AmgNetworkEvent event;
    AmgError error;
    unsigned long serial;
    char folder[] = "INBOX";
    CHECK(amg_network_request(network, AMG_NET_FETCH_MESSAGE, 123UL,
                              "preview", folder, &error) == AMG_OK);
    message = take_queued();
    CHECK(message->uid == 123UL && message->progress_uid == 123UL);
    CHECK(!strcmp(message->argument2, "INBOX"));
    folder[0] = 'x'; /* Caller storage may change immediately after queueing. */
    CHECK(!strcmp(message->progress_mailbox, "INBOX"));
    transfer_begin(network, message);
    serial = network->progress.serial;
    CHECK(serial != 0UL && network->progress.cancellable);
    check_origin(network, 123UL, "INBOX", 1);
    message->progress_mailbox[0] = 'q';
    /* Snapshot is independent of command storage as well as caller storage. */
    check_origin(network, 123UL, "INBOX", 1);
    CHECK(transfer_report(network, AMG_TRANSFER_RECEIVE, 65536U, 131072U));
    CHECK(network->progress.done == 65536U);
    CHECK(transfer_report(network, AMG_TRANSFER_INDEX, 10U, 30U));
    check_origin(network, 123UL, "INBOX", 1);
    CHECK(!amg_network_cancel_transfer(network, serial + 1UL));
    CHECK(!network->io_cancel_requested);
    CHECK(amg_network_cancel_transfer(network, serial));
    CHECK(network->io_cancel_requested && !network->progress.cancellable);
    CHECK(!amg_network_cancel_transfer(network, serial));
    CHECK(!transfer_report(network, AMG_TRANSFER_INDEX, 20U, 30U));
    transfer_end(network);
    check_origin(network, 123UL, "INBOX", 0);
    CHECK(!amg_network_cancel_transfer(network, serial));
    /* The result retains the original UID/mailbox used by the GUI guard. */
    response = message;
    CHECK(amg_network_poll(network, &event) == 1);
    CHECK(event.uid == 123UL && !strcmp(event.argument2, "INBOX"));
    amg_network_event_clear(&event);
    CHECK(amg_network_poll(network, &event) == 0);
}

static void test_accounts_and_jobs(AmgNetwork *network)
{
    AmgNetwork other;
    AmgNetMessage *message, *other_message;
    unsigned long old_serial = network->progress.serial;
    init_network(&other);
    CHECK(amg_network_request(network, AMG_NET_FETCH_MESSAGE, 123UL,
                              "preview", "Sent", NULL) == AMG_OK);
    message = take_queued();
    transfer_begin(network, message);
    CHECK(!network->io_cancel_requested);
    CHECK(network->progress.serial != old_serial);
    CHECK(!amg_network_cancel_transfer(network, old_serial));
    CHECK(amg_network_request(&other, AMG_NET_FETCH_MESSAGE, 123UL,
                              "preview", "INBOX", NULL) == AMG_OK);
    other_message = take_queued();
    transfer_begin(&other, other_message);
    check_origin(network, 123UL, "Sent", 1);
    check_origin(&other, 123UL, "INBOX", 1);
    CHECK(amg_network_cancel_transfer(&other, other.progress.serial));
    CHECK(!network->io_cancel_requested);
    transfer_end(network);
    transfer_end(&other);
    free_net_message(message);
    free_net_message(other_message);
    CHECK(amg_network_request(network, AMG_NET_CHECK_INBOX, 0UL,
                              NULL, NULL, NULL) == AMG_OK);
    message = take_queued();
    transfer_begin(network, message);
    check_origin(network, 0UL, "", 0);
    free_net_message(message);
}

static void test_outgoing(AmgNetwork *network)
{
    AmgMailDraft draft;
    AmgNetMessage *message;
    unsigned long serial;
    char folder[] = "Archive/Selected";
    char too_long[513];
    AmgError error;
    memset(&draft, 0, sizeof(draft));
    draft.from = "me@example.com";
    draft.to = "you@example.com";
    draft.subject = "Progress queue test";
    draft.body_utf8 = "Body";
    draft.date_rfc2822 = "Sat, 26 Sep 2026 10:00:00 +0200";
    draft.message_id = "<test@example.com>";
    draft.progress_uid = 222UL;
    draft.progress_mailbox = folder;
    CHECK(amg_network_request_mail_from_draft(network, &draft, 999UL,
                                              "Drafts", &error) == AMG_OK);
    message = take_queued();
    CHECK(message->uid == 999UL && !strcmp(message->argument1, "Drafts"));
    CHECK(message->progress_uid == 222UL);
    folder[0] = 'z';
    transfer_begin(network, message);
    check_origin(network, 222UL, "Archive/Selected", 1);
    CHECK(!network->progress.cancellable);
    serial = network->progress.serial;
    CHECK(!amg_network_cancel_transfer(network, serial));
    CHECK(transfer_report(network, AMG_TRANSFER_UPLOAD, 0U, 500U));
    CHECK(network->progress.cancellable);
    CHECK(transfer_report(network, AMG_TRANSFER_COMMIT, 500U, 500U));
    CHECK(!network->progress.cancellable);
    CHECK(!amg_network_cancel_transfer(network, serial));
    transfer_end(network);
    free_net_message(message);

    draft.progress_uid = 0UL;
    draft.progress_mailbox = "INBOX";
    CHECK(amg_network_request_draft(network, &draft, "Drafts", &error) == AMG_OK);
    message = take_queued();
    CHECK(!strcmp(message->argument1, "Drafts"));
    transfer_begin(network, message);
    check_origin(network, 0UL, "INBOX", 1);
    free_net_message(message);
    transfer_end(network);

    /* Programmatic/legacy unscoped jobs have no selectable origin. */
    draft.progress_mailbox = NULL;
    CHECK(amg_network_request_mail(network, &draft, &error) == AMG_OK);
    message = take_queued();
    transfer_begin(network, message);
    check_origin(network, 0UL, "", 1);
    free_net_message(message);
    transfer_end(network);

    memset(too_long, 'x', sizeof(too_long) - 1U);
    too_long[sizeof(too_long) - 1U] = 0;
    draft.progress_mailbox = too_long;
    CHECK(amg_network_request_mail(network, &draft, &error) == AMG_ERR_LIMIT);
    CHECK(queued == NULL && error.code == AMG_ERR_LIMIT);
    too_long[511] = 0;
    CHECK(amg_network_request_mail(network, &draft, &error) == AMG_OK);
    message = take_queued();
    CHECK(strlen(message->progress_mailbox) == 511U);
    free_net_message(message);
}

static void test_folder_queue(AmgNetwork *network)
{
    AmgNetMessage *message;
    unsigned wakes;
    char folder[64] = "[Gmail]/All Mail";
    char long_folder[514];
    CHECK(amg_network_request(network, AMG_NET_FETCH_INBOX, 99UL,
                              folder, NULL, NULL) == AMG_OK);
    message = take_queued();
    CHECK(message->progress_uid == 0UL);
    folder[0] = 'x';
    CHECK(!strcmp(message->progress_mailbox, "[Gmail]/All Mail"));
    transfer_begin(network, message);
    check_origin(network, 0UL, "[Gmail]/All Mail", 1);
    CHECK(network->progress.cancellable);
    CHECK(transfer_report(network, AMG_TRANSFER_RECEIVE, 0U, 250U));
    wakes = wake_count;
    CHECK(transfer_report(network, AMG_TRANSFER_RECEIVE, 2U, 250U));
    CHECK(wake_count == wakes);
    CHECK(transfer_report(network, AMG_TRANSFER_RECEIVE, 3U, 250U));
    CHECK(wake_count > wakes && network->progress.done == 3U);
    CHECK(transfer_report(network, AMG_TRANSFER_RECEIVE, 125U, 250U));
    CHECK(network->progress.done == 125U);
    CHECK(amg_network_cancel_transfer(network, network->progress.serial));
    CHECK(!transfer_report(network, AMG_TRANSFER_RECEIVE, 126U, 250U));
    transfer_end(network); free_net_message(message);
    CHECK(amg_network_request(network, AMG_NET_FETCH_INBOX, 0UL,
                              NULL, NULL, NULL) == AMG_OK);
    message = take_queued();
    CHECK(!strcmp(message->progress_mailbox, "INBOX"));
    free_net_message(message);
    memset(long_folder, 'x', sizeof(long_folder) - 1U);
    long_folder[sizeof(long_folder) - 1U] = 0;
    CHECK(amg_network_request(network, AMG_NET_FETCH_INBOX, 0UL,
                              long_folder, NULL, NULL) == AMG_ERR_LIMIT);
    CHECK(queued == NULL);
}

int main(void)
{
    AmgNetwork network;
    responses.mp_SigBit = 3;
    responses.mp_SigTask = &gui_task;
    CHECK(sizeof(AmgNetMessage) <= USHRT_MAX);
    init_network(&network);
    test_receive(&network);
    test_accounts_and_jobs(&network);
    test_outgoing(&network);
    test_folder_queue(&network);
    CHECK(forbid_depth == 0 && queued == NULL && response == NULL);
    CHECK(wake_count > 0U);
    printf("Progress queue/context: %u checks passed (API doubles).\n", checks);
    return 0;
}
