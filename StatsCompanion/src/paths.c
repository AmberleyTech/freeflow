#include "paths.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *home(void) {
    const char *h = getenv("HOME");
    return h ? h : ".";
}

static void copy_path(char *dst, const char *src) {
    size_t i = 0;
    for (; src[i] && i + 1 < 1024; i++)
        dst[i] = src[i];
    dst[i] = 0;
}

static int mkdir_p(const char *path) {
    char tmp[1024];
    size_t len = strlen(path);
    if (len >= sizeof(tmp))
        return -1;
    memcpy(tmp, path, len + 1);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0700) != 0 && access(tmp, F_OK) != 0)
                return -1;
            *p = '/';
        }
    }
    if (mkdir(tmp, 0700) != 0 && access(tmp, F_OK) != 0)
        return -1;
    return 0;
}

int ffs_data_dir(char *buf, size_t n) {
    const char *override = getenv("FFS_DATA_DIR");
    int rc = override ? snprintf(buf, n, "%s", override)
                      : snprintf(buf, n, "%s/Library/Application Support/FreeFlowStats",
                                 home());
    if (rc < 0 || (size_t)rc >= n)
        return -1;
    return mkdir_p(buf);
}

int ffs_find_history(char *db, size_t dn, char *audio, size_t an) {
    const char *db_override = getenv("FFS_HISTORY_DB");
    if (db_override) {
        const char *audio_override = getenv("FFS_AUDIO_DIR");
        if (snprintf(db, dn, "%s", db_override) >= (int)dn)
            return -1;
        if (audio &&
            snprintf(audio, an, "%s", audio_override ? audio_override : "") >=
                (int)an)
            return -1;
        return access(db, R_OK) == 0 ? 0 : -1;
    }

    static const char *names[] = {"FreeFlow", "FreeFlow Dev"};
    char best_db[1024] = "";
    char best_audio[1024] = "";
    long best_mtime = -1;

    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        char candidate[1024];
        snprintf(candidate, sizeof(candidate),
                 "%s/Library/Application Support/%s/PipelineHistory.sqlite",
                 home(), names[i]);
        struct stat st;
        if (stat(candidate, &st) != 0)
            continue;
        if ((long)st.st_mtime > best_mtime) {
            best_mtime = (long)st.st_mtime;
            copy_path(best_db, candidate);
            snprintf(best_audio, sizeof(best_audio),
                     "%s/Library/Application Support/%s/audio", home(),
                     names[i]);
        }
    }
    if (best_mtime < 0)
        return -1;
    if (snprintf(db, dn, "%s", best_db) >= (int)dn)
        return -1;
    if (audio && snprintf(audio, an, "%s", best_audio) >= (int)an)
        return -1;
    return 0;
}

void ffs_store_path(char *buf, size_t n) {
    char dir[1024];
    if (ffs_data_dir(dir, sizeof(dir)) == 0)
        snprintf(buf, n, "%s/stats.json", dir);
    else if (n)
        buf[0] = 0;
}

void ffs_html_path(char *buf, size_t n) {
    char dir[1024];
    if (ffs_data_dir(dir, sizeof(dir)) == 0)
        snprintf(buf, n, "%s/stats.html", dir);
    else if (n)
        buf[0] = 0;
}

void ffs_lock_path(char *buf, size_t n) {
    char dir[1024];
    if (ffs_data_dir(dir, sizeof(dir)) == 0)
        snprintf(buf, n, "%s/lock", dir);
    else if (n)
        buf[0] = 0;
}
