#ifndef AMIMAIL_FILEIO_H
#define AMIMAIL_FILEIO_H

#include "amigmail.h"

/* The .bak sibling is reserved for interrupted-save recovery. Callers close
 * and check the complete temporary file before calling amg_file_replace().
 * These operations are serialized by AmiMail's existing single GUI task.
 * The Amiga rename sequence is recoverable, not power-loss atomic. */
int amg_file_replace(const char *temporary, const char *path);
int amg_file_recover(const char *path);

#endif
