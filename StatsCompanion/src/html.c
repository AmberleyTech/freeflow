#include "html.h"
#include "stats_math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void human_duration(double seconds, char *out, size_t n) {
    long s = (long)(seconds + 0.5);
    if (s < 60)
        snprintf(out, n, "%lds", s);
    else if (s < 3600)
        snprintf(out, n, "%ldm %lds", s / 60, s % 60);
    else if (s < 86400)
        snprintf(out, n, "%ldh %ldm", s / 3600, (s % 3600) / 60);
    else
        snprintf(out, n, "%ldd %ldh", s / 86400, (s % 86400) / 3600);
}

static void human_ts(long ts, char *out, size_t n) {
    if (ts <= 0) {
        snprintf(out, n, "never");
        return;
    }
    char date[11];
    ffs_date_from_ts(ts, date);
    snprintf(out, n, "%s", date);
}

static const char *const CSS =
    ":root{color-scheme:light dark}"
    "*{box-sizing:border-box;margin:0;padding:0}"
    "body{font-family:-apple-system,system-ui,sans-serif;max-width:720px;"
    "margin:0 auto;padding:32px 20px 48px;background:#f6f6f4;color:#1c1c1e}"
    "h1{font-size:22px;font-weight:700}"
    ".sub{color:#8e8e93;font-size:12px;margin-top:4px}"
    ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,"
    "1fr));gap:12px;margin:24px 0}"
    ".card{background:#fff;border:1px solid #e4e4e0;border-radius:12px;"
    "padding:16px}"
    ".card .v{font-size:26px;font-weight:700;font-variant-numeric:"
    "tabular-nums}"
    ".card .k{font-size:12px;color:#8e8e93;margin-top:2px}"
    ".panel{background:#fff;border:1px solid #e4e4e0;border-radius:12px;"
    "padding:16px;margin-bottom:12px}"
    ".panel h2{font-size:13px;font-weight:600;color:#8e8e93;margin-bottom:"
    "10px;text-transform:uppercase;letter-spacing:.04em}"
    ".foot{font-size:12px;color:#8e8e93;line-height:1.7}"
    "@media(prefers-color-scheme:dark){body{background:#151517;color:#f2f2f7}"
    ".card,.panel{background:#1e1e20;border-color:#2e2e32}"
    ".sub,.card .k,.panel h2,.foot{color:#98989f}}";

int ffs_html_write(const FfsStats *s, const char *path, long now_ts) {
    size_t tmplen = strlen(path) + 5;
    char *tmp = malloc(tmplen);
    if (!tmp)
        return -1;
    snprintf(tmp, tmplen, "%s.tmp", path);
    FILE *f = fopen(tmp, "w");
    if (!f) {
        free(tmp);
        return -1;
    }

    int streak = ffs_streak(s, now_ts);
    int covered = 0;
    double recent_wpm = ffs_recent_wpm(s, &covered);
    double all_wpm = ffs_all_time_wpm(s);
    double saved = ffs_time_saved_seconds(s);
    char saved_h[32], updated[32], since[32], total_audio[32];
    human_duration(saved, saved_h, sizeof(saved_h));
    human_ts(s->last_ts, updated, sizeof(updated));
    human_ts(s->first_ts, since, sizeof(since));
    human_duration(s->total_seconds, total_audio, sizeof(total_audio));

    fprintf(f,
            "<!doctype html><html><head><meta charset=\"utf-8\">"
            "<meta name=\"viewport\" content=\"width=device-width,"
            "initial-scale=1\">"
            "<meta http-equiv=\"refresh\" content=\"60\">"
            "<title>FreeFlow Stats</title><style>%s</style></head><body>",
            CSS);
    fprintf(f, "<h1>FreeFlow Stats</h1><div class=\"sub\">Updated %s &middot; "
               "auto-refreshes every minute</div>",
            updated);

    fprintf(f, "<div class=\"grid\">");
    fprintf(f,
            "<div class=\"card\"><div class=\"v\">%d</div><div "
            "class=\"k\">day streak</div></div>",
            streak);
    fprintf(f,
            "<div class=\"card\"><div class=\"v\">%.0f</div><div "
            "class=\"k\">dictation WPM (recent)</div></div>",
            recent_wpm);
    fprintf(f,
            "<div class=\"card\"><div class=\"v\">%.0f</div><div "
            "class=\"k\">dictation WPM (all time)</div></div>",
            all_wpm);
    fprintf(f,
            "<div class=\"card\"><div class=\"v\">%ld</div><div "
            "class=\"k\">transcriptions</div></div>",
            s->total_count);
    fprintf(f,
            "<div class=\"card\"><div class=\"v\">%ld</div><div "
            "class=\"k\">words dictated</div></div>",
            s->total_words);
    fprintf(f,
            "<div class=\"card\"><div class=\"v\">%s</div><div "
            "class=\"k\">time saved vs typing</div></div>",
            saved_h);
    fprintf(f, "</div>");

    /* Last 14 calendar days as an inline SVG bar chart (words per day). */
    fprintf(f, "<div class=\"panel\"><h2>Words per day &middot; last 14 "
               "days</h2>");
    long words_by_day[14];
    char labels[14][11];
    long max_words = 1;
    char cursor[11];
    ffs_date_from_ts(now_ts, cursor);
    ffs_date_add_days(cursor, -13);
    for (int i = 0; i < 14; i++) {
        memcpy(labels[i], cursor, sizeof(labels[i]));
        words_by_day[i] = 0;
        for (int d = 0; d < s->day_count; d++) {
            if (strcmp(s->days[d].date, labels[i]) == 0) {
                words_by_day[i] = s->days[d].words;
                break;
            }
        }
        if (words_by_day[i] > max_words)
            max_words = words_by_day[i];
        ffs_date_add_days(cursor, 1);
    }
    const int w = 640, h = 140, top = 10, bottom = 22, bar_zone = h - top - bottom;
    fprintf(f, "<svg viewBox=\"0 0 %d %d\" width=\"100%%\" role=\"img\" "
               "aria-label=\"Words dictated per day\">",
            w, h);
    for (int i = 0; i < 14; i++) {
        double slot = (double)w / 14.0;
        double bw = slot * 0.62;
        double x = slot * i + (slot - bw) / 2.0;
        double bh = (double)words_by_day[i] / (double)max_words * bar_zone;
        double y = (double)top + (bar_zone - bh);
        fprintf(f,
                "<rect x=\"%.1f\" y=\"%.1f\" width=\"%.1f\" height=\"%.1f\" "
                "rx=\"3\" fill=\"#4f8cff\"><title>%s: %ld words</title></rect>",
                x, y, bw, bh < 0 ? 0 : bh, labels[i], words_by_day[i]);
        if (i % 2 == 1 || i == 13)
            fprintf(f,
                    "<text x=\"%.1f\" y=\"%d\" font-size=\"9\" "
                    "text-anchor=\"middle\" fill=\"#8e8e93\">%.5s</text>",
                    x + bw / 2.0, h - 6, labels[i] + 5);
    }
    fprintf(f, "</svg></div>");

    fprintf(f, "<div class=\"panel foot\">");
    if (s->source_ok)
        fprintf(f, "Watching FreeFlow history &middot; tracking since %s<br>",
                since);
    else
        fprintf(f, "<b>History source unavailable</b> &mdash; is FreeFlow "
                   "installed and has it transcribed at least once?<br>");
    fprintf(f, "Time saved assumes %.0f WPM typing speed (change: "
               "<code>freeflow-stats --set-typing-wpm N</code>)<br>",
            s->typing_wpm);
    fprintf(f, "Dictation time measured from %ld of %ld recordings (%s "
               "total); transcripts are never stored &mdash; aggregates only.",
            s->with_audio, s->with_audio + s->without_audio, total_audio);
    fprintf(f, "</div></body></html>");

    int ok = fclose(f) == 0;
    if (ok && rename(tmp, path) != 0)
        ok = 0;
    if (!ok)
        remove(tmp);
    free(tmp);
    return ok ? 0 : -1;
}
