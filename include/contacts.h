#ifndef AMIGMAIL_CONTACTS_H
#define AMIGMAIL_CONTACTS_H

#include "amigmail.h"

#include <stddef.h>

#define AMG_CONTACT_FIRST_NAME_MAX 128U
#define AMG_CONTACT_LAST_NAME_MAX 128U
#define AMG_CONTACT_COMPANY_MAX 192U
#define AMG_CONTACT_EMAIL_MAX 256U
#define AMG_CONTACT_PHONE_MAX 128U
#define AMG_CONTACT_WEBSITE_MAX 512U

#define AMG_CONTACTS_DEFAULT_PATH "ENVARC:AmiMail/contacts.dat"
#define AMG_CONTACTS_TEMP_PATH "ENVARC:AmiMail/contacts.dat.new"

typedef struct AmgContact {
    unsigned long id;
    char first_name[AMG_CONTACT_FIRST_NAME_MAX];
    char last_name[AMG_CONTACT_LAST_NAME_MAX];
    char company[AMG_CONTACT_COMPANY_MAX];
    char email[AMG_CONTACT_EMAIL_MAX];
    char phone[AMG_CONTACT_PHONE_MAX];
    char mobile[AMG_CONTACT_PHONE_MAX];
    char website[AMG_CONTACT_WEBSITE_MAX];
} AmgContact;

typedef struct AmgContactBook {
    AmgContact *items;
    size_t count;
    size_t capacity;
    unsigned long next_id;
} AmgContactBook;

typedef struct AmgContactImportResult {
    size_t records;
    size_t imported;
    size_t duplicates;
    size_t skipped;
} AmgContactImportResult;

void amg_contacts_init(AmgContactBook *book);
void amg_contacts_free(AmgContactBook *book);
int amg_contacts_load(const char *path, AmgContactBook *book, AmgError *error);
int amg_contacts_save(const char *path, const AmgContactBook *book,
                      AmgError *error);
const AmgContact *amg_contacts_find(const AmgContactBook *book,
                                    unsigned long id);
AmgContact *amg_contacts_find_mutable(AmgContactBook *book,
                                      unsigned long id);
int amg_contacts_is_duplicate(const AmgContactBook *book,
                              const AmgContact *candidate,
                              unsigned long ignore_id);
int amg_contacts_add(AmgContactBook *book, const AmgContact *contact,
                     unsigned long *new_id, AmgError *error);
int amg_contacts_update(AmgContactBook *book, const AmgContact *contact,
                        AmgError *error);
int amg_contacts_delete(AmgContactBook *book, unsigned long id,
                        AmgError *error);
void amg_contact_trim(AmgContact *contact);
int amg_contact_has_data(const AmgContact *contact);

int amg_contacts_import_file(const char *path, AmgContactBook *book,
                             AmgContactImportResult *result,
                             AmgError *error);
int amg_contacts_import_csv(const char *path, AmgContactBook *book,
                            AmgContactImportResult *result,
                            AmgError *error);
int amg_contacts_import_vcf(const char *path, AmgContactBook *book,
                            AmgContactImportResult *result,
                            AmgError *error);

#endif
