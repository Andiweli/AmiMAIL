#define _POSIX_C_SOURCE 200809L
#include "notify_native_double.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "../src/gui_notify.c"

/* The production worker actually runs on a host thread in these tests. The
 * harness models Exec signals and task termination; it does not play audio. */
static struct Task parent_task, child_task;
static _Thread_local struct Task *current_task;
static _Thread_local int forbid_count;
static pthread_mutex_t scheduler;
static pthread_t child_thread;
static int thread_exists;
static void (*entry)(void);
static struct Process child_process;
static struct Library datatype_base;
static Object sound_object;
static struct MsgPort timer_port_object;
static struct timerequest timer_request_object;
static struct Task *completion_task;
static ULONG completion_mask_seen;
static const char *resolved_path;
static int fail_step;
static ULONG trigger_result;
static int play_calls, stop_calls, dispose_calls, library_closed;
static int signals_allocated, signals_freed, ports_created, ports_deleted;
static int requests_created, requests_deleted, device_opened, device_closed;
static int child_created, source_locks;
static char received_path[512];

enum { FAIL_NONE, FAIL_PATH, FAIL_RESOLVE, FAIL_PARENT_SIGNAL, FAIL_CREATE,
 FAIL_LIBRARY, FAIL_CHILD_SIGNAL, FAIL_PORT, FAIL_REQUEST, FAIL_DEVICE,
 FAIL_DATATYPE, FAIL_METHODS_NULL, FAIL_NOT_PLAYABLE };

static void task_init(struct Task *task, int id)
{
    memset(task, 0, sizeof(*task));
    assert(pthread_mutex_init(&task->lock, NULL) == 0);
    assert(pthread_cond_init(&task->condition, NULL) == 0);
    task->id = id;
}

void Forbid(void)
{
    if (!forbid_count) assert(pthread_mutex_lock(&scheduler) == 0);
    ++forbid_count;
}
void Permit(void)
{
    assert(forbid_count > 0);
    if (--forbid_count == 0) assert(pthread_mutex_unlock(&scheduler) == 0);
}
struct Task *FindTask(CONST_STRPTR name)
{
    assert(name == NULL);
    return current_task;
}
void Signal(struct Task *task, ULONG mask)
{
    assert(task != NULL && mask != 0UL);
    assert(pthread_mutex_lock(&task->lock) == 0);
    task->signals |= mask;
    assert(pthread_cond_broadcast(&task->condition) == 0);
    assert(pthread_mutex_unlock(&task->lock) == 0);
}
ULONG SetSignal(ULONG value, ULONG mask)
{
    ULONG old;
    assert(pthread_mutex_lock(&current_task->lock) == 0);
    old = current_task->signals;
    current_task->signals = (old & ~mask) | (value & mask);
    assert(pthread_mutex_unlock(&current_task->lock) == 0);
    return old;
}
ULONG Wait(ULONG mask)
{
    ULONG result;
    struct timespec deadline;
    assert(!forbid_count);
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 5; /* Fail instead of hanging when a signal is lost. */
    assert(pthread_mutex_lock(&current_task->lock) == 0);
    while (!(current_task->signals & mask))
        assert(pthread_cond_timedwait(&current_task->condition,
                                    &current_task->lock, &deadline) == 0);
    result = current_task->signals & mask;
    current_task->signals &= ~mask;
    assert(pthread_mutex_unlock(&current_task->lock) == 0);
    /* Returning a final worker acknowledgement cannot schedule the parent
     * before the simulated DOS process-exit trampoline releases Forbid. */
    Forbid(); Permit();
    return result;
}
BYTE AllocSignal(LONG bit)
{
    assert(bit == -1);
    if (fail_step == (current_task == &parent_task
                    ? FAIL_PARENT_SIGNAL : FAIL_CHILD_SIGNAL)) return -1;
    ++signals_allocated;
    return current_task == &parent_task ? 4 : 5;
}
void FreeSignal(LONG bit)
{
    assert(bit == (current_task == &parent_task ? 4 : 5));
    ++signals_freed;
}
BPTR Lock(CONST_STRPTR path, LONG mode)
{
    assert(path && mode == ACCESS_READ && current_task == &parent_task);
    ++source_locks;
    return fail_step == FAIL_PATH ? 0UL : 17UL;
}
void UnLock(BPTR lock) { assert(lock == 17UL); }
LONG NameFromLock(BPTR lock, STRPTR path, LONG length)
{
    assert(lock == 17UL && length == 512L);
    if (fail_step == FAIL_RESOLVE) return 0;
    assert(strlen(resolved_path) < (size_t)length);
    strcpy((char *)path, resolved_path);
    return 1;
}
struct Library *OpenLibrary(CONST_STRPTR name, ULONG version)
{
    assert(current_task == &child_task);
    assert(!strcmp((const char *)name, "datatypes.library") && version == 40UL);
    return fail_step == FAIL_LIBRARY ? NULL : &datatype_base;
}
void CloseLibrary(struct Library *base)
{
    assert(current_task == &child_task && base == &datatype_base);
    ++library_closed;
}
struct MsgPort *CreateMsgPort(void)
{
    if (fail_step == FAIL_PORT) return NULL;
    ++ports_created;
    timer_port_object.mp_SigTask = &child_task;
    timer_port_object.mp_SigBit = 6;
    return &timer_port_object;
}
void DeleteMsgPort(struct MsgPort *port)
{
    assert(port == &timer_port_object);
    ++ports_deleted;
}
struct IORequest *CreateIORequest(struct MsgPort *port, ULONG size)
{
    assert(port == &timer_port_object && size == sizeof(struct timerequest));
    if (fail_step == FAIL_REQUEST) return NULL;
    ++requests_created;
    memset(&timer_request_object, 0, sizeof(timer_request_object));
    timer_request_object.tr_node.reply = port;
    return &timer_request_object.tr_node;
}
void DeleteIORequest(struct IORequest *request)
{
    assert(request == &timer_request_object.tr_node);
    ++requests_deleted;
}
BYTE OpenDevice(CONST_STRPTR name, ULONG unit, struct IORequest *io, ULONG flags)
{
    assert(!strcmp((const char *)name, TIMERNAME));
    assert(unit == UNIT_VBLANK && flags == 0 && io == &timer_request_object.tr_node);
    if (fail_step == FAIL_DEVICE) return -1;
    ++device_opened;
    return 0;
}
void CloseDevice(struct IORequest *io)
{
    assert(io == &timer_request_object.tr_node);
    ++device_closed;
}
void SendIO(struct IORequest *io)
{
    assert(io->io_Command == TR_ADDREQUEST);
    assert(timer_request_object.tr_time.tv_secs == 120UL);
}
struct IORequest *CheckIO(struct IORequest *io) { return io->complete ? io : NULL; }
LONG AbortIO(struct IORequest *io) { io->complete = 1; return 0; }
LONG WaitIO(struct IORequest *io) { assert(io->complete); return 0; }

static void *process_entry(void *context)
{
    (void)context;
    current_task = &child_task;
    entry();
    assert(forbid_count == 1); /* Production's final handoff is protected. */
    Permit();                 /* DOS process termination, not application code. */
    return NULL;
}
struct Process *CreateNewProcTags(ULONG tag, ...)
{
    va_list args;
    ULONG stack = 0, window = 0;
    va_start(args, tag);
    while (tag != TAG_DONE) {
        ULONG data = va_arg(args, ULONG);
        if (tag == NP_Entry) entry = (void (*)(void))(uintptr_t)data;
        else if (tag == NP_Name)
            assert(!strcmp((const char *)(uintptr_t)data, "AmiMAIL notification sound"));
        else if (tag == NP_StackSize) stack = data;
        else if (tag == NP_WindowPtr) window = data;
        else assert(0);
        tag = va_arg(args, ULONG);
    }
    va_end(args);
    assert(entry && stack >= 16384UL && window == ~0UL);
    if (fail_step == FAIL_CREATE) return NULL;
    assert(!thread_exists);
    ++child_created;
    assert(pthread_create(&child_thread, NULL, process_entry, NULL) == 0);
    thread_exists = 1;
    return &child_process;
}
void test_base(struct Library *base) { assert(base == &datatype_base); }
Object *test_NewDTObjectA(APTR path, const struct TagItem *tags)
{
    size_t i;
    int group = 0, one_shot = 0;
    assert(current_task == &child_task);
    strcpy(received_path, (const char *)path);
    for (i = 0; tags[i].ti_Tag != TAG_DONE; ++i) {
        if (tags[i].ti_Tag == SDTA_SignalTask)
            completion_task = (struct Task *)(uintptr_t)tags[i].ti_Data;
        if (tags[i].ti_Tag == SDTA_SignalBit)
            completion_mask_seen = tags[i].ti_Data;
        if (tags[i].ti_Tag == DTA_GroupID) group = tags[i].ti_Data == GID_SOUND;
        if (tags[i].ti_Tag == SDTA_Cycles) one_shot = tags[i].ti_Data == 1UL;
        if (tags[i].ti_Tag == DTA_Repeat) assert(!tags[i].ti_Data);
    }
    assert(completion_task == &child_task && completion_mask_seen == (1UL << 5));
    assert(group && one_shot);
    if (fail_step == FAIL_DATATYPE) return NULL;
    sound_object.live = 1;
    return &sound_object;
}
struct DTMethod *test_GetDTTriggerMethods(Object *object)
{
    static struct DTMethod playable[] = {
        {"Play", "PLAY", STM_PLAY}, {NULL, NULL, 0UL}
    };
    static struct DTMethod empty[] = {{NULL, NULL, 0UL}};
    assert(current_task == &child_task && object == &sound_object);
    if (fail_step == FAIL_METHODS_NULL) return NULL;
    if (fail_step == FAIL_NOT_PLAYABLE) return empty;
    return playable;
}
ULONG test_DoDTMethodA(Object *obj, void *window, void *requester, Msg msg)
{
    const struct dtTrigger *trigger = (const struct dtTrigger *)msg;
    assert(current_task == &child_task);
    assert(obj == &sound_object && obj->live && !window && !requester);
    assert(trigger->MethodID == DTM_TRIGGER && !trigger->dtt_GInfo && !trigger->dtt_Data);
    if (trigger->dtt_Function == STM_PLAY) {
        ++play_calls;
        return trigger_result;
    }
    assert(trigger->dtt_Function == STM_STOP);
    ++stop_calls;
    return 0;
}
void test_DisposeDTObject(Object *object)
{
    assert(current_task == &child_task && object == &sound_object && object->live);
    if (play_calls) assert(stop_calls == 1);
    ++dispose_calls;
    object->live = 0;
}
static void join_child(void)
{
    if (thread_exists) {
        assert(pthread_join(child_thread, NULL) == 0);
        thread_exists = 0;
    }
}
static void finish(void)
{
    gui_notify_cleanup();
    join_child();
    assert(!sound_job.active && !sound_job.worker);
    assert(signals_allocated == signals_freed);
    assert(ports_created == ports_deleted);
    assert(requests_created == requests_deleted);
    assert(device_opened == device_closed);
}
static void reset_test(int failure)
{
    finish();
    fail_step = failure;
    trigger_result = 0UL;
    parent_task.signals = child_task.signals = 0;
    play_calls = stop_calls = dispose_calls = library_closed = 0;
    signals_allocated = signals_freed = ports_created = ports_deleted = 0;
    requests_created = requests_deleted = device_opened = device_closed = 0;
    child_created = source_locks = 0;
    completion_task = NULL; completion_mask_seen = 0;
    resolved_path = "Work:Sounds/New mail.8svx";
    received_path[0] = 0;
}
int main(void)
{
    AmgAccount account;
    AmgGui gui;
    int failure;
    assert(pthread_mutex_init(&scheduler, NULL) == 0);
    task_init(&parent_task, 1); task_init(&child_task, 2);
    current_task = &parent_task;
    memset(&account, 0, sizeof(account));
    gui.account = &account;

    reset_test(FAIL_NONE);
    assert(!gui_notify_preview_sound(&gui, NULL));
    assert(!gui_notify_preview_sound(&gui, ""));
    assert(child_created == 0);
    assert(gui_notify_preview_sound(&gui, "sounds/new mail.8svx"));
    assert(sound_job.active && !sound_job.finished);
    assert(play_calls == 1 && dispose_calls == 0);
    assert(!strcmp(received_path, resolved_path));
    assert(gui_notify_preview_sound(&gui, "other.wav"));
    assert(child_created == 1); /* No overlapping preview/notification. */
    Signal(completion_task, completion_mask_seen);
    join_child();
    finish();
    assert(stop_calls == 1 && dispose_calls == 1 && library_closed == 1);

    reset_test(FAIL_NONE);
    resolved_path = "Work:Sounds/Gr\374\337e *\"$` mail.wav";
    assert(gui_notify_preview_sound(&gui, "odd name.wav"));
    assert(!strcmp(received_path, resolved_path)); /* No shell interpretation. */
    finish(); /* Close AmiMAIL during playback: join + orderly stop. */
    assert(stop_calls == 1 && dispose_calls == 1);

    reset_test(FAIL_NONE);
    assert(gui_notify_preview_sound(&gui, "sample.wav"));
    Signal(&child_task, 1UL << timer_port_object.mp_SigBit); /* Watchdog */
    join_child(); finish();
    assert(stop_calls == 1 && dispose_calls == 1);

    for (failure = FAIL_PATH; failure <= FAIL_NOT_PLAYABLE; ++failure) {
        reset_test(failure);
        assert(!gui_notify_preview_sound(&gui, "sample.wav"));
        finish();
    }

    /* The classic DTM_TRIGGER return is not a Boolean success contract.
     * Both conventions must keep the object alive until real completion. */
    reset_test(FAIL_NONE);
    trigger_result = 1UL;
    assert(gui_notify_preview_sound(&gui, "sample.8svx"));
    assert(!sound_job.finished && dispose_calls == 0);
    Signal(completion_task, completion_mask_seen);
    join_child(); finish();
    assert(stop_calls == 1 && dispose_calls == 1);

    reset_test(FAIL_NONE);
    strcpy(account.notification_sound_path, "Work:Sounds/new.wav");
    gui_notify_new_mail(&gui);
    assert(!child_created); /* Disabled account setting is respected. */
    account.notification_sound = 1;
    gui_notify_new_mail(&gui);
    assert(child_created == 1 && play_calls == 1);
    gui_notify_new_mail_for_account(&gui, &account);
    assert(child_created == 1);
    finish();
    /* A completed job must not suppress later notifications. */
    reset_test(FAIL_NONE);
    gui_notify_new_mail(&gui);
    assert(child_created == 1);
    finish();
    puts("Sound lifecycle/start/failure/overlap/watchdog/cleanup tests passed.");
    return 0;
}
