#include "gui_internal.h"

#if AMIGMAIL_AMIGA

#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/soundclass.h>
#include <devices/timer.h>
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <exec/io.h>
#include <exec/libraries.h>
#include <exec/tasks.h>
/* Keep the worker's DataTypesBase local without an extern declaration that
 * would trigger -Wshadow. Restore the caller's macro state afterwards. */
#ifndef __NOLIBBASE__
#define __NOLIBBASE__
#define AMG_SOUND_RESTORE_LIBBASE_DECLARATIONS
#endif
#include <proto/datatypes.h>
#ifdef AMG_SOUND_RESTORE_LIBBASE_DECLARATIONS
#undef __NOLIBBASE__
#undef AMG_SOUND_RESTORE_LIBBASE_DECLARATIONS
#endif
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdint.h>
#include <string.h>

#define AMIMAIL_SOUND_PATH_MAX 512U
#define AMIMAIL_SOUND_WATCHDOG_SECONDS 120UL

/* One GUI task submits all account/preview notifications. The worker alone
 * creates, plays, stops and disposes its DataType object. In particular, the
 * GUI never tears down a sound object while it is still playing.
 *
 * Do not require C:SoundPlayer: it is not a dependable installation contract
 * for our AmigaOS 3.2 target. Use the native DataTypes interface directly,
 * without a shell command or a path-dependent external executable.
 */
typedef struct NotificationSoundJob {
    struct Task *owner;
    struct Task * volatile worker;
    ULONG signal_mask;
    BYTE signal_bit;
    int active;
    volatile int startup_done;
    volatile int started;
    volatile int finished;
    volatile int stop_requested;
    char path[AMIMAIL_SOUND_PATH_MAX];
} NotificationSoundJob;

static NotificationSoundJob sound_job;

/* Called only by the GUI task, after the worker's final notification. */
static void notification_sound_reap(void)
{
    Forbid();
    if (!sound_job.active || !sound_job.finished) {
        Permit();
        return;
    }
    SetSignal(0UL, sound_job.signal_mask);
    FreeSignal(sound_job.signal_bit);
    memset(&sound_job, 0, sizeof(sound_job));
    Permit();
}

static int notification_sound_resolve_path(const char *path,
                                           char resolved[AMIMAIL_SOUND_PATH_MAX])
{
    BPTR lock;
    int result;
    if (!path || !path[0]) return 0;
    lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (!lock) return 0;
    /* Resolve relative paths/assigns before starting the worker. No quoting,
     * escaping or interpretation of the user's filename by a shell is needed.
     */
    result = NameFromLock(lock, (STRPTR)resolved,
                          (LONG)AMIMAIL_SOUND_PATH_MAX) != 0;
    UnLock(lock);
    return result;
}

static void notification_sound_worker(void)
{
    /* A function-local base is intentional. The GUI does not open or close
     * this library, and another task cannot replace it during playback. */
    struct Library *DataTypesBase = NULL;
    struct MsgPort *timer_port = NULL;
    struct timerequest *timer_request = NULL;
    Object *sound = NULL;
    struct TagItem tags[8];
    struct dtTrigger trigger;
    struct DTMethod *method;
    BYTE completion_bit = -1;
    ULONG completion_mask = 0UL, wait_mask;
    int timer_open = 0, timer_pending = 0;
    int play_requested = 0, can_play = 0;

    sound_job.worker = FindTask(NULL);
    DataTypesBase = OpenLibrary((CONST_STRPTR)"datatypes.library", 40UL);
    if (!DataTypesBase) goto done;
    completion_bit = AllocSignal(-1);
    if (completion_bit < 0) goto done;
    completion_mask = 1UL << (ULONG)completion_bit;

    /* Bound the wait for a missing completion signal after a successful
     * dispatch. This does not interrupt a DataType blocked inside a method.
     * Notification samples are one-shot sounds, not a media-player loop. */
    timer_port = CreateMsgPort();
    if (!timer_port) goto done;
    timer_request = (struct timerequest *)CreateIORequest(
        timer_port, sizeof(*timer_request));
    if (!timer_request) goto done;
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_VBLANK,
                    (struct IORequest *)timer_request, 0UL) != 0)
        goto done;
    timer_open = 1;

    tags[0].ti_Tag = DTA_SourceType; tags[0].ti_Data = DTST_FILE;
    tags[1].ti_Tag = DTA_GroupID; tags[1].ti_Data = GID_SOUND;
    tags[2].ti_Tag = SDTA_SignalTask;
    tags[2].ti_Data = (ULONG)(uintptr_t)sound_job.worker;
    /* Despite its old name SDTA_SignalBit takes a MASK, not a bit number. */
    tags[3].ti_Tag = SDTA_SignalBit; tags[3].ti_Data = completion_mask;
    tags[4].ti_Tag = SDTA_Cycles; tags[4].ti_Data = 1UL;
    tags[5].ti_Tag = SDTA_Volume; tags[5].ti_Data = 64UL;
    tags[6].ti_Tag = DTA_Repeat; tags[6].ti_Data = FALSE;
    tags[7].ti_Tag = TAG_DONE; tags[7].ti_Data = 0UL;
    sound = NewDTObjectA((APTR)sound_job.path, tags);
    if (!sound || sound_job.stop_requested) goto done;
    /* Classic NDK prototypes declare struct DTMethods * (plural), but
     * datatypesclass.h defines the returned entries as struct DTMethod.
     * Adapt that historical declaration without changing the table. */
    for (method = (struct DTMethod *)GetDTTriggerMethods(sound);
         method && method->dtm_Label; ++method) {
        if (method->dtm_Method == STM_PLAY) {
            can_play = 1;
            break;
        }
    }
    if (!can_play) goto done;

    memset(&trigger, 0, sizeof(trigger));
    trigger.MethodID = DTM_TRIGGER;
    trigger.dtt_Function = STM_PLAY;
    SetSignal(0UL, completion_mask);
    play_requested = 1;
    /* Classic DTM_TRIGGER has no portable Boolean success convention.
     * A valid object and advertised STM_PLAY allow the request; completion
     * (or cancellation/watchdog) owns its lifetime, not the trigger result.
     * In particular, do not dispose immediately on either zero or nonzero. */
    (void)DoDTMethodA(sound, NULL, NULL, (Msg)&trigger);

    timer_request->tr_node.io_Command = TR_ADDREQUEST;
    timer_request->tr_time.tv_secs = AMIMAIL_SOUND_WATCHDOG_SECONDS;
    timer_request->tr_time.tv_micro = 0UL;
    SendIO((struct IORequest *)timer_request);
    timer_pending = 1;

    Forbid();
    sound_job.started = 1;
    sound_job.startup_done = 1;
    Signal(sound_job.owner, sound_job.signal_mask);
    Permit();

    wait_mask = completion_mask | (1UL << timer_port->mp_SigBit) |
                SIGBREAKF_CTRL_C;
    if (!sound_job.stop_requested)
        (void)Wait(wait_mask);

done:
    /* Stop before disposal on cancellation/timeout as well as on completion.
     * Only this worker touches sound and only this worker frees its signal. */
    if (sound) {
        if (play_requested) {
            memset(&trigger, 0, sizeof(trigger));
            trigger.MethodID = DTM_TRIGGER;
            trigger.dtt_Function = STM_STOP;
            (void)DoDTMethodA(sound, NULL, NULL, (Msg)&trigger);
        }
        DisposeDTObject(sound);
    }
    if (timer_pending) {
        if (!CheckIO((struct IORequest *)timer_request))
            AbortIO((struct IORequest *)timer_request);
        WaitIO((struct IORequest *)timer_request);
    }
    if (timer_open) CloseDevice((struct IORequest *)timer_request);
    if (timer_request) DeleteIORequest((struct IORequest *)timer_request);
    if (timer_port) DeleteMsgPort(timer_port);
    if (completion_bit >= 0) {
        SetSignal(0UL, completion_mask);
        FreeSignal(completion_bit);
    }
    if (DataTypesBase) CloseLibrary(DataTypesBase);

    /* The parent may unload AmiMAIL as soon as it sees finished. Keep task
     * switching forbidden from the notification through the return into DOS.
     * Process termination releases this task's Forbid nesting. There must be
     * no Permit(), DOS call or blocking operation after this notification. */
    Forbid();
    sound_job.worker = NULL;
    sound_job.startup_done = 1;
    sound_job.finished = 1;
    Signal(sound_job.owner, sound_job.signal_mask);
}

static int notification_sound_play_path(AmgGui *gui, const char *path)
{
    struct Process *process;
    char resolved[AMIMAIL_SOUND_PATH_MAX];
    BYTE signal_bit;
    int started;

    if (!gui || !notification_sound_resolve_path(path, resolved)) return 0;
    notification_sound_reap();
    /* Do not restart or destroy a sample when another account checks mail.
     * The single already-playing sample wins, just as in the old design. */
    if (sound_job.active) return 1;

    signal_bit = AllocSignal(-1);
    if (signal_bit < 0) return 0;
    memset(&sound_job, 0, sizeof(sound_job));
    sound_job.owner = FindTask(NULL);
    sound_job.signal_bit = signal_bit;
    sound_job.signal_mask = 1UL << (ULONG)signal_bit;
    sound_job.active = 1;
    memcpy(sound_job.path, resolved, strlen(resolved) + 1U);
    SetSignal(0UL, sound_job.signal_mask);

    process = CreateNewProcTags(
        NP_Entry, (ULONG)(uintptr_t)notification_sound_worker,
        NP_Name, (ULONG)(uintptr_t)"AmiMAIL notification sound",
        NP_StackSize, 65536UL,
        NP_WindowPtr, (ULONG)~0UL,
        TAG_DONE);
    if (!process) {
        sound_job.finished = 1;
        notification_sound_reap();
        return 0;
    }

    /* Wait only for load/dispatch acknowledgement, NOT for sample playback.
     * Report file/DataType/capability failures to the chooser. The trigger's
     * return value cannot establish whether the audio hardware is audible. */
    while (!sound_job.startup_done)
        Wait(sound_job.signal_mask);
    started = sound_job.started;
    notification_sound_reap();
    return started;
}

void gui_notify_cleanup(void)
{
    if (!sound_job.active) return;
    /* Signal while holding Forbid so worker cannot exit between checking its
     * task pointer and Signal(). Never kill the task or dispose its object
     * from the parent. Join it before GUI libraries or program code unload. */
    Forbid();
    sound_job.stop_requested = 1;
    if (!sound_job.finished && sound_job.worker)
        Signal(sound_job.worker, SIGBREAKF_CTRL_C);
    Permit();
    while (!sound_job.finished)
        Wait(sound_job.signal_mask);
    notification_sound_reap();
}

int gui_notify_preview_sound(AmgGui *gui, const char *path)
{
    return notification_sound_play_path(gui, path);
}

void gui_notify_new_mail(AmgGui *gui)
{
    gui_notify_new_mail_for_account(gui, gui ? gui->account : NULL);
}

void gui_notify_new_mail_for_account(AmgGui *gui,
                                     const AmgAccount *account)
{
    if (!gui || !account || !account->notification_sound ||
        !account->notification_sound_path[0])
        return;
    (void)notification_sound_play_path(gui, account->notification_sound_path);
}

#endif /* AMIGMAIL_AMIGA */
