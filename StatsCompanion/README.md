# FreeFlow Stats

A tiny, local-only companion that turns your FreeFlow dictation history into
personal stats: words per minute, day streaks, total transcriptions, total
words, and time saved vs typing.

It is a **sidecar**: FreeFlow itself stays 100% upstream and untouched, its
in-app updater keeps working, and no custom FreeFlow build or DMG is ever
needed.

## Quick start (macOS 13+)

```bash
cd StatsCompanion
make            # builds build/freeflow-stats (~52 KB, only links libsqlite3)
make check      # runs the test suite (synthetic data only)
make install    # installs the binary + a launchd agent, done
```

After install, the agent runs by itself whenever FreeFlow records a
transcription. View your stats any time:

```bash
~/Library/Application\ Support/FreeFlowStats/bin/freeflow-stats --open
```

or open `~/Library/Application Support/FreeFlowStats/stats.html` directly.
The page refreshes itself every minute.

Remove with `make uninstall` (the agent and plist are removed; your stats
stay until you delete `~/Library/Application Support/FreeFlowStats`).

## Why it never conflicts with FreeFlow updates

FreeFlow's updater replaces the whole `FreeFlow.app` bundle with upstream
releases. The sidecar never touches that bundle, FreeFlow's source tree, its
build, or its workflows — it only **reads** two things FreeFlow already
writes locally:

- `~/Library/Application Support/FreeFlow/PipelineHistory.sqlite`
  (FreeFlow's own history, already capped at the last 20 transcriptions)
- `~/Library/Application Support/FreeFlow/audio/*.wav`
  (per-recording audio, used only to measure dictation duration)

Everything the sidecar produces lives in its own directory,
`~/Library/Application Support/FreeFlowStats/`, which FreeFlow and its
updater never look at. Updating FreeFlow — in-app or by merging upstream —
cannot break or delete the extension. (One caveat: the history database
layout is an internal detail of FreeFlow; the sidecar discovers it at
runtime and, if a future release renames it, simply reports "history source
unavailable" until the sidecar is updated.)

## Resource budgets (enforced by tests, not by hope)

**RAM.** There is no persistent process. launchd starts the binary only when
the history database changes (`WatchPaths`, plus a 5-minute backstop); it
ingests in milliseconds and exits. Idle footprint: **0 MB**. Peak footprint
of a full ingest+render run: **~5.7 MB measured** on the test fixture (which
also builds the fixture DB through SQLite; the production path is
read-only), hard-capped at 10 MB by `make memcheck`.

**Disk.** Aggregates only, in a compact JSON store — measured **~30 bytes
per day**, so 1,000 days ≈ 29 KB and even the hard-capped 4,096-day
(11-year) store is ~119 KB, **2.3% of the 5 MB budget** (`make diskcheck`
fails the build above a 10x-stricter 500 KB gate). Day buckets beyond the
cap fold into the lifetime totals, so the file is bounded forever. The HTML
page (~5–60 KB) is regenerated in place; no logs or caches are kept.

## What is stored (and what is not)

`stats.json` contains: lifetime totals, per-day buckets
`(date, transcriptions, words, seconds)`, a 20-entry ring of recent events
`(timestamp, words, seconds)`, an ingestion watermark, and your typing-WPM
preference. **Transcript text is never written to disk by the sidecar** — a
privacy test asserts that fixture transcripts appear in neither `stats.json`
nor `stats.html`. FreeFlow's own 20-item history cap remains the only place
transcripts exist locally.

## CLI

| Command | What it does |
| --- | --- |
| `freeflow-stats` | Ingest new history, update `stats.json` + `stats.html` (what launchd runs) |
| `--summary` | Print current stats to the terminal |
| `--html` | Regenerate the stats page |
| `--open` | Ingest, then open the stats page |
| `--set-typing-wpm N` | Set the typing speed behind "time saved" (default 40) |
| `--test` | Run the test suite |
| `--memcheck` / `--diskcheck` | Enforce the RAM / disk budgets |

## How the metrics are computed

- **Dictation WPM** — words ÷ audio duration, read from each recording's
  WAV header (the audio body is never loaded). Reported for the recent 20
  transcriptions and all time; recordings without audio are excluded and
  counted in the coverage note.
- **Streak** — consecutive calendar days with at least one transcription,
  ending today (or yesterday, while today is still in progress).
- **Time saved** — `total words ÷ typing WPM × 60s − time spent dictating`,
  clamped at zero.
- **Totals** — lifetime counts, accumulated incrementally on every run.

## Limitations

- FreeFlow keeps only the last 20 history entries. Because the launchd agent
  fires on every database write, the sidecar normally sees each entry before
  it rotates out — but a burst of more than 20 dictations while you are
  logged out or the Mac is asleep could drop the oldest before ingestion.
- FreeFlow's retry feature updates history rows in place; a retried
  transcription counts once, with its first word count.
- No menu-bar icon — the deliberate price of the sub-10 MB RAM budget. The
  browser page and terminal summary are the UI.
- The launchd agent is macOS-only; the engine itself is portable C99 and its
  full test suite also runs on Linux (used for CI-style verification).

## Verification

```bash
make check      # unit + integration tests (synthetic fixtures only)
make memcheck   # peak RSS must stay under 10 MB
make diskcheck  # 1,000 synthetic days must serialize under 500 KB
```

## Privacy

Read-only access to FreeFlow's local files; no network access, no
telemetry, no analytics, no logs. The stats page is a local file with no
external resources.
