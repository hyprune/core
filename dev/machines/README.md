# Machines prototype: source helper and simulators

`source-helper.py` publishes data for live objects ("machines"): it runs commands, polls
HTTP(S) endpoints and keeps timers, and publishes each as one data source through the
existing `data.publish` IPC method. `sims/` holds five simulators that stand in for real
systems (a CI pipeline, a queue, a server rack, a deployment train and a build server) so
machines can be developed and demonstrated without any external service.

## Why it lives here

This is a **prototype** for the source helper described in the RFC-0013 (live objects)
draft. It runs out of process and uses only the `data.publish` contract that core already
has (IPC 0.2+): no new methods, fields or limits. It sits in `dev/` beside the nested
harness that drives it. Release builds also install it (below), so installed sessions
can feed gauges, but it is still the prototype: the helper the draft describes (a
bundled, supervised extension with a privilege split, user-confirmed commands, secret
storage and a Sources page) is not built yet, and this script exercises the data model
and the limits until it is.

## Installed paths

`dev/release/build.sh` puts these in the release archive (`hyprune-core/`); other
installs of core use the same layout under their prefix:

| Path | From |
| --- | --- |
| `libexec/hyprune/source-helper` | `source-helper.py` |
| `share/hyprune/machines/system-gauges.py` | `scripts/system-gauges.py` |
| `bin/hyprune-system-gauges` | a link to the gauge script, for `argv` in `sources.json` |

Both are standard-library Python and need nothing else from the checkout. A session
launcher always starts the helper (see "Gauges: wire a script"); it runs the helper with its Python as `python3 <prefix>/libexec/hyprune/source-helper`
and puts `<prefix>/bin` first on the helper's `PATH`, so `sources.json` can name the
gauge script as `hyprune-system-gauges` on any install. The release archive's live
launcher (`live/dev/live/live.py`) finds the helper at `../libexec/hyprune/source-helper`;
in a checkout it uses `dev/machines/source-helper.py`. The simulators and
`scripts/gauge-sim.py` stay in the checkout.

Everything is Python 3 standard library.

## Running

```sh
# Fetch every source once and print what would be published (no socket).
dev/machines/source-helper.py --config dev/machines/sources.example.json --once

# Run the loop, printing publications instead of sending them.
dev/machines/source-helper.py --config dev/machines/sources.example.json --dry-run

# Publish to a running core.
dev/machines/source-helper.py --config dev/machines/sources.example.json \
    --socket "$HYPRUNE_SOCKET" --token-file /path/to/token -v
```

| Option | Meaning |
| --- | --- |
| `--config FILE` | Sources configuration (below). Relative paths in it resolve against its directory. Default `$XDG_CONFIG_HOME/hyprune/sources.json` (`~/.config/hyprune/sources.json`). Re-read when it changes (below); it need not exist yet. |
| `--socket PATH` | Core's IPC socket, `$XDG_RUNTIME_DIR/hyprune/<instance>/control.sock`. Default `$HYPRUNE_SOCKET`. There is deliberately no other default, so the helper never attaches to a session by accident. |
| `--token-file FILE` | File holding the launch token. Re-read on every connection attempt. |
| `--token-fd N` | Read token lines from an inherited descriptor: one line per connection. |
| `--token-refresh` | With `--token-fd`: renew the grant for each reconnection (below). Default on when `HYPRUNE_TOKEN_REFRESH=1`. |
| (neither) | `$HYPRUNE_SOURCES_TOKEN` (or `$HYPRUNE_MACHINES_TOKEN`). |
| `--control PATH` | Control socket (below). Default `sources.sock` in the directory of `--socket` (beside core's `control.sock`). |
| `--no-control` | No control socket. |
| `--protocol V` | Protocol to offer in `session.hello` (default: config `protocol`, else `0.2`, the first with `data.publish`). |
| `--dev` | Same as `"devFastIntervals": true` (below). |
| `--once` | Fetch each source once, print `{"sourceId", "values"}` lines, exit (1 if any failed). |
| `--dry-run` | Run the loop; print each publication and the removals at exit. |
| `-v`, `--verbose` | Log one line per publication. Logs go to stderr. |

The helper says hello as `com.hyprune.sources` with the single capability `data.publish`.
Core grants a capability only through a launch token issued for that client ID, so the
session that starts core must carry a launch grant such as
`{"token": "<64 hex>", "client": "com.hyprune.sources", "capabilities": ["data.publish"]}`
in its launch manifest. Initial launch tokens are **one-use and expire 30 s after core
starts**: start the helper promptly, and to reconnect after a dropped connection write a
fresh token into the token file. Without the grant the helper logs
`no data.publish grant` and retries with back-off.

With `--token-fd N --token-refresh` the helper follows the same renewal protocol as the
shell's bridge: the supervisor writes the launch token as the first line; for every later
connection the helper writes `renew\n` to the descriptor and reads the next token line
(waiting at most 10 s, with at most one request outstanding). Without `--token-refresh` the
descriptor may hold a single line and close (a pipe): the helper connects with that token,
and when the connection is later lost and the descriptor has closed, it exits with status 3
(`cannot reconnect: the token channel is closed`) rather than run commands for a
connection that cannot come back. A supervisor that renews nothing restarts it with a new
grant, or a session restart does. The supervised live launcher
(`dev/live`) starts the helper this way; see "Gauges: wire a script" below. The descriptor
is not passed on to commands, and token variables are removed from their environment.
Tokens are never logged.

### Reloading the configuration

The helper checks the configuration file about once a second and re-reads it when it
changes (any edit, or an atomic replace). Added sources start, removed or disabled ones
are withdrawn with `data.remove`, and changed ones restart; unchanged sources keep running.
An invalid file is logged (`config not reloaded`), reported as `configError` in the control
channel's `status`, and the running sources stay as they were; a file that disappears (for
example mid-save) is ignored. Changes to `client` or `protocol` need a restart of the helper
or a `restart-all`.

Without a configuration the helper idles: when the file does not exist, is empty, or has
no `sources`, it still runs, connects to core (when it has a token) and serves the control
channel, and starts the sources as soon as a valid file appears. A file that is invalid at
start is treated the same way (idle, `configError` set) until it is fixed. Only `--once`
exits (status 2) on a missing or invalid file.

### Against a nested session

The nested harness (`dev/nested`) records the nested instance's signature in its state
directory (`session.json`, field `signature`). With a launch grant for
`com.hyprune.sources` in the nested session's manifest:

```sh
sig=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["signature"])' dev/nested/.state/session.json)
dev/machines/source-helper.py --config dev/machines/sources.example.json \
    --socket "$XDG_RUNTIME_DIR/hyprune/$sig/control.sock" --token-file "$TOKEN_FILE" -v
```

On `SIGTERM` or `SIGINT` the helper kills running commands, calls `data.remove` for each
source it published, removes its control socket and exits 0. Core also drops a
connection's sources when it closes.

## Control channel

A local channel for the shell (Director → Objects → Placed) to show, test and restart
sources without hand-editing or restarting anything. It is not core IPC.

- **Socket:** `sources.sock` in the directory of `--socket` (core's per-instance runtime
  directory), or `--control PATH`; `--no-control` disables it. No socket without
  `--socket` or `--control` (`--dry-run` and `--once` have none by default).
- **Security:** mode `0600`; every connection's peer uid is checked (`SO_PEERCRED`) and
  another user's connection gets `{"ok":false,"error":"permission denied"}`. The helper
  never runs anything that is not in its config file or in a `test` request. A stale
  socket left by a helper that died is replaced at start (only if it is a socket and
  nothing answers on it); a live one or any other file in the way leaves the channel off
  (logged). The socket is removed at exit.
- **Framing:** newline-delimited JSON, one request line → one reply line, several requests
  per connection allowed. A request is at most 64 KiB. At most 16 connections at a time.
- **Errors:** `{"ok": false, "error": "..."}` (invalid JSON, no `op`, unknown op, bad
  arguments, unknown source).

Requests are answered without blocking the publishing loop: `status` and `test` run on the
connection's own thread; `restart`, `restart-all` and `reload` are queued to the loop,
which wakes at once, and answered when applied (fetches already running are not waited for).

### `{"op": "status"}`

```json
{"ok": true, "config": "/home/u/.config/hyprune/sources.json", "connected": true, "configError": null,
 "sources": {
   "u-3f9a1c": {"kind": "command", "argv": ["/path/to/system-gauges.py", "ram"], "intervalS": 5,
                "enabled": true, "running": false,
                "lastRun": {"at": 1791800000000, "ok": true,
                            "values": {"value": 71, "display": "12.4 GB", "label": "RAM", "level": "ok",
                                       "_status": "ok", "_exitCode": 0},
                            "error": null, "exitCode": 0, "durationMs": 23},
                "nextRunAt": 1791800005000, "resets": 0},
   "weather": {"kind": "http", "url": "https://...", "intervalS": 60, "enabled": false, "running": false,
               "lastRun": null, "nextRunAt": null, "resets": 0}},
 "limits": {"dev": false,
            "command": {"intervalS": [5, 86400], "devIntervalS": 1, "timeoutS": [1, 60]},
            "http": {"intervalS": [10, 86400], "loopbackIntervalS": 2, "timeoutS": [1, 30]},
            "timer": {"intervalS": [1, 86400]}}}
```

- `config` is the absolute config path (write there, then `reload`). `configError` is why
  the current file is not in use (invalid), else `null`. `connected`: a core connection
  with the `data.publish` grant is open.
- `sources` is keyed by the config `id`, enabled sources first, then disabled ones (those
  show their `kind`, `argv`/`url` and `intervalS` as written).
- `argv` is the vector that runs (a relative program path resolved). `lastRun` is the last
  finished fetch: `at` wall-clock Unix ms, `ok` (`_status` was `ok`), `values` exactly as
  published (with reserved keys; on an error the last good values), `error`, `exitCode`
  (commands; `null` otherwise), `durationMs`. `null` before the first run. `nextRunAt`
  is wall-clock Unix ms (back-off included), `null` while running. `resets` counts
  `resetAverage` restarts.
- `limits` are the bounds the helper enforces now: with `devFastIntervals` or `--dev`
  (`"dev": true`) the command and HTTP floors are 1 s.

### `{"op": "test", "source": {...}}`

```json
{"op": "test", "source": {"kind": "command", "argv": ["/path/to/script", "ram"], "timeoutS": 4, "output": "json"}}
→ {"ok": true, "values": {"value": 71, "display": "12.4 GB"}, "stdout": "{\"value\": 71, ...}\n",
   "stderr": "", "error": null, "exitCode": 0, "durationMs": 31}
```

Runs a source definition once with the same rules as a configured source (no shell, a new
process group, `timeoutS`, the 64 KiB stdout cap, `output`/`key`/`fields`/`env`/`cwd`;
relative paths resolve against the config file's directory) and publishes nothing. The
definition is validated like a config entry (`argv` must be a non-empty list of strings;
an `intervalS`, if given, must respect the floors); an invalid one is an `ok: false` error.
A source that fails still answers `ok: true`, with `error` set (exit code, timeout, invalid
output, field errors) and `values` empty (or the fields that resolved). `stdout` is the
first 4 KiB, `stderr` the last 4 KiB (also on a timeout). `exitCode` is negative when the
command was killed (a timeout: `-9`). `kind: "http"` is also accepted (`exitCode` and
`stdout` stay empty). One or two tests run at a time; more get `another test is running`.

### `{"op": "restart", "id": "<id>", "resetAverage": false}`

→ `{"ok": true}`. `id` is the config id (or the full source id). Kills the source's running
command (its whole process group; the killed run is not published or counted as a
failure), clears the back-off and runs it now; the result is published as usual (at most
10 publications a second per source). An HTTP fetch in flight finishes and the source runs
again right after. `resetAverage: true` increments the source's reset counter, published
as the reserved key `_reset` (an integer) in every publication of that source from then on:
core restarts the running averages bound to the source when `_reset` changes, so each
request resets them exactly once. A disabled or unknown id is an error.

### `{"op": "restart-all"}`

→ `{"ok": true}` (plus `"configError"` when the file could not be read: the running
definitions are restarted instead). Withdraws every source (`data.remove`), closes the core
connection, re-reads the config (including `client` and `protocol`), reconnects at once
with a fresh token (`--token-fd --token-refresh`: a `renew` request; `--token-file`: the
file is read again) and starts every source now. Reset counters are kept.

### `{"op": "reload"}`

→ `{"ok": true, "added": [...], "changed": [...], "removed": [...]}` (config ids). Re-reads
the config now, with the same rules as the once-a-second check (unchanged sources keep
running). An invalid or missing file is an error and the running sources stay.

## Configuration

```json
{
  "client": {"id": "com.hyprune.sources", "name": "Source helper (prototype)"},
  "protocol": "0.2",
  "devFastIntervals": false,
  "sources": [
    {"id": "ci", "kind": "command", "argv": ["sims/ci-sim.py"], "intervalS": 5, "timeoutS": 2},
    {"id": "ci-main", "kind": "http", "url": "https://ci.example.com/api/summary",
     "headers": {"Authorization": {"prefix": "Bearer ", "secret": "ci-token"}},
     "intervalS": 30, "timeoutS": 10,
     "fields": [{"name": "status", "pointer": "/pipelines/0/status", "type": "string"},
                {"name": "queued", "countWhere": {"array": "/jobs", "pointer": "/status", "equals": "queued"}, "type": "int"},
                {"name": "jobsStarted", "pointer": "/pipelines/0/jobs_started", "type": "counter"}]},
    {"id": "minutes", "kind": "timer", "timer": "pulse", "everyS": 60}
  ]
}
```

Each source publishes source ID `<client id>.<id>` (`id` is a slug, `[a-z][a-z0-9-]*`).

| Key | Kinds | Meaning |
| --- | --- | --- |
| `id` | all | Slug; unique. |
| `kind` | all | `command`, `http` or `timer`. |
| `enabled` | all | `false` skips the source. |
| `intervalS` | all | Seconds between fetches, at most 86400. Command: ≥ 5, default 60. HTTP: ≥ 10 (≥ 2 for a loopback host), default 60, ±10% jitter. Timer: ≥ 1, default 1. |
| `timeoutS` | command, http | Command 1–60, HTTP 1–30; default 10 (or the interval if shorter). |
| `ttlMs` | all | `data.publish` TTL, 100–60000; default 10 × the interval, at most 60000. |
| `fields` | command (`json`), http | Picked values (below). Without `fields`, every top-level scalar is published. |
| `argv` | command | Argument vector, run without a shell (use `["sh", "-c", "..."]` explicitly if needed). A relative program path with a `/` resolves against `cwd`. |
| `cwd` | command | Working directory, relative to the config file's directory (default: that directory). |
| `env` | command | Extra environment variables (strings). |
| `output` | command | `json` (default), `number` (first line), `text` (first line), `lines` (count of non-empty lines). |
| `key` | command (non-`json`), timer | Value name for single-value outputs (default `value`; timers `pulse`/`remaining`). |
| `url` | http | `https://`, or `http://` for a loopback host or a source marked `"local": true`. |
| `local` | http | Allows plain HTTP and private addresses. A public host name that resolves to a loopback or private address is refused otherwise. |
| `method`, `body` | http | `GET` (default) or `POST` with a fixed JSON `body`. |
| `headers` | http | Strings, or `{"secret": "name", "prefix": "Bearer "}`. |
| `timer` | timer | `pulse`: a counter that grows every `everyS` seconds (counted from `fromS`, default the Unix epoch). `countdown`: whole seconds left until `untilS` (Unix seconds) / `until` (ISO 8601), or of a `durationS` started when the helper starts. |

`devFastIntervals` (or `--dev`) is a **prototype-only override**: it lowers the command and
HTTP interval floors to 1 s so the simulators can drive smooth demos. Never use it for real
endpoints or expensive commands; the shipped helper has no such switch.

Secrets in this prototype come from environment variables:
`{"secret": "ci-token"}` reads `HYPRUNE_SOURCES_SECRET_CI_TOKEN`. They are sent only in the
request header and are removed from error messages.

### Fields

```json
{"name": "queued", "type": "int", "countWhere": {"array": "/jobs", "pointer": "/status", "equals": "queued"}}
```

Each field has a `name` (not starting with `_`, ≤ 128 bytes), an optional `type`, an
optional scalar `default` (used when the selection does not resolve), and exactly one
selection:

| Selection | Value |
| --- | --- |
| `"pointer": "/a/0/b"` | The value at an [RFC 6901](https://www.rfc-editor.org/rfc/rfc6901) JSON Pointer (`~1` = `/`, `~0` = `~`; `""` is the whole document). It must be a scalar. |
| `"length": "/jobs"` | Length of the array at the pointer. |
| `"countWhere": {"array", "pointer", "equals"}` | Number of array elements whose sub-pointer equals the value. |
| `"first": {"array", "pointer"?, "equals"?, "select"?}` | The first element passing the test (or the first element), then the `select` sub-pointer. |

| Type | Accepts | Publishes |
| --- | --- | --- |
| (none) | any scalar | as is |
| `string` | any scalar | text, cut to 256 bytes |
| `number` | number or numeric string | number |
| `int` | number or numeric string | rounded integer |
| `bool` | bool, `"true"`/`"false"`, number | bool |
| `counter` | non-negative integer (or integral string) | integer; a decrease is logged (core treats it as a reset and fires nothing) |

A field that fails (missing pointer, non-scalar target, type mismatch) keeps its last good
value; the source reports `_status: "error"` with the field errors in `_error`.

## Script contract (command sources)

- Run without a shell in a new process group, stdin closed, working directory `cwd`.
- `output: "json"`: stdout is **one JSON object**, or **JSON lines** (one object per line,
  merged in order, later keys win). Without `fields`, top-level scalars (bool, finite
  number, string) are published; other values are dropped with a warning. Strings are cut
  to 256 bytes; keys starting with `_` are reserved and dropped; at most 28 keys are kept
  (32 minus the metadata keys).
- stdout over **64 KiB** is an error (not truncated). The last **4 KiB** of stderr is kept
  for the log and the error message.
- A run longer than `timeoutS` is an error: the whole process group is killed (`SIGKILL`).
- A non-zero exit is an error; `_exitCode` carries the code (negative: killed by a signal).

## Errors, back-off and metadata

Every publication carries reserved keys: `_status` (`ok` or `error`), `_error` on an error
(≤ 256 characters, without URL query strings, fragments or secret values), and
`_exitCode` (command) or `_httpStatus` (HTTP), and `_reset` (an integer) once the control
channel's `restart` with `resetAverage` was used for the source. On an error the last good
values stay in the publication, so a machine dims rather than resets.

A failed fetch retries after interval × 2ⁿ (±20% jitter, n = consecutive failures), at most
10 minutes; a success resets it. HTTP `429`/`503` with `Retry-After` waits at least that
long (at most an hour). HTTP bodies over 1 MiB are errors; redirects are followed only
within the same origin; no cookies are kept. At most four fetches run at once and a source
never overlaps itself.

## Publishing

- A source is published when its values (including metadata) change, and otherwise as a
  keep-alive every `ttlMs / 2`, so data does not expire while the helper runs.
- At most one publication per source per 100 ms (core's 10 Hz limit). A `-32008` rate
  refusal is retried after 200 ms; other `-32008` refusals (source or size limits) after 5 s.
- `sequence` is wall-clock milliseconds, bumped by one when equal or behind, so it is
  strictly increasing per source and a restarted helper is never stale.
- A lost connection reconnects with back-off (1 s doubling to 60 s) and republishes every
  source.

## Simulators

Each simulator prints **one JSON object** of scalars and exits. They are stateless and
deterministic: the output is a pure function of the time and a seed, so running one every
second tells a continuous story and any moment can be reproduced.

| Variable | Meaning |
| --- | --- |
| `MACHINES_SIM_TIME` | Time to simulate, Unix seconds (default: now). |
| `MACHINES_SIM_SHIFT` | Seconds added to the current time: slides the whole story (for demos that need an event in a window). |
| `MACHINES_SIM_SEED` | Story seed (default `hyprune`). |
| `MACHINES_SIM_EPOCH` | Start of the story, Unix seconds (default 0). Counters count from it and are 0 before it. |

```sh
dev/machines/sims/ci-sim.py                          # now
dev/machines/sims/ci-sim.py --at 1800000000          # a given moment
dev/machines/sims/ci-sim.py --timeline 1800000000 1800000600 1 --with-time   # JSON lines
```

`--timeline FROM TO STEP` helps pick demo seeds, for example a seed where CI fails inside a
given minute:

```sh
for seed in a b c d e; do
  MACHINES_SIM_SEED=$seed dev/machines/sims/ci-sim.py --timeline 1800000000 1800000060 1 |
    grep -q '"status":"failed"' && echo "$seed"
done
```

Simulators with counters (ci, deploy, build) play fixed blocks of time from seeded
templates that start and end idle. A counter is closed-form (whole template groups plus the
blocks before it), so it never decreases, across any boundary, and each run takes a few
milliseconds. Display numbers (job numbers, versions) wrap after long periods.

Keys are camelCase; indexed keys are `name.N`. The vocabulary is what a real system would
report; bindings map it onto machine inputs.

### `ci-sim.py` → `com.hyprune.sources.ci`

A four-stage pipeline (build 4–7 s, test 5–9 s, package 2–4 s, deploy 3–6 s). Jobs arrive
every 5–9 s in busy spells (occasional quick pushes build a queue), one job per stage at a
time; a job that finished a stage waits there while the next stage is busy. About 10% of
jobs fail at some stage (shown for 3 s); a passed job shows `success` on the deploy stage
for 2 s.

| Key | Values |
| --- | --- |
| `status` | Pipeline overall: `failed` while a stage shows a failure, `running` while a stage runs, `pending` when jobs only wait, `success` while a pass shows, otherwise the last outcome. |
| `stage.1` … `stage.4` | `pending` (empty), `running`, `success` (done; waiting or showing a pass), `failed` |
| `jobsStarted`, `jobsSucceeded`, `jobsFailed` | Counters |
| `queued` | Jobs waiting to enter the build stage, 0–12 |
| `active` | Jobs started and not finished (= started − succeeded − failed) |
| `progress` | Newest job, 0–1 |
| `lastJob` | Newest job, e.g. `main #1432` |
| `lastResult` | `success`, `failed` or `none` |

### `queue-sim.py` → `com.hyprune.sources.queue`

Arrivals follow slow waves (1.5–5.5 items/s); depth rides waves up to about 55. Now and
then the consumer stalls and a backlog builds to 300–480, then the consumer catches up.

| Key | Values |
| --- | --- |
| `depth` | Items waiting, 0–500 |
| `oldestAgeS` | Age of the oldest item (FIFO), seconds |
| `rateIn`, `rateOut` | Items per second |
| `level` | `ok`, `warn` (oldest > 30 s), `fail` (oldest > 90 s) |

### `rack-sim.py` → `com.hyprune.sources.rack`

Eight units with phase-shifted load waves and noise. Every few minutes a unit fails for
20–60 s (at most two at once) and the others pick up its load; now and then a unit is off
for 2–5 minutes of maintenance.

| Key | Values |
| --- | --- |
| `health.1` … `health.8` | `ok`, `warn` (load > 0.85), `fail` (down), `off` (maintenance) |
| `load.1` … `load.8` | 0–1 (0 when `fail` or `off`) |
| `load` | Mean of the eight loads |
| `down` | Units in `fail` |
| `name` | `rack-a` (`MACHINES_SIM_NAME` overrides) |

### `deploy-sim.py` → `com.hyprune.sources.deploy`

A cycle every 60–90 s: `queued` 5–10 s, `building`, `rolling`, `live` 15–25 s. About 15% of
cycles fail while rolling: `failed` holds 8 s, then the same version waits `queued` for
the next cycle.

| Key | Values |
| --- | --- |
| `phase` | `queued`, `building`, `rolling`, `live`, `failed` |
| `progress` | 0 while queued, 0–0.4 building, 0.4–1 rolling, 1 live; held where it stopped while failed |
| `version` | Version in flight, or the live one, e.g. `v2.14.3` |
| `environment` | `production` (`MACHINES_SIM_ENVIRONMENT` overrides) |
| `deploys` | Counter of successful deploys (grows as `live` starts) |

### `build-sim.py` → `com.hyprune.sources.build`

One build at a time: `idle` 6–25 s, `running` 8–20 s, then `success` or `failed` (about 1 in
5) for 4–8 s. Now and then a burst of 3–6 quick builds (3–6 s each) passes in a row.

| Key | Values |
| --- | --- |
| `status` | `idle`, `running`, `success`, `failed` |
| `progress` | 0–1 while running, 1 while a result shows, 0 when idle |
| `successes`, `failures` | Counters |
| `streak` | Consecutive successes up to now |
| `lastJob` | Current or latest build, e.g. `web #4821` |

## Gauges: wire a script

Every placed gauge (any machine object) reads its inputs from the source
`com.hyprune.sources.<instance id>` by default, with keys equal to the input ids: `value`
(0-100), `display` (short text such as `71%`), `label` (such as `RAM`), and optionally
`level` (`ok`, `warn` or `fail`) and `average` (a value that replaces the gauge's own
running average). So a script only has to print those keys, and a `sources.json` entry
whose `id` is the instance id publishes them to that gauge.

In the Director (IPC 0.24 cores and shells):

1. Place a gauge: Director → Objects → Catalog, Add a gauge (category Machines), then
   place it with tool 5 (Placement).
2. Director → Objects → Placed, select the gauge. Its **Data source** section lists the
   gauge's inputs (id, type, range, what each shows) with the values core holds, their
   condition and age, the source it reads (`com.hyprune.sources.<instance id>`), that
   source's status and error, and the helper's last run.
3. Set **Command** (Choose… browses folders inside the Director) and optional
   **Arguments** (one per line), and the interval (**Every**, within the helper's limits:
   5 s to 24 h). The panel shows the exact argv that will run (`Runs: …`).
4. **Test** runs that argv once through the helper (its command rules, nothing published)
   and shows the JSON it printed, checked against the inputs (missing keys, unknown keys,
   type and range mismatches).
5. **Save** writes the entry whose `id` is the instance id into
   `$XDG_CONFIG_HOME/hyprune/sources.json` (atomically; other entries, keys and their
   order are kept) and asks the helper to reload: values arrive within one interval. No
   world reload and no restart. **Remove** deletes the entry and withdraws the source.
6. **Restart** kills a running command of this source and runs it now; with **Reset
   average** the gauge's running average starts again from that run (`_reset`).
   **Restart helper** stops every source, re-reads the file, reconnects with a renewed
   grant and starts everything.

Saving or testing a command from the panel is the user's confirmation to run it; the
helper never runs anything that is not in `sources.json` or a Test request. The launcher
(`dev/live/up`, also in the release archive) and the starter session (`hyprune-session`,
in the hyprune/hyprune repository) always start the helper with its own renewable
`data.publish` grant, even before `sources.json` exists (it idles until the file
appears); the live launcher logs it to `source-helper.log` and `down` stops it.

A ready script: `scripts/system-gauges.py` (below) for CPU, RAM, GPU and VRAM; on an
installed system use it by name, `hyprune-system-gauges ram` (see "Installed paths").

### Advanced: editing the files

The panel edits `sources.json`; it can also be written by hand (re-read on change). The
entry for a gauge has the instance id as `id`:

```json
{"sources": [
  {"id": "u-3f9a1c", "kind": "command", "argv": ["/path/to/system-gauges.py", "ram"],
   "intervalS": 5, "timeoutS": 4}
]}
```

Command intervals are at least 5 s. Relative paths resolve against the directory of
`sources.json`. `sources.gauges.example.json` shows four system gauges (its ids,
`u-000001` to `u-000004`, are placeholders: use your gauges' ids).

`$XDG_CONFIG_HOME/hyprune/machines.json` binds a gauge to another source or
other keys (for example one script publishing several values, or a longer averaging
window), and is re-read on change:

```json
{"instances": {"u-3f9a1c": {"source": "com.hyprune.sources.system",
   "inputs": {"value": {"key": "ram"}, "display": {"key": "ramText"}}, "averageWindowS": 120}}}
```

`--dev` (or `"devFastIntervals": true`) allows 1 s intervals for the simulators in tests
and demos only; do not use it for real scripts.

### `scripts/system-gauges.py cpu|ram|gpu|vram`

Prints `{"value", "display", "label", "level"}` (`level` warn from 75, fail from 90) and
always exits 0; anything it cannot read shows value 0 with a display such as `n/a`.

| Kind | Reads | Display |
| --- | --- | --- |
| `cpu` | `/proc/stat`, compared with the previous run (state in `$XDG_RUNTIME_DIR/hyprune-system-gauges.json`; the first run samples for 0.25 s) | `37%` |
| `ram` | `/proc/meminfo`: MemTotal − MemAvailable | `12.4 GB` |
| `gpu` | NVIDIA: `nvidia-smi`; AMD: `gpu_busy_percent`; Intel: current clock / maximum clock (an approximation) | `37%`, Intel `~35%` |
| `vram` | NVIDIA: `nvidia-smi`; AMD: `mem_info_vram_used` / `_total`; Intel: none | `3.2 GB`, Intel `shared` |

With several GPUs it reads the discrete one (NVIDIA, then AMD, then Intel; `--gpu
nvidia|amd|intel` picks one). A runtime-suspended GPU is never woken: it shows `asleep`.
`nvidia-smi` runs only while one of your processes has the NVIDIA GPU open; otherwise the
gauge shows `idle`, so polling does not keep a laptop's dGPU from suspending. Use
`intervalS` 10 or more for `gpu` and `vram`.

### `scripts/gauge-sim.py`

A deterministic gauge feed for tests and demos, using the simulators' variables and
options (`MACHINES_SIM_TIME`, `_SHIFT`, `_SEED`, `_EPOCH`; `--at`, `--timeline`). The
value steps to a new seeded level (0-100) every `--period` seconds (default 6), counted
from the epoch.

| Option | Meaning |
| --- | --- |
| `--label TEXT` | `label` key (default `SIM`); also part of the seed, so gauges differ. |
| `--unit TEXT` | Appended to `display` (default `%`). |
| `--period S` | Seconds per step (default 6). |
| `--steps N` | Steps in the sequence, then it repeats (default 0: endless). |
| `--hold` | After `--steps` steps keep the last value for good (a gauge settles and stops drawing new frames). |

## Tests

```sh
python3 -m unittest dev/machines/test_machines.py
```

They cover output parsing and caps, timeouts (process group killed), back-off, JSON
pointers and fields, sequencing, publish-on-change and keep-alive, HTTP behaviour against a
local server, the gauge scripts (a fake `/proc` and `/sys`: a suspended GPU is never
queried), token renewal over a descriptor (and the exit once a one-use descriptor is spent),
reloading the configuration, the control channel (status shape, test runs that publish
nothing, restart killing a running command and `_reset`, restart-all reconnecting with a
renewed token, reload, idling until a config appears, the peer uid check, stale sockets),
30-minute timelines
of every simulator (exact keys, value domains, monotonic
counters, failures, outages, bursts), and the helper against a fake core socket (hello,
`data.publish` parameters, a rate refusal, reconnecting with a fresh token, `data.remove`
on `SIGTERM`).
