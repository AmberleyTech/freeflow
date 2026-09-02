#include "store.h"
#include "stats_math.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void ffs_stats_init(FfsStats *s) {
    memset(s, 0, sizeof(*s));
    s->version = FFS_VERSION;
    s->typing_wpm = 40.0;
}

void ffs_stats_add_event(FfsStats *s, long ts, long words, double seconds) {
    s->total_count++;
    s->total_words += words;
    if (seconds >= 0.0) {
        s->total_seconds += seconds;
        s->with_audio++;
    } else {
        s->without_audio++;
    }
    if (s->first_ts == 0 || ts < s->first_ts)
        s->first_ts = ts;
    if (ts > s->last_ts)
        s->last_ts = ts;

    char date[11];
    ffs_date_from_ts(ts, date);

    /* Binary search for the bucket; days stay sorted ascending. */
    int lo = 0, hi = s->day_count;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        int cmp = strcmp(s->days[mid].date, date);
        if (cmp == 0) {
            s->days[mid].count++;
            s->days[mid].words += words;
            if (seconds >= 0.0)
                s->days[mid].seconds += seconds;
            goto recent;
        }
        if (cmp < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    if (s->day_count == FFS_MAX_DAYS) {
        /* Hard bound: oldest days fold into the lifetime totals above. */
        memmove(s->days, s->days + 1, sizeof(FfsDay) * (FFS_MAX_DAYS - 1));
        s->day_count--;
        if (lo > 0)
            lo--;
    }
    memmove(s->days + lo + 1, s->days + lo, sizeof(FfsDay) * (s->day_count - lo));
    s->day_count++;
    memset(&s->days[lo], 0, sizeof(FfsDay));
    memcpy(s->days[lo].date, date, sizeof(s->days[lo].date));
    s->days[lo].count = 1;
    s->days[lo].words = words;
    s->days[lo].seconds = seconds >= 0.0 ? seconds : 0.0;

recent:
    if (s->recent_len == FFS_RECENT) {
        memmove(s->recent, s->recent + 1, sizeof(FfsEvent) * (FFS_RECENT - 1));
        s->recent_len--;
    }
    s->recent[s->recent_len].ts = ts;
    s->recent[s->recent_len].words = words;
    s->recent[s->recent_len].seconds = seconds;
    s->recent_len++;
}

/* ---- compact JSON: fixed key order, positional arrays, no whitespace ----
 * {"v":1,"tw":40,"tot":[c,w,sec,wa,wo],"f":..,"l":..,"wm":..,"src":1,
 *  "d":[["YYYY-MM-DD",c,w,s],...],"r":[[ts,w,s],...]}
 * Seconds print with %.3f; -1 marks unknown duration in the recent ring.
 */

int ffs_store_save(const FfsStats *s, const char *path) {
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
    fprintf(f, "{\"v\":%d,\"tw\":%g,\"tot\":[%ld,%ld,%.3f,%ld,%ld],"
               "\"f\":%ld,\"l\":%ld,\"wm\":%ld,\"src\":%d,\"d\":[",
            s->version, s->typing_wpm, s->total_count, s->total_words,
            s->total_seconds, s->with_audio, s->without_audio, s->first_ts,
            s->last_ts, s->watermark, s->source_ok);
    for (int i = 0; i < s->day_count; i++) {
        const FfsDay *d = &s->days[i];
        fprintf(f, "%s[\"%s\",%ld,%ld,%.3f]", i ? "," : "", d->date, d->count,
                d->words, d->seconds);
    }
    fprintf(f, "],\"r\":[");
    for (int i = 0; i < s->recent_len; i++) {
        const FfsEvent *e = &s->recent[i];
        fprintf(f, "%s[%ld,%ld,%.3f]", i ? "," : "", e->ts, e->words,
                e->seconds);
    }
    fprintf(f, "]}");

    int ok = 0;
    if (fflush(f) == 0) {
        int fd = fileno(f);
        if (fd >= 0)
            fsync(fd);
        ok = 1;
    }
    if (fclose(f) != 0)
        ok = 0;
    if (ok && rename(tmp, path) != 0)
        ok = 0;
    if (!ok)
        remove(tmp);
    free(tmp);
    return ok ? 0 : -1;
}

typedef struct {
    const char *p;
    int ok;
} Cur;

static void ws(Cur *c) {
    while (*c->p == ' ' || *c->p == '\t' || *c->p == '\n' || *c->p == '\r')
        c->p++;
}

static int expect(Cur *c, char ch) {
    ws(c);
    if (*c->p != ch) {
        c->ok = 0;
        return 0;
    }
    c->p++;
    return 1;
}

/* Match a quoted key followed by ':'. */
static int key(Cur *c, const char *k) {
    ws(c);
    if (*c->p != '"') {
        c->ok = 0;
        return 0;
    }
    c->p++;
    while (*k) {
        if (*c->p != *k) {
            c->ok = 0;
            return 0;
        }
        c->p++;
        k++;
    }
    if (*c->p != '"') {
        c->ok = 0;
        return 0;
    }
    c->p++;
    return expect(c, ':');
}

static long read_long(Cur *c) {
    ws(c);
    char *end;
    long v = strtol(c->p, &end, 10);
    if (end == c->p) {
        c->ok = 0;
        return 0;
    }
    c->p = end;
    return v;
}

static double read_double(Cur *c) {
    ws(c);
    char *end;
    double v = strtod(c->p, &end);
    if (end == c->p) {
        c->ok = 0;
        return 0;
    }
    c->p = end;
    return v;
}

static void read_string(Cur *c, char *out, size_t n) {
    ws(c);
    if (*c->p != '"') {
        c->ok = 0;
        out[0] = 0;
        return;
    }
    c->p++;
    size_t i = 0;
    while (*c->p && *c->p != '"') {
        if (i + 1 < n)
            out[i++] = *c->p;
        c->p++;
    }
    if (*c->p != '"')
        c->ok = 0;
    else
        c->p++;
    out[i] = 0;
}

int ffs_store_load(FfsStats *s, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f)
        return 0; /* fresh store */
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return -1;
    }
    long size = ftell(f);
    if (size < 0 || size > 4L * 1024 * 1024) { /* far above any valid store */
        fclose(f);
        return -1;
    }
    rewind(f);
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return -1;
    }
    size_t got = fread(buf, 1, (size_t)size, f);
    fclose(f);
    buf[got] = 0;

    FfsStats parsed;
    ffs_stats_init(&parsed);

    Cur c = {buf, 1};
    expect(&c, '{');
    if (key(&c, "v"))
        parsed.version = (int)read_long(&c);
    expect(&c, ',');
    if (key(&c, "tw"))
        parsed.typing_wpm = read_double(&c);
    expect(&c, ',');
    if (key(&c, "tot")) {
        expect(&c, '[');
        parsed.total_count = read_long(&c);
        expect(&c, ',');
        parsed.total_words = read_long(&c);
        expect(&c, ',');
        parsed.total_seconds = read_double(&c);
        expect(&c, ',');
        parsed.with_audio = read_long(&c);
        expect(&c, ',');
        parsed.without_audio = read_long(&c);
        expect(&c, ']');
    }
    expect(&c, ',');
    if (key(&c, "f"))
        parsed.first_ts = read_long(&c);
    expect(&c, ',');
    if (key(&c, "l"))
        parsed.last_ts = read_long(&c);
    expect(&c, ',');
    if (key(&c, "wm"))
        parsed.watermark = read_long(&c);
    expect(&c, ',');
    if (key(&c, "src"))
        parsed.source_ok = (int)read_long(&c);
    expect(&c, ',');
    if (key(&c, "d")) {
        expect(&c, '[');
        while (c.ok && expect(&c, '[')) {
            if (parsed.day_count >= FFS_MAX_DAYS) {
                c.ok = 0;
                break;
            }
            FfsDay *d = &parsed.days[parsed.day_count];
            read_string(&c, d->date, sizeof(d->date));
            expect(&c, ',');
            d->count = read_long(&c);
            expect(&c, ',');
            d->words = read_long(&c);
            expect(&c, ',');
            d->seconds = read_double(&c);
            expect(&c, ']');
            parsed.day_count++;
            ws(&c);
            if (*c.p == ',')
                c.p++;
            else
                break;
        }
        expect(&c, ']');
    }
    expect(&c, ',');
    if (key(&c, "r")) {
        expect(&c, '[');
        while (c.ok && expect(&c, '[')) {
            if (parsed.recent_len >= FFS_RECENT) {
                c.ok = 0;
                break;
            }
            FfsEvent *e = &parsed.recent[parsed.recent_len];
            e->ts = read_long(&c);
            expect(&c, ',');
            e->words = read_long(&c);
            expect(&c, ',');
            e->seconds = read_double(&c);
            expect(&c, ']');
            parsed.recent_len++;
            ws(&c);
            if (*c.p == ',')
                c.p++;
            else
                break;
        }
        expect(&c, ']');
    }
    expect(&c, '}');

    free(buf);
    if (!c.ok)
        return -1;
    *s = parsed;
    return 0;
}
