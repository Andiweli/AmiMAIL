#include "account.h"
#include "codec.h"
#include "contacts.h"
#include "crypto.h"
#include "imap_parser.h"
#include "i18n.h"
#include "mime.h"
#include "mailto.h"
#include "oauth.h"
#include "smtp.h"
#include "storage.h"
#include "tls.h"
#include "update.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned tests_run;
static unsigned tests_failed;

#define CHECK(expression) do { ++tests_run; if (!(expression)) { \
    ++tests_failed; fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#expression); } } while(0)

static const char *text(AmgBuffer *buffer){amg_buffer_terminate(buffer);return (const char*)buffer->data;}

static void test_base64(void)
{
    AmgBuffer encoded,decoded;amg_buffer_init(&encoded);amg_buffer_init(&decoded);
    CHECK(amg_base64_encode((const unsigned char*)"hello",5U,&encoded)==AMG_OK);CHECK(!strcmp(text(&encoded),"aGVsbG8="));
    CHECK(amg_base64_decode((const char*)encoded.data,encoded.length,&decoded)==AMG_OK);CHECK(decoded.length==5U&&!memcmp(decoded.data,"hello",5U));
    CHECK(amg_base64_decode("@@",2U,&decoded)==AMG_ERR_PARSE);amg_buffer_free(&encoded);amg_buffer_free(&decoded);
}

static void test_quoted_printable(void)
{
    AmgBuffer output;amg_buffer_init(&output);CHECK(amg_quoted_printable_decode("Gr=C3=BC=C3=9Fe=\r\n!",21U,&output)==AMG_OK);
    CHECK(!strcmp(text(&output),"Grüße!"));amg_buffer_free(&output);
}

static void test_utf7(void)
{
    static const char *samples[]={"INBOX","Reisen & Urlaub","Österreich/Grüße","日本語"};size_t i;
    for(i=0;i<sizeof(samples)/sizeof(samples[0]);++i){AmgBuffer wire,back;amg_buffer_init(&wire);amg_buffer_init(&back);
        CHECK(amg_modified_utf7_encode(samples[i],&wire)==AMG_OK);CHECK(amg_modified_utf7_decode(text(&wire),&back)==AMG_OK);CHECK(!strcmp(text(&back),samples[i]));
        amg_buffer_free(&wire);amg_buffer_free(&back);}
    {AmgBuffer output;amg_buffer_init(&output);CHECK(amg_modified_utf7_encode("&",&output)==AMG_OK);CHECK(!strcmp(text(&output),"&-"));amg_buffer_free(&output);}
}

static void test_imap_parser(void)
{
    AmgImapParser parser;AmgImapEvent event;const char *chunks[]={"* 1 FETCH (BODY[] {","5}\r\nhe","llo)\r\nA1 OK done\r\n"};int result;
    amg_imap_parser_init(&parser);CHECK(amg_imap_parser_feed(&parser,chunks[0],strlen(chunks[0]))==AMG_OK);CHECK(amg_imap_parser_next(&parser,&event)==0);
    CHECK(amg_imap_parser_feed(&parser,chunks[1],strlen(chunks[1]))==AMG_OK);result=amg_imap_parser_next(&parser,&event);CHECK(result==1&&event.type==AMG_IMAP_EVENT_LINE);
    CHECK(amg_imap_parser_next(&parser,&event)==0);CHECK(amg_imap_parser_feed(&parser,chunks[2],strlen(chunks[2]))==AMG_OK);
    result=amg_imap_parser_next(&parser,&event);CHECK(result==1&&event.type==AMG_IMAP_EVENT_LITERAL&&event.length==5U&&!memcmp(event.data,"hello",5U));
    result=amg_imap_parser_next(&parser,&event);CHECK(result==1&&event.type==AMG_IMAP_EVENT_LINE);
    result=amg_imap_parser_next(&parser,&event);CHECK(result==1&&event.type==AMG_IMAP_EVENT_LINE);amg_imap_parser_free(&parser);
    {size_t literal=0;CHECK(amg_imap_parse_literal_length((const unsigned char*)"x {123+}\r\n",10U,&literal)==1&&literal==123U);}
    {size_t literal=175536460U;CHECK(amg_imap_parse_literal_length((const unsigned char*)"A1 OK done\r\n",12U,&literal)==0&&literal==0U);}
    CHECK(amg_imap_greeting_is_success(
        (const unsigned char *)"* OK Gimap ready for requests\r\n",31U));
    CHECK(amg_imap_greeting_is_success(
        (const unsigned char *)"  * PREAUTH ready\r\n",19U));
    CHECK(amg_imap_greeting_is_preauth(
        (const unsigned char *)"  * PREAUTH ready\r\n",19U));
    CHECK(!amg_imap_greeting_is_preauth(
        (const unsigned char *)"* OK ready\r\n",12U));
    CHECK(!amg_imap_greeting_is_success(
        (const unsigned char *)"* BYE unavailable\r\n",19U));
    CHECK(amg_imap_greeting_status(
        (const unsigned char *)"* OK Gimap",10U)==1);
    CHECK(amg_imap_greeting_status(
        (const unsigned char *)"\xef\xbb\xbf\t* PREAUTH ready\n",20U)==1);
    CHECK(amg_imap_greeting_status(
        (const unsigned char *)"* BYE unavailable\n",18U)==-1);
    CHECK(amg_imap_greeting_status(
        (const unsigned char *)"* O",3U)==0);
    {
        const char *select_response =
            "* FLAGS (\\Seen)\r\n"
            "* 12345 EXISTS\r\n"
            "* OK [UIDVALIDITY 987654] UIDs valid\r\n"
            "A000001 OK selected\r\n";
        unsigned long exists = 0;
        unsigned long uid_validity = 0;
        CHECK(amg_imap_parse_exists(
            (const unsigned char *)select_response,
            strlen(select_response), &exists) == 1);
        CHECK(exists == 12345UL);
        CHECK(amg_imap_parse_uidvalidity(
            (const unsigned char *)select_response,
            strlen(select_response), &uid_validity) == 1);
        CHECK(uid_validity == 987654UL);
        CHECK(!amg_imap_parse_uidvalidity(
            (const unsigned char *)"* 1 EXISTS\r\nA1 OK done\r\n",
            strlen("* 1 EXISTS\r\nA1 OK done\r\n"), &uid_validity));
    }
    {
        const char *fetch_response =
            "* 9876 FETCH (UID 456789 FLAGS (\\Seen))\r\n"
            "A000002 OK fetched\r\n";
        unsigned long sequence = 0;
        CHECK(amg_imap_parse_fetch_sequence(
            (const unsigned char *)fetch_response,
            strlen(fetch_response), 456789UL, &sequence) == 1);
        CHECK(sequence == 9876UL);
        CHECK(!amg_imap_parse_fetch_sequence(
            (const unsigned char *)fetch_response,
            strlen(fetch_response), 111UL, &sequence));
    }
    {
        const char *header1 =
            "From: Alice <alice@example.com>\r\nSubject: First\r\n\r\n";
        const char *header2 =
            "From: Bob <bob@example.com>\r\nSubject: Second\r\n\r\n";
        char prefix[256];
        AmgBuffer response;
        AmgImapFetchRecord record;
        size_t position = 0;
        amg_buffer_init(&response);
        snprintf(prefix, sizeof(prefix),
                 "* 49 FETCH (UID 9001 FLAGS (\\Seen \\Flagged) "
                 "RFC822.SIZE 2049 BODY[HEADER.FIELDS (FROM SUBJECT)] {%lu}\r\n",
                 (unsigned long)strlen(header1));
        CHECK(amg_buffer_append_cstr(&response, prefix) == AMG_OK);
        CHECK(amg_buffer_append_cstr(&response, header1) == AMG_OK);
        CHECK(amg_buffer_append_cstr(&response, ")\r\n") == AMG_OK);
        snprintf(prefix, sizeof(prefix),
                 "* 50 FETCH (UID 9002 FLAGS (\\Deleted) RFC822.SIZE 100 "
                 "BODY[HEADER.FIELDS (FROM SUBJECT)] {%lu}\r\n",
                 (unsigned long)strlen(header2));
        CHECK(amg_buffer_append_cstr(&response, prefix) == AMG_OK);
        CHECK(amg_buffer_append_cstr(&response, header2) == AMG_OK);
        CHECK(amg_buffer_append_cstr(&response,
                                     ")\r\nA000003 OK fetched\r\n") == AMG_OK);
        CHECK(amg_imap_fetch_record_next(response.data, response.length,
                                         &position, &record) == 1);
        CHECK(record.uid == 9001UL && record.rfc822_size == 2049UL);
        CHECK(record.seen && record.flagged);
        CHECK(!record.deleted);
        CHECK(record.literal_length == strlen(header1) &&
              !memcmp(record.literal, header1, strlen(header1)));
        CHECK(amg_imap_fetch_record_next(response.data, response.length,
                                         &position, &record) == 1);
        CHECK(record.uid == 9002UL && record.rfc822_size == 100UL);
        CHECK(!record.seen && !record.flagged);
        CHECK(record.deleted);
        CHECK(record.literal_length == strlen(header2) &&
              !memcmp(record.literal, header2, strlen(header2)));
        CHECK(amg_imap_fetch_record_next(response.data, response.length,
                                         &position, &record) == 0);
        amg_buffer_free(&response);
    }
    {
        unsigned char *long_line = (unsigned char *)malloc(16386U);
        CHECK(long_line != NULL);
        if (long_line) {
            memset(long_line, 'X', 16384U);
            long_line[16384U] = '\r';
            long_line[16385U] = '\n';
            amg_imap_parser_init(&parser);
            CHECK(amg_imap_parser_feed(&parser, long_line, 16386U) == AMG_OK);
            CHECK(amg_imap_parser_next(&parser, &event) == 1);
            CHECK(event.length == 16386U);
            amg_imap_parser_free(&parser);
            free(long_line);
        }
    }
    {
        const char *oversized_literal = "* 1 FETCH (BODY[] {8388609}\r\n";
        amg_imap_parser_init(&parser);
        CHECK(amg_imap_parser_feed(&parser, oversized_literal,
                                   strlen(oversized_literal)) == AMG_OK);
        CHECK(amg_imap_parser_next(&parser, &event) == AMG_ERR_LIMIT);
        CHECK(parser.failure == AMG_IMAP_PARSER_FAILURE_LITERAL_LIMIT);
        CHECK(parser.failure_size == AMIGMAIL_MAX_MESSAGE + 1U);
        amg_imap_parser_free(&parser);
    }
}

static void test_account_normalize(void)
{
    AmgAccount account;
    amg_account_init(&account);
    strcpy(account.display_name, "  Andreas  ");
    strcpy(account.email, " andreas@gmail.com ");
    strcpy(account.imap_host, " imap.gmail.com ");
    strcpy(account.imap_username, " andreas@gmail.com ");
    strcpy(account.smtp_host, " smtp.gmail.com ");
    strcpy(account.smtp_username, " andreas@gmail.com ");
    strcpy(account.sent_mailbox, " Sent Items ");
    strcpy(account.drafts_mailbox, " Drafts ");
    strcpy(account.all_mailbox, " Archive ");
    strcpy(account.spam_mailbox, " Junk Mail ");
    strcpy(account.trash_mailbox, " Deleted Items ");
    CHECK(amg_account_set_secret(&account.imap_password,
                                 "abcd efgh ijkl mnop") == AMG_OK);
    CHECK(amg_account_set_secret(&account.smtp_password,
                                 "qrst uvwx yzab cdef") == AMG_OK);
    amg_account_normalize(&account);
    CHECK(!strcmp(account.display_name, "Andreas"));
    CHECK(!strcmp(account.email, "andreas@gmail.com"));
    CHECK(!strcmp(account.imap_host, "imap.gmail.com"));
    CHECK(!strcmp(account.imap_username, "andreas@gmail.com"));
    CHECK(!strcmp(account.smtp_host, "smtp.gmail.com"));
    CHECK(!strcmp(account.smtp_username, "andreas@gmail.com"));
    CHECK(!strcmp(account.sent_mailbox, "Sent Items"));
    CHECK(!strcmp(account.drafts_mailbox, "Drafts"));
    CHECK(!strcmp(account.all_mailbox, "Archive"));
    CHECK(!strcmp(account.spam_mailbox, "Junk Mail"));
    CHECK(!strcmp(account.trash_mailbox, "Deleted Items"));
    CHECK(account.imap_password &&
          !strcmp(account.imap_password, "abcdefghijklmnop"));
    CHECK(account.smtp_password &&
          !strcmp(account.smtp_password, "qrstuvwxyzabcdef"));
    amg_account_clear(&account);
}

static void test_storage_metadata(void)
{
    const char *path = "build/test-account.cfg";
    const char *master_path = "build/test-master.cfg";
    AmgAccount saved, loaded;
    AmgError error;
    char master[128];
    char raw[4096] = {0};
    FILE *file;
    size_t raw_length;
    amg_account_init(&saved);
    amg_account_init(&loaded);
    strcpy(saved.display_name, "Andreas");
    strcpy(saved.email, "andreas@example.com");
    strcpy(saved.imap_host, "imap.example.com");
    strcpy(saved.imap_username, "andreas");
    strcpy(saved.smtp_host, "smtp.example.com");
    strcpy(saved.smtp_username, "andreas-send");
    strcpy(saved.sent_mailbox, "Sent Items");
    strcpy(saved.drafts_mailbox, "Draft Box");
    strcpy(saved.all_mailbox, "Archive");
    strcpy(saved.spam_mailbox, "Junk Mail");
    strcpy(saved.trash_mailbox, "Deleted Messages");
    saved.fetch_on_start = 1;
    saved.periodic_fetch = 1;
    saved.fetch_days = 180U;
    saved.imap_starttls = 1;
    saved.smtp_starttls = 1;
    saved.save_sent_copy = 0;
    saved.notification_sound = 1;
    strcpy(saved.notification_sound_path, "PROGDIR:notify.8svx");
    CHECK(amg_storage_save_account(path, &saved, NULL, &error) == AMG_OK);
    file=fopen(path,"rb");
    CHECK(file!=NULL);
    raw_length=0U;
    if(file){raw_length=fread(raw,1U,sizeof(raw)-1U,file);fclose(file);raw[raw_length]=0;}
    CHECK(raw_length>0U);
    CHECK(!strncmp(raw,"AMIMAIL-ACCOUNT-2\n",18U));
    CHECK(strstr(raw,"remembered_master=")==NULL);
    CHECK(amg_storage_load_account(path, NULL, &loaded, &error) == AMG_OK);
    CHECK(!strcmp(loaded.display_name, saved.display_name));
    CHECK(!strcmp(loaded.email, saved.email));
    CHECK(loaded.fetch_on_start == 1);
    CHECK(loaded.periodic_fetch == 1);
    CHECK(loaded.fetch_days == 180U);
    CHECK(loaded.imap_starttls == 1);
    CHECK(loaded.smtp_starttls == 1);
    CHECK(loaded.save_sent_copy == 0);
    CHECK(loaded.notification_sound == 1);
    CHECK(!strcmp(loaded.notification_sound_path, "PROGDIR:notify.8svx"));
    CHECK(!strcmp(loaded.imap_host, "imap.example.com") &&
          loaded.imap_port == 993U);
    CHECK(!strcmp(loaded.imap_username, "andreas"));
    CHECK(!strcmp(loaded.smtp_host, "smtp.example.com") &&
          loaded.smtp_port == 465U);
    CHECK(!strcmp(loaded.smtp_username, "andreas-send"));
    CHECK(!strcmp(loaded.sent_mailbox, "Sent Items"));
    CHECK(!strcmp(loaded.drafts_mailbox, "Draft Box"));
    CHECK(!strcmp(loaded.all_mailbox, "Archive"));
    CHECK(!strcmp(loaded.spam_mailbox, "Junk Mail"));
    CHECK(!strcmp(loaded.trash_mailbox, "Deleted Messages"));
    file=fopen(master_path,"wb");
    CHECK(file!=NULL);
    if(file){fputs("AMIMAIL-ACCOUNT-1\nremembered_master=546573742d4d6173746572\n",file);fclose(file);}
    CHECK(amg_storage_load_legacy_master(
        master_path,master,sizeof(master))==AMG_OK);
    CHECK(!strcmp(master,"Test-Master"));
    amg_secure_clear(master,sizeof(master));
    file=fopen(master_path,"wb");
    CHECK(file!=NULL);
    if(file){fputs("AMIMAIL-ACCOUNT-2\nremembered_master=546573742d4d6173746572\n",file);fclose(file);}
    CHECK(amg_storage_load_legacy_master(
        master_path,master,sizeof(master))!=AMG_OK);
    CHECK(master[0]==0);
    remove(path);
    remove(master_path);
    amg_account_clear(&saved);
    amg_account_clear(&loaded);
}

static void test_headers_and_rfc2047(void)
{
    const char *source="Subject: =?UTF-8?B?R3LDvMOfZQ==?=\r\nX-Test: first\r\n\tsecond\r\n\r\nBody";AmgMailHeaders headers;size_t body;
    AmgBuffer decoded;amg_mail_headers_init(&headers);amg_buffer_init(&decoded);
    CHECK(amg_mail_headers_parse(source,strlen(source),&headers,&body)==AMG_OK);CHECK(!strcmp(amg_mail_header_get(&headers,"x-test"),"first second"));
    CHECK(!strcmp(source+body,"Body"));CHECK(amg_rfc2047_decode(amg_mail_header_get(&headers,"Subject"),&decoded)==AMG_OK);CHECK(!strcmp(text(&decoded),"Grüße"));
    amg_mail_headers_free(&headers);amg_buffer_free(&decoded);
}

static void test_mime(void)
{
    const char *message="Content-Type: multipart/alternative; boundary=abc\r\n\r\n--abc\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: quoted-printable\r\n\r\nHallo=20Welt\r\n--abc\r\nContent-Type: text/html\r\n\r\n<b>Hallo</b>\r\n--abc--\r\n";
    const char *with_attachment="Content-Type: multipart/mixed; boundary=mix\r\n\r\n--mix\r\nContent-Type: text/plain; charset=UTF-8\r\n\r\nHallo\r\n--mix\r\nContent-Type: application/pdf; name=\"rechnung.pdf\"\r\nContent-Disposition: attachment; filename=\"rechnung.pdf\"\r\nContent-Transfer-Encoding: base64\r\n\r\nQUJD\r\n--mix--\r\n";
    AmgBuffer output,name,data;AmgError error;size_t attachment_count=0;amg_buffer_init(&output);CHECK(amg_mime_extract_text(message,strlen(message),&output,&error)==AMG_OK);CHECK(strstr(text(&output),"Hallo Welt")!=NULL);amg_buffer_free(&output);
    amg_buffer_init(&output);CHECK(amg_mime_attachment_summary(with_attachment,strlen(with_attachment),&output,&error)==AMG_OK);CHECK(strstr(text(&output),"rechnung.pdf")!=NULL);CHECK(strstr(text(&output),"application/pdf")!=NULL);amg_buffer_free(&output);
    CHECK(amg_mime_attachment_count(with_attachment,strlen(with_attachment),&attachment_count,&error)==AMG_OK);CHECK(attachment_count==1U);
    amg_buffer_init(&name);amg_buffer_init(&data);CHECK(amg_mime_extract_attachment(with_attachment,strlen(with_attachment),0U,&name,&data,&error)==AMG_OK);CHECK(!strcmp(text(&name),"rechnung.pdf"));CHECK(data.length==3U&&!memcmp(data.data,"ABC",3U));amg_buffer_free(&name);amg_buffer_free(&data);
    amg_buffer_init(&output);CHECK(amg_html_to_text("<p>A &amp; B</p><script>evil()</script><br>C",47U,&output)==AMG_OK);CHECK(strstr(text(&output),"evil")==NULL);CHECK(strstr((char*)output.data,"A & B")!=NULL);amg_buffer_free(&output);
}


static void test_mailto(void)
{
    AmgMailtoRequest request;
    AmgError error;
    char *argv1[] = {
        (char *)"AmiMail",
        (char *)"IGNORED",
        (char *)"mailto:found@example.com?subject=Found"
    };

    memset(&error, 0, sizeof(error));
    amg_mailto_request_init(&request);
    CHECK(amg_mailto_parse(
        "mailto:max@example.com?subject=Hallo%20Welt&body=Zeile%201%0D%0AZeile%202",
        &request, &error) == AMG_OK);
    CHECK(request.to_utf8 && !strcmp(request.to_utf8, "max@example.com"));
    CHECK(request.subject_utf8 && !strcmp(request.subject_utf8, "Hallo Welt"));
    CHECK(request.body_utf8 && !strcmp(request.body_utf8, "Zeile 1\r\nZeile 2"));
    amg_mailto_request_clear(&request);

    amg_mailto_request_init(&request);
    CHECK(amg_mailto_parse(
        "MAILTO:first@example.com?to=second@example.com&cc=copy%40example.com&bcc=blind%40example.com&subject=Gr%C3%BC%C3%9Fe",
        &request, &error) == AMG_OK);
    CHECK(request.to_utf8 &&
          !strcmp(request.to_utf8, "first@example.com, second@example.com"));
    CHECK(request.cc_utf8 && !strcmp(request.cc_utf8, "copy@example.com"));
    CHECK(request.bcc_utf8 && !strcmp(request.bcc_utf8, "blind@example.com"));
    CHECK(request.subject_utf8 && !strcmp(request.subject_utf8, "Grüße"));
    amg_mailto_request_clear(&request);

    amg_mailto_request_init(&request);
    CHECK(amg_mailto_parse(
        "mailto:test+tag@example.com?subject=A+B&subject=ignored&body=x%26y",
        &request, &error) == AMG_OK);
    CHECK(request.to_utf8 && !strcmp(request.to_utf8, "test+tag@example.com"));
    CHECK(request.subject_utf8 && !strcmp(request.subject_utf8, "A+B"));
    CHECK(request.body_utf8 && !strcmp(request.body_utf8, "x&y"));
    amg_mailto_request_clear(&request);

    amg_mailto_request_init(&request);
    CHECK(amg_mailto_parse(
        "mailto:user@example.com?subject=Hello%0D%0ABcc%3Aevil%40example.com",
        &request, &error) == AMG_OK);
    CHECK(request.subject_utf8 &&
          !strcmp(request.subject_utf8, "Hello  Bcc:evil@example.com"));
    amg_mailto_request_clear(&request);

    amg_mailto_request_init(&request);
    CHECK(amg_mailto_parse("mailto:test@example.com?subject=%ZZ",
                           &request, &error) == AMG_ERR_PARSE);
    CHECK(amg_mailto_parse("mailto:test@example.com?body=%00",
                           &request, &error) == AMG_ERR_PARSE);
    CHECK(amg_mailto_parse("https://example.com", &request, &error) ==
          AMG_ERR_ARGUMENT);
    amg_mailto_request_clear(&request);

    CHECK(amg_mailto_find_argument(3, argv1) == argv1[2]);
    CHECK(amg_mailto_find_argument(1, argv1) == NULL);
    {
        char *argv0_mailto[] = {
            (char *)"mailto:ibrowse@example.com?subject=IBrowse"
        };
        CHECK(amg_mailto_find_argument(1, argv0_mailto) == argv0_mailto[0]);
    }
    {
        char *url;
        int detached = 0;
        memset(&error, 0, sizeof(error));
        url = amg_mailto_startup_url(0, NULL,
            "mailto:raw@example.com\n", &detached, &error);
        CHECK(url != NULL && !strcmp(url, "mailto:raw@example.com"));
        CHECK(detached == 0);
        free(url);
    }
    {
        char *url;
        int detached = 0;
        memset(&error, 0, sizeof(error));
        url = amg_mailto_startup_url(0, NULL,
            "\"mailto:quoted@example.com?subject=Hello World\"\n",
            &detached, &error);
        CHECK(url != NULL &&
              !strcmp(url, "mailto:quoted@example.com?subject=Hello World"));
        CHECK(detached == 0);
        free(url);
    }
    {
        char *wrapped_argv[] = {
            (char *)"AmiMail",
            (char *)"URL=mailto:wrapped@example.com"
        };
        const char *found = amg_mailto_find_argument(2, wrapped_argv);
        CHECK(found != NULL && !strcmp(found, "mailto:wrapped@example.com"));
    }
    {
        const char *path = "build/AmiMailMailto.test";
        char option[256];
        char *file_argv[2];
        char *url;
        int detached = 0;
        FILE *file = fopen(path, "wb");
        CHECK(file != NULL);
        if (file) {
            CHECK(fputs("mailto:file@example.com?subject=Detached\n", file) >= 0);
            CHECK(fclose(file) == 0);
        }
        snprintf(option, sizeof(option), "--amm-mailto-file=%s", path);
        file_argv[0] = (char *)"AmiMail";
        file_argv[1] = option;
        memset(&error, 0, sizeof(error));
        url = amg_mailto_startup_url(2, file_argv, NULL,
                                     &detached, &error);
        CHECK(url != NULL &&
              !strcmp(url, "mailto:file@example.com?subject=Detached"));
        CHECK(detached == 1);
        {
            FILE *probe = fopen(path, "rb");
            CHECK(probe == NULL);
            if (probe) fclose(probe);
        }
        free(url);
    }
}

static void test_smtp(void)
{
    AmgBuffer output,subject;AmgReplyDraft draft;AmgError error;memset(&draft,0,sizeof(draft));amg_buffer_init(&output);amg_buffer_init(&subject);
    CHECK(amg_smtp_dot_stuff("a\r\n.b\r\n..c\r\n",14U,&output)==AMG_OK);CHECK(!strcmp(text(&output),"a\r\n..b\r\n...c\r\n"));amg_buffer_free(&output);
    CHECK(amg_smtp_reply_subject("Re: Test",&subject)==AMG_OK);CHECK(!strcmp(text(&subject),"Re: Test"));amg_buffer_free(&subject);
    draft.from="me@gmail.com";draft.to="you@example.com";draft.subject="Test";draft.body_utf8="Hallo\n.Zeile";draft.in_reply_to="<old@example>";draft.references="<first@example> <old@example>";
    draft.date_rfc2822="Wed, 12 Aug 2026 10:00:00 +0200";draft.message_id="<new@gmail.com>";amg_buffer_init(&output);
    CHECK(amg_smtp_build_reply(&draft,&output,&error)==AMG_OK);CHECK(strstr(text(&output),"Subject: Re: Test\r\n")!=NULL);CHECK(strstr((char*)output.data,"\r\n..Zeile")!=NULL);amg_buffer_free(&output);
    {
        AmgMailDraft mail;
        memset(&mail,0,sizeof(mail));
        mail.from="me@example.com";mail.to="to@example.com";mail.cc="";mail.bcc="hidden@example.com";mail.subject="Sent copy";mail.body_utf8="Body";mail.date_rfc2822="Wed, 12 Aug 2026 10:00:00 +0200";mail.message_id="<sent@example.com>";
        amg_buffer_init(&output);
        CHECK(amg_smtp_build_mail(&mail,1,&output,&error)==AMG_OK);
        CHECK(strstr(text(&output),"Bcc: hidden@example.com\r\n")!=NULL);
        amg_buffer_free(&output);
        amg_buffer_init(&output);
        CHECK(amg_smtp_build_mail(&mail,0,&output,&error)==AMG_OK);
        CHECK(strstr(text(&output),"Bcc:")==NULL);
        amg_buffer_free(&output);
    }
    {
        const char *path="build/draft-roundtrip.bin";
        FILE *file=fopen(path,"wb");
        AmgAttachmentInput attachment;
        AmgMailDraft mail;
        AmgMailHeaders headers;
        AmgBuffer body,name,data;
        size_t body_offset=0,attachment_count=0;
        CHECK(file!=NULL);
        if(file){CHECK(fwrite("ABC",1U,3U,file)==3U);CHECK(fclose(file)==0);}
        memset(&attachment,0,sizeof(attachment));
        attachment.path=path;attachment.name_utf8="rechnung.pdf";attachment.size=3U;
        memset(&mail,0,sizeof(mail));
        mail.from="me@example.com";mail.to="to@example.com";mail.cc="cc@example.com";mail.bcc="hidden@example.com";
        mail.subject="Draft roundtrip";mail.body_utf8="Draft body";mail.date_rfc2822="Wed, 12 Aug 2026 10:00:00 +0200";
        mail.message_id="<draft@example.com>";mail.in_reply_to="<old@example.com>";mail.references="<first@example.com> <old@example.com>";
        mail.attachments=&attachment;mail.attachment_count=1U;
        amg_buffer_init(&output);
        CHECK(amg_smtp_build_mail(&mail,1,&output,&error)==AMG_OK);
        CHECK(amg_buffer_terminate(&output)==AMG_OK);
        amg_mail_headers_init(&headers);
        CHECK(amg_mail_headers_parse((const char*)output.data,output.length,&headers,&body_offset)==AMG_OK);
        CHECK(!strcmp(amg_mail_header_get(&headers,"To"),"to@example.com"));
        CHECK(!strcmp(amg_mail_header_get(&headers,"Cc"),"cc@example.com"));
        CHECK(!strcmp(amg_mail_header_get(&headers,"Bcc"),"hidden@example.com"));
        CHECK(!strcmp(amg_mail_header_get(&headers,"In-Reply-To"),"<old@example.com>"));
        CHECK(!strcmp(amg_mail_header_get(&headers,"References"),"<first@example.com> <old@example.com>"));
        amg_mail_headers_free(&headers);
        amg_buffer_init(&body);
        CHECK(amg_mime_extract_text((const char*)output.data,output.length,&body,&error)==AMG_OK);
        CHECK(strstr(text(&body),"Draft body")!=NULL);
        amg_buffer_free(&body);
        CHECK(amg_mime_attachment_count((const char*)output.data,output.length,&attachment_count,&error)==AMG_OK);
        CHECK(attachment_count==1U);
        amg_buffer_init(&name);amg_buffer_init(&data);
        CHECK(amg_mime_extract_attachment((const char*)output.data,output.length,0U,&name,&data,&error)==AMG_OK);
        CHECK(!strcmp(text(&name),"rechnung.pdf"));
        CHECK(data.length==3U&&!memcmp(data.data,"ABC",3U));
        amg_buffer_free(&name);amg_buffer_free(&data);amg_buffer_free(&output);
        remove(path);
    }
    {
        AmgBuffer ehlo;
        amg_buffer_init(&ehlo);
        CHECK(amg_buffer_append_cstr(&ehlo,
              "250-mail.example\r\n250-STARTTLS\r\n250-AUTH PLAIN LOGIN XOAUTH2\r\n250 SIZE 10485760\r\n") == AMG_OK);
        CHECK(amg_smtp_response_has_capability(&ehlo, "STARTTLS"));
        CHECK(amg_smtp_response_has_capability(&ehlo, "SIZE"));
        CHECK(amg_smtp_response_auth_has_mechanism(&ehlo, "PLAIN"));
        CHECK(amg_smtp_response_auth_has_mechanism(&ehlo, "LOGIN"));
        CHECK(amg_smtp_response_auth_has_mechanism(&ehlo, "XOAUTH2"));
        CHECK(!amg_smtp_response_auth_has_mechanism(&ehlo, "CRAM-MD5"));
        amg_buffer_free(&ehlo);
    }
    {
        AmgBuffer ehlo;
        amg_buffer_init(&ehlo);
        CHECK(amg_buffer_append_cstr(&ehlo,
              "250-mail.example\r\n250-AUTH=PLAIN LOGIN\r\n250 OK\r\n") == AMG_OK);
        CHECK(amg_smtp_response_auth_has_mechanism(&ehlo, "PLAIN"));
        CHECK(amg_smtp_response_auth_has_mechanism(&ehlo, "LOGIN"));
        amg_buffer_free(&ehlo);
    }
}

static void test_oauth(void)
{
    AmgOAuthTokens tokens;AmgError error;AmgOAuthConfig config={"id","","http://127.0.0.1:1234/","https://mail.google.com/"};AmgBuffer url;char verifier[129],challenge[129],state[65];
    memset(&tokens,0,sizeof(tokens));CHECK(amg_oauth_parse_token_json("{\"access_token\":\"abc\",\"expires_in\":3600,\"refresh_token\":\"def\"}",&tokens,&error)==AMG_OK);
    CHECK(!strcmp(tokens.access_token,"abc")&&!strcmp(tokens.refresh_token,"def")&&tokens.expires_in==3600UL);amg_oauth_tokens_clear(&tokens);
    CHECK(amg_oauth_parse_token_json("{\"error\":\"invalid_grant\"}",&tokens,&error)==AMG_ERR_AUTH);CHECK(strstr(error.message,"invalid_grant")!=NULL);
    CHECK(amg_oauth_generate_pkce(verifier,challenge,state,&error)==AMG_OK);CHECK(strlen(verifier)>=43U&&strlen(challenge)==43U&&strlen(state)>20U);
    amg_buffer_init(&url);CHECK(amg_oauth_build_authorize_url(&config,challenge,state,&url)==AMG_OK);CHECK(strstr(text(&url),"code_challenge_method=S256")!=NULL);CHECK(strstr((char*)url.data,"scope=https%3A%2F%2Fmail.google.com%2F")!=NULL);amg_buffer_free(&url);
}

static void test_sha256(void)
{
    static const unsigned char expected[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};unsigned char digest[32];
    amg_sha256((const unsigned char*)"abc",3U,digest);CHECK(!memcmp(digest,expected,32U));
}

static void test_account(void)
{
    AmgAccount account;AmgError error;amg_account_init(&account);strcpy(account.email,"user@example.com");strcpy(account.imap_host,"imap.example.com");strcpy(account.smtp_host,"smtp.example.com");strcpy(account.imap_username,"imap-user");strcpy(account.smtp_username,"smtp-user");amg_account_set_secret(&account.imap_password,"short");
    CHECK(amg_account_validate(&account,&error)==AMG_OK);CHECK(!strcmp(amg_account_imap_user(&account),"imap-user"));CHECK(!strcmp(amg_account_smtp_user(&account),"smtp-user"));CHECK(!strcmp(amg_account_smtp_password(&account),"short"));CHECK(amg_account_should_append_sent(&account)==1);account.save_sent_copy=0;CHECK(amg_account_should_append_sent(&account)==0);amg_account_set_secret(&account.imap_password,"");CHECK(amg_account_validate(&account,&error)==AMG_ERR_AUTH);amg_account_clear(&account);

    amg_account_init(&account);strcpy(account.email,"user@gmail.com");strcpy(account.imap_host,"imap.gmail.com");strcpy(account.smtp_host,"smtp.gmail.com");amg_account_set_secret(&account.imap_password,"abcdefghijklmnop");
    account.imap_port=143;account.imap_starttls=1;account.smtp_port=587;account.smtp_starttls=1;
    CHECK(amg_account_validate(&account,&error)==AMG_ERR_UNSUPPORTED);CHECK(strstr(error.message,"993")!=NULL);
    account.imap_port=993;account.imap_starttls=0;
    CHECK(amg_account_validate(&account,&error)==AMG_OK);
    CHECK(amg_account_should_append_sent(&account)==0);
    account.smtp_port=465;account.smtp_starttls=1;
    CHECK(amg_account_validate(&account,&error)==AMG_ERR_UNSUPPORTED);
    account.smtp_port=465;account.smtp_starttls=0;
    CHECK(amg_account_validate(&account,&error)==AMG_OK);
    amg_account_clear(&account);
}

static void test_contacts(void)
{
    const char *csv_path = "build/test-contacts.csv";
    const char *vcf_path = "build/test-contacts.vcf";
    const char *email_only_csv_path = "build/test-contacts-email-only.csv";
    const char *db_path = "build/test-contacts.dat";
    FILE *file;
    AmgContactBook book, loaded, email_only_book;
    AmgContactImportResult imported;
    AmgError error;
    const AmgContact *contact;

    amg_contacts_init(&book);
    amg_contacts_init(&loaded);
    amg_contacts_init(&email_only_book);
    memset(&error, 0, sizeof(error));

    file = fopen(csv_path, "wb");
    CHECK(file != NULL);
    if (file) {
        CHECK(fputs(
            "First Name,Last Name,Organization Name,E-mail 1 - Value,Phone 1 - Label,Phone 1 - Value,Phone 2 - Label,Phone 2 - Value,Website 1 - Value,Address 1 - Formatted\n"
            "Anna,Müller,ACME,anna@example.com,Mobile,+431111,Work,+432222,https://example.com,\"Street 1\nVienna\"\n"
            "Anna,Müller,Other,ANNA@example.com,Mobile,+439999,,,,\n"
            ",,Strba & Urban Installationen,,Other,+43 2213 30000 ::: +4322133000089,,,,\n",
            file) >= 0);
        CHECK(fclose(file) == 0);
    }
    CHECK(amg_contacts_import_csv(csv_path, &book, &imported, &error) == AMG_OK);
    CHECK(imported.records == 3U);
    CHECK(imported.imported == 2U);
    CHECK(imported.duplicates == 1U);
    CHECK(book.count == 2U);
    contact = amg_contacts_find(&book, book.items[0].id);
    CHECK(contact != NULL && !strcmp(contact->email, "anna@example.com"));
    CHECK(contact != NULL && !strcmp(contact->mobile, "+431111"));
    CHECK(contact != NULL && !strcmp(contact->phone, "+432222"));
    if (contact) {
        AmgContact same = *contact;
        AmgContact same_without_email = *contact;
        same_without_email.email[0] = 0;
        CHECK(!amg_contacts_is_duplicate(&book, &same, same.id));
        CHECK(amg_contacts_is_duplicate(&book, &same_without_email, 0UL));
    }
    CHECK(amg_contacts_save(db_path, &book, &error) == AMG_OK);
    CHECK(amg_contacts_load(db_path, &loaded, &error) == AMG_OK);
    CHECK(loaded.count == 2U);

    file = fopen(vcf_path, "wb");
    CHECK(file != NULL);
    if (file) {
        CHECK(fputs(
            "BEGIN:VCARD\r\nVERSION:3.0\r\nN:Müller;Anna;;;\r\nEMAIL:anna@example.com\r\nTEL;TYPE=CELL:+431111\r\nTEL;TYPE=WORK:+432222\r\nORG:ACME\r\nURL:https://example.com\r\nEND:VCARD\r\n"
            "BEGIN:VCARD\r\nVERSION:3.0\r\nORG:Strba & Urban Installationen\r\nTEL:+43 2213 30000\r\nTEL:+4322133000089\r\nEND:VCARD\r\n",
            file) >= 0);
        CHECK(fclose(file) == 0);
    }
    CHECK(amg_contacts_import_vcf(vcf_path, &book, &imported, &error) == AMG_OK);
    CHECK(imported.records == 2U);
    CHECK(imported.imported == 0U);
    CHECK(imported.duplicates == 2U);
    CHECK(book.count == 2U);

    file = fopen(email_only_csv_path, "wb");
    CHECK(file != NULL);
    if (file) {
        CHECK(fputs("E-mail 1 - Value\nonly@example.com\n", file) >= 0);
        CHECK(fclose(file) == 0);
    }
    CHECK(amg_contacts_import_csv(email_only_csv_path, &email_only_book,
                                  &imported, &error) == AMG_OK);
    CHECK(imported.records == 1U);
    CHECK(imported.imported == 1U);
    CHECK(email_only_book.count == 1U);
    CHECK(!strcmp(email_only_book.items[0].email, "only@example.com"));

    amg_contacts_free(&email_only_book);
    amg_contacts_free(&loaded);
    amg_contacts_free(&book);
    remove(csv_path);
    remove(vcf_path);
    remove(email_only_csv_path);
    remove(db_path);
}



static void test_update(void)
{
    static const char latest_json[] =
        "{\"tag_name\":\"v1.4\",\"assets\":[]}";
    AmgUpdateInfo info;
    AmgError error;

    CHECK(amg_update_is_newer("v1.4", "1.3"));
    CHECK(amg_update_is_newer("v1.10", "1.9"));
    CHECK(amg_update_is_newer("v2.0", "1.99"));
    CHECK(amg_update_is_newer("v1.4.1", "1.4"));
    CHECK(!amg_update_is_newer("v1.3", "1.3"));
    CHECK(!amg_update_is_newer("v1.2", "1.3"));
    CHECK(!amg_update_is_newer("release-1.4", "1.3"));
    CHECK(amg_update_is_newer("v1.0", "1.0 RC1"));
    CHECK(amg_update_is_newer("v1.0-RC2", "1.0 RC1"));
    CHECK(!amg_update_is_newer("v1.0-RC1", "1.0 RC1"));
    CHECK(!amg_update_is_newer("v1.0-RC1", "1.0"));
    CHECK(!amg_update_is_newer("v1.0-RC2", AMIMAIL_VERSION));
    CHECK(amg_update_is_newer("v1.0-RC3", AMIMAIL_VERSION));
    CHECK(amg_update_is_newer("v1.0", AMIMAIL_VERSION));
    CHECK(!amg_update_is_newer("v1.0-RC1", AMIMAIL_VERSION));

    memset(&info, 0, sizeof(info));
    memset(&error, 0, sizeof(error));
    CHECK(amg_update_parse_latest_json(
        (const unsigned char *)latest_json, strlen(latest_json),
        &info, &error) == AMG_OK);
    CHECK(!strcmp(info.tag, "v1.4"));
    CHECK(!strcmp(
        info.download_url,
        "https://github.com/Andiweli/AmiMAIL/releases/download/v1.4/AmiMAIL-v1.4.lha"));

    memset(&info, 0, sizeof(info));
    memset(&error, 0, sizeof(error));
    CHECK(amg_update_parse_latest_json(
        (const unsigned char *)"{\"name\":\"AmiMAIL\"}",
        strlen("{\"name\":\"AmiMAIL\"}"),
        &info, &error) == AMG_ERR_PARSE);
}

static void test_i18n(void)
{
    char formatted[64];
    amg_i18n_init();
    CHECK(!strcmp(amg_tr(123456L, "English"), "English"));
    amg_tr_snprintf(formatted, sizeof(formatted),
                    123457L, "%lu message", 3UL);
    CHECK(!strcmp(formatted, "3 message"));
    amg_i18n_cleanup();
}

int main(void)
{
    test_base64();test_quoted_printable();test_utf7();test_imap_parser();test_headers_and_rfc2047();test_mime();test_mailto();test_smtp();test_oauth();test_sha256();test_account();test_account_normalize();test_storage_metadata();test_contacts();test_i18n();test_update();
    printf("%u checks, %u failures\n",tests_run,tests_failed);return tests_failed?1:0;
}
