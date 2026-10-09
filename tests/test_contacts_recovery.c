/* Exercise the actual contact reader/writer and shared recovery policy.
 * Only a denied fopen is injected; interrupted renames use real files. */
#include "contacts.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char *denied_read;
static FILE *contacts_test_open(const char *path, const char *mode)
{
    if (denied_read && !strcmp(path, denied_read) && !strcmp(mode, "rb")) {
        errno = EACCES;
        return NULL;
    }
    return fopen(path, mode);
}
#define fopen contacts_test_open
#include "../src/contacts.c"
#undef fopen

static unsigned checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; \
    fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

int main(void)
{
    const char *path = "build/contact-recovery.dat";
    const char *legacy = "build/contact-recovery.dat.old";
    const char *backup = "build/contact-recovery.dat.bak";
    AmgContactBook book, loaded;
    AmgContact contact;
    AmgError error = {0};
    FILE *file;
    unsigned pass;
    (void)remove(path); (void)remove(legacy); (void)remove(backup);
    amg_contacts_init(&book); amg_contacts_init(&loaded);
    memset(&contact, 0, sizeof(contact));
    snprintf(contact.email, sizeof(contact.email), "recovery@example.com");
    CHECK(amg_contacts_add(&book, &contact, NULL, &error) == AMG_OK);

    /* Both historical .old and current .bak copies survive a restart. */
    for (pass = 0U; pass < 2U; ++pass) {
        const char *saved = pass ? backup : legacy;
        CHECK(amg_contacts_save(path, &book, &error) == AMG_OK);
        CHECK(rename(path, saved) == 0);
        CHECK(amg_contacts_load(path, &loaded, &error) == AMG_OK);
        CHECK(loaded.count == 1U);
        CHECK(loaded.count && !strcmp(loaded.items[0].email, contact.email));
        CHECK(amg_contacts_save(path, &loaded, &error) == AMG_OK);
        CHECK(amg_contacts_load(path, &loaded, &error) == AMG_OK);
        CHECK(loaded.count == 1U);
    }
    /* Saving before loading also recovers the previous sole copy first. */
    CHECK(rename(path, legacy) == 0);
    CHECK(amg_contacts_save(path, &book, &error) == AMG_OK);
    CHECK(amg_contacts_load(path, &loaded, &error) == AMG_OK);
    CHECK(loaded.count == 1U);

    /* Read failure must report an error and retain the current in-memory book. */
    denied_read = path;
    CHECK(amg_contacts_load(path, &loaded, &error) == AMG_ERR_IO);
    CHECK(error.code == AMG_ERR_IO && loaded.count == 1U);
    denied_read = NULL;
    CHECK(amg_contacts_load(path, &loaded, &error) == AMG_OK);
    CHECK(loaded.count == 1U);
    /* An invalid recovery candidate is retained instead of overwritten. */
    CHECK(rename(path, backup) == 0);
    CHECK(amg_contacts_load(backup, &loaded, &error) == AMG_OK);
    CHECK(loaded.count == 1U);
    file = fopen(path, "wb"); CHECK(file != NULL);
    if (file) { CHECK(fputs("invalid\n", file) >= 0); CHECK(fclose(file) == 0); }
    CHECK(amg_contacts_load(path, &loaded, &error) == AMG_ERR_PARSE);
    CHECK(loaded.count == 1U);
    (void)remove(path); (void)remove(backup); (void)remove(legacy);
    CHECK(amg_contacts_load(path, &loaded, &error) == AMG_OK);
    CHECK(loaded.count == 0U);
    amg_contacts_free(&loaded); amg_contacts_free(&book);
    printf("contacts-recovery: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
