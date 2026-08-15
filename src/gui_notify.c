#include "gui_internal.h"

#if AMIGMAIL_AMIGA

#include <datatypes/datatypes.h>
#include <datatypes/datatypesclass.h>
#include <datatypes/soundclass.h>
#include <exec/tasks.h>
#include <proto/datatypes.h>
#include <proto/exec.h>
#include <utility/tagitem.h>

#include <stdint.h>

extern struct Library *DataTypesBase;

static void notification_sound_dispose(AmgGui *gui, int stop)
{
    if (!gui || !gui->notification_sound_object) return;
    if (stop) {
        struct dtTrigger trigger;
        /* Stopping is best-effort only. The normal lifetime is completion
         * signal -> DisposeDTObject(). */
        trigger.MethodID = DTM_TRIGGER;
        trigger.dtt_GInfo = NULL;
        trigger.dtt_Function = STM_STOP;
        trigger.dtt_Data = NULL;
        (void)DoDTMethodA(gui->notification_sound_object, NULL, NULL,
                          (Msg)&trigger);
    }
    DisposeDTObject(gui->notification_sound_object);
    gui->notification_sound_object = NULL;
}

int gui_notify_init(AmgGui *gui)
{
    if (!gui) return 0;
    if (gui->notification_sound_signal_bit >= 0)
        return 1;
    gui->notification_sound_signal_task = FindTask(NULL);
    gui->notification_sound_signal_bit = AllocSignal(-1);
    if (gui->notification_sound_signal_bit < 0) {
        gui->notification_sound_signal_mask = 0UL;
        gui->notification_sound_signal_task = NULL;
        return 0;
    }
    gui->notification_sound_signal_mask =
        1UL << (ULONG)gui->notification_sound_signal_bit;
    return 1;
}

void gui_notify_cleanup(AmgGui *gui)
{
    if (!gui) return;
    notification_sound_dispose(gui, 1);
    if (gui->notification_sound_signal_bit >= 0) {
        SetSignal(0UL, gui->notification_sound_signal_mask);
        FreeSignal(gui->notification_sound_signal_bit);
    }
    gui->notification_sound_signal_bit = -1;
    gui->notification_sound_signal_mask = 0UL;
    gui->notification_sound_signal_task = NULL;
}

ULONG gui_notify_signal_mask(const AmgGui *gui)
{
    return gui ? gui->notification_sound_signal_mask : 0UL;
}

void gui_notify_handle_signal(AmgGui *gui)
{
    if (!gui) return;
    notification_sound_dispose(gui, 0);
}

static int notification_sound_play_path(AmgGui *gui, const char *path)
{
    struct TagItem tags[7];
    Object *object;

    if (!gui || !path || !path[0] || !DataTypesBase) return 0;
    if (gui->notification_sound_signal_bit < 0 && !gui_notify_init(gui))
        return 0;

    /* A new playback replaces a still playing one. Clear the old completion
     * signal before starting the new object. */
    notification_sound_dispose(gui, 1);
    SetSignal(0UL, gui->notification_sound_signal_mask);

    /* Be explicit about a file source and a sound-class object. This makes
     * extension-independent IFF/8SVX/WAV decoding the responsibility of the
     * installed DataType subclass. */
    tags[0].ti_Tag = DTA_GroupID;
    tags[0].ti_Data = GID_SOUND;
    tags[1].ti_Tag = DTA_SourceType;
    tags[1].ti_Data = DTST_FILE;
    tags[2].ti_Tag = SDTA_SignalTask;
    tags[2].ti_Data =
        (ULONG)(uintptr_t)gui->notification_sound_signal_task;
    tags[3].ti_Tag = SDTA_SignalBit;
    tags[3].ti_Data = gui->notification_sound_signal_mask;
    tags[4].ti_Tag = SDTA_Volume;
    tags[4].ti_Data = 64UL;
    tags[5].ti_Tag = SDTA_Cycles;
    tags[5].ti_Data = 1UL;
    tags[6].ti_Tag = TAG_DONE;
    tags[6].ti_Data = 0UL;

    object = NewDTObjectA((APTR)path, tags);
    if (!object) return 0;

    gui->notification_sound_object = object;

    /* Some classic sound DataTypes only finish loading their sample during
     * layout. Use the same method sequence as established Amiga DataTypes
     * programs, then trigger playback with the public varargs method rather
     * than constructing a dtTrigger message by hand. */
    (void)DoDTMethod(object, NULL, NULL, DTM_PROCLAYOUT, 0, 1);
    (void)DoDTMethod(object, NULL, NULL, DTM_TRIGGER, 0, STM_PLAY, 0);
    return 1;
}

int gui_notify_preview_sound(AmgGui *gui, const char *path)
{
    return notification_sound_play_path(gui, path);
}

void gui_notify_new_mail(AmgGui *gui)
{
    if (!gui || !gui->account || !gui->account->notification_sound ||
        !gui->account->notification_sound_path[0])
        return;
    (void)notification_sound_play_path(
        gui, gui->account->notification_sound_path);
}

#endif /* AMIGMAIL_AMIGA */
