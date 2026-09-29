/* Actual scheduler, MIME/header decoders and account storage on the host. */
#include "account.h"
#include "periodic.h"
#include "mail_notice.h"
#include "buffer.h"
#include "storage.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

static void add_fetch(AmgBuffer *payload, unsigned long uid,
                      const char *flags, const char *headers)
{
    char prefix[192];
    int n = snprintf(prefix, sizeof(prefix),
        "* 1 FETCH (UID %lu FLAGS (%s) RFC822.SIZE 200 "
        "BODY[HEADER.FIELDS (SUBJECT DATE)] {%lu}\r\n",
        uid, flags, (unsigned long)strlen(headers));
    CHECK(n > 0 && (size_t)n < sizeof(prefix));
    CHECK(amg_buffer_append(payload, prefix, (size_t)n) == AMG_OK);
    CHECK(amg_buffer_append_cstr(payload, headers) == AMG_OK);
    CHECK(amg_buffer_append_cstr(payload, ")\r\n") == AMG_OK);
}

static void schedule_tests(void)
{
    static const unsigned int minutes[] = {1U, 2U, 5U, 10U, 15U, 30U};
    size_t option, slot;
    AmgPeriodicSchedule s;
    for (option = 0U; option < AMG_PERIODIC_INTERVAL_COUNT; ++option) {
        uint32_t now, period = 60U * minutes[option];
        memset(&s, 0, sizeof(s));
        CHECK(amg_periodic_interval_minutes(option) == minutes[option]);
        CHECK(amg_periodic_interval_index(minutes[option]) == option);
        CHECK(amg_periodic_wait(&s, 0U) == 0UL);
        amg_periodic_configure(&s, 3U, 1, minutes[option], 100U);
        CHECK(amg_periodic_wait(&s, 100U) == period);
        for (now = 100U; now < 100U + period * 3U; ++now) {
            unsigned long due = amg_periodic_take_due(&s, now);
            CHECK(due == ((now != 100U && (now - 100U) % period == 0U)
                           ? (1UL << 3) : 0UL));
            CHECK(amg_periodic_take_due(&s, now) == 0UL);
            CHECK(amg_periodic_wait(&s, now) >= 1UL);
        }
    }
    /* Five distinct stable account slots, not five copies of the minimum. */
    memset(&s, 0, sizeof(s));
    for (slot = 0U; slot < AMG_MAX_ACCOUNTS; ++slot)
        amg_periodic_configure(&s, slot, 1, minutes[slot], 0U);
    for (option = 1U; option <= 1800U; ++option) {
        unsigned long expected = 0UL;
        for (slot = 0U; slot < AMG_MAX_ACCOUNTS; ++slot)
            if (option % (60U * minutes[slot]) == 0U) expected |= 1UL << slot;
        CHECK(amg_periodic_take_due(&s, (uint32_t)option) == expected);
    }
    /* Ordinary settings saves, tab switches and reordering keep deadlines. */
    memset(&s, 0, sizeof(s));
    amg_periodic_configure(&s, 0U, 1, 5U, 0U);
    amg_periodic_configure(&s, 1U, 1, 15U, 0U);
    amg_periodic_configure(&s, 1U, 1, 15U, 200U);
    amg_periodic_configure(&s, 0U, 1, 5U, 200U);
    CHECK(s.due[0] == 300U && s.due[1] == 900U);
    amg_periodic_configure(&s, 0U, 1, 1U, 210U);
    CHECK(s.due[0] == 270U && s.due[1] == 900U);
    amg_periodic_configure(&s, 0U, 0, 1U, 215U);
    CHECK(s.seconds[0] == 0U && amg_periodic_take_due(&s, 300U) == 0UL);
    amg_periodic_configure(&s, 0U, 1, 1U, 310U);
    CHECK(s.due[0] == 370U);
    CHECK(amg_periodic_take_due(&s, 5000U) == 3UL);
    CHECK(amg_periodic_take_due(&s, 5000U) == 0UL);
    CHECK(s.due[0] == 5060U && s.due[1] == 5900U);
    /* Wrapping monotonic uint32_t seconds, maximum deadline horizon 1800 s. */
    memset(&s, 0, sizeof(s));
    amg_periodic_configure(&s, 4U, 1, 1U, UINT32_MAX - 29U);
    CHECK(s.due[4] == 30U);
    CHECK(amg_periodic_wait(&s, UINT32_MAX - 29U) == 60UL);
    CHECK(amg_periodic_take_due(&s, 29U) == 0UL);
    CHECK(amg_periodic_take_due(&s, 30U) == 16UL);
    CHECK(amg_periodic_wait(&s, 30U) == 60UL);
    CHECK(amg_periodic_interval_minutes((size_t)-1) == 5U);
    CHECK(amg_periodic_interval_index(8U) == 2U);
    CHECK(amg_periodic_interval_index(UINT_MAX) == 2U);
    CHECK(amg_periodic_take_due(NULL, 0U) == 0UL);
    CHECK(amg_periodic_wait(NULL, 0U) == 0UL);
}

static void subject_tests(void)
{
    AmgBuffer payload;
    char result[161], tiny[9];
    size_t cap;
    amg_buffer_init(&payload);
    add_fetch(&payload, 80UL, "", "Subject: latest arrival\r\nDate: old\r\n\r\n");
    add_fetch(&payload, 79UL, "", "Subject: older delivery\r\nDate: newer\r\n\r\n");
    add_fetch(&payload, 500UL, "\\Deleted", "Subject: deleted\r\n\r\n");
    add_fetch(&payload, 5UL, "", "Subject: old baseline\r\n\r\n");
    CHECK(amg_mail_notice_subject(payload.data, payload.length, 70UL,
          "(No subject)", result, sizeof(result)) == AMG_OK);
    CHECK(!strcmp(result, "latest arrival"));
    CHECK(amg_mail_notice_subject(payload.data, payload.length, 80UL,
          "(No subject)", result, sizeof(result)) != AMG_OK);
    CHECK(!strcmp(result, "(No subject)"));
    amg_buffer_free(&payload);

    amg_buffer_init(&payload);
    add_fetch(&payload, 1UL, "", "Subject: =?ISO-8859-1?Q?Gr=FC=DFe?=\r\n\r\n");
    CHECK(amg_mail_notice_subject(payload.data,payload.length,0UL,"none",result,sizeof(result)) == AMG_OK);
    CHECK(!strcmp(result,"Gr\374\337e"));
    add_fetch(&payload, 2UL, "", "Subject: =?UTF-8?Q?Gr=C3=BC=C3=9Fe?=\r\n\t=?UTF-8?Q?_Andreas?=\r\n\r\n");
    CHECK(amg_mail_notice_subject(payload.data,payload.length,1UL,"none",result,sizeof(result)) == AMG_OK);
    CHECK(!strcmp(result,"Gr\374\337e Andreas"));
    add_fetch(&payload, 3UL, "", "Subject: =?UTF-8?B?R3LDvMOfZQ==?=\r\n\r\n");
    CHECK(amg_mail_notice_subject(payload.data,payload.length,2UL,"none",result,sizeof(result)) == AMG_OK);
    CHECK(!strcmp(result,"Gr\374\337e"));
    add_fetch(&payload, 4UL, "", "Subject: Raw Gr\303\274\303\237e\r\n\r\n");
    CHECK(amg_mail_notice_subject(payload.data,payload.length,3UL,"none",result,sizeof(result)) == AMG_OK);
    CHECK(!strcmp(result,"Raw Gr\374\337e"));
    add_fetch(&payload, 5UL, "", "From: someone@example.invalid\r\n\r\n");
    CHECK(amg_mail_notice_subject(payload.data,payload.length,4UL,"(Kein Betreff)",result,sizeof(result)) == AMG_OK);
    CHECK(!strcmp(result,"(Kein Betreff)"));
    add_fetch(&payload, 6UL, "", "Subject: \t  \r\n\r\n");
    CHECK(amg_mail_notice_subject(payload.data,payload.length,5UL,"(No subject)",result,sizeof(result)) == AMG_OK);
    CHECK(!strcmp(result,"(No subject)"));
    amg_buffer_free(&payload);

    amg_buffer_init(&payload);
    add_fetch(&payload, 1UL, "", "Subject: =?UTF-8?Q?one=0Atwo=1Bthree?=\r\n\r\n");
    CHECK(amg_mail_notice_subject(payload.data,payload.length,0UL,"none",result,sizeof(result)) == AMG_OK);
    CHECK(!strcmp(result,"one two three"));
    amg_buffer_free(&payload);
    /* Input is length delimited, including malformed and unterminated records. */
    CHECK(amg_mail_notice_subject((const unsigned char *)"* 1 FETCH (UID 9 BODY[] {900}\r\nx",
        sizeof("* 1 FETCH (UID 9 BODY[] {900}\r\nx") - 1U,0UL,
          "(No subject)",result,sizeof(result)) < 0);
    CHECK(!strcmp(result,"(No subject)"));
    CHECK(amg_mail_notice_subject(NULL,0U,0UL,"(No subject)",result,sizeof(result)) < 0);
    CHECK(!strcmp(result,"(No subject)"));

    amg_mail_notice_clip(" \t Gr\374\337e\r\n\\n *\" end  ",result,sizeof(result));
    CHECK(!strcmp(result,"Gr\374\337e n *\" end"));
    CHECK(!strchr(result, '\n') && !strchr(result, '\\'));
    amg_mail_notice_clip("0123456789abcdef",tiny,sizeof(tiny));
    CHECK(!strcmp(tiny,"01234..."));
    for (cap=0U;cap<sizeof(result);++cap) {
        unsigned char guarded[163];
        memset(guarded, 0xA5, sizeof(guarded));
        amg_mail_notice_clip("Long subject   with   whitespace and quotes *\"",
                              (char *)guarded+1U,cap);
        CHECK(guarded[0]==0xA5 && guarded[cap+1U]==0xA5);
        if (cap) CHECK(strlen((char *)guarded+1U)<cap);
    }
}

static void storage_tests(void)
{
    AmgAccount source, loaded, copy;
    AmgError error;
    size_t i, n;
    FILE *f;
    char data[20000];
    amg_account_init(&source); amg_account_init(&copy);
    CHECK(source.periodic_fetch_minutes==5U && !source.periodic_fetch);
    source.enabled=1; source.herald_notifications=1;
    strcpy(source.account_name,"Private"); strcpy(source.email,"user@example.invalid");
    strcpy(source.imap_host,"imap.example.invalid");
    CHECK(amg_account_set_secret(&source.imap_password,"only-a-test") == AMG_OK);
    for (i=0U;i<AMG_PERIODIC_INTERVAL_COUNT;++i) {
        int enabled;
        source.periodic_fetch_minutes=amg_periodic_interval_minutes(i);
        for (enabled=0;enabled<=1;++enabled) {
            source.periodic_fetch=enabled;
            CHECK(amg_account_copy(&copy,&source)==AMG_OK);
            CHECK(copy.periodic_fetch_minutes==source.periodic_fetch_minutes);
            CHECK(copy.imap_password != source.imap_password &&
                  !strcmp(copy.imap_password,"only-a-test"));
            CHECK(amg_storage_save_account("interval.cfg",&source,NULL,&error)==AMG_OK);
            amg_account_init(&loaded);
            CHECK(amg_storage_load_account("interval.cfg",NULL,&loaded,&error)==AMG_OK);
            CHECK(loaded.periodic_fetch==enabled);
            CHECK(loaded.periodic_fetch_minutes==source.periodic_fetch_minutes);
            CHECK(loaded.herald_notifications==1);
            /* The non-AmiSSL host path deliberately stores no secrets. */
            CHECK(loaded.imap_password == NULL);
            amg_account_clear(&loaded);
        }
    }
    f=fopen("interval.cfg","rb"); CHECK(f);
    n=fread(data,1U,sizeof(data)-1U,f); CHECK(!ferror(f));CHECK(!fclose(f));data[n]=0;
    {
        char *setting=strstr(data,"periodic_fetch_minutes=30\n"); CHECK(setting);
        memmove(setting,setting+strlen("periodic_fetch_minutes=30\n"),
                strlen(setting+strlen("periodic_fetch_minutes=30\n"))+1U);
    }
    {
        const char *bad[]={NULL,"0","8","3","-1","31","1x",
            "-4294967295","-18446744073709551615","+1"," 1",
            "9999999999999999999999999",""};
        for (i=0U;i<sizeof(bad)/sizeof(bad[0]);++i) {
            f=fopen("legacy.cfg","wb");CHECK(f);CHECK(fputs(data,f)>=0);
            if(bad[i])CHECK(fprintf(f,"periodic_fetch_minutes=%s\n",bad[i])>0);
            CHECK(!fclose(f)); amg_account_init(&loaded);
            loaded.periodic_fetch_minutes=1U;
            CHECK(amg_storage_load_account("legacy.cfg",NULL,&loaded,&error)==AMG_OK);
            CHECK(loaded.periodic_fetch_minutes==5U && loaded.periodic_fetch);
            CHECK(loaded.herald_notifications==1);
            amg_account_clear(&loaded);
        }
    }
    source.periodic_fetch_minutes=8U; amg_account_normalize(&source);
    CHECK(source.periodic_fetch_minutes==5U);
    amg_account_clear(&source);amg_account_clear(&copy);
    remove("interval.cfg");remove("legacy.cfg");
}

int main(void)
{
    schedule_tests(); subject_tests(); storage_tests();
    printf("Herald subject / independent schedules / storage: %u checks passed.\n",checks);
    return 0;
}
