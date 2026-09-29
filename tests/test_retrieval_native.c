#include "retrieval_native_double.h"
#include "storage.h"
#include "retrieval_production.inc"

/* Network queue doubles: verify only due accounts are queued and busy jobs
 * are skipped, including a slow connection. No socket or IMAP server here. */
int amg_network_is_running(const AmgNetwork *n){return n && n->running;}
int amg_network_is_connected(const AmgNetwork *n){return n && n->connected;}
int amg_network_start(AmgNetwork *n,const AmgAccount *a,AmgError *e)
{(void)a;(void)e;n->running=1;return n->fail?AMG_ERR_IO:AMG_OK;}
int amg_network_request(AmgNetwork *n,AmgNetCommandType type,unsigned long uid,
                        const char *a,const char *b,AmgError *e)
{(void)uid;(void)a;(void)b;(void)e;++n->calls;n->last=type;return n->fail?AMG_ERR_IO:AMG_OK;}

static Object *field(const char *text)
{Object *o=new_gadget(&string_class);snprintf(o->text,sizeof(o->text),"%s",text);return o;}

static void prefs_tests(void)
{
    AmgAccountSet set;
    AmgGui gui;
    AccountPageGadgets page;
    struct Window w;
    Object *rows, *choose, *help, *file_button;
    AmgAccount candidate;
    AmgError error;
    size_t i;
    char sent[512]="Sent",drafts[512]="Drafts",all[512]="All";
    char spam[512]="Spam",trash[512]="Trash";
    int save_sent=1;
    amg_account_set_init(&set);memset(&gui,0,sizeof(gui));memset(&page,0,sizeof(page));
    gui.account_set=&set;gui.account=&set.accounts[0];
    gui.account->periodic_fetch_minutes=5U;gui.account->periodic_fetch=0;
    rows=build_test_rows(&gui,&page,160UL);
    CHECK(rows->child_count==5U);
    CHECK(!strcmp(rows->children[0]->children[0]->text,"Email retrieval:"));
    CHECK(!strcmp(rows->children[2]->children[0]->text,"Notifications:"));
    CHECK(!strcmp(rows->children[4]->children[0]->text,"External:"));
    for(i=0U;i<5U;++i)CHECK(rows->children[i]->children[0]->min_width==160UL);
    choose=page.periodic_interval;
    CHECK(choose->cl==&chooser_class && choose->popup && choose->disabled);
    CHECK(choose->chosen==2UL && choose->choice_count==6U);
    CHECK(!strcmp(choose->choices[0],"1 min") && !strcmp(choose->choices[5],"30 min"));
    CHECK(rows->children[1]->children[3]==choose && choose->weight==0UL);
    help=rows->children[4]->children[3];file_button=rows->children[3]->children[2];
    CHECK(!strcmp(help->text,"?"));CHECK(!strcmp(file_button->text,"..."));
    CHECK(help->min_width==32UL && help->max_width==32UL && help->weight==0UL);
    CHECK(help->min_width==file_button->min_width && help->max_width==file_button->max_width);
    page.periodic_fetch->selected=TRUE;test_toggle_periodic(&page,&w);
    CHECK(!choose->disabled);choose->chosen=5UL;
    page.periodic_fetch->selected=FALSE;test_toggle_periodic(&page,&w);
    CHECK(choose->disabled && choose->chosen==5UL);

    page.enabled=field("");page.enabled->selected=1UL;
    page.account_name=field("Private");page.name=field("Tester");page.email=field("user@example.invalid");
    page.imap_host=field("imap.example.invalid");page.imap_port=field("993");
    page.imap_username=field("user");page.imap_password=field("test-secret");
    page.imap_starttls=field("");page.smtp_host=field("smtp.example.invalid");
    page.smtp_port=field("465");page.smtp_username=field("user");page.smtp_password=field("");
    page.smtp_starttls=field("");page.smtp_same_credentials=field("");
    page.fetch_days=field("180");
    amg_account_init(&candidate);
    CHECK(account_page_collect(&page,&candidate,sent,drafts,all,spam,trash,save_sent,&error)==AMG_OK);
    CHECK(!candidate.periodic_fetch && candidate.periodic_fetch_minutes==30U);
    CHECK(candidate.imap_password && !strcmp(candidate.imap_password,"test-secret"));
    CHECK(set.accounts[0].periodic_fetch_minutes==5U); /* Draft edits do not commit/Cancel. */
    for(i=0U;i<6U;++i) {
        set.accounts[1].periodic_fetch=(int)(i & 1U);
        set.accounts[1].periodic_fetch_minutes=amg_periodic_interval_minutes(i);
        account_page_show(&page,&w,&set.accounts[1],sent,drafts,all,spam,trash,&save_sent);
        CHECK(choose->chosen==(ULONG)i);
        CHECK(choose->disabled==(set.accounts[1].periodic_fetch?FALSE:TRUE));
        CHECK(account_page_collect(&page,&candidate,sent,drafts,all,spam,trash,save_sent,&error)==AMG_OK);
        CHECK(candidate.periodic_fetch_minutes==set.accounts[1].periodic_fetch_minutes);
    }
    account_page_show(&page,&w,&set.accounts[0],sent,drafts,all,spam,trash,&save_sent);
    CHECK(choose->chosen==2UL && choose->disabled);
    amg_account_clear(&candidate);amg_account_set_clear(&set);
    DisposeObject(rows);
    DisposeObject(page.enabled);DisposeObject(page.account_name);DisposeObject(page.name);DisposeObject(page.email);
    DisposeObject(page.imap_host);DisposeObject(page.imap_port);DisposeObject(page.imap_starttls);
    DisposeObject(page.imap_username);DisposeObject(page.imap_password);DisposeObject(page.smtp_host);
    DisposeObject(page.smtp_port);DisposeObject(page.smtp_starttls);DisposeObject(page.smtp_same_credentials);
    DisposeObject(page.smtp_username);DisposeObject(page.smtp_password);DisposeObject(page.fetch_days);
    CHECK(!live_objects);
}

/* All nine user options: eight account flags and the Sent-copy checkbox.
 * Real collect/show/storage code runs against explicit gadget state doubles.
 * Mouse rendering/activation is intentionally NOT simulated as proof. */
static unsigned account_checkbox_mask(const AmgAccount *a)
{
    return (a->save_sent_copy ? 1U : 0U) |
           (a->enabled ? 2U : 0U) |
           (a->imap_starttls ? 4U : 0U) |
           (a->smtp_starttls ? 8U : 0U) |
           (a->smtp_same_credentials ? 16U : 0U) |
           (a->fetch_on_start ? 32U : 0U) |
           (a->periodic_fetch ? 64U : 0U) |
           (a->notification_sound ? 128U : 0U) |
           (a->herald_notifications ? 256U : 0U);
}

static void set_account_checkbox_mask(AmgAccount *a, unsigned mask)
{
    a->save_sent_copy = (mask & 1U) != 0U;
    a->enabled = (mask & 2U) != 0U;
    a->imap_starttls = (mask & 4U) != 0U;
    a->smtp_starttls = (mask & 8U) != 0U;
    a->smtp_same_credentials = (mask & 16U) != 0U;
    a->fetch_on_start = (mask & 32U) != 0U;
    a->periodic_fetch = (mask & 64U) != 0U;
    a->notification_sound = (mask & 128U) != 0U;
    a->herald_notifications = (mask & 256U) != 0U;
}

static void native_checkbox_tests(void)
{
    static const ULONG ids[] = {
        GID_FOLDER_SAVE_SENT, GID_ACCOUNT_ENABLED,
        GID_ACCOUNT_IMAP_STARTTLS, GID_ACCOUNT_SMTP_STARTTLS,
        GID_ACCOUNT_SMTP_SAME_CREDENTIALS, GID_ACCOUNT_FETCH_ON_START,
        GID_ACCOUNT_PERIODIC_FETCH, GID_ACCOUNT_NOTIFICATION_SOUND,
        GID_ACCOUNT_HERALD
    };
    AmgAccountSet set;
    AmgAccount candidate, loaded;
    AmgGui gui;
    AccountPageGadgets page;
    struct Window window;
    Object *boxes[9], *strings[13];
    char sent[512], drafts[512], all[512], spam[512], trash[512];
    int save_sent;
    unsigned mask;
    size_t i, slot;
    ULONG state;
    unsigned before;
    AmgError error;

    /* Constructor failures must not call a NULL library or allocate a
     * replacement button pretending to be a checkbox. */
    before = checkbox_class_calls;
    CheckBoxBase = NULL;
    CHECK(native_checkbox(GID_ACCOUNT_ENABLED, 1) == NULL);
    CHECK(checkbox_class_calls == before && live_objects == 0U);
    CheckBoxBase = &checkbox_library;
    checkbox_class_missing = 1;
    before = checkbox_object_calls;
    CHECK(native_checkbox(GID_ACCOUNT_ENABLED, 1) == NULL);
    CHECK(checkbox_object_calls == before && live_objects == 0U);
    checkbox_class_missing = 0;
    checkbox_allocation_failure = 1;
    CHECK(native_checkbox(GID_ACCOUNT_ENABLED, 0) == NULL);
    CHECK(live_objects == 0U);
    checkbox_allocation_failure = 0;
    boxes[0] = native_checkbox(GID_ACCOUNT_ENABLED, -7);
    CHECK(boxes[0] && boxes[0]->selected == TRUE);
    DisposeObject(boxes[0]);

    amg_account_set_init(&set);
    amg_account_init(&candidate);
    amg_account_init(&loaded);
    memset(&gui, 0, sizeof(gui));
    memset(&page, 0, sizeof(page));
    memset(&window, 0, sizeof(window));
    gui.account_set = &set;
    for (slot = 0U; slot < AMG_MAX_ACCOUNTS; ++slot) {
        AmgAccount *a = &set.accounts[slot];
        snprintf(a->account_name, sizeof(a->account_name), "Account %lu",
                 (unsigned long)slot);
        strcpy(a->email, "test@example.invalid");
        strcpy(a->display_name, "Test User");
        strcpy(a->imap_host, "imap.example.invalid");
        strcpy(a->smtp_host, "smtp.example.invalid");
        strcpy(a->imap_username, "imap-test");
        strcpy(a->smtp_username, "smtp-test");
        strcpy(a->notification_sound_path, "SYS:Prefs/Presets/test.8svx");
        strcpy(a->sent_mailbox, "Sent");
        strcpy(a->drafts_mailbox, "Drafts");
        strcpy(a->all_mailbox, "All Mail");
        strcpy(a->spam_mailbox, "Spam");
        strcpy(a->trash_mailbox, "Trash");
    }
    for (i = 0U; i < 13U; ++i) strings[i] = field("");
    page.account_name = strings[0]; page.name = strings[1];
    page.email = strings[2]; page.imap_host = strings[3];
    page.imap_port = strings[4]; page.imap_username = strings[5];
    page.imap_password = strings[6]; page.smtp_host = strings[7];
    page.smtp_port = strings[8]; page.smtp_username = strings[9];
    page.smtp_password = strings[10]; page.fetch_days = strings[11];
    page.notification_path = strings[12];
    page.periodic_interval = new_gadget(&chooser_class);

    for (mask = 0U; mask < 512U; ++mask) {
        slot = mask % AMG_MAX_ACCOUNTS;
        gui.account = &set.accounts[slot];
        set_account_checkbox_mask(gui.account, mask);
        gui.account->periodic_fetch_minutes =
            amg_periodic_interval_minutes(mask % AMG_PERIODIC_INTERVAL_COUNT);
        save_sent = gui.account->save_sent_copy;
        CHECK(build_all_test_checkboxes(&gui, &save_sent, boxes) == boxes[0]);
        for (i = 0U; i < 9U; ++i) {
            CHECK(boxes[i] && boxes[i]->cl == &test_checkbox_class);
            CHECK(boxes[i]->id == ids[i] && boxes[i]->relverify == TRUE);
            CHECK(GetAttr(GA_Selected, boxes[i], &state));
            CHECK(state == ((mask & (1U << i)) ? TRUE : FALSE));
        }
        page.enabled = boxes[1]; page.imap_starttls = boxes[2];
        page.smtp_starttls = boxes[3]; page.smtp_same_credentials = boxes[4];
        page.fetch_on_start = boxes[5]; page.periodic_fetch = boxes[6];
        page.notification_sound = boxes[7]; page.herald_notifications = boxes[8];
        account_page_show(&page, &window, gui.account,
                           sent, drafts, all, spam, trash, &save_sent);
        CHECK(account_page_collect(&page, &candidate, sent, drafts, all,
                                    spam, trash, save_sent, &error) == AMG_OK);
        CHECK(account_checkbox_mask(&candidate) == mask);
        CHECK(candidate.periodic_fetch_minutes == gui.account->periodic_fetch_minutes);
        CHECK(page.periodic_interval->disabled == (candidate.periodic_fetch ? FALSE : TRUE));
        CHECK(page.smtp_username->disabled == (candidate.smtp_same_credentials ? TRUE : FALSE));
        CHECK(page.smtp_password->disabled == page.smtp_username->disabled);

        /* Simulate the native selected-state change only. The actual
         * GADGETUP handlers must observe it, not toggle it a second time. */
        SetGadgetAttrs(page.periodic_fetch, &window, NULL,
                       GA_Selected, (ULONG)!candidate.periodic_fetch, TAG_DONE);
        test_toggle_periodic(&page, &window);
        CHECK(page.periodic_fetch->selected == (candidate.periodic_fetch ? FALSE : TRUE));
        CHECK(page.periodic_interval->disabled == (candidate.periodic_fetch ? TRUE : FALSE));
        CHECK(page.periodic_interval->chosen == mask % AMG_PERIODIC_INTERVAL_COUNT);
        SetGadgetAttrs(page.smtp_same_credentials, &window, NULL,
                       GA_Selected, (ULONG)!candidate.smtp_same_credentials, TAG_DONE);
        test_toggle_smtp(&page, &window);
        CHECK(page.smtp_same_credentials->selected == (candidate.smtp_same_credentials ? FALSE : TRUE));
        CHECK(page.smtp_username->disabled == page.smtp_same_credentials->selected);
        CHECK(page.smtp_password->disabled == page.smtp_same_credentials->selected);
        CHECK(!strcmp(page.smtp_username->text, "smtp-test"));

        /* Cancelling/discarding edited gadget states leaves stored account
         * data alone. Re-showing the source restores all eight flags. */
        CHECK(account_checkbox_mask(gui.account) == mask);
        account_page_show(&page, &window, gui.account,
                           sent, drafts, all, spam, trash, &save_sent);
        CHECK(account_page_collect(&page, &candidate, sent, drafts, all,
                                    spam, trash, save_sent, &error) == AMG_OK);
        CHECK(account_checkbox_mask(&candidate) == mask);

        /* Exercise the actual host account serializer/deserializer for
         * every combination, not a test-only copy of its field logic. */
        CHECK(amg_storage_save_account("checkbox-account.cfg", &candidate,
                                        NULL, &error) == AMG_OK);
        CHECK(amg_storage_load_account("checkbox-account.cfg", NULL,
                                        &loaded, &error) == AMG_OK);
        CHECK(account_checkbox_mask(&loaded) == mask);
        CHECK(loaded.periodic_fetch_minutes == candidate.periodic_fetch_minutes);
        CHECK(!strcmp(loaded.account_name, candidate.account_name));
        CHECK(!strcmp(loaded.notification_sound_path, candidate.notification_sound_path));
        amg_account_clear(&loaded);
        amg_account_init(&loaded);
        for (i = 0U; i < 9U; ++i) DisposeObject(boxes[i]);
    }
    CHECK(remove("checkbox-account.cfg") == 0);
    for (i = 0U; i < 13U; ++i) DisposeObject(strings[i]);
    DisposeObject(page.periodic_interval);
    amg_account_clear(&candidate); amg_account_clear(&loaded);
    amg_account_set_clear(&set);
    CHECK(live_objects == 0U);
    puts("Native checkbox construction/state/512 account-storage combinations passed (API doubles).");
}

static void timer_tests(void)
{
    AmgAccountSet accounts;
    AmgGui gui;
    AmgError error;
    AmgNetwork networks[AMG_MAX_ACCOUNTS];
    unsigned failure;
    size_t i;
    amg_account_set_init(&accounts);memset(&gui,0,sizeof(gui));
    memset(networks,0,sizeof(networks));gui.account_set=&accounts;
    for(i=0U;i<AMG_MAX_ACCOUNTS;++i){gui.networks[i]=&networks[i];networks[i].running=networks[i].connected=1;}
    for(failure=1U;failure<=3U;++failure) {
        native_call=0U;fail_native=failure;
        CHECK(!periodic_timer_init(&gui));periodic_timer_cleanup(&gui);
        CHECK(!live_ports && !live_requests && !device.opened);
    }
    fail_native=0U;eclock=UINT64_C(100000000);
    CHECK(periodic_timer_init(&gui));CHECK(!any_periodic_account_enabled(&gui));
    CHECK(!gui.periodic_timer_pending);
    accounts.accounts[2].enabled=accounts.accounts[2].periodic_fetch=1;
    accounts.accounts[2].periodic_fetch_minutes=5U;
    periodic_timer_restart(&gui);
    CHECK(gui.periodic_timer_pending && gui.periodic_timer_request->tr_time.tv_secs==300UL);
    eclock+=UINT64_C(30000000);periodic_timer_restart(&gui);
    CHECK(gui.periodic_timer_request->tr_time.tv_secs==270UL && abort_count>0U);
    accounts.accounts[0].periodic_fetch=1;accounts.accounts[0].periodic_fetch_minutes=1U;
    periodic_timer_restart(&gui);CHECK(gui.periodic_timer_request->tr_time.tv_secs==60UL);
    eclock+=UINT64_C(60000000);gui.periodic_timer_request->tr_node.complete=1;
    test_timer_signal(&gui,&error);
    CHECK(networks[0].calls==1U && networks[2].calls==0U);
    CHECK(gui.periodic_timer_request->tr_time.tv_secs==60UL);
    CHECK(gui.periodic_check_pending==1);
    eclock+=UINT64_C(60000000);gui.periodic_timer_request->tr_node.complete=1;
    test_timer_signal(&gui,&error);CHECK(networks[0].calls==1U); /* One outstanding check. */
    gui.periodic_check_pending=0;gui.account_runtime[0].periodic_check_pending=0;
    /* A long modal dialog causes one due check per account, not catch-up jobs. */
    eclock=UINT64_C(1000000000);gui.periodic_timer_request->tr_node.complete=1;
    test_timer_signal(&gui,&error);
    CHECK(networks[0].calls==2U && networks[2].calls==1U);
    CHECK(gui.periodic_schedule.due[0]==1060U && gui.periodic_schedule.due[2]==1300U);
    accounts.accounts[0].periodic_fetch=0;accounts.accounts[2].periodic_fetch=0;
    periodic_timer_restart(&gui);CHECK(!gui.periodic_timer_pending);
    accounts.accounts[4].enabled=accounts.accounts[4].periodic_fetch=1;
    accounts.accounts[4].periodic_fetch_minutes=2U;
    accounts.accounts[4].auth_mode=AMG_AUTH_OAUTH2_GOOGLE; /* Locked double. */
    periodic_timer_restart(&gui);CHECK(!gui.periodic_timer_pending);
    accounts.accounts[4].auth_mode=AMG_AUTH_PASSWORD;periodic_timer_restart(&gui);
    CHECK(gui.periodic_timer_request->tr_time.tv_secs==120UL);
    networks[4].connected=0;
    eclock+=UINT64_C(120000000);gui.periodic_timer_request->tr_node.complete=1;
    test_timer_signal(&gui,&error);
    CHECK(networks[4].calls==1U && networks[4].last==AMG_NET_CONNECT);
    eclock+=UINT64_C(120000000);gui.periodic_timer_request->tr_node.complete=1;
    test_timer_signal(&gui,&error);CHECK(networks[4].calls==1U);
    periodic_timer_cleanup(&gui);CHECK(!live_ports && !live_requests && !device.opened);
    amg_account_set_clear(&accounts);
}
int main(void)
{
    prefs_tests();native_checkbox_tests();timer_tests();
    printf("ReAction preferences / timer / due-account queue: %u checks passed (API doubles).\n",checks);
    return 0;
}
