#include "gui_internal.h"
#include "i18n.h"

#include <errno.h>
#include <stdio.h>
#include <stdint.h>

#if AMIGMAIL_AMIGA
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <dos/var.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/nodes.h>
#include <exec/tasks.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/window.h>
#include <proto/layout.h>
#include <proto/button.h>

#define REQUESTER_LOG_PATH "T:AmiMAIL-requesters.log"
#define REQUESTER_LOG_LIMIT (256L * 1024L)
#define T(id, en) amg_tr((id), (en))

/* GUI-process diagnostics only: never call from an input/render/interrupt
 * hook. No mail text, address, account name or mailbox name is written.
 * Normal traces require: SetEnv AmiMAIL_RequesterDebug 1
 * Failures are recorded even without that switch. Logging cannot authorize
 * an operation and must not overwrite the DOS/C error of the real operation.
 *
 * MEMF_LARGEST is a diagnostic snapshot, not an allocation guarantee. Exec
 * also checks its free-list consistency while obtaining the largest block;
 * a previously corrupted memory list can therefore be detected here. */
static void requester_log(const AmgGui *gui, const char *event,
                           unsigned long value1, unsigned long value2,
                           int force)
{
    static unsigned long sequence;
    struct Task *task = FindTask(NULL);
    struct Process *process;
    APTR old_requester_window;
    LONG saved_ioerr, length;
    int saved_errno = errno;
    char enabled[8];
    char line[768];
    struct DateStamp stamp;
    ULONG signals, chip, chip_largest, fast, fast_largest;
    BPTR file = 0;
    int written;

    if (!task || task->tc_Node.ln_Type != NT_PROCESS) return;
    saved_ioerr = IoErr();
    process = (struct Process *)task;
    old_requester_window = process->pr_WindowPtr;
    /* Suppress DOS requesters for *diagnostic I/O only*. Restore exactly the
     * previous value on every path, including a missing/unwritable T: assign. */
    process->pr_WindowPtr = (APTR)(intptr_t)-1;
    if (!force) {
        length = GetVar((CONST_STRPTR)"AmiMAIL_RequesterDebug", (STRPTR)enabled,
                        (LONG)sizeof(enabled), GVF_GLOBAL_ONLY);
        if (length != 1L || enabled[0] != '1') goto done;
    }

    signals = SetSignal(0UL, 0UL); /* Observe; never clear pending signals. */
    chip = AvailMem(MEMF_CHIP);
    chip_largest = AvailMem(MEMF_CHIP | MEMF_LARGEST);
    fast = AvailMem(MEMF_FAST);
    fast_largest = AvailMem(MEMF_FAST | MEMF_LARGEST);
    DateStamp(&stamp);
    written = snprintf(line, sizeof(line),
        "AmiMAIL %s task=%08lx seq=%lu time=%ld/%ld/%ld %s "
        "v1=%lu(0x%08lx) v2=%lu(0x%08lx) account=%lu uid=%lu "
        "window=%08lx screen=%08lx pending=%08lx requester=%08lx "
        "ioerr_context=%ld chip=%lu chip_largest=%lu fast=%lu fast_largest=%lu "
        "classes=window:%u.%u,layout:%u.%u,button:%u.%u\n",
        AMIMAIL_VERSION, (unsigned long)(uintptr_t)task, ++sequence,
        (long)stamp.ds_Days, (long)stamp.ds_Minute, (long)stamp.ds_Tick,
        event ? event : "unknown", value1, value1, value2, value2,
        gui ? (unsigned long)gui->active_account + 1UL : 0UL,
        gui ? (unsigned long)gui->active_message_uid : 0UL,
        (unsigned long)(uintptr_t)(gui ? gui->window : NULL),
        (unsigned long)(uintptr_t)(gui ? gui->screen : NULL),
        (unsigned long)signals, (unsigned long)(uintptr_t)old_requester_window,
        (long)saved_ioerr, (unsigned long)chip, (unsigned long)chip_largest,
        (unsigned long)fast, (unsigned long)fast_largest,
        WindowBase ? (unsigned)WindowBase->lib_Version : 0U,
        WindowBase ? (unsigned)WindowBase->lib_Revision : 0U,
        LayoutBase ? (unsigned)LayoutBase->lib_Version : 0U,
        LayoutBase ? (unsigned)LayoutBase->lib_Revision : 0U,
        ButtonBase ? (unsigned)ButtonBase->lib_Version : 0U,
        ButtonBase ? (unsigned)ButtonBase->lib_Revision : 0U);
    if (written < 0 || (size_t)written >= sizeof(line)) goto done;

    file = Open((CONST_STRPTR)REQUESTER_LOG_PATH, MODE_READWRITE);
    if (!file) goto done;
    if (Seek(file, 0L, OFFSET_END) < 0L) goto done;
    length = Seek(file, 0L, OFFSET_CURRENT);
    /* Append, never truncate existing evidence. A full log can be copied
     * elsewhere and removed manually before the next reproduction. */
    if (length < 0L || length > REQUESTER_LOG_LIMIT - (LONG)written) goto done;
    (void)Write(file, line, (LONG)written);

done:
    if (file) Close(file);
    process->pr_WindowPtr = old_requester_window;
    SetIoErr(saved_ioerr);
    errno = saved_errno;
}

void gui_requester_trace(const AmgGui *gui, const char *event,
                         unsigned long value1, unsigned long value2)
{
    requester_log(gui, event, value1, value2, 0);
}

void gui_requester_failure(AmgGui *gui, unsigned int stage,
                           long ioerr_context)
{
    char message[192];
    if (gui) gui->confirmation_failure = stage;
    /* The immediately captured IoErr is only context: BOOPSI constructors
     * are not required to set a meaningful DOS error on failure. */
    requester_log(gui, "requester.failure", (unsigned long)stage,
                   (unsigned long)ioerr_context, 1);
    if (!gui) return;
    amg_tr_snprintf(message, sizeof(message), MSG_CONFIRMATION_OPEN_FAILED,
        "Confirmation could not be opened (R%lu). Action cancelled.",
        (unsigned long)stage);
    status_local(gui, message);
}
#endif /* AMIGMAIL_AMIGA */
