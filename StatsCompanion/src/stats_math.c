#include "stats_math.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

long ffs_count_words(const char *text) {
    long words = 0;
    int in_word = 0;
    if (!text)
        return 0;
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        if (isspace(*p)) {
            in_word = 0;
        } else if (!in_word) {
            in_word = 1;
            words++;
        }
    }
    return words;
}

void ffs_date_from_ts(long ts, char out[11]) {
    time_t t = (time_t)ts;
    struct tm tmv;
    localtime_r(&t, &tmv);
    strftime(out, 11, "%Y-%m-%d", &tmv);
}

void ffs_date_add_days(char date[11], int delta) {
    struct tm tmv;
    int y = 0, m = 0, d = 0;
    memset(&tmv, 0, sizeof(tmv));
    if (sscanf(date, "%d-%d-%d", &y, &m, &d) != 3)
        return;
    tmv.tm_year = y - 1900;
    tmv.tm_mon = m - 1;
    tmv.tm_mday = d + delta;
    tmv.tm_hour = 12; /* midday: avoid DST midnight edges */
    tmv.tm_isdst = -1;
    if (mktime(&tmv) == (time_t)-1)
        return;
    strftime(date, 11, "%Y-%m-%d", &tmv);
}

/* Days since 1970-01-01 from a civil date (Howard Hinnant's algorithm). */
static long days_from_civil(long y, unsigned m, unsigned d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (long)doe - 719468;
}

long ffs_day_number(const char date[11]) {
    long y = 0;
    unsigned m = 0, d = 0;
    if (sscanf(date, "%ld-%u-%u", &y, &m, &d) != 3)
        return 0;
    return days_from_civil(y, m, d);
}

int ffs_streak(const FfsStats *s, long now_ts) {
    if (s->day_count <= 0)
        return 0;
    char today[11];
    ffs_date_from_ts(now_ts, today);
    long today_n = ffs_day_number(today);
    long last_n = ffs_day_number(s->days[s->day_count - 1].date);

    long cursor;
    if (last_n == today_n)
        cursor = today_n;
    else if (last_n == today_n - 1)
        cursor = today_n - 1; /* today still in progress, streak alive */
    else
        return 0;

    int streak = 0;
    for (int i = s->day_count - 1; i >= 0; i--) {
        long dn = ffs_day_number(s->days[i].date);
        if (dn == cursor) {
            streak++;
            cursor--;
        } else if (dn < cursor) {
            break;
        }
    }
    return streak;
}

double ffs_wpm(long words, double seconds) {
    if (seconds <= 0.0)
        return 0.0;
    return (double)words / (seconds / 60.0);
}

double ffs_all_time_wpm(const FfsStats *s) {
    long words = s->audio_words > 0 ? s->audio_words : s->total_words;
    return ffs_wpm(words, s->total_seconds);
}

double ffs_recent_wpm(const FfsStats *s, int *covered) {
    long words = 0;
    double seconds = 0.0;
    int n = 0;
    for (int i = 0; i < s->recent_len; i++) {
        if (s->recent[i].seconds >= 0.0) {
            words += s->recent[i].words;
            seconds += s->recent[i].seconds;
            n++;
        }
    }
    if (covered)
        *covered = n;
    return ffs_wpm(words, seconds);
}

double ffs_time_saved_seconds(const FfsStats *s) {
    if (s->typing_wpm <= 0.0)
        return 0.0;
    double typing = (double)s->total_words / s->typing_wpm * 60.0;
    double saved = typing - s->total_seconds;
    return saved > 0.0 ? saved : 0.0;
}
