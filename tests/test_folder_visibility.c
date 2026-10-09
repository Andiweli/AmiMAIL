/* Native folder payload mapping, sorting and pruning. UI/encoding doubles
 * are explicit; account host detection and all mapped C functions are real. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "account.h"
#include "imap.h"
#include "catalog_ids.h"

#define GUI_SYSTEM_LABEL_COUNT 7U
#define T(id, en) amg_tr((id), (en))
#include "folder_types.inc"

typedef struct AmgGui {
    AmgAccount *account;
    GuiLabel labels[AMIGMAIL_MAX_LABELS];
    size_t label_count;
} AmgGui;

static unsigned checks, rebuilt, loaded;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "line %d: %s\n", __LINE__, #x); abort(); } } while (0)
static const char *amg_tr(long id, const char *text) { (void)id; return text; }
static void utf8_to_local_copy(const char *text, char *out, size_t capacity)
{ snprintf(out, capacity, "%s", text); }
static void load_label_expansion_state(AmgGui *gui) { CHECK(gui); ++loaded; }
void rebuild_label_lists(AmgGui *gui);
void rebuild_label_lists(AmgGui *gui) { CHECK(gui); ++rebuilt; }
size_t update_labels_from_payload(AmgGui *gui, const unsigned char *payload, size_t length);
#include "folder_production.inc"

static GuiLabel *find_label(AmgGui *gui, const char *mailbox)
{
    size_t i;
    for (i = 0; i < gui->label_count; ++i)
        if (!strcmp(gui->labels[i].mailbox_utf8, mailbox)) return &gui->labels[i];
    return NULL;
}

static void test_google_children(void)
{
    AmgAccount account;
    AmgGui gui;
    char payload[2048];
    const char *hosts[] = {"imap.gmail.com", "imap.googlemail.com", "IMAP.GMAIL.COM"};
    size_t h;
    snprintf(payload, sizeof(payload),
        "0\t0\t/\t[Gmail]\n0\t0\t/\t[Google Mail]\n"
        "%lu\t1\t/\tINBOX\n%lu\t1\t/\t[Gmail]/Sent Mail\n"
        "%lu\t1\t/\t[Gmail]/All Mail\n"
        "0\t1\t/\t[Gmail]/Important\n0\t1\t/\t[Gmail]/Important/Work\n"
        "0\t1\t/\t[Google Mail]/Other\n0\t1\t/\tProjects\n"
        "0\t1\t/\tProjects/Client\n", AMG_LABEL_INBOX, AMG_LABEL_SENT, AMG_LABEL_ALL);
    for (h = 0; h < sizeof(hosts) / sizeof(hosts[0]); ++h) {
        GuiLabel *important, *child;
        memset(&account, 0, sizeof(account));
        snprintf(account.imap_host, sizeof(account.imap_host), "%s", hosts[h]);
        memset(&gui, 0, sizeof(gui)); gui.account = &account;
        CHECK(update_labels_from_payload(&gui, (const unsigned char *)payload,
                                         strlen(payload)) == 10U);
        CHECK(!find_label(&gui, "[Gmail]"));
        CHECK(!find_label(&gui, "[Google Mail]"));
        CHECK(gui.label_count == GUI_SYSTEM_LABEL_COUNT + 5U);
        important = find_label(&gui, "[Gmail]/Important");
        child = find_label(&gui, "[Gmail]/Important/Work");
        CHECK(important && child);
        CHECK(important->generation == 1U && important->parent_index == (size_t)-1);
        CHECK(important->has_children && important->selectable);
        CHECK(!strcmp(important->display_local, "Important"));
        CHECK(!strcmp(important->server_mailbox_utf8, "[Gmail]/Important"));
        CHECK(child->generation == 2U && &gui.labels[child->parent_index] == important);
        CHECK(!strcmp(child->server_mailbox_utf8, "[Gmail]/Important/Work"));
        CHECK(find_label(&gui, "[Google Mail]/Other")->generation == 1U);
        CHECK(find_label(&gui, "Projects")->has_children);
        CHECK(find_label(&gui, "Projects/Client")->generation == 2U);
        CHECK(!strcmp(gui.labels[2].server_mailbox_utf8, "[Gmail]/Sent Mail"));
        CHECK(gui.labels[2].available && gui.labels[2].selectable);
        CHECK(!strcmp(gui.labels[4].server_mailbox_utf8, "[Gmail]/All Mail"));
    }
}

static void test_other_accounts(void)
{
    static const char payload[] = "0\t0\t/\t[Gmail]\n0\t1\t/\t[Gmail]/Important\n";
    static const char selectable[] = "0\t1\t/\t[Gmail]\n";
    AmgAccount account;
    AmgGui gui;
    memset(&account, 0, sizeof(account));
    strcpy(account.imap_host, "imap.example.com");
    memset(&gui, 0, sizeof(gui)); gui.account = &account;
    CHECK(update_labels_from_payload(&gui, (const unsigned char *)payload,
                                     sizeof(payload) - 1U) == 2U);
    CHECK(!find_label(&gui, "[Gmail]"));
    CHECK(find_label(&gui, "[Gmail]/Important")->generation == 1U);
    gui.account = NULL;
    CHECK(update_labels_from_payload(&gui, (const unsigned char *)payload,
                                     sizeof(payload) - 1U) == 2U);
    CHECK(!find_label(&gui, "[Gmail]"));
    /* A selectable namesake is still a genuine mailbox on another provider. */
    gui.account=&account;
    CHECK(update_labels_from_payload(&gui,(const unsigned char *)selectable,
                                    sizeof(selectable)-1U)==1U);
    CHECK(find_label(&gui,"[Gmail]") && find_label(&gui,"[Gmail]")->selectable);
    gui.account = &account; strcpy(account.imap_host, "imap.gmail.com");
    CHECK(update_labels_from_payload(&gui, (const unsigned char *)selectable,
                                     sizeof(selectable) - 1U) == 1U);
    CHECK(!find_label(&gui, "[Gmail]"));
    account.auth_mode=AMG_AUTH_OAUTH2_GOOGLE;strcpy(account.imap_host,"mail.proxy.test");
    CHECK(update_labels_from_payload(&gui,(const unsigned char *)selectable,
                                    sizeof(selectable)-1U)==1U);
    CHECK(!find_label(&gui,"[Gmail]"));
    {
        static const char aliases[]="0\t1\t/\t[GoogleMail]\n0\t1\t/\t[Google Mail]\n"
            "0\t1\t/\t[GoogleMail]/Important\n";
        CHECK(update_labels_from_payload(&gui,(const unsigned char *)aliases,
                                        sizeof(aliases)-1U)==3U);
        CHECK(!find_label(&gui,"[GoogleMail]") && !find_label(&gui,"[Google Mail]"));
        CHECK(find_label(&gui,"[GoogleMail]/Important")->generation==1U);
        CHECK(!strcmp(find_label(&gui,"[GoogleMail]/Important")->server_mailbox_utf8,
                      "[GoogleMail]/Important"));
    }
    CHECK(update_labels_from_payload(&gui, NULL, 0U) == 0U);
    CHECK(gui.label_count == GUI_SYSTEM_LABEL_COUNT);
}

int main(void)
{
    test_google_children(); test_other_accounts();
    CHECK(rebuilt == 10U && loaded == rebuilt);
    printf("folder visibility: %u checks passed (native mapping; UI doubles).\n", checks);
    return 0;
}
