#include "ingest.h"
#include "history_db.h"
#include "paths.h"
#include "stats_math.h"
#include "wav.h"

#include <stdio.h>
#include <string.h>

/* Audio file names come from FreeFlow's database (untrusted input): accept
 * plain file names only, never paths. */
static int safe_filename(const char *name) {
    if (!name || !name[0])
        return 0;
    if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\'))
        return 0;
    return 1;
}

static int already_seen(const FfsStats *st, long pk) {
    for (int i = 0; i < st->seen_pk_len; i++) {
        if (st->seen_pk[i] == pk)
            return 1;
    }
    return 0;
}

static void remember_pk(FfsStats *st, long pk) {
    if (already_seen(st, pk))
        return;
    if (st->seen_pk_len == FFS_SEEN_PKS) {
        memmove(st->seen_pk, st->seen_pk + 1,
                sizeof(long) * (FFS_SEEN_PKS - 1));
        st->seen_pk_len--;
    }
    st->seen_pk[st->seen_pk_len++] = pk;
}

int ffs_ingest(FfsStats *st, const char *db_path, const char *audio_dir,
               long *added) {
    char found_db[1024];
    char found_audio[1024];
    *added = 0;

    if (!db_path) {
        if (ffs_find_history(found_db, sizeof(found_db), found_audio,
                             sizeof(found_audio)) != 0) {
            st->source_ok = 0;
            return 0;
        }
        db_path = found_db;
        audio_dir = found_audio;
    }

    FfsDb *db = NULL;
    if (ffs_db_open(db_path, &db) != 0) {
        st->source_ok = 0;
        return 0; /* schema changed upstream; report, don't corrupt */
    }

    /* If the database was rebuilt, primary keys restart below our watermark.
     * Re-scan but skip events at or before the last one we counted. */
    int rebuilt = 0;
    long max_pk = ffs_db_max_pk(db);
    if (max_pk >= 0 && st->watermark > 0 && max_pk < st->watermark) {
        st->watermark = 0;
        st->seen_pk_len = 0;
        rebuilt = 1;
    }

    /* Legacy stores only had a watermark. Treat those PKs as already counted
     * so an upgrade does not double-count the live window. */
    int legacy = (!rebuilt && st->seen_pk_len == 0 && st->watermark > 0);

    long high_water = st->watermark;
    /* Always scan the live window (FreeFlow keeps <= 20 rows). Dedup via
     * seen_pk so an empty first write can still be counted on retry. */
    int rc = ffs_db_begin(db, 0);
    if (rc == 0) {
        FfsHistoryRow row;
        while ((rc = ffs_db_next(db, &row)) == 1) {
            if (row.unix_ts <= 0)
                continue;
            if (rebuilt && st->last_ts > 0 && row.unix_ts <= st->last_ts)
                continue; /* already counted before the rebuild */
            if (already_seen(st, row.pk))
                continue;
            if (legacy && row.pk <= st->watermark) {
                remember_pk(st, row.pk);
                if (row.pk > high_water)
                    high_water = row.pk;
                continue;
            }

            const char *text = (row.post && row.post[0]) ? row.post : row.raw;
            long words = ffs_count_words(text);
            if (words == 0)
                continue; /* empty / failed: do not remember, retry can count */

            double seconds = -1.0;
            if (audio_dir && audio_dir[0] && safe_filename(row.audio)) {
                char wav_path[1200];
                snprintf(wav_path, sizeof(wav_path), "%s/%s", audio_dir,
                         row.audio);
                seconds = ffs_wav_duration(wav_path);
            }
            ffs_stats_add_event(st, row.unix_ts, words, seconds);
            remember_pk(st, row.pk);
            if (row.pk > high_water)
                high_water = row.pk;
            (*added)++;
        }
    }

    st->watermark = high_water;
    st->source_ok = (rc == 0) ? 1 : 0;
    ffs_db_close(db);
    return rc == 0 ? 0 : -1;
}
