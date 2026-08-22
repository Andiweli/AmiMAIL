#include "gui_internal.h"
#include "i18n.h"

#include <stdio.h>
#include <string.h>

#if AMIGMAIL_AMIGA

#include <dos/dos.h>
#include <dos/var.h>
#include <proto/dos.h>

#define WINDOW_STATE_DRAWER "ENVARC:AmiMail"
#define WINDOW_STATE_PATH "ENVARC:AmiMail/window.state"
#define WINDOW_STATE_TEMP "ENVARC:AmiMail/window.state.new"
#define WINDOW_STATE_HEADER "AMIMAIL-WINDOW-2"
#define INBOX_NOTIFY_STATE_PATH "ENVARC:AmiMail/inbox-notify.state"
#define INBOX_NOTIFY_STATE_TEMP "ENVARC:AmiMail/inbox-notify.state.new"
#define INBOX_NOTIFY_STATE_HEADER "AMIMAIL-INBOX-NOTIFY-1"
#define MAIL_STATUS_VAR "AmiMAILStatus"
#define MAIL_STATUS_ENVARC_PATH "ENVARC:" MAIL_STATUS_VAR
#define T(id, en) amg_tr((id), (en))

static int mail_status_inactive_exists = 0;
static int mail_status_shutdown_saved = 0;

static unsigned long notification_account_fingerprint(const AmgAccount *account)
{
    const char *parts[3];
    unsigned long hash = 2166136261UL;
    size_t p;
    if (!account) return 0UL;
    parts[0] = account->imap_host;
    parts[1] = amg_account_imap_user(account);
    parts[2] = account->email;
    for (p = 0U; p < 3U; ++p) {
        const unsigned char *text = (const unsigned char *)parts[p];
        while (text && *text) {
            hash ^= (unsigned long)*text++;
            hash *= 16777619UL;
        }
        hash ^= 0xffUL;
        hash *= 16777619UL;
    }
    hash ^= (unsigned long)(account->imap_port & 0xffU);
    hash *= 16777619UL;
    hash ^= (unsigned long)((account->imap_port >> 8) & 0xffU);
    hash *= 16777619UL;
    return hash;
}

static void ensure_state_drawer(void)
{
    BPTR lock = Lock((STRPTR)WINDOW_STATE_DRAWER, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        return;
    }
    lock = CreateDir((STRPTR)WINDOW_STATE_DRAWER);
    if (lock) UnLock(lock);
}

static LONG clamp_long(LONG value, LONG minimum, LONG maximum)
{
    if (maximum < minimum) maximum = minimum;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

void gui_state_prepare_window(AmgGui *gui)
{
    FILE *file;
    char header[64];
    long left, top, inner_width, inner_height, outer_width, outer_height;
    LONG screen_width, screen_height;
    LONG max_left, max_top;
    if (!gui || !gui->screen) return;
    gui->window_state_valid = 0;
    file = fopen(WINDOW_STATE_PATH, "rb");
    if (!file) return;
    if (!fgets(header, sizeof(header), file) ||
        strncmp(header, WINDOW_STATE_HEADER,
                strlen(WINDOW_STATE_HEADER)) != 0 ||
        fscanf(file,
               "left=%ld\ntop=%ld\ninner_width=%ld\ninner_height=%ld\n"
               "outer_width=%ld\nouter_height=%ld\n",
               &left, &top, &inner_width, &inner_height,
               &outer_width, &outer_height) != 6) {
        fclose(file);
        return;
    }
    fclose(file);

    screen_width = (LONG)gui->screen->Width;
    screen_height = (LONG)gui->screen->Height;
    if (screen_width <= 0L || screen_height <= 0L) return;

    /* The saved inner size is the exact value expected by window.class.
     * Do not clamp it to WA_MinWidth/WA_MinHeight here: those minimums apply
     * to the complete Intuition window, including borders. Doing so would
     * add the borders a second time when restoring a minimum-sized window. */
    inner_width = clamp_long((LONG)inner_width, 1L, screen_width);
    inner_height = clamp_long((LONG)inner_height, 1L, screen_height);
    outer_width = clamp_long((LONG)outer_width, 1L, screen_width);
    outer_height = clamp_long((LONG)outer_height, 1L, screen_height);
    max_left = screen_width - (LONG)outer_width;
    max_top = screen_height - (LONG)outer_height;
    left = clamp_long((LONG)left, 0L, max_left);
    top = clamp_long((LONG)top, 0L, max_top);

    gui->saved_window_left = (LONG)left;
    gui->saved_window_top = (LONG)top;
    gui->saved_window_width = (LONG)inner_width;
    gui->saved_window_height = (LONG)inner_height;
    gui->window_state_valid = 1;
}

void gui_state_save_window(const AmgGui *gui)
{
    FILE *file;
    int write_failed = 0;
    if (!gui || !gui->window) return;
    ensure_state_drawer();
    file = fopen(WINDOW_STATE_TEMP, "wb");
    if (!file) return;
    {
        LONG inner_width = (LONG)gui->window->Width -
            (LONG)gui->window->BorderLeft - (LONG)gui->window->BorderRight;
        LONG inner_height = (LONG)gui->window->Height -
            (LONG)gui->window->BorderTop - (LONG)gui->window->BorderBottom;
        if (inner_width < 1L) inner_width = 1L;
        if (inner_height < 1L) inner_height = 1L;
        if (fprintf(file,
                    "%s\nleft=%ld\ntop=%ld\ninner_width=%ld\ninner_height=%ld\n"
                    "outer_width=%ld\nouter_height=%ld\n",
                    WINDOW_STATE_HEADER,
                    (long)gui->window->LeftEdge,
                    (long)gui->window->TopEdge,
                    (long)inner_width,
                    (long)inner_height,
                    (long)gui->window->Width,
                    (long)gui->window->Height) < 0)
            write_failed = 1;
    }
    if (fclose(file) != 0) write_failed = 1;
    if (write_failed) {
        DeleteFile((STRPTR)WINDOW_STATE_TEMP);
        return;
    }
    DeleteFile((STRPTR)WINDOW_STATE_PATH);
    if (!Rename((STRPTR)WINDOW_STATE_TEMP, (STRPTR)WINDOW_STATE_PATH))
        DeleteFile((STRPTR)WINDOW_STATE_TEMP);
}

void gui_state_load_inbox_notification(AmgGui *gui)
{
    FILE *file;
    char header[64];
    unsigned long fingerprint = 0UL;
    unsigned long uid_validity = 0UL;
    unsigned long latest_uid = 0UL;
    int ready = 0;
    unsigned long expected;

    if (!gui || !gui->account) return;
    gui->inbox_latest_uid = 0UL;
    gui->inbox_uid_validity = 0UL;
    gui->inbox_baseline_ready = 0;
    expected = notification_account_fingerprint(gui->account);
    if (!expected) return;

    file = fopen(INBOX_NOTIFY_STATE_PATH, "rb");
    if (!file) return;
    if (!fgets(header, sizeof(header), file) ||
        strncmp(header, INBOX_NOTIFY_STATE_HEADER,
                strlen(INBOX_NOTIFY_STATE_HEADER)) != 0 ||
        fscanf(file,
               "account=%lu\nready=%d\nuid_validity=%lu\nlatest_uid=%lu\n",
               &fingerprint, &ready, &uid_validity, &latest_uid) != 4 ||
        fingerprint != expected || ready != 1) {
        fclose(file);
        return;
    }
    fclose(file);
    gui->inbox_latest_uid = latest_uid;
    gui->inbox_uid_validity = uid_validity;
    gui->inbox_baseline_ready = 1;
}

void gui_state_save_inbox_notification(const AmgGui *gui)
{
    FILE *file;
    int write_failed = 0;
    unsigned long fingerprint;
    if (!gui || !gui->account || !gui->inbox_baseline_ready) return;
    fingerprint = notification_account_fingerprint(gui->account);
    if (!fingerprint) return;
    ensure_state_drawer();
    file = fopen(INBOX_NOTIFY_STATE_TEMP, "wb");
    if (!file) return;
    if (fprintf(file,
                "%s\naccount=%lu\nready=1\nuid_validity=%lu\nlatest_uid=%lu\n",
                INBOX_NOTIFY_STATE_HEADER,
                fingerprint,
                gui->inbox_uid_validity,
                gui->inbox_latest_uid) < 0)
        write_failed = 1;
    if (fclose(file) != 0) write_failed = 1;
    if (write_failed) {
        DeleteFile((STRPTR)INBOX_NOTIFY_STATE_TEMP);
        return;
    }
    DeleteFile((STRPTR)INBOX_NOTIFY_STATE_PATH);
    if (!Rename((STRPTR)INBOX_NOTIFY_STATE_TEMP,
                (STRPTR)INBOX_NOTIFY_STATE_PATH))
        DeleteFile((STRPTR)INBOX_NOTIFY_STATE_TEMP);
}

static void set_mail_status_value(const char *value)
{
    if (!value || !value[0]) return;
    (void)SetVar((STRPTR)MAIL_STATUS_VAR, (STRPTR)value,
                 -1L, GVF_GLOBAL_ONLY);
}

static void save_mail_status_inactive(void)
{
    const char *value = T(MSG_AMIMAIL_IS_NOT_ACTIVE, "AmiMail is not active");
    if (!value || !value[0]) return;
    (void)SetVar((STRPTR)MAIL_STATUS_VAR, (STRPTR)value, -1L,
                 GVF_GLOBAL_ONLY | GVF_SAVE_VAR);
    mail_status_inactive_exists = 1;
}

static void ensure_saved_inactive_status(void)
{
    BPTR lock;
    if (mail_status_inactive_exists) return;
    lock = Lock((STRPTR)MAIL_STATUS_ENVARC_PATH, ACCESS_READ);
    if (lock) {
        UnLock(lock);
        mail_status_inactive_exists = 1;
        return;
    }
    save_mail_status_inactive();
}

void gui_state_set_mail_status_active(void)
{
    /* Keep a persistent inactive fallback in ENVARC:.  Live status values
     * remain ENV:-only, so a reboot always restores the safe inactive state. */
    ensure_saved_inactive_status();
    mail_status_shutdown_saved = 0;
    set_mail_status_value(T(MSG_NO_NEW_MAIL, "No new Mail"));
}

void gui_state_set_mail_status_inactive(void)
{
    if (mail_status_shutdown_saved) {
        set_mail_status_value(T(MSG_AMIMAIL_IS_NOT_ACTIVE, "AmiMail is not active"));
        return;
    }
    save_mail_status_inactive();
    mail_status_shutdown_saved = 1;
}

static void sync_mail_status(AmgGui *gui)
{
    const char *value;
    if (!gui || !gui->inbox_unseen_known) {
        gui_state_set_mail_status_active();
        return;
    }
    if (gui->inbox_unseen_count > 0UL)
        value = T(MSG_NEW_MAIL_S_IN_INBOX, "New mail(s) in Inbox");
    else
        value = T(MSG_NO_NEW_MAIL, "No new Mail");
    set_mail_status_value(value);
}

void gui_state_set_inbox_unseen(AmgGui *gui, unsigned long count)
{
    if (!gui) return;
    gui->inbox_unseen_count = count;
    gui->inbox_unseen_known = 1;
    sync_mail_status(gui);
}

void gui_state_adjust_inbox_unseen(AmgGui *gui, long delta)
{
    unsigned long amount;
    if (!gui || delta == 0L) return;
    if (!gui->inbox_unseen_known) {
        if (delta < 0L) return;
        gui->inbox_unseen_known = 1;
        gui->inbox_unseen_count = 0UL;
    }
    if (delta > 0L) {
        amount = (unsigned long)delta;
        if (gui->inbox_unseen_count > ~0UL - amount)
            gui->inbox_unseen_count = ~0UL;
        else
            gui->inbox_unseen_count += amount;
    } else {
        amount = (unsigned long)(-delta);
        if (amount >= gui->inbox_unseen_count)
            gui->inbox_unseen_count = 0UL;
        else
            gui->inbox_unseen_count -= amount;
    }
    sync_mail_status(gui);
}

#endif /* AMIGMAIL_AMIGA */
