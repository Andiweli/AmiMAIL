#include "storage.h"
#include "tls.h"
#include "crypto.h"
#include "i18n.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef const char *CONST_STRPTR;
#define DeleteFile remove
#define T(id, text) (text)
static int fail_config, fail_promotion, random_generation;
static int read_account_salt(const char *path, unsigned char salt[16], AmgError *error);
static int save_account_internal(const char *path, const AmgAccount *account,
    const char *master, const unsigned char *key, const unsigned char *salt,
    unsigned long iterations, AmgError *error);
static int amg_file_recover(const char *path) { (void)path; return AMG_OK; }
static int amg_file_replace(const char *temporary, const char *path)
{
    if (fail_promotion && !strcmp(path, "account.key")) return AMG_ERR_IO;
    return rename(temporary, path) == 0 ? AMG_OK : AMG_ERR_IO;
}
int amg_tls_global_init(AmgError *error) { (void)error; return AMG_OK; }
void amg_tls_global_cleanup(void) {}
int amg_random_bytes(unsigned char *data, size_t length)
{
    memset(data, ++random_generation, length);
    return AMG_OK;
}
#include SUT_FILE

static int save_account_internal(const char *path, const AmgAccount *account,
    const char *master, const unsigned char *key, const unsigned char *salt,
    unsigned long iterations, AmgError *error)
{
    FILE *file;
    (void)account; (void)master; (void)key; (void)error;
    if (fail_config) return AMG_ERR_IO;
    file = fopen(path, "wb"); assert(file);
    assert(fprintf(file, "%ssecrets=aes-256-gcm\niterations=%lu\n", STORAGE_HEADER_V2, iterations) > 0);
    assert(write_hex_line(file, "salt", salt, 16) == AMG_OK);
    assert(fclose(file) == 0);
    return AMG_OK;
}

int main(void)
{
    AmgAccount account;
    AmgError error = {0};
    unsigned char salt[16], key[32], previous[32];
    unsigned long iterations;
    FILE *file;
    memset(&account, 0, sizeof(account));
    assert(amg_storage_save_account_auto("account.cfg", "account.key", &account, &error) == AMG_OK);
    assert(load_cached_storage_key("account.cfg", "account.key", salt, key, &iterations, &error) == AMG_OK);
    memcpy(previous, key, 32);
    fail_config = 1;
    assert(amg_storage_save_account_auto("account.cfg", "account.key", &account, &error) == AMG_ERR_IO);
    assert(load_cached_storage_key("account.cfg", "account.key", salt, key, &iterations, &error) == AMG_OK);
    assert(!memcmp(previous, key, 32));
    /* Trigger regeneration without destroying the previous key, then fail the
     * configuration commit. The old key bytes must still be present. */
    assert(rename("account.cfg", "saved.cfg") == 0);
    assert(amg_storage_save_account_auto("account.cfg", "account.key", &account, &error) == AMG_ERR_IO);
    assert(rename("saved.cfg", "account.cfg") == 0);
    assert(load_cached_storage_key("account.cfg", "account.key", salt, key, &iterations, &error) == AMG_OK);
    assert(!memcmp(previous, key, 32));
    fail_config = 0; fail_promotion = 1;
    assert(rename("account.cfg", "saved.cfg") == 0);
    assert(amg_storage_save_account_auto("account.cfg", "account.key", &account, &error) == AMG_ERR_IO);
    assert(load_cached_storage_key("account.cfg", "account.key", salt, key, &iterations, &error) == AMG_OK);
    assert(memcmp(previous, key, 32) && error.code == AMG_OK);
    file = fopen("account.key.pending", "rb"); assert(file); fclose(file);
    fail_promotion = 0;
    assert(load_cached_storage_key("account.cfg", "account.key", salt, key, &iterations, &error) == AMG_OK);
    assert(!fopen("account.key.pending", "rb"));
    assert(load_specific_storage_key("account.cfg", "account.key", salt, key, &iterations, &error) == AMG_OK);
    /* A staged key with the wrong salt must never unlock the active account. */
    assert(rename("account.key", "account.key.pending") == 0);
    assert(rename("account.cfg", "new.cfg") == 0);
    assert(rename("saved.cfg", "account.cfg") == 0);
    assert(load_cached_storage_key("account.cfg", "account.key", salt, key, &iterations, &error) != AMG_OK);
    puts("Key/config transactions: failed save preserves old key; committed pending key recovers and promotes; mismatched key rejected.");
    return 0;
}
