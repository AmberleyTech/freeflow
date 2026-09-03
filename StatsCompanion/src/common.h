/* FreeFlow Stats sidecar — shared model and constants.
 *
 * Resource budgets (enforced by --memcheck / --diskcheck):
 *   RAM:  no persistent process; peak RSS of a single run must stay < 10 MB.
 *   Disk: hundreds of days of aggregates must fit in < 5 MB.
 *
 * The store holds aggregates only. Transcript text is never persisted.
 */
#ifndef FFS_COMMON_H
#define FFS_COMMON_H

#include <stddef.h>

#define FFS_VERSION 1

/* Hard bounds so memory and disk can never grow without limit. */
#define FFS_MAX_DAYS 4096 /* ~11 years of per-day buckets */
#define FFS_RECENT 20     /* recent-event ring for "current" WPM */
#define FFS_SEEN_PKS 64   /* live-window PKs already counted */

#define FFS_DISK_BUDGET_BYTES (5L * 1024 * 1024) /* user requirement */
#define FFS_DISKCHECK_LIMIT_BYTES (500L * 1024)  /* 10x stricter gate */
#define FFS_RSS_LIMIT_KB (10L * 1024)            /* user requirement */

typedef struct {
    char date[11]; /* "YYYY-MM-DD" */
    long count;
    long words;
    double seconds; /* dictation seconds with known audio duration */
} FfsDay;

typedef struct {
    long ts;        /* unix seconds */
    long words;
    double seconds; /* < 0 when audio duration is unknown */
} FfsEvent;

typedef struct {
    int version;
    double typing_wpm; /* user preference, default 40 */
    long total_count;
    long total_words;
    double total_seconds; /* dictation seconds over events with known duration */
    long with_audio;
    long without_audio;
    long audio_words; /* words from events with known duration (for WPM) */
    long first_ts; /* "tracking since" */
    long last_ts;
    long watermark; /* max history Z_PK observed (rebuild detection) */
    int source_ok;  /* whether the last run could read FreeFlow's database */
    FfsDay days[FFS_MAX_DAYS]; /* sorted ascending by date */
    int day_count;
    FfsEvent recent[FFS_RECENT]; /* chronological */
    int recent_len;
    long seen_pk[FFS_SEEN_PKS]; /* PKs already folded into totals */
    int seen_pk_len;
} FfsStats;

void ffs_stats_init(FfsStats *s);
/* Record one completed transcription. seconds < 0 means unknown duration. */
void ffs_stats_add_event(FfsStats *s, long ts, long words, double seconds);

#endif
