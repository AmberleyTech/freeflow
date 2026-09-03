/* freeflow-stats — tiny sidecar that aggregates FreeFlow dictation stats.
 *
 * Modes:
 *   (none)               ingest new history, update stats.json + stats.html
 *   --summary            print current stats to the terminal
 *   --html               regenerate the stats page
 *   --open               ingest, then open the stats page
 *   --set-typing-wpm N   set the typing speed used for "time saved"
 *   --test / --memcheck / --diskcheck   self-verification
 *
 * Runs are short-lived by design: launchd spawns the binary when FreeFlow's
 * history changes, so the resident footprint between dictations is zero.
 */
#include "common.h"
#include "html.h"
#include "ingest.h"
#include "paths.h"
#include "selfcheck.h"
#include "stats_math.h"
#include "store.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static void print_summary(const FfsStats *s, long now) {
    int covered = 0;
    double recent_wpm = ffs_recent_wpm(s, &covered);
    double all_wpm = ffs_all_time_wpm(s);
    double saved = ffs_time_saved_seconds(s);
    printf("FreeFlow Stats\n");
    printf("  streak:           %d day%s\n", ffs_streak(s, now),
           ffs_streak(s, now) == 1 ? "" : "s");
    printf("  dictation WPM:    %.0f recent / %.0f all-time\n", recent_wpm,
           all_wpm);
    printf("  transcriptions:   %ld\n", s->total_count);
    printf("  words dictated:   %ld\n", s->total_words);
    printf("  time saved:       %.1f hours (at %.0f WPM typing)\n",
           saved / 3600.0, s->typing_wpm);
    printf("  source:           %s\n",
           s->source_ok ? "ok" : "unavailable (no FreeFlow history found)");
}

static int acquire_lock(void) {
    char path[1100];
    ffs_lock_path(path, sizeof(path));
    if (!path[0])
        return -1;
    int fd = open(path, O_CREAT | O_RDWR, 0600);
    if (fd < 0)
        return -1;
    if (flock(fd, LOCK_EX | LOCK_NB) == 0)
        return fd;
    /* WatchPaths can fire on sqlite + WAL + shm together; wait briefly so
     * the overlapping run still sees the committed row instead of exiting. */
    for (int attempt = 0; attempt < 20; attempt++) {
        usleep(50000);
        if (flock(fd, LOCK_EX | LOCK_NB) == 0)
            return fd;
    }
    close(fd);
    return -2; /* another run still in progress after ~1s */
}

static int open_path(const char *path) {
    if (!path || !path[0])
        return -1;
    pid_t pid = fork();
    if (pid < 0)
        return -1;
    if (pid == 0) {
#ifdef __APPLE__
        execl("/usr/bin/open", "open", path, (char *)NULL);
#else
        execlp("xdg-open", "xdg-open", path, (char *)NULL);
#endif
        _exit(127);
    }
    int st = 0;
    if (waitpid(pid, &st, 0) != pid)
        return -1;
    return (WIFEXITED(st) && WEXITSTATUS(st) == 0) ? 0 : -1;
}

static int load_store(FfsStats *st, const char *store_path) {
    int rc = ffs_store_load(st, store_path);
    if (rc < 0)
        fprintf(stderr, "freeflow-stats: refusing to overwrite a corrupt "
                        "stats store\n");
    return rc;
}

static int run_ingest(int open_page) {
    int lock = acquire_lock();
    if (lock == -2)
        return 0;
    if (lock < 0)
        return 1;

    char store_path[1100], html_path[1100];
    ffs_store_path(store_path, sizeof(store_path));
    ffs_html_path(html_path, sizeof(html_path));

    FfsStats st;
    ffs_stats_init(&st);
    if (load_store(&st, store_path) < 0) {
        flock(lock, LOCK_UN);
        close(lock);
        return 1;
    }

    long added = 0;
    int rc = ffs_ingest(&st, NULL, NULL, &added);

    long now = (long)time(NULL);
    if (ffs_store_save(&st, store_path) != 0)
        rc = 1;
    if (ffs_html_write(&st, html_path, now) != 0)
        rc = 1;

    printf("freeflow-stats: %ld new, %ld total transcriptions (%s)\n", added,
           st.total_count, st.source_ok ? "source ok" : "source unavailable");

    if (open_page) {
        if (open_path(html_path) != 0)
            fprintf(stderr, "could not open %s\n", html_path);
    }

    flock(lock, LOCK_UN);
    close(lock);
    return rc;
}

static int set_typing_wpm(const char *arg) {
    double wpm = strtod(arg, NULL);
    if (wpm < 1.0 || wpm > 400.0) {
        fprintf(stderr, "typing WPM must be between 1 and 400\n");
        return 1;
    }
    int lock = acquire_lock();
    if (lock < 0)
        return lock == -2 ? 0 : 1;

    char store_path[1100], html_path[1100];
    ffs_store_path(store_path, sizeof(store_path));
    ffs_html_path(html_path, sizeof(html_path));

    FfsStats st;
    ffs_stats_init(&st);
    if (load_store(&st, store_path) < 0) {
        flock(lock, LOCK_UN);
        close(lock);
        return 1;
    }
    st.typing_wpm = wpm;
    int rc = ffs_store_save(&st, store_path);
    if (rc == 0)
        rc = ffs_html_write(&st, html_path, (long)time(NULL));
    printf("typing speed set to %.0f WPM\n", wpm);

    flock(lock, LOCK_UN);
    close(lock);
    return rc == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc >= 2) {
        if (strcmp(argv[1], "--test") == 0)
            return ffs_run_tests();
        if (strcmp(argv[1], "--memcheck") == 0)
            return ffs_memcheck();
        if (strcmp(argv[1], "--diskcheck") == 0)
            return ffs_diskcheck();
        if (strcmp(argv[1], "--summary") == 0) {
            FfsStats st;
            ffs_stats_init(&st);
            char store_path[1100];
            ffs_store_path(store_path, sizeof(store_path));
            if (load_store(&st, store_path) < 0)
                return 1;
            print_summary(&st, (long)time(NULL));
            return 0;
        }
        if (strcmp(argv[1], "--html") == 0) {
            FfsStats st;
            ffs_stats_init(&st);
            char store_path[1100], html_path[1100];
            ffs_store_path(store_path, sizeof(store_path));
            ffs_html_path(html_path, sizeof(html_path));
            if (load_store(&st, store_path) < 0)
                return 1;
            return ffs_html_write(&st, html_path, (long)time(NULL)) == 0 ? 0
                                                                         : 1;
        }
        if (strcmp(argv[1], "--open") == 0)
            return run_ingest(1);
        if (strcmp(argv[1], "--set-typing-wpm") == 0) {
            if (argc < 3) {
                fprintf(stderr, "usage: freeflow-stats --set-typing-wpm N\n");
                return 1;
            }
            return set_typing_wpm(argv[2]);
        }
        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            printf("usage: freeflow-stats [--summary] [--html] [--open]\n"
                   "                      [--set-typing-wpm N]\n"
                   "                      [--test] [--memcheck] [--diskcheck]\n"
                   "\nNo arguments: ingest new FreeFlow history and update\n"
                   "stats.json + stats.html (this is what launchd runs).\n");
            return 0;
        }
        fprintf(stderr, "unknown option: %s (try --help)\n", argv[1]);
        return 1;
    }
    return run_ingest(0);
}
