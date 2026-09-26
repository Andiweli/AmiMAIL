#ifndef AMIMAIL_MAILFILE_H
#define AMIMAIL_MAILFILE_H

#include "mime.h"
#include "transfer.h"
#include "imap_parser.h"

/* The original message remains on disk. Offsets refer to immutable bytes;
 * only MIME metadata and a bounded, decoded text body are kept in memory. */
typedef struct AmgMailFilePart {
    size_t start, body, end;
    size_t first_child, next_sibling;
    char *name_utf8;
    char content_type[128];
    int kind;                   /* 0 other, 1 plain, 2 HTML, 3 multi, 4 alt */
    int encoding;               /* 0 raw, 1 base64, 2 quoted-printable */
    int embedded;
} AmgMailFilePart;

typedef struct AmgMailFile {
    char path[AMG_SPOOL_PATH_MAX];
    size_t length, offset;
    AmgMailHeaders headers;
    AmgMailFilePart *parts;
    size_t count, capacity, attachment_count;
    AmgBuffer text;
    int text_result;
    int remove_on_close;
    unsigned long uid;
} AmgMailFile;

int amg_mailfile_open(const char *path, int remove_on_close,
                      AmgTransfer *transfer, AmgMailFile **output,
                      AmgError *error);
int amg_mailfile_open_range(const char *path, size_t offset, size_t length,
                            int remove_on_close, AmgTransfer *transfer,
                            AmgMailFile **output, AmgError *error);
void amg_mailfile_close(AmgMailFile *file);
const AmgMailFilePart *amg_mailfile_attachment(const AmgMailFile *file,
                                              size_t index);
int amg_mailfile_summary(const AmgMailFile *file, AmgBuffer *attachments,
                         AmgBuffer *graphics);
int amg_mailfile_payload(AmgMailFile *file, const AmgImapFetchRecord *record,
                         AmgBuffer *payload, AmgError *error);
int amg_mailfile_extract(const AmgMailFile *file, size_t index, FILE *output,
                         size_t limit, size_t *written,
                         AmgTransfer *transfer, AmgError *error);

int amg_mailfile_save_attachment(const AmgMailFile *mail, size_t index,
                                 const char *directory, const char *name,
                                 char destination[AMG_SPOOL_PATH_MAX],
                                 AmgTransfer *transfer, AmgError *error);

#endif
