#ifndef FFS_HISTORY_DB_H
#define FFS_HISTORY_DB_H

/* Read-only access to FreeFlow's Core Data history store.
 *
 * FreeFlow persists pipeline history via Core Data in a plain SQLite file.
 * The layout is undocumented, so the schema is discovered at runtime:
 * the table is found under sqlite_master and columns are matched by name
 * (timestamp / rawTranscript / postProcessedTranscript / audioFileName).
 * If a future FreeFlow version renames them, open fails cleanly and the UI
 * reports "source unavailable" instead of misreading data.
 */

typedef struct FfsDb FfsDb;

typedef struct {
    long pk;      /* Z_PK, monotonic */
    long unix_ts; /* Core Data CFAbsoluteTime converted to unix seconds */
    const char *raw;
    const char *post;
    const char *audio; /* file name inside FreeFlow's audio dir, may be NULL */
} FfsHistoryRow;

/* Open path read-only and introspect the schema. Returns 0 on success.
 * Row text pointers are valid only until the next ffs_db_next/close. */
int ffs_db_open(const char *path, FfsDb **out);

/* Highest primary key in the table, or -1 on error. Used to detect the
 * database being rebuilt (pk sequence restarts). */
long ffs_db_max_pk(FfsDb *db);

/* Prepare streaming of rows with pk > after_pk, oldest first. */
int ffs_db_begin(FfsDb *db, long after_pk);

/* 1 = row filled, 0 = no more rows, -1 = error. */
int ffs_db_next(FfsDb *db, FfsHistoryRow *row);

void ffs_db_close(FfsDb *db);

#endif
