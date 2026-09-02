#include "history_db.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* CFAbsoluteTime epoch (2001-01-01 UTC) offset from the unix epoch. */
#define CF_UNIX_EPOCH_DELTA 978307200.0

struct FfsDb {
    sqlite3 *db;
    sqlite3_stmt *stmt;
    char table[64];
    char col_pk[64];
    char col_ts[64];
    char col_raw[64];
    char col_post[64];
    char col_audio[64]; /* empty when the schema has no audio column */
};

static int contains_ci(const char *haystack, const char *needle) {
    size_t nl = strlen(needle);
    if (nl == 0)
        return 1;
    for (const char *h = haystack; *h; h++) {
        size_t i = 0;
        while (i < nl && h[i] &&
               (char)(h[i] >= 'A' && h[i] <= 'Z' ? h[i] + 32 : h[i]) ==
                   (char)(needle[i] >= 'A' && needle[i] <= 'Z'
                              ? needle[i] + 32
                              : needle[i]))
            i++;
        if (i == nl)
            return 1;
    }
    return 0;
}

static void copy_ident(char *dst, size_t n, const char *src) {
    size_t i = 0;
    for (; src[i] && i + 1 < n; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

/* Inspect one table; fill db fields and return 1 when it looks like the
 * pipeline-history table. */
static int probe_table(FfsDb *db, const char *table) {
    char sql[128];
    snprintf(sql, sizeof(sql), "PRAGMA table_info(\"%s\")", table);
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
        return 0;

    char pk[64] = "", ts[64] = "", raw[64] = "", post[64] = "", audio[64] = "";
    while (sqlite3_step(st) == SQLITE_ROW) {
        const char *name = (const char *)sqlite3_column_text(st, 1);
        if (!name)
            continue;
        if (!pk[0] &&
            (strcmp(name, "Z_PK") == 0 || contains_ci(name, "_PK")))
            copy_ident(pk, sizeof(pk), name);
        else if (!ts[0] && contains_ci(name, "TIMESTAMP"))
            copy_ident(ts, sizeof(ts), name);
        else if (!raw[0] && contains_ci(name, "RAWTRANSCRIPT"))
            copy_ident(raw, sizeof(raw), name);
        else if (!post[0] && contains_ci(name, "POSTPROCESSEDTRANSCRIPT"))
            copy_ident(post, sizeof(post), name);
        else if (!audio[0] && contains_ci(name, "AUDIOFILENAME"))
            copy_ident(audio, sizeof(audio), name);
    }
    sqlite3_finalize(st);

    if (!pk[0] || !ts[0] || !raw[0] || !post[0])
        return 0;
    copy_ident(db->table, sizeof(db->table), table);
    copy_ident(db->col_pk, sizeof(db->col_pk), pk);
    copy_ident(db->col_ts, sizeof(db->col_ts), ts);
    copy_ident(db->col_raw, sizeof(db->col_raw), raw);
    copy_ident(db->col_post, sizeof(db->col_post), post);
    copy_ident(db->col_audio, sizeof(db->col_audio), audio);
    return 1;
}

static int introspect(FfsDb *db) {
    sqlite3_stmt *st = NULL;
    const char *sql = "SELECT name FROM sqlite_master WHERE type='table' "
                      "AND name LIKE 'Z%' ORDER BY name";
    if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) != SQLITE_OK)
        return -1;

    /* Pass 1 prefers names containing PIPELINEHISTORY; pass 2 accepts any
     * table with the required columns. */
    char candidates[16][64];
    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW && n < 16) {
        const char *name = (const char *)sqlite3_column_text(st, 0);
        if (name)
            copy_ident(candidates[n++], 64, name);
    }
    sqlite3_finalize(st);

    int fallback = -1;
    for (int i = 0; i < n; i++) {
        FfsDb probe = {.db = db->db};
        if (probe_table(&probe, candidates[i])) {
            if (contains_ci(candidates[i], "PIPELINEHISTORY")) {
                *db = probe;
                return 0;
            }
            if (fallback < 0)
                fallback = i;
        }
    }
    if (fallback >= 0) {
        FfsDb probe = {.db = db->db};
        if (probe_table(&probe, candidates[fallback])) {
            *db = probe;
            return 0;
        }
    }
    return -1;
}

int ffs_db_open(const char *path, FfsDb **out) {
    *out = NULL;
    FfsDb *db = calloc(1, sizeof(*db));
    if (!db)
        return -1;

    char uri[1200];
    snprintf(uri, sizeof(uri), "file:%s?mode=ro", path);
    if (sqlite3_open_v2(uri, &db->db,
                        SQLITE_OPEN_READONLY | SQLITE_OPEN_URI,
                        NULL) != SQLITE_OK) {
        sqlite3_close(db->db);
        free(db);
        return -1;
    }
    sqlite3_exec(db->db, "PRAGMA query_only=ON", NULL, NULL, NULL);
    sqlite3_busy_timeout(db->db, 250);

    if (introspect(db) != 0) {
        sqlite3_close(db->db);
        free(db);
        return -1;
    }
    *out = db;
    return 0;
}

long ffs_db_max_pk(FfsDb *db) {
    char sql[192];
    snprintf(sql, sizeof(sql), "SELECT MAX(\"%s\") FROM \"%s\"", db->col_pk,
             db->table);
    sqlite3_stmt *st = NULL;
    long result = -1;
    if (sqlite3_prepare_v2(db->db, sql, -1, &st, NULL) == SQLITE_OK &&
        sqlite3_step(st) == SQLITE_ROW &&
        sqlite3_column_type(st, 0) != SQLITE_NULL)
        result = (long)sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return result;
}

int ffs_db_begin(FfsDb *db, long after_pk) {
    if (db->stmt) {
        sqlite3_finalize(db->stmt);
        db->stmt = NULL;
    }
    char sql[448];
    if (db->col_audio[0])
        snprintf(sql, sizeof(sql),
                 "SELECT \"%s\",\"%s\",\"%s\",\"%s\",\"%s\" FROM \"%s\" "
                 "WHERE \"%s\" > ?1 ORDER BY \"%s\" LIMIT 4096",
                 db->col_pk, db->col_ts, db->col_raw, db->col_post,
                 db->col_audio, db->table, db->col_pk, db->col_pk);
    else
        snprintf(sql, sizeof(sql),
                 "SELECT \"%s\",\"%s\",\"%s\",\"%s\" FROM \"%s\" "
                 "WHERE \"%s\" > ?1 ORDER BY \"%s\" LIMIT 4096",
                 db->col_pk, db->col_ts, db->col_raw, db->col_post, db->table,
                 db->col_pk, db->col_pk);
    if (sqlite3_prepare_v2(db->db, sql, -1, &db->stmt, NULL) != SQLITE_OK)
        return -1;
    sqlite3_bind_int64(db->stmt, 1, (sqlite3_int64)after_pk);
    return 0;
}

int ffs_db_next(FfsDb *db, FfsHistoryRow *row) {
    if (!db->stmt)
        return -1;
    int rc = sqlite3_step(db->stmt);
    if (rc == SQLITE_DONE)
        return 0;
    if (rc != SQLITE_ROW)
        return -1;

    row->pk = (long)sqlite3_column_int64(db->stmt, 0);
    double cf = sqlite3_column_double(db->stmt, 1);
    row->unix_ts = (long)(cf + CF_UNIX_EPOCH_DELTA + 0.5);
    row->raw = (const char *)sqlite3_column_text(db->stmt, 2);
    row->post = (const char *)sqlite3_column_text(db->stmt, 3);
    row->audio = db->col_audio[0]
                     ? (const char *)sqlite3_column_text(db->stmt, 4)
                     : NULL;
    return 1;
}

void ffs_db_close(FfsDb *db) {
    if (!db)
        return;
    if (db->stmt)
        sqlite3_finalize(db->stmt);
    sqlite3_close(db->db);
    free(db);
}
