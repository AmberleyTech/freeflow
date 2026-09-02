#ifndef FFS_PATHS_H
#define FFS_PATHS_H

#include <stddef.h>

/* Resolve the sidecar's data directory (created if missing):
 * $FFS_DATA_DIR, else ~/Library/Application Support/FreeFlowStats. */
int ffs_data_dir(char *buf, size_t n);

/* Locate FreeFlow's history database and audio directory.
 * $FFS_HISTORY_DB / $FFS_AUDIO_DIR override; otherwise both app-name
 * variants ("FreeFlow", "FreeFlow Dev") are probed and the most recently
 * modified database wins. Returns 0 when found. */
int ffs_find_history(char *db, size_t dn, char *audio, size_t an);

/* Paths inside the data directory. */
void ffs_store_path(char *buf, size_t n);
void ffs_html_path(char *buf, size_t n);
void ffs_lock_path(char *buf, size_t n);

#endif
