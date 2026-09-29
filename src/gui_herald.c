#include "gui_internal.h"
#include "herald.h"
#include "i18n.h"
#include "mail_notice.h"

#include <stdio.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#define T(id, en) amg_tr((id), (en))

static void herald_account_name(const char *account_name,
                                  char name[81])
{
    const char *source = account_name && account_name[0]
        ? account_name : T(MSG_ACCOUNT, "Account");
    size_t i;
    /* The fallback is an account label, never the configured email address.
     * Test cards can use the full label; new-mail cards reserve subject space. */
    snprintf(name, 81U, "%.80s", source);
    for (i = 0U; name[i]; ++i) {
        unsigned char c = (unsigned char)name[i];
        if (c < 32U || c == 127U || c == '\\') name[i] = ' ';
    }
}

void gui_herald_new_mail(AmgGui *gui, size_t account_slot,
                         unsigned long count, const unsigned char *payload,
                         size_t payload_length, unsigned long previous_uid)
{
    const AmgAccount *account;
    char text[AMG_HERALD_TEXT_MAX + 1U], name[81], short_name[33];
    char subject[AMG_HERALD_TEXT_MAX + 1U];
    const char *subject_label;
    size_t used, label_length;
    if (!gui || !gui->account_set || !count ||
        account_slot >= AMG_MAX_ACCOUNTS) return;
    account = &gui->account_set->accounts[account_slot];
    if (!account->enabled || !account->herald_notifications ||
        !amg_herald_running()) return;
    if (!gui->herald) gui->herald = amg_herald_create();
    if (!gui->herald) return; /* Optional service: no modal error/status noise. */
    herald_account_name(account->account_name, name);
    amg_mail_notice_clip(name, short_name, sizeof(short_name));
    if (count == 1UL)
        amg_tr_snprintf(text, sizeof(text), MSG_HERALD_ONE_NEW_MESSAGE,
                        "1 new message - %s", short_name);
    else
        amg_tr_snprintf(text, sizeof(text), MSG_HERALD_NEW_MESSAGES,
                        "%lu new messages - %s", count, short_name);
    amg_mail_notice_clip(T(MSG_NO_SUBJECT, "(No subject)"),
                          subject, sizeof(subject));
    (void)amg_mail_notice_subject(payload, payload_length, previous_uid,
        T(MSG_NO_SUBJECT, "(No subject)"), subject, sizeof(subject));
    subject_label = count == 1UL
        ? T(MSG_HERALD_SUBJECT_LABEL, "Subject: ")
        : T(MSG_HERALD_LATEST_SUBJECT_LABEL, "Latest: ");
    used = strlen(text);
    label_length = strlen(subject_label);
    /* Herald expands the literal backslash+n into a line break. Untrusted
     * account/subject text has already had all backslashes neutralized. */
    if (used + 2U + label_length + 4U < sizeof(text)) {
        text[used++] = '\\';
        text[used++] = 'n';
        memcpy(text + used, subject_label, label_length);
        used += label_length;
        amg_mail_notice_clip(subject, text + used, sizeof(text) - used);
    }
    (void)amg_herald_notify(gui->herald, account_slot, text, 0);
}

AmgHeraldResult gui_herald_test(AmgGui *gui, size_t account_slot,
                                const char *account_name)
{
    char text[AMG_HERALD_TEXT_MAX + 1U], name[81];
    if (!gui || account_slot >= AMG_MAX_ACCOUNTS)
        return AMG_HERALD_UNAVAILABLE;
    if (!amg_herald_running()) return AMG_HERALD_NOT_RUNNING;
    if (!gui->herald) gui->herald = amg_herald_create();
    if (!gui->herald) return AMG_HERALD_UNAVAILABLE;
    herald_account_name(account_name, name);
    amg_tr_snprintf(text, sizeof(text), MSG_HERALD_TEST_TEXT,
                    "Test notification - %s", name);
    return amg_herald_notify(gui->herald, account_slot, text, 1);
}

const char *gui_herald_result_text(AmgHeraldResult result)
{
    switch (result) {
        case AMG_HERALD_QUEUED:
            return T(MSG_HERALD_TEST_QUEUED, "Herald test queued...");
        case AMG_HERALD_ACCEPTED:
            return T(MSG_HERALD_TEST_SENT,
                     "Herald accepted the test notification.");
        case AMG_HERALD_NOT_RUNNING:
            return T(MSG_HERALD_NOT_RUNNING, "Herald is not running.");
        case AMG_HERALD_TIMEOUT:
            return T(MSG_HERALD_TEST_TIMEOUT,
                     "Herald did not reply; delivery is unknown.");
        case AMG_HERALD_BUSY:
            return T(MSG_HERALD_BUSY,
                     "Herald is still processing a previous request.");
        case AMG_HERALD_UNAVAILABLE:
            return T(MSG_HERALD_UNAVAILABLE,
                     "Herald support is unavailable (rexxsyslib/timer).");
        default:
            return T(MSG_HERALD_TEST_FAILED,
                     "Herald could not accept the test notification.");
    }
}
#endif
