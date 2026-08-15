#ifndef AMIGMAIL_STORAGE_H
#define AMIGMAIL_STORAGE_H

#include "account.h"

int amg_storage_load_account(const char *path, const char *master_password,
                             AmgAccount *account, AmgError *error);
int amg_storage_save_account(const char *path, const AmgAccount *account,
                             const char *master_password, AmgError *error);
int amg_storage_load_legacy_master(const char *path, char *output,
                                   size_t capacity);
int amg_storage_load_account_session(const char *account_path,
                                     const char *session_path,
                                     AmgAccount *account, AmgError *error);
int amg_storage_cache_session_key(const char *account_path,
                                  const char *session_path,
                                  const char *master_password,
                                  AmgError *error);
void amg_storage_forget_session_key(const char *session_path);

#endif
