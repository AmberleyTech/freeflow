#ifndef FFS_HTML_H
#define FFS_HTML_H

#include "common.h"

/* Regenerate the self-contained stats page (inline CSS/SVG, no JS, no
 * network, meta-refresh 60s). Written via temp file + rename. Contains
 * aggregates only — never transcript text. */
int ffs_html_write(const FfsStats *s, const char *path, long now_ts);

#endif
