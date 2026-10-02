# The build bench

Chromium is built and the shell is exercised on one headless build host (the
"bench"), never on a seat. These scripts are the only way the repository touches
it. Everything long-running is a transient user unit on the bench (`systemd-run
--user`), so a closed ssh session never kills a build, and rule R19 holds on the
bench too: every compositor test runs under `runtime-test`. The bench host name
is `worker` (an ssh alias); `BENCH_HOST` overrides it.

## The scripts

| Script | Runs on | What |
|---|---|---|
| `sync.sh` | here | rsync this worktree to `bench:~/views-shell-wt/<branch>/` and print the remote path |
| `job.sh start <name> <script> [args]` | here | start `<script>` (a path on the bench) as unit `vs-<name>`, logging to `~/views-bench/logs/<name>.log` |
| `job.sh wait <name> [seconds]` | here | poll until the unit stops (default 1200 s); exit with the job's rc, 124 on timeout |
| `job.sh status <name>` | here | print `rc <n>` (the recorded rc) and exit with it; 125 while running or unknown |
| `job.sh log <name> [lines]` | here | tail the job log |
| `lock.sh hold\|release\|status` | here | hold `~/views-bench/bench.lock` from a detached ssh session across many separate commands, end it, or report who holds it (read-only); see "The bench lock" |
| `worker/run.sh <name> <cmd...>` | bench | the unit body: runs `<cmd>` inside the FHS build environment, records `~/views-bench/results/<name>.rc` |
| `worker/fhs.sh <cmd...>` | bench | run one command inside the FHS build environment (depot_tools on PATH, ccache) |
| `worker/wire.sh <worktree>` | bench | pristine tree, copy `shell/` to `src/views_shell/`, mirror `style/`, `schemas/`, `examples/` and `tools/fixtures/` to `src/views_shell/data/`, apply `shell/patches/series` with plain `git apply`, print `WIRE-OK` |
| `worker/ensure-out.sh <out> <args-file>` | bench, in the FHS env | bring `out/<out>` to `<args-file>` idempotently: copy the args when they differ or when siso state (`.siso_deps`) is present, `gn clean` in that case (siso back to ninja needs it), `gn gen`, print `ENSURE-OK <out> cleaned\|unchanged` |
| `worker/build.sh <out> <target...>` | bench | `autoninja -C out/<out> <targets>` in the Chromium checkout |
| `worker/census.sh <out> <label> <binary> <report>` | bench, in the FHS env | size as linked and after the checkout's `llvm-strip`, `ldd` count, `runtime_deps` files and bytes, `gn desc deps --all` grouped by the first and the first two path segments |
| `worker/headless.sh <outdir> <cmd...>` | bench, outside the FHS env | under `runtime-test`: headless scroll (pixman by default), run `<cmd>` with `WAYLAND_DEBUG=1`, wait for the first attach, settle, measure, capture the tree, a screenshot and the log, then stop everything; writes `summary.txt` (`attaches:`, `colours:`, the protocol counts, the measure row) |
| `worker/session-demo.sh` | bench | a client for `headless.sh`: starts `views_shell`, waits for its first attach, sends `notify-send` once the shell owns `org.freedesktop.Notifications`, and with `--demo-keyboard` types with `wtype -s 1500`; prints `views_shell exited rc <n>` for a `--run-for-seconds` run |
| `worker/measure.sh <label> <seconds> <pid> <results.tsv>` | bench | PSS (`smaps_rollup`), RSS, idle CPU, context switches, threads and fds of a process tree, one row |
| `worker/runtime-test-gpu` | bench | a copy of the bench's `runtime-test` with one added flag, `--allow-dri` (binds `/dev/dri`, nothing else); installed at `~/views-bench/bin/runtime-test-gpu`, never in place of `~/.local/bin/runtime-test` |
| `seq/<item>.sh [stage...]` | bench, outside the FHS env | one work item's whole bench sequence, run as a unit under the bench lock; see "The sequences" |
| `args.views.gn`, `args.release.gn` | here | the two build configurations `ensure-out.sh` applies (component development; non-component footprint, `shell/build/README.md`) |

The bench layout (`~/views-bench/` on the host): `build-env` (a link to the
FHS environment built from `worker/flake.nix`), `scroll/` (stock scroll, the
dotfiles pin), `bin/` (`runtime-test-gpu`), `logs/`, `results/`, `marks/`
(bootstrap markers), `bench.lock`. The Chromium checkout is `~/chromium/src` at
the tag in `shell/build/CHROMIUM_VERSION`, with `out/views` (component,
development) and `out/release` (non-component, for footprint); `out/views-t4`
is a chapter-1 leftover (7.4 GB, drifted args) that nothing uses.

## The transient-unit recipe

Every item's bench work is one script, `seq/<item>.sh`, synced with the
worktree and run as one transient user unit that holds the bench lock for its
whole life. From the worktree:

```
R=$(tools/bench/sync.sh)
ssh worker "rm -f ~/views-bench/results/<item>-seq.rc; systemctl --user reset-failed vs-<item>-seq 2>/dev/null; \
  systemd-run --user --unit=vs-<item>-seq --collect -p Environment=HOME=\$HOME \
  -p 'Environment=PATH=/run/wrappers/bin:/etc/profiles/per-user/\$USER/bin:/run/current-system/sw/bin:/nix/var/nix/profiles/default/bin' \
  /run/current-system/sw/bin/bash -c \
  'flock -w 7200 ~/views-bench/bench.lock bash $R/tools/bench/seq/<item>.sh >> ~/views-bench/logs/<item>-seq.log 2>&1; echo \$? > ~/views-bench/results/<item>-seq.rc'"
tools/bench/job.sh wait <item>-seq 14400
tools/bench/job.sh log <item>-seq 300
```

This is the unit shape `job.sh start` makes, with the lock taken inside the
unit and `worker/run.sh` left out: `run.sh` enters the FHS environment for the
whole command, and a sequence must run outside it, because `runtime-test` must
not nest inside it; the sequence enters the environment per command
(`~/views-bench/build-env -c '...'`). `job.sh wait`, `status` and `log` read the
same `results/<name>.rc` and `logs/<name>.log`, so they work on such a unit.
`job.sh start` itself remains for a single build or gn command that needs the
environment and no lock. Every step echoes its rc and the script exits non-zero
if any step failed. Do not re-run `sync.sh` for a branch while its sequence
runs: `rsync --delete` would replace the script being read.

## The sequences

Each `seq/<item>.sh` takes stage names (all by default). The common stages are
`wire` (pristine checkout, this worktree's `shell/` and data mirror, the
patches), `build` (`ensure-out.sh views tools/bench/args.views.gn`, `gn check
'//views_shell/*'`, `views_shell` and `views_shell_unittests`) and the item's
runs and tests; `claims` re-runs the PR's prove commands literally in the same
lock hold.

| Sequence | Item | Beyond wire and build |
|---|---|---|
| `w1a.sh` | the production viz host | `graph` (gn desc and gn path evidence), `runs` (bar, popup, plain, in software and GPU compositing), `tests` |
| `w1b.sh` | the release build | `out/release` from `args.release.gn`, `census.sh` for both binaries in both directories, the three footprint runs; `w1b.sh prove` re-runs C10.2, C10.3 and C10.5 |
| `w1c.sh` | the FD census and the GPU-fair rows | `CLIENT_FD_DUMP=1` runs on pixman, then gles2 with SwiftShader and with the default client GL under `runtime-test-gpu --allow-dri` |
| `w2a.sh` | the scroll adapter | `record` (a fresh transcript from headless scroll), `tests` (`Scroll*:WmModel*`), `probes` (`wm_probe --dump`, `--switch`, a timeout, `--watch`) |
| `w2b.sh` | the style kit and the bar | `tests` (`Theme*:Bar*:Clock*`), `runs` (three themes, no theme, a light popup, left-tabs), `prove`, `claims` |
| `w2c.sh` | the notifications library | `graph`, `display` (every Views test inside `runtime-test` with a private bus and a private headless scroll), `bus`, `nobus` |
| `w3a.sh` | the ui-tree renderer | `format` (clang-format dry run), `plain` (no display), `display` (every root shown in a Widget), `graph` |
| `w3b.sh` | the plugin host | `tests`, `probes` (`plugin_probe` against echo-process and music-scratchpad), `isolated` (the plain launch path inside `runtime-test`), `claims` |
| `w3c.sh` | the assembly | `runs` through `session-demo.sh` (switch, notify, keyboard, exit) and the footprint rows (software, GPU-fair), `claims`, `release` (the assembled program on `out/release`) |

Two facts every sequence relies on. The bench is shared and `out/views` holds
whatever the last lock holder built, so a prove that runs a binary runs inside
the lock hold that built it, and PROVE rows otherwise read the sequence's saved
logs (`~/views-bench/results/<item>-*`). And the FHS `build-env` prints a
3-line banner on stdout before the command's output, so the first line of a
`gn desc` is line 4.

## headless.sh switches

All optional, read from the environment of `headless.sh`:

| Variable | Default | Effect |
|---|---|---|
| `CLIENT_SETTLE` | `4` | seconds between the first attach and the capture |
| `CLIENT_FD_DUMP` | `0` | `1`: after the settle, `readlink` every `/proc/<pid>/fd` entry of the client's process tree into `fds-<pid>.txt` (fd, kind, target, and the peer path of a unix socket) |
| `CLIENT_FD_RECHECK` | `0` | with `CLIENT_FD_DUMP=1`, a second census this many seconds after the first (`fds-recheck-<pid>.txt`), to tell a leak from a steady allocation |
| `BENCH_RUNTIME_TEST` | `~/.local/bin/runtime-test` | the isolation wrapper; `~/views-bench/bin/runtime-test-gpu` for GPU runs |
| `BENCH_RUNTIME_TEST_ARGS` | empty | wrapper flags before `--`, word-split, e.g. `--allow-dri` |
| `BENCH_WLR_RENDERER` | `pixman` | the compositor's `WLR_RENDERER`; `gles2` needs a render node inside the wrapper |
| `WLR_RENDER_DRM_DEVICE` | unset | passed to the compositor only, e.g. `/dev/dri/renderD128` |

The renderer choice reaches the compositor only; the client's GL is chosen by
its own switches (`--use-gl=angle --use-angle=swiftshader`, finding F2, or none
for Chromium's default, which draws over linux-dmabuf once `/dev/dri` is
present). `summary.txt` gains a `renderer:` line (the compositor's renderer and
the nodes visible in `/dev/dri`) and, with the FD census, three lines per
census:

```
fd kinds: socket=N anon_inode:[eventfd]=N pipe=N memfd=N /dev/shm=N file=N other=N
fd other: anon_inode:[eventpoll]=N anon_inode:[timerfd]=N /dev/null=N ...
fd per process: <pid>:<comm>=N ...
```

`file` is any path outside `/dev`; `other` is everything else, split on the
second line. The same lines with ` (recheck)` follow the recheck. A GPU run
looks like:

```
BENCH_RUNTIME_TEST=~/views-bench/bin/runtime-test-gpu BENCH_RUNTIME_TEST_ARGS=--allow-dri \
  BENCH_WLR_RENDERER=gles2 WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128 \
  bash worker/headless.sh ~/views-bench/results/<run> ~/views-bench/build-env -c '<client>'
```

The bench's render node also serves other GPU work; a GPU run only opens the
node and never stops or restarts anything. A busy node is a measurement, not a
reason to intervene.

A console client (`wm_probe`, `plugin_probe`) never attaches a Wayland buffer,
so `headless.sh` waits out its attach timeout (about 20 s) and reports `client
alive at capture: 0`; the evidence is the client log and the rc the sequence
echoes. A session bus for a run comes from `dbus-run-session` started outside
the FHS environment and wrapping it (`dbus-run-session -- ~/views-bench/build-env
-c ...`); inside the FHS environment it fails with "Configuration file needs one
or more <listen> elements". At teardown `headless.sh` kills the client's `bwrap`
first and the compositor right after, so a still-living `views_shell` logs a
Wayland broken pipe and an adapter disconnect after capture and measure; that is
harmless.

## The bench lock

The bench is shared. Every wire, build and run sequence holds
`~/views-bench/bench.lock` (`flock -w <s> ~/views-bench/bench.lock ...`); a
`seq/<item>.sh` script runs as a transient unit under it (above). The lock file
has existed since 2026-10-02 00:11:43, created by the first `flock`.

### Holding the lock from here: `lock.sh`

`tools/bench/lock.sh hold` waits (up to 14,400 s) for the lock on the bench,
prints `HOLDING` and returns while a detached ssh session keeps it
(`flock -w 14400 ~/views-bench/bench.lock sh -c 'echo HOLDING; read _'`, the
local process group's id in `.bench-lock.pid`, its output in `.bench-lock.log`,
both gitignored). `lock.sh release` kills that process group; the remote
`read` sees end of input and flock lets go. A dropped connection releases it
the same way, so check `status` after a network fault. `lock.sh status` reads
`stat` and `/proc/locks` on the bench and never opens or creates the lock file:
it prints `not held`, or `held by pid N: <command>` and a `waiting: N` line,
plus `this checkout: holding (pgid N)` when this checkout's session is alive.
It exits 0 in every case and 2 when the bench cannot be reached.

The lock exists for a verifier that re-runs bench rows of PROVE.md, which ssh
one by one and never take the lock themselves:

```
tools/bench/lock.sh hold
tools/prove.sh --form bench --latest --ids '^P(9|10|11)\.'
tools/bench/lock.sh release
```

A row that takes the lock itself (`flock ... bench.lock`; `tools/prove.sh
--dry-run` marks it `[takes bench.lock]`) would wait for the hold: run it
outside one. `BENCH_HOST` and `BENCH_LOCK` (a plain path) point the script at
another host or file, which is how it is tested without the bench. A row that
runs a binary from `out/views` is only valid if the checkout is wired and built
from the row's commit inside the same hold (`seq/<item>.sh wire build claims`).

## Re-running proofs: `tools/prove.sh` and `tools/prove-lint.py`

`PROVE.md` is one append-only table (`id | claim | task | form | command | rc |
result | evidence | commit | at`). `python3 tools/prove-lint.py` checks every
row (ten cells, `P<n>.<m>` beside `C<n>.<m>`, the task, form, rc ⇔ result,
an existing commit, an ISO-8601 UTC `at`, no `/home/<user>`, and a runnable
command cell) and prints `prove-lint ok (N rows)` or each offending row, plus
information lines (legacy rows, claims SPEC.md does not name, rows recorded as
`local` that contact the bench). `tools/prove.sh` re-runs rows from the
repository root:

| Flag | Effect |
|---|---|
| `--latest` | the newest row per id only (re-verification repeats ids) |
| `--ids <regex>` | rows whose id matches |
| `--form local\|bench` | by effective form: a row is bench when recorded so or when its command ssh-es or calls `tools/bench/{sync,job,lock}.sh` or a `worker/` script |
| `--dry-run` | print `<id> [<form>] <command>` and run nothing |
| `--timeout <s>` | per-row limit, default 1800 (rc 124) |
| `--no-toolbox` | do not put nixpkgs' python3 with jsonschema and node on PATH |
| `-v` | print each row's output |

It prints `<id> <rc> <pass|fail> <seconds>` per row and `PROVE-RUN
<passed>/<total>`, writes `prove-results.tsv` (gitignored) and exits 1 if a row
failed. Rows whose command cell is prose (legacy exceptions listed in
`tools/prove-lint.py`) print `<id> - manual 0` and are run by hand. The runner
never ssh-es and never takes the lock by itself; the commands in bench rows do.
Chapter-1 bench rows (P2.x–P4.x) name chapter-1 worktree paths and job names
that no longer exist on the bench; a verifier re-proves those claims with fresh
rows rather than trusting them.

## Environment variables

| Variable | Read by | Meaning |
|---|---|---|
| `BENCH_HOST` | `sync.sh`, `job.sh`, `lock.sh` | the ssh alias of the bench (default `worker`) |
| `BENCH_LOCK` | `lock.sh` | the lock file's path on the bench (default `~/views-bench/bench.lock`) |
| `CLIENT_SETTLE`, `CLIENT_FD_DUMP`, `CLIENT_FD_RECHECK`, `BENCH_RUNTIME_TEST`, `BENCH_RUNTIME_TEST_ARGS`, `BENCH_WLR_RENDERER`, `WLR_RENDER_DRM_DEVICE` | `headless.sh` | above |
| `PROVE_SH_NESTED` | `prove.sh` | set by the runner when a row runs it again: skips rows that call `tools/prove.sh` and writes no `prove-results.tsv` |
| `VIEWS_SHELL_NO_REEXEC` | `tools/plugin-registry.py`, `tools/ui-tree-render.py` | stops the re-exec under nixpkgs' python3 with jsonschema |
