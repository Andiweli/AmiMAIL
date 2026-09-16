#include "gui_internal.h"

#if AMIGMAIL_AMIGA

#include <dos/dos.h>
#include <dos/dostags.h>
#include <exec/tasks.h>
#include <proto/dos.h>
#include <proto/exec.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define AMIMAIL_SOUNDPLAYER "C:SoundPlayer"
#define AMIMAIL_SOUND_TASK_NAME "AmiMAIL SoundPlayer"
#define AMIMAIL_SOUND_COMMAND_MAX 640U

/*
 * AmiMAIL intentionally does not own a live sound.datatype object anymore.
 * On classic AmigaOS a notification can overlap an already active AHI client
 * (for example AmigaAMP). Keeping the DataType object in the GUI task made
 * object completion/cleanup part of AmiMAIL's lifetime and exposed the main
 * process to any timing-sensitive sound.datatype/AHI teardown path.
 *
 * AmigaOS 3.2 already provides C:SoundPlayer. Delegate the whole playback
 * lifetime to that OS command in a separate process. AmiMAIL only launches
 * it and never stops or disposes an active sound object itself.
 */

static int path_is_shell_safe(const char *path)
{
    if (!path || !path[0]) return 0;
    return strchr(path, '"') == NULL &&
           strchr(path, '\r') == NULL &&
           strchr(path, '\n') == NULL;
}

static int file_exists(const char *path)
{
    BPTR lock;
    if (!path || !path[0]) return 0;
    lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    if (!lock) return 0;
    UnLock(lock);
    return 1;
}

static int notification_sound_process_active(void)
{
    struct Task *task;

    /* FindTask() walks Exec's task lists. Keep the lookup atomic; the pointer
     * is only tested for NULL and is never dereferenced after Permit(). */
    Forbid();
    task = FindTask((CONST_STRPTR)AMIMAIL_SOUND_TASK_NAME);
    Permit();
    return task != NULL;
}

static int notification_sound_play_path(AmgGui *gui, const char *path)
{
    char command[AMIMAIL_SOUND_COMMAND_MAX];
    BPTR input;
    LONG result;
    int written;

    (void)gui;
    if (!path_is_shell_safe(path)) return 0;
    if (!file_exists(path) || !file_exists(AMIMAIL_SOUNDPLAYER)) return 0;

    /* Never overlap two notification/preview sounds. If the helper process is
     * still alive, the existing sound wins and the new request is discarded.
     * This is deliberate: a notification is optional and must never compete
     * with itself or put pressure on the audio backend. */
    if (notification_sound_process_active()) return 1;

    written = snprintf(command, sizeof(command),
                       "%s QUIET \"%s\"", AMIMAIL_SOUNDPLAYER, path);
    if (written < 0 || (size_t)written >= sizeof(command)) return 0;

    /* SystemTags(SYS_Asynch) transfers ownership of the supplied input handle
     * after a successful launch. Explicit NIL: input/output keeps the detached
     * helper completely independent from AmiMAIL's Workbench/Shell streams. */
    input = Open((STRPTR)"NIL:", MODE_OLDFILE);
    if (!input) return 0;
    result = SystemTags((STRPTR)command,
                        SYS_Asynch, TRUE,
                        SYS_Input, input,
                        SYS_Output, 0L,
                        SYS_Error, 0L,
                        NP_Name, (ULONG)(uintptr_t)AMIMAIL_SOUND_TASK_NAME,
                        NP_StackSize, 16384UL,
                        TAG_DONE);
    if (result == -1) {
        if (input) Close(input);
        return 0;
    }
    return 1;
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
    (void)notification_sound_play_path(
        gui, account->notification_sound_path);
}

#endif /* AMIGMAIL_AMIGA */
