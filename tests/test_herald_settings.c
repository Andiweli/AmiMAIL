#include "account.h"
#include "storage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); return 1; } } while (0)
int main(void)
{
    AmgAccount original,copy,loaded; AmgAccountSet set;
    AmgError error; FILE *f; unsigned bit,sound; char data[20000]; size_t n;
    amg_account_set_init(&set);
    for(bit=0;bit<AMG_MAX_ACCOUNTS;++bit) CHECK(!set.accounts[bit].herald_notifications);
    amg_account_set_clear(&set);
    for(bit=0;bit<=1U;++bit) for(sound=0;sound<=1U;++sound) {
        amg_account_init(&original); amg_account_init(&copy); amg_account_init(&loaded);
        original.herald_notifications=(int)bit; original.notification_sound=(int)sound;
        original.enabled=1; strcpy(original.account_name,"Private account");
        strcpy(original.email,"user@example.invalid");strcpy(original.imap_host,"imap.example.invalid");
        CHECK(amg_account_copy(&copy,&original)==AMG_OK);
        CHECK(copy.herald_notifications==(int)bit && copy.notification_sound==(int)sound);
        CHECK(amg_storage_save_account("account.cfg",&original,NULL,&error)==AMG_OK);
        CHECK(amg_storage_load_account("account.cfg",NULL,&loaded,&error)==AMG_OK);
        CHECK(loaded.herald_notifications==(int)bit && loaded.notification_sound==(int)sound);
        CHECK(!strcmp(loaded.account_name,original.account_name));
        amg_account_clear(&original);amg_account_clear(&copy);amg_account_clear(&loaded);
    }
    f=fopen("account.cfg","rb");CHECK(f);n=fread(data,1,sizeof(data)-1U,f);CHECK(!ferror(f));CHECK(!fclose(f));data[n]=0;
    { char *setting=strstr(data,"herald_notifications=1\n");CHECK(setting);
      memmove(setting,setting+strlen("herald_notifications=1\n"),strlen(setting+strlen("herald_notifications=1\n"))+1U); }
    f=fopen("account-old.cfg","wb");CHECK(f);CHECK(fwrite(data,1,strlen(data),f)==strlen(data));CHECK(!fclose(f));
    amg_account_init(&loaded);loaded.herald_notifications=1;
    CHECK(amg_storage_load_account("account-old.cfg",NULL,&loaded,&error)==AMG_OK);
    CHECK(!loaded.herald_notifications && loaded.notification_sound);
    amg_account_clear(&loaded);
    /* Arbitrary, malformed or negative values never enable a new feature. */
    { const char *bad[]={"-1","true","99","1x",""};size_t i;
      for(i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        f=fopen("account-bad.cfg","wb");CHECK(f);CHECK(fputs(data,f)>=0);
        CHECK(fprintf(f,"herald_notifications=%s\n",bad[i])>=0);CHECK(!fclose(f));
        amg_account_init(&loaded);CHECK(amg_storage_load_account("account-bad.cfg",NULL,&loaded,&error)==AMG_OK);
        CHECK(!loaded.herald_notifications);amg_account_clear(&loaded);
      }
    }
    remove("account.cfg");remove("account-old.cfg");remove("account-bad.cfg");
    printf("Herald settings round-trip/migration: %u checks passed.\n",checks);
    return 0;
}
