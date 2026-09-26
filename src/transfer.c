#include "transfer.h"
#include "i18n.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if AMIGMAIL_AMIGA
#include <dos/dos.h>
#include <dos/var.h>
#include <proto/dos.h>
#include <proto/exec.h>
#else
#include <sys/stat.h>
#if defined(_WIN32)
#include <direct.h>
#include <process.h>
#else
#include <unistd.h>
#endif
#endif
#define T(id, en) amg_tr((id), (en))

int amg_transfer_report(AmgTransfer *transfer, AmgTransferPhase phase,
                        size_t done, size_t total, AmgError *error)
{
    if (transfer && transfer->report &&
        !transfer->report(transfer->context, phase, done, total)) {
        amg_error_set(error, AMG_ERR_CANCELLED,
                      T(MSG_TRANSFER_CANCELLED, "Transfer cancelled."));
        return AMG_ERR_CANCELLED;
    }
    return AMG_OK;
}

static int create_directory(const char *path)
{
#if AMIGMAIL_AMIGA
    BPTR lock = CreateDir((CONST_STRPTR)path);
    if (!lock) return 0;
    UnLock(lock);
    return 1;
#elif defined(_WIN32)
    return _mkdir(path) == 0;
#else
    return mkdir(path, 0700) == 0;
#endif
}

int amg_spool_directory(char path[AMG_SPOOL_PATH_MAX], AmgError *error)
{
    int ok = 0;
    if (!path) return AMG_ERR_ARGUMENT;
    path[0] = 0;
#if AMIGMAIL_AMIGA
    {
        char configured[AMG_SPOOL_PATH_MAX];
        BPTR lock;
        LONG length = GetVar((CONST_STRPTR)"AmiMAIL_SpoolDir",
                              (STRPTR)configured, (LONG)sizeof(configured),
                              GVF_GLOBAL_ONLY);
        if (length < 0)
            strcpy(configured, "PROGDIR:Spool");
        else if (!length || length >= (LONG)sizeof(configured) - 1L)
            goto fail;
        lock = Lock((CONST_STRPTR)configured, ACCESS_READ);
        if (!lock) {
            (void)create_directory(configured);
            lock = Lock((CONST_STRPTR)configured, ACCESS_READ);
        }
        if (lock) {
            ok = NameFromLock(lock, (STRPTR)path,
                              (LONG)AMG_SPOOL_PATH_MAX) != 0;
            UnLock(lock);
        }
    }
fail:
#else
    {
        const char *configured = getenv("AmiMAIL_SpoolDir");
        struct stat st;
        if (!configured || !*configured) configured = "build/spool";
        if (strlen(configured) < AMG_SPOOL_PATH_MAX - 48U) {
            strcpy(path, configured);
            if (stat(path, &st) != 0) (void)create_directory(path);
            ok = stat(path, &st) == 0 && S_ISDIR(st.st_mode);
        }
    }
#endif
    if (!ok || strlen(path) >= AMG_SPOOL_PATH_MAX - 48U) {
        path[0] = 0;
        amg_error_set(error, AMG_ERR_IO,
            T(MSG_SPOOL_UNAVAILABLE,
              "Temporary folder unavailable. Check AmiMAIL_SpoolDir."));
        return AMG_ERR_IO;
    }
    return AMG_OK;
}

int amg_spool_create(const char *directory, char path[AMG_SPOOL_PATH_MAX],
                     FILE **file, AmgError *error)
{
    static unsigned long serial;
    char folder[AMG_SPOOL_PATH_MAX];
    unsigned long owner, number;
    unsigned attempt;
    if (!path || !file) return AMG_ERR_ARGUMENT;
    if (!directory || !*directory) {
        *file = NULL; path[0] = 0;
        amg_error_set(error, AMG_ERR_IO,
            T(MSG_SPOOL_UNAVAILABLE,
              "Temporary folder unavailable. Check AmiMAIL_SpoolDir."));
        return AMG_ERR_IO;
    }
    *file = NULL;
    path[0] = 0;
#if AMIGMAIL_AMIGA
    owner = (unsigned long)(uintptr_t)FindTask(NULL);
#elif defined(_WIN32)
    owner = (unsigned long)_getpid();
#else
    owner = (unsigned long)getpid();
#endif
    for (attempt = 0U; attempt < 128U; ++attempt) {
        int written;
#if AMIGMAIL_AMIGA
        Forbid();
#endif
        number = ++serial;
#if AMIGMAIL_AMIGA
        Permit();
#endif
        written = snprintf(folder, sizeof(folder), "%s%sjob-%08lx-%08lx",
                            directory,
                            directory[strlen(directory) - 1U] == ':' ||
                            directory[strlen(directory) - 1U] == '/' ? "" : "/",
                            owner, number);
        if (written < 0 || (size_t)written + 13U >= sizeof(folder)) break;
        /* An exclusively created directory prevents truncating an existing
         * file, even with old Amiga stdio implementations without fopen x. */
        if (!create_directory(folder)) continue;
        memcpy(path, folder, (size_t)written);
        memcpy(path + written, "/message.eml", 13U);
        *file = fopen(path, "wb");
        if (*file) return AMG_OK;
        amg_spool_remove(path);
        break;
    }
    path[0] = 0;
    amg_error_set(error, AMG_ERR_IO,
        T(MSG_SPOOL_CREATE_FAILED, "Temporary file could not be created."));
    return AMG_ERR_IO;
}

void amg_spool_remove(const char *path)
{
    char folder[AMG_SPOOL_PATH_MAX];
    char *name;
    if (!path || !*path || strlen(path) >= sizeof(folder)) return;
    strcpy(folder, path);
    name = strrchr(folder, '/');
    if (!name || strcmp(name + 1, "message.eml")) return;
    *name = 0;
    name = strrchr(folder, '/');
    if (!name) name = strrchr(folder, ':');
    if (!name || strncmp(name + 1, "job-", 4U)) return;
#if AMIGMAIL_AMIGA
    if (DeleteFile((CONST_STRPTR)path) || IoErr() == ERROR_OBJECT_NOT_FOUND)
        (void)DeleteFile((CONST_STRPTR)folder);
#elif defined(_WIN32)
    if (remove(path) == 0 || errno == ENOENT) (void)_rmdir(folder);
#else
    if (remove(path) == 0 || errno == ENOENT) (void)rmdir(folder);
#endif
}
