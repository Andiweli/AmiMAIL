#include "fileio.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if AMIGMAIL_AMIGA
#include <dos/dos.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#else
#include <errno.h>
#include <sys/stat.h>
#endif

/* 1: regular file; 0: absent; -1: directory, device or access error. */
static int file_state(const char *path)
{
#if AMIGMAIL_AMIGA
    BPTR lock = Lock((CONST_STRPTR)path, ACCESS_READ);
    struct FileInfoBlock *info;
    int result = -1;
    if (!lock) return IoErr() == ERROR_OBJECT_NOT_FOUND ? 0 : -1;
    info = (struct FileInfoBlock *)AllocDosObject(DOS_FIB, NULL);
    if (info) {
        if (Examine(lock, info) && info->fib_DirEntryType < 0) result = 1;
        FreeDosObject(DOS_FIB, info);
    }
    UnLock(lock);
    return result;
#else
    struct stat info;
    if (stat(path, &info) == 0) return S_ISREG(info.st_mode) ? 1 : -1;
    return errno == ENOENT ? 0 : -1;
#endif
}

static int move_file(const char *from, const char *to)
{
    /* Amiga Rename() cannot replace an existing target. Enforce the same
     * condition on the host, including in fault-injection regression tests. */
    if (file_state(to) != 0) return 0;
#if AMIGMAIL_AMIGA
    return Rename((CONST_STRPTR)from, (CONST_STRPTR)to) != 0;
#else
    return rename(from, to) == 0;
#endif
}

static int delete_file(const char *path)
{
#if AMIGMAIL_AMIGA
    return DeleteFile((CONST_STRPTR)path) != 0;
#else
    return remove(path) == 0;
#endif
}

static char *backup_name(const char *path)
{
    size_t length;
    char *backup;
    if (!path || !*path) return NULL;
    length = strlen(path);
    if (length > SIZE_MAX - 5U) return NULL;
    backup = (char *)malloc(length + 5U);
    if (backup) {
        memcpy(backup, path, length);
        memcpy(backup + length, ".bak", 5U);
    }
    return backup;
}

int amg_file_recover(const char *path)
{
    char *backup;
    int state, saved;
    if (!path || !*path) return AMG_ERR_ARGUMENT;
    state = file_state(path);
    if (state == 1) return AMG_OK;
    if (state < 0) return AMG_ERR_IO;
    backup = backup_name(path);
    if (!backup) return AMG_ERR_MEMORY;
    saved = file_state(backup);
    if (saved == 1) state = move_file(backup, path) ? 1 : -1;
    else if (saved < 0) state = -1;
    free(backup);
    return state < 0 ? AMG_ERR_IO : AMG_OK;
}

int amg_file_replace(const char *temporary, const char *path)
{
    char *backup;
    int old_state, saved_state, result = AMG_ERR_IO;
    if (!temporary || !*temporary || !path || !*path ||
        !strcmp(temporary, path))
        return AMG_ERR_ARGUMENT;
    backup = backup_name(path);
    if (!backup) return AMG_ERR_MEMORY;
    /* Never rename the only recovery copy as if it were a new candidate. */
    if (!strcmp(temporary, backup) || file_state(temporary) != 1)
        goto done;
    if (amg_file_recover(path) != AMG_OK) goto done;
    old_state = file_state(path);
    saved_state = file_state(backup);
    if (old_state < 0 || saved_state < 0) goto done;
    if (old_state == 0) {
        if (saved_state != 0) goto done;
        result = move_file(temporary, path) ? AMG_OK : AMG_ERR_IO;
        goto done;
    }
    /* A present destination is authoritative. A leftover .bak can only be
     * retired while that destination still exists. Never delete path here. */
    if (saved_state == 1 && !delete_file(backup)) goto done;
    if (!move_file(path, backup)) goto done;
    if (move_file(temporary, path)) {
        /* Failure to remove a backup does NOT mean the new save failed. */
        (void)delete_file(backup);
        result = AMG_OK;
    } else {
        /* If restoration fails too, leave .bak untouched for recovery on
         * the next load/save. Never discard the last known-good copy. */
        (void)move_file(backup, path);
    }
done:
    free(backup);
    return result;
}
