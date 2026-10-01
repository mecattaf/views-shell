# The build bench

Chromium is built and the shell is exercised on one headless build host (the
"bench"), never on a seat. These scripts are the only way the repository touches
it. Everything long-running is a transient user unit on the bench (`systemd-run
--user`), so a closed ssh session never kills a build (rule R19 holds on the
bench too: every compositor test runs under `runtime-test`).

| Script | Runs on | What |
|---|---|---|
| `sync.sh` | here | rsync this worktree to `bench:~/views-shell-wt/<branch>/` and print the remote path |
| `job.sh start <name> <script> [args]` | here | start `<script>` (a path on the bench) as unit `vs-<name>`, logging to `~/views-bench/logs/<name>.log` |
| `job.sh wait <name> [seconds]` | here | poll until the unit stops (default 1200 s); exit with the job's rc, 124 on timeout |
| `job.sh status <name>` | here | print `rc <n>` (the recorded rc) and exit with it; 125 while running or unknown |
| `job.sh log <name> [lines]` | here | tail the job log |
| `worker/run.sh <name> <cmd...>` | bench | the unit body: runs `<cmd>` inside the FHS build environment, records `~/views-bench/results/<name>.rc` |
| `worker/fhs.sh <cmd...>` | bench | run one command inside the FHS build environment (depot_tools on PATH, ccache) |
| `worker/build.sh <out> <target...>` | bench | `autoninja -C out/<out> <targets>` in the Chromium checkout |
| `worker/wire.sh <worktree>` | bench | pristine tree, copy `shell/` to `src/views_shell/`, apply `shell/patches/series` with plain `git apply`, print `WIRE-OK` |
| `worker/headless.sh <outdir> <cmd...>` | bench | under `runtime-test`: headless scroll (pixman), run `<cmd>` with `WAYLAND_DEBUG=1`, capture the tree, a screenshot and the log, then stop everything |
| `worker/measure.sh <label> <seconds> <pid> <results.tsv>` | bench | PSS, RSS, CPU, context switches, threads and fds of a process tree, one row |
| `worker/runtime-test-gpu` | bench | a copy of the bench's `runtime-test` with one added flag, `--allow-dri` (binds `/dev/dri`, nothing else); installed at `~/views-bench/bin/runtime-test-gpu`, never in place of `~/.local/bin/runtime-test` |
| `worker/ensure-out.sh <out> <args-file>` | bench | bring `out/<out>` to `<args-file>` idempotently (a `gn clean` when the args differ or siso built it last), `gn gen`, print `ENSURE-OK` |
| `worker/census.sh <out> <label> <binary> <report>` | bench | size (as linked and stripped), `ldd`, `runtime_deps` and `gn desc deps --all` counts of one target |
| `seq/<item>.sh` | bench | one work item's whole bench sequence (wire, build, runs), run as a unit under the bench lock |
| `lock.sh hold\|release\|status` | here | hold `~/views-bench/bench.lock` from a detached ssh session across many separate commands, end it, or report who holds it (read-only); see below |

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
for Chromium's default). `summary.txt` gains a `renderer:` line (the
compositor's renderer and the nodes visible in `/dev/dri`) and, with the FD
census, three lines per census:

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

## The bench lock

The bench is shared. Every wire, build and run sequence holds
`~/views-bench/bench.lock` (`flock -w <s> ~/views-bench/bench.lock ...`); a
`seq/<item>.sh` script runs as a transient unit under it. Do not re-run
`sync.sh` for a branch while its sequence runs: `rsync --delete` would replace
the script being read.

Bench layout (`~/views-bench/` on the host): `build-env` (a link to the FHS environment built from
`worker/flake.nix`), `scroll/` (stock scroll, the dotfiles pin), `logs/`,
`results/`, `marks/` (bootstrap markers). The Chromium checkout is
`~/chromium/src` at the tag in `shell/build/CHROMIUM_VERSION`, with `out/views`
(component, development) and `out/release` (non-component, for footprint).

The bench host name is `worker` (an ssh alias). Override with `BENCH_HOST`.

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
another host or file, which is how it is tested without the bench.

## Re-running proofs: `tools/prove.sh` and `tools/prove-lint.py`

`PROVE.md` is one append-only table (`id | claim | task | form | command | rc |
result | evidence | commit | at`). `python3 tools/prove-lint.py` checks every
row (ten cells, `P<n>.<m>` beside `C<n>.<m>`, the task, form, rc ⇔ result,
an existing commit, an ISO-8601 UTC `at`, no `/home/<user>`, and a runnable
command cell) and prints `prove-lint ok (N rows)` or each offending row.
`tools/prove.sh` re-runs rows from the repository root:

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
