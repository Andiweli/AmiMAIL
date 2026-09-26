#include "mailfile.h"
#include "i18n.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#if AMIGMAIL_AMIGA
#include <dos/dos.h>
#include <proto/dos.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif
#define T(id, en) amg_tr((id), (en))

/* Publish without replacing a destination, including one created between a
 * name check and the commit. Return 1 on success, 0 on collision, -1 on I/O. */
static int publish_new(const char *temporary, const char *destination)
{
#if AMIGMAIL_AMIGA
    if (Rename((CONST_STRPTR)temporary, (CONST_STRPTR)destination)) return 1;
    return IoErr() == ERROR_OBJECT_EXISTS ? 0 : -1;
#elif defined(_WIN32)
    if (MoveFileA(temporary, destination)) return 1;
    return GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
#else
    if (link(temporary, destination) == 0) {
        (void)remove(temporary);
        return 1;
    }
    return errno == EEXIST ? 0 : -1;
#endif
}

int amg_mailfile_save_attachment(const AmgMailFile *mail, size_t index,
                                 const char *directory, const char *name,
                                 char destination[AMG_SPOOL_PATH_MAX],
                                 AmgTransfer *transfer, AmgError *error)
{
    char temporary[AMG_SPOOL_PATH_MAX];
    FILE *file = NULL;
    size_t length, written = 0U, i;
    unsigned long suffix;
    const char *separator;
    int result;
    if (!mail || !directory || !*directory || !name || !*name || !destination)
        return AMG_ERR_ARGUMENT;
    destination[0] = 0;
    /* Callers convert the MIME name to the local charset. Reject path
     * components here as well, so the library API cannot escape the drawer. */
    if (!strcmp(name, ".") || !strcmp(name, "..")) return AMG_ERR_ARGUMENT;
    for (i = 0U; name[i]; ++i)
        if ((unsigned char)name[i] < 32U || name[i] == '/' ||
            name[i] == '\\' || name[i] == ':') return AMG_ERR_ARGUMENT;
    length = strlen(directory);
    separator = directory[length - 1U] == '/' || directory[length - 1U] == ':' ? "" : "/";
    result = amg_spool_create(directory, temporary, &file, error);
    if (result != AMG_OK) return result;
    result = amg_mailfile_extract(mail, index, file, AMIMAIL_MAX_MIME_MESSAGE,
                                  &written, transfer, error);
    if (fclose(file) != 0 && result == AMG_OK) result = AMG_ERR_IO;
    if (result == AMG_OK)
        result = amg_transfer_report(transfer, AMG_TRANSFER_EXPORT,
                                      written, written, error);
    if (result == AMG_OK) {
        result = AMG_ERR_LIMIT;
        for (suffix = 0UL; suffix <= 9999UL; ++suffix) {
            int n, publish;
            if (suffix)
                n = snprintf(destination, AMG_SPOOL_PATH_MAX, "%s%s%s.%lu",
                             directory, separator, name, suffix);
            else
                n = snprintf(destination, AMG_SPOOL_PATH_MAX, "%s%s%s",
                             directory, separator, name);
            if (n < 0 || (size_t)n >= AMG_SPOOL_PATH_MAX) break;
            publish = publish_new(temporary, destination);
            if (publish > 0) { result = AMG_OK; break; }
            if (publish < 0) { result = AMG_ERR_IO; break; }
        }
    }
    amg_spool_remove(temporary);
    if (result != AMG_OK) {
        destination[0] = 0;
        if (!error || error->code == AMG_OK)
            amg_error_set(error, result,
                T(MSG_ATTACHMENT_COULD_NOT_BE_WRITTEN_TO_DISK,
                  "Attachment could not be written to disk."));
    }
    return result;
}
