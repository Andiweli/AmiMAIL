#ifndef AMIMAIL_UPDATE_H
#define AMIMAIL_UPDATE_H

#include "amigmail.h"

#define AMG_UPDATE_TAG_MAX 32U
#define AMG_UPDATE_URL_MAX 768U
#define AMG_UPDATE_PATH_MAX 256U

typedef struct AmgUpdateInfo {
    char tag[AMG_UPDATE_TAG_MAX];
    char download_url[AMG_UPDATE_URL_MAX];
} AmgUpdateInfo;

int amg_update_is_newer(const char *candidate_tag,
                        const char *current_version);
int amg_update_parse_latest_json(const unsigned char *json, size_t length,
                                 AmgUpdateInfo *info, AmgError *error);
int amg_update_check_latest(AmgUpdateInfo *info, AmgError *error);
int amg_update_download(const char *url, const char *destination,
                        AmgError *error);

#endif
