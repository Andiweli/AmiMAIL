#ifndef AMIGMAIL_STORAGE_H
#define AMIGMAIL_STORAGE_H

#include "account.h"

int amg_storage_load_account_auto(const char *account_path,
                                  const char *key_path,
                                  AmgAccount *account, AmgError *error);
int amg_storage_save_account_auto(const char *account_path,
                                  const char *key_path,
                                  const AmgAccount *account,
                                  AmgError *error);
int amg_storage_load_account(const char *path, const char *master_password,
                             AmgAccount *account, AmgError *error);
int amg_storage_save_account(const char *path, const AmgAccount *account,
                             const char *master_password, AmgError *error);
int amg_storage_save_account_cached(const char *account_path,
                                    const char *key_path,
                                    const AmgAccount *account,
                                    AmgError *error);
int amg_storage_load_legacy_master(const char *path, char *output,
                                   size_t capacity);
int amg_storage_load_account_session(const char *account_path,
                                     const char *session_path,
                                     AmgAccount *account, AmgError *error);
int amg_storage_cache_session_key(const char *account_path,
                                  const char *session_path,
                                  const char *master_password,
                                  AmgError *error);
int amg_storage_copy_session_key(const char *source_path,
                                 const char *destination_path,
                                 AmgError *error);
void amg_storage_forget_session_key(const char *session_path);
int amg_storage_account_needs_kdf_refresh(const char *path);
const char *amg_storage_account_path(size_t index);
const char *amg_storage_session_key_path(size_t index);
const char *amg_storage_persistent_key_path(size_t index);
void amg_storage_delete_account_files(size_t index);
int amg_storage_load_account_order(size_t order[AMG_MAX_ACCOUNTS]);
int amg_storage_save_account_order(const size_t order[AMG_MAX_ACCOUNTS],
                                   AmgError *error);

#endif
