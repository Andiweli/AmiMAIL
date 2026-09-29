/* Actual production IMAP command/parser paths against a command-gated peer.
 * No real network, Amiga ABI or provider-specific server is exercised. */
#include "transfer_tls_double.h"
#include "imap.h"

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

typedef struct Step { const char *command; const char *reply; } Step;
static const Step *script;
static size_t step_count, step_index, peer_limit;
long mutation_read(AmgTlsConnection *, void *, size_t, AmgError *);
int mutation_write(AmgTlsConnection *, const void *, size_t, AmgError *);

long mutation_read(AmgTlsConnection *c, void *data, size_t n, AmgError *error)
{
    if (td_position >= peer_limit) {
        amg_error_set(error, AMG_ERR_IO, "Injected missing response");
        return -1;
    }
    if (n > peer_limit - td_position) n = peer_limit - td_position;
    return td_read(c, data, n, error);
}
int mutation_write(AmgTlsConnection *c, const void *data, size_t n, AmgError *error)
{
    char expected[512], tag[32];
    const char *reply;
    int size;
    CHECK(td_position == peer_limit);
    CHECK(step_index < step_count);
    if (step_index >= step_count) return AMG_ERR_PROTOCOL;
    size = snprintf(expected, sizeof(expected), "A%06lu %s\r\n",
                    (unsigned long)step_index + 1UL, script[step_index].command);
    CHECK(size > 0 && (size_t)size == n);
    CHECK(size > 0 && (size_t)size == n && !memcmp(expected, data, n));
    reply = script[step_index].reply;
    /* Replies use '@' only for the current tag, never message data. */
    snprintf(tag, sizeof(tag), "A%06lu", (unsigned long)step_index + 1UL);
    while (*reply) {
        if (*reply == '@') { CHECK(amg_buffer_append_cstr(&td_input, tag) == AMG_OK); ++reply; }
        else { CHECK(amg_buffer_append_char(&td_input, (unsigned char)*reply++) == AMG_OK); }
    }
    peer_limit = td_input.length;
    ++step_index;
    return td_write_all(c, data, n, error);
}
#undef amg_tls_read
#undef amg_tls_write_all
#define amg_tls_read mutation_read
#define amg_tls_write_all mutation_write
#include "../src/imap.c"

#define PRESENT "* 1 FETCH (UID 42 FLAGS (\\Seen))\r\n@ OK checked\r\n"
#define MARKED "* 1 FETCH (FLAGS (\\Deleted \\Seen) UID 42)\r\n@ OK checked\r\n"
#define ABSENT "@ OK no such UID\r\n"
#define OK "@ OK done\r\n"
#define NO "@ NO rejected\r\n"
#define SELECTED "* 3 EXISTS\r\n* OK [UIDVALIDITY 123] current\r\n@ OK [READ-WRITE] selected\r\n"
#define STATE "UID FETCH 42 (UID FLAGS)"
#define COPY "UID COPY 42 \"Trash\""
#define MOVE "UID MOVE 42 \"Trash\""
#define STORE "UID STORE 42 +FLAGS.SILENT (\\Deleted)"
#define EXPUNGE "UID EXPUNGE 42"

static void reset(AmgImapSession *session, const Step *steps, size_t count,
                   size_t chunk, int native_move, int uidplus, const char *selected)
{
    td_reset("", 0U, chunk);
    script = steps; step_count = count; step_index = peer_limit = 0;
    amg_imap_session_init(session);
    session->connection = (AmgTlsConnection *)&td_dummy;
    session->selected_exists = 3UL;
    session->capability_move = native_move;
    session->capability_uidplus = uidplus;
    strcpy(session->selected_mailbox, selected);
    strcpy(session->special_mailboxes[3], "Trash");
}

static void run_case(const Step *steps, size_t count, int native_move, int uidplus,
                     const char *selected, const char *source, const char *target,
                     int expected, const char *reason, unsigned long remaining)
{
    static const size_t chunks[] = {1U, 2U, 7U, 4096U};
    size_t k;
    for (k=0U;k<sizeof(chunks)/sizeof(chunks[0]);++k) {
        AmgImapSession session;
        AmgError error = {0};
        int result;
        reset(&session, steps, count, chunks[k], native_move, uidplus, selected);
        result = amg_imap_move_label(&session, 42UL, source, target, &error);
        CHECK(result == expected);
        CHECK(error.code == expected);
        CHECK(step_index == count);
        CHECK(td_position == peer_limit);
        CHECK(session.selected_exists == remaining);
        if (reason) CHECK(strstr(error.message, reason) != NULL);
        else CHECK(error.message[0] == 0);
        CHECK(amg_buffer_terminate(&td_output) == AMG_OK);
        /* No bare EXPUNGE, CLOSE or unexpected FETCH of message contents. */
        CHECK(strstr((char *)td_output.data, " EXPUNGE\r\n") == NULL);
        CHECK(strstr((char *)td_output.data, " CLOSE\r\n") == NULL);
        CHECK(strstr((char *)td_output.data, "BODY") == NULL);
    }
}
#define RUN(arr,m,u,sel,src,dst,ret,msg,num) \
 run_case(arr,sizeof(arr)/sizeof(arr[0]),m,u,sel,src,dst,ret,msg,num)

static void test_moves(void)
{
    const Step native[]={{STATE,PRESENT},{MOVE,OK},{STATE,ABSENT}};
    const Step fallback[]={{STATE,PRESENT},{COPY,OK},{STORE,OK},{EXPUNGE,OK},{STATE,ABSENT}};
    const Step deferred[]={{STATE,PRESENT},{COPY,OK},{STORE,OK},{STATE,MARKED}};
    const Step auto_expunge[]={{STATE,PRESENT},{COPY,OK},{STORE,OK},{STATE,ABSENT}};
    const Step missing[]={{STATE,ABSENT}};
    const Step other_uid[]={{STATE,"* 1 FETCH (UID 43 FLAGS ())\r\n@ OK done\r\n"}};
    const Step noop_move[]={{STATE,PRESENT},{MOVE,OK},{STATE,PRESENT}};
    const Step refused_move[]={{STATE,PRESENT},{MOVE,NO}};
    const Step refused_copy[]={{STATE,PRESENT},{COPY,NO}};
    const Step refused_store[]={{STATE,PRESENT},{COPY,OK},{STORE,NO}};
    const Step refused_expunge[]={{STATE,PRESENT},{COPY,OK},{STORE,OK},{EXPUNGE,NO}};
    const Step no_ack[]={{STATE,PRESENT},{MOVE,OK},{STATE,""}};
    const Step failed_verify[]={{STATE,PRESENT},{MOVE,OK},{STATE,NO}};
    const Step false_deleted[]={{STATE,PRESENT},{COPY,OK},{STORE,OK},{STATE,PRESENT}};
    const Step still_marked_uidplus[]={{STATE,PRESENT},{COPY,OK},{STORE,OK},{EXPUNGE,OK},{STATE,MARKED}};
    const Step case_path[]={{"SELECT \"archive\"",SELECTED},{STATE,PRESENT},{MOVE,OK},{STATE,ABSENT}};
    const Step case_target[]={{STATE,PRESENT},{"UID MOVE 42 \"archive\"",OK},{STATE,ABSENT}};
    const Step quote_target[]={{STATE,PRESENT},{"UID MOVE 42 \"T\\\"rash\"",OK},{STATE,ABSENT}};
    RUN(native,1,1,"INBOX","INBOX","Trash",AMG_OK,NULL,2UL);
    RUN(native,1,0,"INBOX","INBOX","\\Trash",AMG_OK,NULL,2UL);
    RUN(fallback,0,1,"INBOX","INBOX","Trash",AMG_OK,NULL,2UL);
    RUN(deferred,0,0,"INBOX","INBOX","Trash",AMG_OK,"only marked deleted",3UL);
    RUN(auto_expunge,0,0,"INBOX","INBOX","Trash",AMG_OK,NULL,2UL);
    RUN(missing,1,1,"INBOX","INBOX","Trash",AMG_ERR_PROTOCOL,"no longer",3UL);
    RUN(other_uid,1,1,"INBOX","INBOX","Trash",AMG_ERR_PROTOCOL,"no longer",3UL);
    RUN(noop_move,1,1,"INBOX","INBOX","Trash",AMG_ERR_PROTOCOL,"kept the source",3UL);
    RUN(refused_move,1,1,"INBOX","INBOX","Trash",AMG_ERR_PROTOCOL,"rejected",3UL);
    RUN(refused_copy,0,1,"INBOX","INBOX","Trash",AMG_ERR_PROTOCOL,"rejected",3UL);
    RUN(refused_store,0,1,"INBOX","INBOX","Trash",AMG_ERR_PROTOCOL,"rejected",3UL);
    RUN(refused_expunge,0,1,"INBOX","INBOX","Trash",AMG_ERR_PROTOCOL,"rejected",3UL);
    RUN(no_ack,1,1,"INBOX","INBOX","Trash",AMG_ERR_UNCERTAIN,"verification failed",3UL);
    RUN(failed_verify,1,1,"INBOX","INBOX","Trash",AMG_ERR_UNCERTAIN,"verification failed",3UL);
    RUN(false_deleted,0,0,"INBOX","INBOX","Trash",AMG_ERR_PROTOCOL,"kept the source",3UL);
    RUN(still_marked_uidplus,0,1,"INBOX","INBOX","Trash",AMG_ERR_PROTOCOL,"kept the source",3UL);
    RUN(case_path,1,1,"Archive","archive","Trash",AMG_OK,NULL,2UL);
    RUN(case_target,1,1,"Archive","Archive","archive",AMG_OK,NULL,2UL);
    RUN(native,1,1,"INBOX","inbox","Trash",AMG_OK,NULL,2UL);
    RUN(quote_target,1,1,"INBOX","INBOX","T\"rash",AMG_OK,NULL,2UL);
    {
        AmgImapSession session; AmgError error={0};
        reset(&session,NULL,0U,1U,1,1,"INBOX");
        CHECK(amg_imap_move_label(&session,42UL,"INBOX","inbox",&error)==AMG_ERR_ARGUMENT);
        CHECK(step_index==0U && td_writes==0U);
    }
}

static void test_capabilities(void)
{
    const char *inputs[]={
        "* CAPABILITY IMAP4rev1 MOVE UIDPLUS SPECIAL-USE AUTH=PLAIN SASL-IR\r\nA000001 OK done\r\n",
        "* capability IMAP4REV2\r\nA000001 OK done\r\n",
        "* CAPABILITY IMAP4rev1 MOVEFOO UIDPLUS2 AUTH=PLAIN-PLUS\r\nA000001 OK MOVE UIDPLUS\r\n",
        "* CAPABILITY IMAP4rev1\r\nA000001 OK no MOVE here\r\n"};
    unsigned i;
    for(i=0;i<4U;++i){
        AmgImapSession session;AmgBuffer b;size_t n=strlen(inputs[i]);
        amg_imap_session_init(&session);amg_buffer_init(&b);
        /* Deliberately omit NUL: capability scanning must respect length. */
        b.data=(unsigned char*)malloc(n);CHECK(b.data!=NULL);if(!b.data)continue;
        memcpy(b.data,inputs[i],n);b.length=b.capacity=n;
        update_capabilities(&session,&b);
        CHECK(session.capability_move==(i<2U));
        CHECK(session.capability_uidplus==(i<2U));
        CHECK(session.capability_special_use==(i<2U));
        CHECK(session.capability_auth_plain==(i==0U));
        amg_buffer_free(&b);
    }
    CHECK(mailbox_equal("INBOX","inbox"));
    CHECK(!mailbox_equal("Archive","archive"));
    CHECK(mailbox_equal("Archive","Archive"));
}

static void test_select_alias(void)
{
    const Step steps[]={{"SELECT \"INBOX\"",SELECTED}};
    AmgImapSession session;AmgError error={0};
    reset(&session,steps,1U,4096U,1,1,"INBOX");
    CHECK(amg_imap_select(&session,session.selected_mailbox,&error)==AMG_OK);
    CHECK(!strcmp(session.selected_mailbox,"INBOX"));
    CHECK(session.uid_validity==123UL);
}
static void test_post_login_capabilities(void)
{
    const Step login_move[] = {
        {"CAPABILITY", "* CAPABILITY IMAP4rev1\r\n@ OK capabilities\r\n"},
        {"LOGIN \"user@example.com\" \"password\"", OK},
        {"CAPABILITY", "* CAPABILITY IMAP4rev1 MOVE UIDPLUS\r\n@ OK capabilities\r\n"},
        {"SELECT \"INBOX\"", SELECTED}, {STATE, PRESENT}, {MOVE, OK}, {STATE, ABSENT}
    };
    const Step login_uidplus[] = {
        {"CAPABILITY", "* CAPABILITY IMAP4rev1\r\n@ OK capabilities\r\n"},
        {"LOGIN \"user@example.com\" \"password\"", OK},
        {"CAPABILITY", "* CAPABILITY IMAP4rev1 UIDPLUS\r\n@ OK capabilities\r\n"}
    };
    const Step sasl[] = {
        {"CAPABILITY", "* CAPABILITY IMAP4rev1 AUTH=PLAIN SASL-IR LOGINDISABLED\r\n@ OK capabilities\r\n"},
        {"AUTHENTICATE PLAIN AHVzZXJAZXhhbXBsZS5jb20AcGFzc3dvcmQ=", OK},
        {"CAPABILITY", "* CAPABILITY IMAP4rev1 MOVE UIDPLUS\r\n@ OK capabilities\r\n"}
    };
    const Step starttls[] = {
        {"CAPABILITY", "* CAPABILITY IMAP4rev1 STARTTLS\r\n@ OK capabilities\r\n"},
        {"STARTTLS", OK},
        {"CAPABILITY", "* CAPABILITY IMAP4rev1\r\n@ OK capabilities\r\n"},
        {"LOGIN \"user@example.com\" \"password\"", OK},
        {"CAPABILITY", "* CAPABILITY IMAP4rev1 MOVE UIDPLUS\r\n@ OK capabilities\r\n"}
    };
    const Step preauth[] = {
        {"CAPABILITY", "* CAPABILITY IMAP4rev1 MOVE UIDPLUS\r\n@ OK capabilities\r\n"}
    };
    const Step cap_failure[] = {
        {"CAPABILITY", "* CAPABILITY IMAP4rev1\r\n@ OK capabilities\r\n"},
        {"LOGIN \"user@example.com\" \"password\"", OK},
        {"CAPABILITY", NO}
    };
    const Step *sets[]={login_move,login_uidplus,sasl,starttls,preauth,cap_failure};
    const size_t sizes[]={7U,3U,3U,5U,1U,3U};
    const size_t chunks[]={1U,7U,4096U};
    size_t i,k;
    for(i=0U;i<6U;++i) for(k=0U;k<3U;++k){
        AmgImapSession session;AmgAccount account;AmgError error={0};
        const char *greeting=i==4U?"* PREAUTH already authenticated\r\n":"* OK welcome\r\n";
        reset(&session,sets[i],sizes[i],chunks[k],0,0,"");
        session.connection=NULL;
        CHECK(amg_buffer_append_cstr(&td_input,greeting)==AMG_OK);
        peer_limit=td_input.length;
        amg_account_init(&account);
        strcpy(account.email,"user@example.com");strcpy(account.imap_host,"mail.example.com");
        strcpy(account.trash_mailbox,"Trash");
        CHECK(amg_account_set_secret(&account.imap_password,"password")==AMG_OK);
        account.imap_starttls=i==3U;
        CHECK(amg_imap_connect(&session,&account,NULL,&error)==(i==5U?AMG_ERR_PROTOCOL:AMG_OK));
        if(i==5U){CHECK(session.connection==NULL&&session.authenticated==0);}
        else{
            CHECK(session.authenticated==1&&session.capability_uidplus==1);
            CHECK(session.capability_move==(i!=1U));
        }
        if(i==0U){
            CHECK(amg_imap_move_label(&session,42UL,"INBOX","\\Trash",&error)==AMG_OK);
            CHECK(session.selected_exists==2UL);
        }
        CHECK(step_index==sizes[i]&&td_position==peer_limit);
        amg_account_clear(&account);
    }
}

int main(void)
{
    test_capabilities(); test_select_alias(); test_moves(); test_post_login_capabilities();
    amg_buffer_free(&td_input);amg_buffer_free(&td_output);
    printf("imap-mutations: %u checks, %u failures\n",checks,failures);
    return failures ? 1 : 0;
}
