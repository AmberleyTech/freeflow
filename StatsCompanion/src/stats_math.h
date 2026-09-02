#ifndef FFS_STATS_MATH_H
#define FFS_STATS_MATH_H

#include "common.h"

/* Count words by scanning text in place (runs of non-whitespace). */
long ffs_count_words(const char *text);

/* Calendar helpers. ffs_date_from_ts uses the local timezone. */
void ffs_date_from_ts(long ts, char out[11]);
long ffs_day_number(const char date[11]); /* serial day for comparisons */

/* Consecutive-day streak ending today; a day with no entry yet today still
 * counts a streak that ended yesterday. */
int ffs_streak(const FfsStats *s, long now_ts);

/* Words per minute of dictation. Returns 0 when duration is unknown. */
double ffs_wpm(long words, double seconds);

/* WPM over the recent-event ring (events with known duration only).
 * Sets *covered to the number of events used. */
double ffs_recent_wpm(const FfsStats *s, int *covered);

/* Estimated seconds saved vs typing at the configured WPM, clamped >= 0. */
double ffs_time_saved_seconds(const FfsStats *s);

#endif
