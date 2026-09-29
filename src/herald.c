/* Optional, asynchronous Herald protocol-level-1 client.
 * RexxMsg construction/REXX node name and ReadArgs quoting follow the
 * HeraldSend client by Marcus Gerards (2026), used under the Developer/
 * HeraldSend exception. See docs/HERALD_CLIENT_LICENSE.txt.
 * No Herald binary, interpreter, Shell command or private server ABI used.
 */
#include "herald.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int amg_herald_build_notify(size_t account_slot, int test,
                            const char *text_local, char *command,
                            size_t capacity)
{
    size_t used, i;
    int length;
    if (command && capacity) command[0] = 0;
    if (!text_local || !command || !capacity ||
        account_slot >= AMG_MAX_ACCOUNTS ||
        strlen(text_local) > AMG_HERALD_TEXT_MAX)
        return AMG_ERR_ARGUMENT;
    command[0] = 0;
    /* Test has its own ID AND group; it must not replace real new-mail cards.
     * TEXT must be last. NOSOUND leaves the existing AmiMAIL sound setting
     * independent and avoids two simultaneous notification samples. */
    length = snprintf(command, capacity,
        "NOTIFY APP=AmiMAIL ID=%s%lu GROUP=%s%lu UPDATE PRI=0 NOSOUND "
        "TITLE=\"AmiMAIL\" TEXT=\"",
        test ? "test" : "mail", (unsigned long)account_slot,
        test ? "test" : "mail", (unsigned long)account_slot);
    if (length < 0 || (size_t)length >= capacity) {
        command[0] = 0;
        return AMG_ERR_LIMIT;
    }
    used = (size_t)length;
    for (i = 0U; text_local[i]; ++i) {
        unsigned char c = (unsigned char)text_local[i];
        size_t needed;
        if (c < 32U || c == 127U) c = ' ';
        needed = (c == '"' || c == '*') ? 2U : 1U;
        if (capacity - used < needed + 2U) {
            command[0] = 0;
            return AMG_ERR_LIMIT;
        }
        if (needed == 2U) command[used++] = '*';
        command[used++] = (char)c;
    }
    command[used++] = '"';
    command[used] = 0;
    return AMG_OK;
}

#if AMIGMAIL_AMIGA
#include <devices/timer.h>
#include <exec/io.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/ports.h>
#include <rexx/storage.h>
#include <rexx/errors.h>
#include <proto/exec.h>
#ifndef __NOLIBBASE__
#define __NOLIBBASE__
#define AMG_HERALD_RESTORE_LIBBASE
#endif
#include <proto/rexxsyslib.h>
#ifdef AMG_HERALD_RESTORE_LIBBASE
#undef __NOLIBBASE__
#undef AMG_HERALD_RESTORE_LIBBASE
#endif

#define HERALD_REPLY_SECONDS 5UL
/* No more than five NOTIFY commands per ten seconds (Herald flood control).
 * Two seconds AFTER each completed attempt is deliberately conservative. */
#define HERALD_PACE_SECONDS 2UL
#define HERALD_QUEUE_SIZE (AMG_MAX_ACCOUNTS + 1U)
#define HERALD_TEST_SLOT AMG_MAX_ACCOUNTS

enum HeraldStage { HERALD_IDLE, HERALD_REGISTER, HERALD_NOTIFY };
enum HeraldTimer { HERALD_NO_TIMER, HERALD_REPLY_TIMER, HERALD_PACE_TIMER };

typedef struct HeraldNotice {
    int valid;
    int test;
    size_t slot;
    char command[AMG_HERALD_COMMAND_MAX];
} HeraldNotice;

struct AmgHerald {
    struct RxsLib *rexx;
    struct MsgPort *reply_port;
    struct MsgPort *timer_port;
    struct timerequest *timer;
    struct RexxMsg *inflight;
    struct MsgPort *target;
    enum HeraldStage stage;
    enum HeraldTimer timer_kind;
    int timer_open;
    int timed_out;
    int retry;
    int active_valid;
    size_t next_slot;
    HeraldNotice active;
    HeraldNotice queued[HERALD_QUEUE_SIZE];
    AmgHeraldResult test_result;
};

int amg_herald_running(void)
{
    int running;
    Forbid();
    running = FindPort((STRPTR)"HERALD") != NULL;
    Permit();
    return running;
}

static void herald_cancel_timer(AmgHerald *client)
{
    if (!client->timer || client->timer_kind == HERALD_NO_TIMER) return;
    if (!CheckIO((struct IORequest *)client->timer))
        AbortIO((struct IORequest *)client->timer);
    /* Only our timer.device request; never wait for the Herald server. */
    WaitIO((struct IORequest *)client->timer);
    SetSignal(0UL, 1UL << client->timer_port->mp_SigBit);
    client->timer_kind = HERALD_NO_TIMER;
}

static void herald_arm_timer(AmgHerald *client, enum HeraldTimer kind,
                              ULONG seconds)
{
    herald_cancel_timer(client);
    client->timer->tr_node.io_Command = TR_ADDREQUEST;
    client->timer->tr_time.tv_secs = seconds;
    client->timer->tr_time.tv_micro = 0UL;
    client->timer_kind = kind;
    SendIO((struct IORequest *)client->timer);
}

static void herald_free_message(AmgHerald *client)
{
    struct RxsLib *RexxSysBase = client->rexx;
    struct RexxMsg *message = client->inflight;
    if (!message) return;
    /* A nonzero Result2 is an Argstring ONLY when Result1 is RC_OK. */
    if (message->rm_Result1 == RC_OK && message->rm_Result2)
        DeleteArgstring((UBYTE *)(uintptr_t)message->rm_Result2);
    if (message->rm_Args[0])
        DeleteArgstring((UBYTE *)message->rm_Args[0]);
    if (message->rm_Node.mn_Node.ln_Name)
        DeleteArgstring((UBYTE *)message->rm_Node.mn_Node.ln_Name);
    DeleteRexxMsg(message);
    client->inflight = NULL;
    client->target = NULL;
}

/* Withdraw only a message provably still in the SAME server's public queue.
 * Never remove/free a message Herald has already taken with GetMsg().
 * FindPort + queue walk + Remove are protected against task switches. */
static int herald_withdraw_queued(AmgHerald *client)
{
    struct MsgPort *port;
    struct Node *node;
    int found = 0;
    if (!client->inflight) return 1;
    Forbid();
    port = FindPort((STRPTR)"HERALD");
    if (port && port == client->target) {
        for (node = port->mp_MsgList.lh_Head;
             node && node->ln_Succ; node = node->ln_Succ) {
            if (node == &client->inflight->rm_Node.mn_Node) {
                Remove(node);
                found = 1;
                break;
            }
        }
    }
    Permit();
    return found;
}

static AmgHeraldResult herald_send(AmgHerald *client, const char *command)
{
    struct RxsLib *RexxSysBase = client->rexx;
    struct RexxMsg *message;
    struct MsgPort *target;
    if (client->inflight) return AMG_HERALD_BUSY;
    message = CreateRexxMsg(client->reply_port, NULL, NULL);
    if (!message) return AMG_HERALD_UNAVAILABLE;
    client->inflight = message;
    message->rm_Result1 = RC_OK;
    message->rm_Result2 = 0;
    message->rm_Args[0] = (STRPTR)CreateArgstring((STRPTR)command,
                                                 (ULONG)strlen(command));
    /* Do not point at AmiMAIL's code/data segment: even an exceptionally
     * late reply after shutdown must still see a valid "REXX" name. */
    message->rm_Node.mn_Node.ln_Name = (char *)CreateArgstring(
        (STRPTR)"REXX", 4UL);
    if (!message->rm_Args[0] || !message->rm_Node.mn_Node.ln_Name) {
        herald_free_message(client);
        return AMG_HERALD_UNAVAILABLE;
    }
    message->rm_Action = RXCOMM | RXFF_RESULT;
    Forbid();
    target = FindPort((STRPTR)"HERALD");
    if (target) PutMsg(target, (struct Message *)message);
    Permit();
    if (!target) {
        herald_free_message(client);
        return AMG_HERALD_NOT_RUNNING;
    }
    client->target = target;
    herald_arm_timer(client, HERALD_REPLY_TIMER, HERALD_REPLY_SECONDS);
    return AMG_HERALD_QUEUED;
}

static void herald_clear_queue(AmgHerald *client, AmgHeraldResult reason)
{
    if (client->queued[HERALD_TEST_SLOT].valid)
        client->test_result = reason;
    memset(client->queued, 0, sizeof(client->queued));
}

static void herald_finish(AmgHerald *client, AmgHeraldResult result)
{
    if (client->active_valid && client->active.test)
        client->test_result = result;
    client->active_valid = 0;
    client->stage = HERALD_IDLE;
    if (result == AMG_HERALD_NOT_RUNNING)
        herald_clear_queue(client, result);
    herald_arm_timer(client, HERALD_PACE_TIMER, HERALD_PACE_SECONDS);
}

static void herald_start_next(AmgHerald *client)
{
    size_t i;
    AmgHeraldResult result;
    if (client->inflight || client->active_valid ||
        client->timer_kind != HERALD_NO_TIMER || client->timed_out)
        return;
    for (i = 0U; i < HERALD_QUEUE_SIZE; ++i) {
        size_t slot = (client->next_slot + i) % HERALD_QUEUE_SIZE;
        if (!client->queued[slot].valid) continue;
        client->active = client->queued[slot];
        client->queued[slot].valid = 0;
        client->active_valid = 1;
        client->next_slot = (slot + 1U) % HERALD_QUEUE_SIZE;
        client->stage = HERALD_REGISTER;
        client->retry = 0;
        /* Register for each batch: handles Herald restarting while AmiMAIL
         * stays open, even if its new port reuses the same address. */
        result = herald_send(client, "REGISTERAPP APP=AmiMAIL");
        if (result != AMG_HERALD_QUEUED) herald_finish(client, result);
        break;
    }
}

AmgHerald *amg_herald_create(void)
{
    AmgHerald *client = (AmgHerald *)calloc(1U, sizeof(*client));
    if (!client) return NULL;
    client->rexx = (struct RxsLib *)OpenLibrary(
        (CONST_STRPTR)"rexxsyslib.library", 36UL);
    if (!client->rexx) goto failed;
    client->reply_port = CreateMsgPort();
    if (!client->reply_port) goto failed;
    client->timer_port = CreateMsgPort();
    if (!client->timer_port) goto failed;
    client->timer = (struct timerequest *)CreateIORequest(
        client->timer_port, sizeof(*client->timer));
    if (!client->timer) goto failed;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK,
                   (struct IORequest *)client->timer, 0UL) != 0)
        goto failed;
    client->timer_open = 1;
    return client;
failed:
    amg_herald_destroy(client);
    return NULL;
}

unsigned long amg_herald_signal_mask(const AmgHerald *client)
{
    if (!client) return 0UL;
    return (client->reply_port ? 1UL << client->reply_port->mp_SigBit : 0UL) |
           (client->timer_port ? 1UL << client->timer_port->mp_SigBit : 0UL);
}

void amg_herald_poll(AmgHerald *client)
{
    struct Message *reply;
    if (!client || !client->reply_port) return;
    while ((reply = GetMsg(client->reply_port)) != NULL) {
        struct RexxMsg *message = client->inflight;
        int ok, already, not_registered;
        AmgHeraldResult result;
        const char *text;
        if (reply != (struct Message *)message) {
            /* Private port: unsolicited messages are never ours to free. */
            if (reply->mn_Node.ln_Type != NT_REPLYMSG) ReplyMsg(reply);
            continue;
        }
        text = message->rm_Result1 == RC_OK && message->rm_Result2
            ? (const char *)(uintptr_t)message->rm_Result2 : "";
        ok = !strcmp(text, "OK") || !strncmp(text, "OK,", 3U);
        already = !strcmp(text, "ERROR: APP ALREADY REGISTERED");
        not_registered = !strcmp(text, "ERROR: APP NOT REGISTERED");
        herald_cancel_timer(client);
        herald_free_message(client);
        if (client->timed_out || !client->active_valid ||
            !client->active.valid) {
            client->timed_out = 0;
            herald_finish(client, AMG_HERALD_TIMEOUT);
            continue;
        }
        if (client->stage == HERALD_REGISTER && (ok || already)) {
            client->stage = HERALD_NOTIFY;
            result = herald_send(client, client->active.command);
            if (result != AMG_HERALD_QUEUED) herald_finish(client, result);
        } else if (client->stage == HERALD_NOTIFY && not_registered &&
                   !client->retry) {
            client->retry = 1;
            client->stage = HERALD_REGISTER;
            result = herald_send(client, "REGISTERAPP APP=AmiMAIL");
            if (result != AMG_HERALD_QUEUED) herald_finish(client, result);
        } else {
            herald_finish(client, ok && client->stage == HERALD_NOTIFY
                ? AMG_HERALD_ACCEPTED : AMG_HERALD_REJECTED);
        }
    }
    if (client->timer_kind != HERALD_NO_TIMER &&
        CheckIO((struct IORequest *)client->timer)) {
        enum HeraldTimer kind = client->timer_kind;
        herald_cancel_timer(client);
        if (kind == HERALD_REPLY_TIMER && client->inflight) {
            if (client->active_valid && client->active.test)
                client->test_result = AMG_HERALD_TIMEOUT;
            herald_clear_queue(client, AMG_HERALD_TIMEOUT);
            if (herald_withdraw_queued(client)) {
                herald_free_message(client);
                herald_finish(client, AMG_HERALD_TIMEOUT);
            } else {
                /* Herald owns it. One outstanding message is the hard bound;
                 * never accumulate messages or free borrowed memory on timeout.
                 * A late reply is consumed above without sending a stale notice. */
                client->timed_out = 1;
            }
        }
    }
    herald_start_next(client);
}

AmgHeraldResult amg_herald_notify(AmgHerald *client, size_t account_slot,
                                 const char *text_local, int test)
{
    HeraldNotice notice;
    size_t slot;
    if (!client) return AMG_HERALD_UNAVAILABLE;
    if (account_slot >= AMG_MAX_ACCOUNTS) return AMG_HERALD_REJECTED;
    amg_herald_poll(client);
    if (client->timed_out) return AMG_HERALD_BUSY;
    if (!amg_herald_running()) return AMG_HERALD_NOT_RUNNING;
    if (test && (client->queued[HERALD_TEST_SLOT].valid ||
                 (client->active_valid && client->active.test)))
        return AMG_HERALD_BUSY;
    memset(&notice, 0, sizeof(notice));
    if (amg_herald_build_notify(account_slot, test, text_local, notice.command,
                                sizeof(notice.command)) != AMG_OK)
        return AMG_HERALD_REJECTED;
    notice.valid = 1;
    notice.test = test ? 1 : 0;
    notice.slot = account_slot;
    slot = test ? HERALD_TEST_SLOT : account_slot;
    /* Only the latest still-unsent batch for a slot is relevant. No history
     * replay when Herald was absent and no unbounded notification queue. */
    client->queued[slot] = notice;
    if (test) client->test_result = AMG_HERALD_QUEUED;
    herald_start_next(client);
    return test ? client->test_result : AMG_HERALD_QUEUED;
}

AmgHeraldResult amg_herald_test_result(const AmgHerald *client)
{
    return client ? client->test_result : AMG_HERALD_UNAVAILABLE;
}

void amg_herald_discard_account(AmgHerald *client, size_t account_slot)
{
    if (!client || account_slot >= AMG_MAX_ACCOUNTS) return;
    client->queued[account_slot].valid = 0;
    if (client->active_valid && !client->active.test &&
        client->active.slot == account_slot &&
        client->stage == HERALD_REGISTER)
        client->active.valid = 0;
}

void amg_herald_destroy(AmgHerald *client)
{
    struct Message *reply;
    int borrowed = 0;
    if (!client) return;
    herald_cancel_timer(client);
    /* Give an already executing request at most 250 ms to come back. No new
     * commands are sent during shutdown. This avoids retaining a healthy
     * server's message merely because Quit raced its reply. */
    if (client->inflight && client->timer_open) {
        Forbid();
        reply = GetMsg(client->reply_port);
        if (reply == (struct Message *)client->inflight)
            herald_free_message(client);
        else if (reply && reply->mn_Node.ln_Type != NT_REPLYMSG)
            ReplyMsg(reply);
        if (client->inflight && herald_withdraw_queued(client))
            herald_free_message(client);
        Permit();
        if (client->inflight) {
            client->timer->tr_node.io_Command = TR_ADDREQUEST;
            client->timer->tr_time.tv_secs = 0UL;
            client->timer->tr_time.tv_micro = 250000UL;
            client->timer_kind = HERALD_REPLY_TIMER;
            SendIO((struct IORequest *)client->timer);
            while (client->inflight &&
                   !CheckIO((struct IORequest *)client->timer)) {
                (void)Wait(amg_herald_signal_mask(client));
                while ((reply = GetMsg(client->reply_port)) != NULL) {
                    if (reply == (struct Message *)client->inflight)
                        herald_free_message(client);
                    else if (reply->mn_Node.ln_Type != NT_REPLYMSG)
                        ReplyMsg(reply);
                }
            }
            herald_cancel_timer(client);
        }
    }
    /* Shutdown never sends NOTIFY/UNREGISTERAPP or waits without a deadline.
     * It is safe to reclaim replies or a request still queued at the server. */
    if (client->inflight && client->reply_port) {
        Forbid();
        while ((reply = GetMsg(client->reply_port)) != NULL) {
            if (reply == (struct Message *)client->inflight)
                herald_free_message(client);
            else if (reply->mn_Node.ln_Type != NT_REPLYMSG)
                ReplyMsg(reply);
        }
        if (client->inflight) {
            if (herald_withdraw_queued(client))
                herald_free_message(client);
            else {
                /* No cancellation handshake exists for borrowed RexxMsg.
                 * Quarantine this ONE request/port/library reference if a
                 * crashed/hung receiver still owns it. Disable signalling
                 * before freeing the GUI task's signal bit: a late ReplyMsg
                 * can safely enqueue but cannot signal an unloaded task.
                 * The retained name/arguments are separate public allocations,
                 * not pointers into our executable. See the documented rare
                 * shutdown limitation; never substitute a use-after-free. */
                client->reply_port->mp_Flags = PA_IGNORE;
                client->reply_port->mp_SigTask = NULL;
                SetSignal(0UL, 1UL << client->reply_port->mp_SigBit);
                FreeSignal((LONG)client->reply_port->mp_SigBit);
                borrowed = 1;
            }
        }
        Permit();
    }
    if (client->timer_open)
        CloseDevice((struct IORequest *)client->timer);
    if (client->timer) DeleteIORequest((struct IORequest *)client->timer);
    if (client->timer_port) DeleteMsgPort(client->timer_port);
    if (!borrowed) {
        if (client->reply_port) DeleteMsgPort(client->reply_port);
        if (client->rexx) CloseLibrary((struct Library *)client->rexx);
    }
    free(client);
}

#else
struct AmgHerald { int unused; };
int amg_herald_running(void) { return 0; }
AmgHerald *amg_herald_create(void) { return NULL; }
void amg_herald_destroy(AmgHerald *client) { (void)client; }
unsigned long amg_herald_signal_mask(const AmgHerald *client)
{ (void)client; return 0UL; }
void amg_herald_poll(AmgHerald *client) { (void)client; }
AmgHeraldResult amg_herald_notify(AmgHerald *client, size_t account_slot,
                                 const char *text_local, int test)
{
    (void)client; (void)account_slot; (void)text_local; (void)test;
    return AMG_HERALD_UNAVAILABLE;
}
AmgHeraldResult amg_herald_test_result(const AmgHerald *client)
{ (void)client; return AMG_HERALD_UNAVAILABLE; }
void amg_herald_discard_account(AmgHerald *client, size_t account_slot)
{ (void)client; (void)account_slot; }
#endif
