#ifndef FFS_STORE_H
#define FFS_STORE_H

#include "common.h"

/* Load aggregates from path. Missing or unreadable file leaves s at its
 * initialized defaults and returns 0; a present-but-corrupt file returns -1
 * (s is still usable, and the file is not destroyed until the next save). */
int ffs_store_load(FfsStats *s, const char *path);

/* Persist atomically (temp file + rename). ~50 bytes per day bucket. */
int ffs_store_save(const FfsStats *s, const char *path);

#endif
