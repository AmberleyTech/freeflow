/* Dependency-free test harness + resource-budget self-checks.
 *
 * All fixtures are synthetic and invented for these tests; no real
 * transcripts, audio, or user paths are ever used. Compiled into the
 * shipping binary so `freeflow-stats --test/--memcheck/--diskcheck` can
 * self-verify on the user's machine.
 */
#include "../src/common.h"
#include "../src/history_db.h"
#include "../src/html.h"
#include "../src/ingest.h"
#include "../src/paths.h"
#include "../src/selfcheck.h"
#include "../src/stats_math.h"
#include "../src/store.h"
#include "../src/wav.h"

#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static int g_checks;
static int g_failures;

#define CHECK(cond)                                                        \
    do {                                                                   \
        g_checks++;                                                        \
        if (!(cond)) {                                                     \
            g_failures++;                                                  \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
        }                                                                  \
    } while (0)

static long mk_ts(int y, int m, int d, int hh, int mm) {
    struct tm tmv;
    memset(&tmv, 0, sizeof(tmv));
    tmv.tm_year = y - 1900;
    tmv.tm_mon = m - 1;
    tmv.tm_mday = d;
    tmv.tm_hour = hh;
    tmv.tm_min = mm;
    return (long)timegm(&tmv);
}

/* ---------------------------------------------------------------- fixtures */

#define CF_EPOCH_DELTA 978307200.0

static void make_history_db(const char *path, const char *table_name) {
    sqlite3 *db = NULL;
    char sql[512];
    if (sqlite3_open(path, &db) != SQLITE_OK)
        return;
    snprintf(sql, sizeof(sql),
             "CREATE TABLE \"%s\" (Z_PK INTEGER PRIMARY KEY, Z_ENT INTEGER, "
             "Z_OPT INTEGER, ZTIMESTAMP REAL, ZRAWTRANSCRIPT TEXT, "
             "ZPOSTPROCESSEDTRANSCRIPT TEXT, ZAUDIOFILENAME TEXT, "
             "ZINTENT TEXT)",
             table_name);
    sqlite3_exec(db, sql, NULL, NULL, NULL);
    sqlite3_exec(db,
                 "CREATE TABLE Z_PRIMARYKEY (Z_ENT INTEGER PRIMARY KEY, "
                 "Z_NAME TEXT, Z_SUPER INTEGER, Z_MAX INTEGER)",
                 NULL, NULL, NULL);
    sqlite3_close(db);
}

static void insert_row(sqlite3 *db, const char *table, long pk, long unix_ts,
                       const char *raw, const char *post, const char *audio) {
    char sql[1024];
    snprintf(sql, sizeof(sql),
             "INSERT INTO \"%s\" (Z_PK,Z_ENT,Z_OPT,ZTIMESTAMP,"
             "ZRAWTRANSCRIPT,ZPOSTPROCESSEDTRANSCRIPT,ZAUDIOFILENAME,"
             "ZINTENT) VALUES (?1,1,1,?2,?3,?4,?5,'dictation')",
             table);
    sqlite3_stmt *st = NULL;
    sqlite3_prepare_v2(db, sql, -1, &st, NULL);
    sqlite3_bind_int64(st, 1, pk);
    sqlite3_bind_double(st, 2, (double)unix_ts - CF_EPOCH_DELTA);
    sqlite3_bind_text(st, 3, raw, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, post, -1, SQLITE_STATIC);
    if (audio)
        sqlite3_bind_text(st, 5, audio, -1, SQLITE_STATIC);
    else
        sqlite3_bind_null(st, 5);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

/* Canonical 44-byte WAV header; duration = data_size / byte_rate. Only the
 * header is written — the parser must never need the audio body. */
static void make_wav(const char *path, unsigned sample_rate, unsigned channels,
                     unsigned bits, unsigned data_size) {
    unsigned char h[44];
    memset(h, 0, sizeof(h));
    memcpy(h, "RIFF", 4);
    memcpy(h + 8, "WAVE", 4);
    memcpy(h + 12, "fmt ", 4);
    memcpy(h + 36, "data", 4);
    unsigned byte_rate = sample_rate * channels * bits / 8;
    unsigned short block_align = (unsigned short)(channels * bits / 8);
    unsigned riff_size = 36 + data_size;
    unsigned short pcm = 1;
    unsigned short fmt_size = 16;
    memcpy(h + 4, &riff_size, 4);
    memcpy(h + 16, &fmt_size, 2);
    memcpy(h + 20, &pcm, 2);
    memcpy(h + 22, &channels, 2);
    memcpy(h + 24, &sample_rate, 4);
    memcpy(h + 28, &byte_rate, 4);
    memcpy(h + 32, &block_align, 2);
    memcpy(h + 34, &bits, 2);
    memcpy(h + 40, &data_size, 4);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fwrite(h, 1, sizeof(h), f);
    fclose(f);
}

/* Apple AVAudioFile inserts a FLLR pad so `data` starts ~4 KB in. The
 * duration must come from the data-chunk size field, without loading the
 * payload and without requiring it to sit in the first 256 bytes. */
static void make_wav_padded(const char *path, unsigned sample_rate,
                            unsigned channels, unsigned bits,
                            unsigned data_size, unsigned filler) {
    unsigned byte_rate = sample_rate * channels * bits / 8;
    unsigned short block_align = (unsigned short)(channels * bits / 8);
    unsigned short pcm = 1;
    unsigned short ch16 = (unsigned short)channels;
    unsigned short bits16 = (unsigned short)bits;
    unsigned fmt_size = 16;
    unsigned riff_size = 4 + (8 + 16) + (8 + filler) + 8; /* no data payload */

    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fwrite("RIFF", 1, 4, f);
    fwrite(&riff_size, 4, 1, f);
    fwrite("WAVE", 1, 4, f);

    fwrite("fmt ", 1, 4, f);
    fwrite(&fmt_size, 4, 1, f);
    fwrite(&pcm, 2, 1, f);
    fwrite(&ch16, 2, 1, f);
    fwrite(&sample_rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&block_align, 2, 1, f);
    fwrite(&bits16, 2, 1, f);

    fwrite("FLLR", 1, 4, f);
    fwrite(&filler, 4, 1, f);
    if (filler) {
        unsigned char *z = calloc(1, filler);
        if (z) {
            fwrite(z, 1, filler, f);
            free(z);
        }
    }

    fwrite("data", 1, 4, f);
    fwrite(&data_size, 4, 1, f);
    fclose(f);
}

static void make_dir(const char *path) {
    char tmp[1024];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, 0700);
            *p = '/';
        }
    }
    mkdir(tmp, 0700);
}

static long file_size(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 ? (long)st.st_size : -1;
}

/* Read a whole small file; caller frees. */
static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *buf = malloc((size_t)n + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    return buf;
}

/* ------------------------------------------------------------------- tests */

static void test_word_count(void) {
    CHECK(ffs_count_words(NULL) == 0);
    CHECK(ffs_count_words("") == 0);
    CHECK(ffs_count_words("   \n\t ") == 0);
    CHECK(ffs_count_words("hello") == 1);
    CHECK(ffs_count_words("hello world") == 2);
    CHECK(ffs_count_words("  spaced   out\ntext  ") == 3);
    CHECK(ffs_count_words("one,two three!") == 2);
    CHECK(ffs_count_words("caf\xC3\xA9 au lait") == 3); /* utf-8 safe */
}

static void test_streak_math(void) {
    FfsStats s;
    ffs_stats_init(&s);
    long now = mk_ts(2026, 9, 2, 12, 0);
    setenv("TZ", "UTC", 1);
    tzset();

    CHECK(ffs_streak(&s, now) == 0);
    ffs_stats_add_event(&s, mk_ts(2026, 8, 30, 9, 0), 10, 60.0);
    ffs_stats_add_event(&s, mk_ts(2026, 8, 31, 9, 0), 10, 60.0);
    ffs_stats_add_event(&s, mk_ts(2026, 9, 1, 9, 0), 10, 60.0);
    /* Today has nothing yet: streak ending yesterday still counts. */
    CHECK(ffs_streak(&s, now) == 3);
    ffs_stats_add_event(&s, mk_ts(2026, 9, 2, 8, 0), 10, 60.0);
    CHECK(ffs_streak(&s, now) == 4);

    /* A gap breaks the streak. */
    FfsStats g;
    ffs_stats_init(&g);
    ffs_stats_add_event(&g, mk_ts(2026, 8, 25, 9, 0), 10, 60.0);
    ffs_stats_add_event(&g, mk_ts(2026, 9, 1, 9, 0), 10, 60.0);
    ffs_stats_add_event(&g, mk_ts(2026, 9, 2, 9, 0), 10, 60.0);
    CHECK(ffs_streak(&g, now) == 2);

    /* Streak over a month boundary. */
    FfsStats m;
    ffs_stats_init(&m);
    ffs_stats_add_event(&m, mk_ts(2026, 2, 27, 9, 0), 5, 30.0);
    ffs_stats_add_event(&m, mk_ts(2026, 2, 28, 9, 0), 5, 30.0);
    ffs_stats_add_event(&m, mk_ts(2026, 3, 1, 9, 0), 5, 30.0);
    CHECK(ffs_streak(&m, mk_ts(2026, 3, 1, 18, 0)) == 3);

    /* 2026 is not a leap year; 2028 is. */
    FfsStats ly;
    ffs_stats_init(&ly);
    ffs_stats_add_event(&ly, mk_ts(2028, 2, 28, 9, 0), 5, 30.0);
    ffs_stats_add_event(&ly, mk_ts(2028, 2, 29, 9, 0), 5, 30.0);
    ffs_stats_add_event(&ly, mk_ts(2028, 3, 1, 9, 0), 5, 30.0);
    CHECK(ffs_streak(&ly, mk_ts(2028, 3, 1, 18, 0)) == 3);
}

static void test_rates(void) {
    FfsStats s;
    ffs_stats_init(&s);
    ffs_stats_add_event(&s, mk_ts(2026, 9, 1, 9, 0), 120, 60.0); /* 120 wpm */
    ffs_stats_add_event(&s, mk_ts(2026, 9, 1, 10, 0), 60, 60.0); /* 60 wpm */
    ffs_stats_add_event(&s, mk_ts(2026, 9, 1, 11, 0), 50, -1.0); /* no audio */

    CHECK(ffs_wpm(120, 60.0) == 120.0);
    CHECK(ffs_wpm(10, 0.0) == 0.0);
    CHECK(ffs_wpm(230, 120.0) == 115.0); /* all-time mixes durations */

    int covered = 0;
    double recent = ffs_recent_wpm(&s, &covered);
    CHECK(covered == 2);
    CHECK(recent == 90.0);
    CHECK(ffs_all_time_wpm(&s) == 90.0); /* 180 audio-words / 120s, not 230/120 */

    /* 230 words at 40 wpm typing = 345s; minus 120s dictated = 225s saved. */
    s.typing_wpm = 40.0;
    double saved = ffs_time_saved_seconds(&s);
    CHECK(saved > 224.9 && saved < 225.1);

    /* Clamped at zero when dictation was slower than typing. */
    FfsStats slow;
    ffs_stats_init(&slow);
    slow.typing_wpm = 200.0;
    ffs_stats_add_event(&slow, mk_ts(2026, 9, 1, 9, 0), 10, 600.0);
    CHECK(ffs_time_saved_seconds(&slow) == 0.0);
}

static void test_store_roundtrip(void) {
    char dir[] = "/tmp/ffs-test-roundtrip-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char path[1100];
    snprintf(path, sizeof(path), "%s/stats.json", dir);

    FfsStats s;
    ffs_stats_init(&s);
    s.typing_wpm = 55.0;
    ffs_stats_add_event(&s, mk_ts(2026, 8, 31, 9, 0), 40, 30.5);
    ffs_stats_add_event(&s, mk_ts(2026, 9, 1, 9, 0), 60, -1.0);
    ffs_stats_add_event(&s, mk_ts(2026, 9, 1, 10, 0), 25, 12.25);
    s.watermark = 7;
    s.source_ok = 1;

    CHECK(ffs_store_save(&s, path) == 0);

    FfsStats back;
    ffs_stats_init(&back);
    CHECK(ffs_store_load(&back, path) == 0);
    CHECK(back.version == FFS_VERSION);
    CHECK(back.typing_wpm == 55.0);
    CHECK(back.total_count == 3);
    CHECK(back.total_words == 125);
    CHECK(back.with_audio == 2 && back.without_audio == 1);
    CHECK(back.audio_words == 65);
    CHECK(back.total_seconds > 42.7 && back.total_seconds < 42.8);
    CHECK(back.day_count == 2);
    CHECK(strcmp(back.days[0].date, "2026-08-31") == 0);
    CHECK(back.days[1].count == 2 && back.days[1].words == 85);
    CHECK(back.recent_len == 3);
    CHECK(back.recent[1].seconds < 0.0);
    CHECK(back.watermark == 7);
    CHECK(back.source_ok == 1);
    CHECK(back.first_ts == mk_ts(2026, 8, 31, 9, 0));

    /* Corrupt files are reported without destroying data. */
    FILE *f = fopen(path, "w");
    fputs("{\"v\":1,\"tw\":garbage", f);
    fclose(f);
    FfsStats c;
    ffs_stats_init(&c);
    CHECK(ffs_store_load(&c, path) == -1);

    /* Missing file is a fresh start, not an error. */
    FfsStats fresh;
    ffs_stats_init(&fresh);
    CHECK(ffs_store_load(&fresh, "/tmp/ffs-definitely-missing.json") == 0);
    CHECK(fresh.total_count == 0);

    /* Empty arrays (fresh install / typing-WPM-only store) must round-trip. */
    char empty_path[1100];
    snprintf(empty_path, sizeof(empty_path), "%s/empty.json", dir);
    FfsStats empty;
    ffs_stats_init(&empty);
    empty.typing_wpm = 42.0;
    CHECK(ffs_store_save(&empty, empty_path) == 0);
    FfsStats empty_back;
    ffs_stats_init(&empty_back);
    CHECK(ffs_store_load(&empty_back, empty_path) == 0);
    CHECK(empty_back.typing_wpm == 42.0);
    CHECK(empty_back.day_count == 0 && empty_back.recent_len == 0);
    CHECK(empty_back.seen_pk_len == 0);

    /* Legacy 5-element tot without pks still loads. */
    char legacy_path[1100];
    snprintf(legacy_path, sizeof(legacy_path), "%s/legacy.json", dir);
    FILE *lf = fopen(legacy_path, "w");
    fputs("{\"v\":1,\"tw\":40,\"tot\":[2,10,30.000,1,1],"
          "\"f\":1,\"l\":2,\"wm\":9,\"src\":1,\"d\":[],\"r\":[]}",
          lf);
    fclose(lf);
    FfsStats legacy;
    ffs_stats_init(&legacy);
    CHECK(ffs_store_load(&legacy, legacy_path) == 0);
    CHECK(legacy.total_count == 2 && legacy.total_words == 10);
    CHECK(legacy.with_audio == 1 && legacy.without_audio == 1);
    CHECK(legacy.watermark == 9);
    CHECK(legacy.seen_pk_len == 0);
}

static void test_day_cap_fold(void) {
    FfsStats s;
    ffs_stats_init(&s);
    long base = mk_ts(2020, 1, 1, 12, 0);
    for (int i = 0; i < FFS_MAX_DAYS + 50; i++)
        ffs_stats_add_event(&s, base + (long)i * 86400, 3, 10.0);
    CHECK(s.day_count == FFS_MAX_DAYS);
    CHECK(s.total_count == FFS_MAX_DAYS + 50);
    CHECK(s.total_words == (long)(FFS_MAX_DAYS + 50) * 3);
    /* Buckets stay sorted and contiguous after folding. */
    for (int i = 1; i < s.day_count; i++)
        CHECK(ffs_day_number(s.days[i].date) ==
              ffs_day_number(s.days[i - 1].date) + 1);
}

static void test_wav(void) {
    char dir[] = "/tmp/ffs-test-wav-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char path[1100];
    snprintf(path, sizeof(path), "%s/a.wav", dir);
    /* 16 kHz mono 16-bit => 32000 B/s; 320000 bytes => exactly 10 seconds. */
    make_wav(path, 16000, 1, 16, 320000);
    double d = ffs_wav_duration(path);
    CHECK(d > 9.99 && d < 10.01);
    CHECK(ffs_wav_duration("/tmp/ffs-no-such-file.wav") < 0.0);

    snprintf(path, sizeof(path), "%s/padded.wav", dir);
    make_wav_padded(path, 16000, 1, 16, 320000, 4096);
    d = ffs_wav_duration(path);
    CHECK(d > 9.99 && d < 10.01);

    /* Truncated/garbage files fail cleanly. */
    snprintf(path, sizeof(path), "%s/b.wav", dir);
    FILE *f = fopen(path, "wb");
    fputs("not a wav", f);
    fclose(f);
    CHECK(ffs_wav_duration(path) < 0.0);
}

/* Build a synthetic Core-Data-style fixture and ingest it end to end. */
static void test_ingest_pipeline(const char *table_name) {
    char dir[] = "/tmp/ffs-test-ingest-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char db[1100], audio_dir[1100], store[1100], page[1100];
    snprintf(db, sizeof(db), "%s/PipelineHistory.sqlite", dir);
    snprintf(audio_dir, sizeof(audio_dir), "%s/audio", dir);
    snprintf(store, sizeof(store), "%s/stats.json", dir);
    snprintf(page, sizeof(page), "%s/stats.html", dir);
    make_dir(audio_dir);

    make_history_db(db, table_name);
    char wav_path[1100];
    snprintf(wav_path, sizeof(wav_path), "%s/aaa.wav", audio_dir);
    make_wav(wav_path, 16000, 1, 16, 320000); /* 10 s */

    sqlite3 *sq = NULL;
    CHECK(sqlite3_open(db, &sq) == SQLITE_OK);
    insert_row(sq, table_name, 1, mk_ts(2026, 9, 1, 9, 0),
               "raw alpha beta", "clean alpha beta gamma", "aaa.wav");
    insert_row(sq, table_name, 2, mk_ts(2026, 9, 1, 10, 0),
               "SENTINELPHRASE fallback words", "", NULL); /* uses raw */
    insert_row(sq, table_name, 3, mk_ts(2026, 9, 1, 11, 0), "", "",
               NULL); /* empty: skipped */
    insert_row(sq, table_name, 4, mk_ts(2026, 9, 2, 9, 0), "delta epsilon",
               "delta epsilon zeta", "../../etc/passwd"); /* unsafe name */
    insert_row(sq, table_name, 5, mk_ts(2026, 9, 2, 10, 0), "raw five",
               "clean five", "missing.wav"); /* audio gone */
    sqlite3_close(sq);

    FfsStats s;
    ffs_stats_init(&s);
    long added = 0;
    CHECK(ffs_ingest(&s, db, audio_dir, &added) == 0);
    CHECK(added == 4); /* empty row skipped */
    CHECK(s.source_ok == 1);
    CHECK(s.total_count == 4);
    CHECK(s.total_words == 4 + 3 + 3 + 2);
    CHECK(s.with_audio == 1); /* only aaa.wav resolved */
    CHECK(s.without_audio == 3);
    CHECK(s.total_seconds > 9.9 && s.total_seconds < 10.1);
    CHECK(s.watermark == 5);
    CHECK(s.day_count == 2);
    CHECK(s.seen_pk_len == 4);

    /* Re-running must not double count (watermark). */
    long again = 0;
    CHECK(ffs_ingest(&s, db, audio_dir, &again) == 0);
    CHECK(again == 0);
    CHECK(s.total_count == 4);

    /* New rows are picked up incrementally. */
    CHECK(sqlite3_open(db, &sq) == SQLITE_OK);
    insert_row(sq, table_name, 6, mk_ts(2026, 9, 3, 8, 0), "raw six",
               "clean six eta", NULL);
    sqlite3_close(sq);
    long more = 0;
    CHECK(ffs_ingest(&s, db, audio_dir, &more) == 0);
    CHECK(more == 1);
    CHECK(s.total_count == 5);
    CHECK(s.total_words == 15);
    CHECK(s.watermark == 6);

    CHECK(ffs_store_save(&s, store) == 0);
    FfsStats reloaded;
    ffs_stats_init(&reloaded);
    CHECK(ffs_store_load(&reloaded, store) == 0);
    CHECK(reloaded.seen_pk_len == 5);
    CHECK(reloaded.audio_words == 4); /* only aaa.wav row had duration */

    /* Privacy invariant: transcript text never reaches disk. */
    CHECK(ffs_store_save(&s, store) == 0);
    CHECK(ffs_html_write(&s, page, mk_ts(2026, 9, 3, 12, 0)) == 0);
    char *store_bytes = slurp(store);
    char *page_bytes = slurp(page);
    CHECK(store_bytes != NULL && page_bytes != NULL);
    if (store_bytes && page_bytes) {
        CHECK(strstr(store_bytes, "SENTINELPHRASE") == NULL);
        CHECK(strstr(store_bytes, "alpha") == NULL);
        CHECK(strstr(page_bytes, "SENTINELPHRASE") == NULL);
        CHECK(strstr(page_bytes, "alpha") == NULL);
        CHECK(strstr(page_bytes, "FreeFlow Stats") != NULL);
    }
    free(store_bytes);
    free(page_bytes);

    /* Streak across the fixture days (9-1, 9-2, 9-3). */
    setenv("TZ", "UTC", 1);
    tzset();
    CHECK(ffs_streak(&s, mk_ts(2026, 9, 3, 18, 0)) == 3);
}

static void test_ingest_standard_table(void) {
    test_ingest_pipeline("ZPIPELINEHISTORYENTRY");
}

static void test_ingest_renamed_table(void) {
    /* Fallback introspection: a table not named PIPELINEHISTORY still works
     * when the column pattern matches. */
    test_ingest_pipeline("ZRUNLOGENTRY");
}

static void test_unavailable_source(void) {
    FfsStats s;
    ffs_stats_init(&s);
    long added = -1;
    CHECK(ffs_ingest(&s, "/tmp/ffs-no-such-db.sqlite", "/tmp", &added) == 0);
    CHECK(added == 0);
    CHECK(s.source_ok == 0);

    /* A database without the expected schema also degrades cleanly. */
    char db[] = "/tmp/ffs-test-empty-XXXXXX.sqlite";
    int fd = mkstemps(db, 7);
    CHECK(fd >= 0);
    if (fd >= 0)
        close(fd);
    sqlite3 *sq = NULL;
    CHECK(sqlite3_open(db, &sq) == SQLITE_OK);
    sqlite3_exec(sq, "CREATE TABLE ZUNRELATED (Z_PK INTEGER PRIMARY KEY)",
                 NULL, NULL, NULL);
    sqlite3_close(sq);
    CHECK(ffs_ingest(&s, db, "/tmp", &added) == 0);
    CHECK(s.source_ok == 0);
}

static void test_rebuild_detection(void) {
    char dir[] = "/tmp/ffs-test-rebuild-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char db[1100], audio_dir[1100];
    snprintf(db, sizeof(db), "%s/h.sqlite", dir);
    snprintf(audio_dir, sizeof(audio_dir), "%s/audio", dir);
    make_dir(audio_dir);

    make_history_db(db, "ZPIPELINEHISTORYENTRY");
    sqlite3 *sq = NULL;
    CHECK(sqlite3_open(db, &sq) == SQLITE_OK);
    insert_row(sq, "ZPIPELINEHISTORYENTRY", 40, mk_ts(2026, 9, 1, 9, 0),
               "one two", "one two", NULL);
    insert_row(sq, "ZPIPELINEHISTORYENTRY", 41, mk_ts(2026, 9, 2, 9, 0),
               "three four", "three four", NULL);
    sqlite3_close(sq);

    FfsStats s;
    ffs_stats_init(&s);
    long added = 0;
    CHECK(ffs_ingest(&s, db, audio_dir, &added) == 0);
    CHECK(added == 2);
    CHECK(s.watermark == 41);

    /* Database rebuilt: primary keys restart below the watermark. */
    remove(db);
    make_history_db(db, "ZPIPELINEHISTORYENTRY");
    CHECK(sqlite3_open(db, &sq) == SQLITE_OK);
    /* Old event re-appears with a fresh low pk plus one genuinely new. */
    insert_row(sq, "ZPIPELINEHISTORYENTRY", 1, mk_ts(2026, 9, 2, 9, 0),
               "three four", "three four", NULL);
    insert_row(sq, "ZPIPELINEHISTORYENTRY", 2, mk_ts(2026, 9, 3, 9, 0),
               "five six seven", "five six seven", NULL);
    sqlite3_close(sq);

    added = 0;
    CHECK(ffs_ingest(&s, db, audio_dir, &added) == 0);
    CHECK(added == 1); /* the already-counted older row is skipped */
    CHECK(s.total_count == 3);
    CHECK(s.total_words == 2 + 2 + 3);
    CHECK(s.watermark == 2);
}

static void test_empty_then_retry(void) {
    char dir[] = "/tmp/ffs-test-retry-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char db[1100], audio_dir[1100];
    snprintf(db, sizeof(db), "%s/h.sqlite", dir);
    snprintf(audio_dir, sizeof(audio_dir), "%s/audio", dir);
    make_dir(audio_dir);

    make_history_db(db, "ZPIPELINEHISTORYENTRY");
    sqlite3 *sq = NULL;
    CHECK(sqlite3_open(db, &sq) == SQLITE_OK);
    insert_row(sq, "ZPIPELINEHISTORYENTRY", 7, mk_ts(2026, 9, 1, 9, 0), "",
               "", NULL); /* failed first pass */
    sqlite3_close(sq);

    FfsStats s;
    ffs_stats_init(&s);
    long added = 0;
    CHECK(ffs_ingest(&s, db, audio_dir, &added) == 0);
    CHECK(added == 0);
    CHECK(s.total_count == 0);
    CHECK(s.watermark == 0); /* empty rows do not advance the watermark */
    CHECK(s.seen_pk_len == 0);

    CHECK(sqlite3_open(db, &sq) == SQLITE_OK);
    sqlite3_stmt *st = NULL;
    CHECK(sqlite3_prepare_v2(sq,
                             "UPDATE ZPIPELINEHISTORYENTRY SET "
                             "ZRAWTRANSCRIPT=?1, ZPOSTPROCESSEDTRANSCRIPT=?2 "
                             "WHERE Z_PK=7",
                             -1, &st, NULL) == SQLITE_OK);
    sqlite3_bind_text(st, 1, "hello world", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, "hello world", -1, SQLITE_STATIC);
    CHECK(sqlite3_step(st) == SQLITE_DONE);
    sqlite3_finalize(st);
    sqlite3_close(sq);

    added = 0;
    CHECK(ffs_ingest(&s, db, audio_dir, &added) == 0);
    CHECK(added == 1);
    CHECK(s.total_count == 1);
    CHECK(s.total_words == 2);
    CHECK(s.watermark == 7);
    CHECK(s.seen_pk_len == 1 && s.seen_pk[0] == 7);
}

static void test_legacy_watermark_migration(void) {
    char dir[] = "/tmp/ffs-test-legacywm-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char db[1100], audio_dir[1100];
    snprintf(db, sizeof(db), "%s/h.sqlite", dir);
    snprintf(audio_dir, sizeof(audio_dir), "%s/audio", dir);
    make_dir(audio_dir);
    make_history_db(db, "ZPIPELINEHISTORYENTRY");
    sqlite3 *sq = NULL;
    CHECK(sqlite3_open(db, &sq) == SQLITE_OK);
    insert_row(sq, "ZPIPELINEHISTORYENTRY", 3, mk_ts(2026, 9, 1, 9, 0),
               "one two", "one two", NULL);
    insert_row(sq, "ZPIPELINEHISTORYENTRY", 4, mk_ts(2026, 9, 1, 10, 0),
               "three four", "three four", NULL);
    sqlite3_close(sq);

    FfsStats s;
    ffs_stats_init(&s);
    s.watermark = 4; /* old store: already counted, no pks ring */
    long added = -1;
    CHECK(ffs_ingest(&s, db, audio_dir, &added) == 0);
    CHECK(added == 0);
    CHECK(s.total_count == 0);
    CHECK(s.seen_pk_len == 2);
}

static void test_bad_identifier(void) {
    char dir[] = "/tmp/ffs-test-ident-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char db[1100], audio_dir[1100];
    snprintf(db, sizeof(db), "%s/h.sqlite", dir);
    snprintf(audio_dir, sizeof(audio_dir), "%s/audio", dir);
    make_dir(audio_dir);

    sqlite3 *sq = NULL;
    CHECK(sqlite3_open(db, &sq) == SQLITE_OK);
    sqlite3_exec(sq,
                 "CREATE TABLE \"ZBAD-X\" (Z_PK INTEGER PRIMARY KEY, "
                 "ZTIMESTAMP REAL, ZRAWTRANSCRIPT TEXT, "
                 "ZPOSTPROCESSEDTRANSCRIPT TEXT)",
                 NULL, NULL, NULL);
    sqlite3_close(sq);

    FfsStats s;
    ffs_stats_init(&s);
    long added = -1;
    CHECK(ffs_ingest(&s, db, audio_dir, &added) == 0);
    CHECK(added == 0);
    CHECK(s.source_ok == 0); /* hyphenated name is rejected */

    /* A legal table still wins when both exist. */
    make_history_db(db, "ZPIPELINEHISTORYENTRY");
    CHECK(sqlite3_open(db, &sq) == SQLITE_OK);
    insert_row(sq, "ZPIPELINEHISTORYENTRY", 1, mk_ts(2026, 9, 1, 9, 0),
               "one two", "one two", NULL);
    sqlite3_close(sq);
    added = 0;
    CHECK(ffs_ingest(&s, db, audio_dir, &added) == 0);
    CHECK(added == 1);
    CHECK(s.source_ok == 1);
}

static void test_corrupt_not_overwritten(void) {
    char dir[] = "/tmp/ffs-test-corrupt-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char path[1100];
    snprintf(path, sizeof(path), "%s/stats.json", dir);

    FfsStats s;
    ffs_stats_init(&s);
    ffs_stats_add_event(&s, mk_ts(2026, 9, 1, 9, 0), 40, 30.0);
    CHECK(ffs_store_save(&s, path) == 0);

    FILE *f = fopen(path, "w");
    fputs("{\"v\":1,\"tw\":garbage", f);
    fclose(f);

    FfsStats loaded;
    ffs_stats_init(&loaded);
    CHECK(ffs_store_load(&loaded, path) == -1);
    char *left = slurp(path);
    CHECK(left != NULL);
    if (left)
        CHECK(strstr(left, "garbage") != NULL);
    free(left);
}

static void test_date_add_days(void) {
    char d[11];
    memcpy(d, "2026-03-07", 11);
    ffs_date_add_days(d, 1);
    CHECK(strcmp(d, "2026-03-08") == 0);
    ffs_date_add_days(d, -1);
    CHECK(strcmp(d, "2026-03-07") == 0);
    memcpy(d, "2028-02-28", 11);
    ffs_date_add_days(d, 1);
    CHECK(strcmp(d, "2028-02-29") == 0);
}

int ffs_run_tests(void) {
    test_word_count();
    test_streak_math();
    test_rates();
    test_store_roundtrip();
    test_day_cap_fold();
    test_wav();
    test_ingest_standard_table();
    test_ingest_renamed_table();
    test_unavailable_source();
    test_rebuild_detection();
    test_empty_then_retry();
    test_legacy_watermark_migration();
    test_bad_identifier();
    test_corrupt_not_overwritten();
    test_date_add_days();

    printf("%s: %d checks, %d failures\n",
           g_failures == 0 ? "PASS" : "FAIL", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

/* ------------------------------------------------------------ budget gates */

static long peak_rss_kb(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0)
        return -1;
#ifdef __APPLE__
    return ru.ru_maxrss / 1024; /* macOS reports bytes */
#else
    return ru.ru_maxrss; /* Linux reports kilobytes */
#endif
}

int ffs_memcheck(void) {
    /* Worst-realistic-case run: fixture with the full 20-row history cap,
     * wordy synthetic transcripts, then ingest + save + render. */
    char dir[] = "/tmp/ffs-memcheck-XXXXXX";
    if (!mkdtemp(dir)) {
        printf("memcheck: could not create temp dir\n");
        return 1;
    }
    char db[1100], audio_dir[1100], store[1100], page[1100];
    snprintf(db, sizeof(db), "%s/PipelineHistory.sqlite", dir);
    snprintf(audio_dir, sizeof(audio_dir), "%s/audio", dir);
    snprintf(store, sizeof(store), "%s/stats.json", dir);
    snprintf(page, sizeof(page), "%s/stats.html", dir);
    make_dir(audio_dir);
    make_history_db(db, "ZPIPELINEHISTORYENTRY");

    char text[1200];
    memset(text, 'x', sizeof(text) - 1);
    text[sizeof(text) - 1] = 0;
    for (size_t i = 0; i < sizeof(text) - 1; i += 8)
        text[i] = ' '; /* ~150 words per row */

    sqlite3 *sq = NULL;
    if (sqlite3_open(db, &sq) != SQLITE_OK) {
        printf("memcheck: fixture db failed\n");
        return 1;
    }
    for (long i = 1; i <= 20; i++)
        insert_row(sq, "ZPIPELINEHISTORYENTRY", i,
                   mk_ts(2026, 9, 1, 8, 0) + i * 60, text, text, NULL);
    sqlite3_close(sq);

    FfsStats s;
    ffs_stats_init(&s);
    long added = 0;
    if (ffs_ingest(&s, db, audio_dir, &added) != 0 || added != 20) {
        printf("memcheck: ingestion failed\n");
        return 1;
    }
    if (ffs_store_save(&s, store) != 0 ||
        ffs_html_write(&s, page, mk_ts(2026, 9, 2, 12, 0)) != 0) {
        printf("memcheck: output failed\n");
        return 1;
    }

    long kb = peak_rss_kb();
    printf("memcheck: peak RSS %ld KB (limit %ld KB, target < 1024 KB)\n", kb,
           FFS_RSS_LIMIT_KB);
    if (kb < 0 || kb > FFS_RSS_LIMIT_KB) {
        printf("memcheck: FAIL — over budget\n");
        return 1;
    }
    printf("memcheck: PASS\n");
    return 0;
}

int ffs_diskcheck(void) {
    char dir[] = "/tmp/ffs-diskcheck-XXXXXX";
    if (!mkdtemp(dir)) {
        printf("diskcheck: could not create temp dir\n");
        return 1;
    }
    char store[1100];
    snprintf(store, sizeof(store), "%s/stats.json", dir);

    FfsStats s;
    ffs_stats_init(&s);
    long base = mk_ts(2020, 1, 1, 12, 0);
    for (int i = 0; i < 1000; i++) {
        ffs_stats_add_event(&s, base + (long)i * 86400, 137, 245.5);
        ffs_stats_add_event(&s, base + (long)i * 86400 + 3600, 98, 180.25);
    }
    if (ffs_store_save(&s, store) != 0) {
        printf("diskcheck: save failed\n");
        return 1;
    }
    long bytes = file_size(store);
    double per_day = s.day_count ? (double)bytes / s.day_count : 0;
    printf("diskcheck: %d days -> %ld bytes (%.0f B/day); limit %ld bytes, "
           "user budget %ld bytes\n",
           s.day_count, bytes, per_day, FFS_DISKCHECK_LIMIT_BYTES,
           FFS_DISK_BUDGET_BYTES);
    if (bytes < 0 || bytes > FFS_DISKCHECK_LIMIT_BYTES) {
        printf("diskcheck: FAIL — over budget\n");
        return 1;
    }

    /* Boundedness: fill past the cap and confirm size stops growing. */
    for (int i = 1000; i < FFS_MAX_DAYS + 400; i++)
        ffs_stats_add_event(&s, base + (long)i * 86400, 137, 245.5);
    if (s.day_count != FFS_MAX_DAYS) {
        printf("diskcheck: FAIL — day cap not enforced (%d days)\n",
               s.day_count);
        return 1;
    }
    if (ffs_store_save(&s, store) != 0)
        return 1;
    long capped = file_size(store);
    printf("diskcheck: capped at %d days -> %ld bytes (%.1f%% of budget)\n",
           s.day_count, capped, 100.0 * capped / FFS_DISK_BUDGET_BYTES);
    if (capped < 0 || capped > FFS_DISKCHECK_LIMIT_BYTES) {
        printf("diskcheck: FAIL — capped store over budget\n");
        return 1;
    }
    printf("diskcheck: PASS\n");
    return 0;
}
