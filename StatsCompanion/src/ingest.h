#ifndef FFS_INGEST_H
#define FFS_INGEST_H

#include "common.h"

/* Ingest new history rows into the aggregate store.
 *
 * db_path/audio_dir of NULL mean auto-detect FreeFlow's data. *added
 * receives the number of newly counted transcriptions. Returns 0 on success
 * (including "source unavailable", reflected in st->source_ok), -1 on error.
 *
 * Rows are consumed one at a time; transcript text is counted in place and
 * never copied or persisted. The history database content is treated as
 * untrusted input.
 */
int ffs_ingest(FfsStats *st, const char *db_path, const char *audio_dir,
               long *added);

#endif
