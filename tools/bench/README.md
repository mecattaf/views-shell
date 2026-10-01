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
| `job.sh status <name>` | here | print the recorded rc and exit with it; 125 while running or unknown |
| `job.sh log <name> [lines]` | here | tail the job log |
| `worker/run.sh <name> <cmd...>` | bench | the unit body: runs `<cmd>` inside the FHS build environment, records `~/views-bench/results/<name>.rc` |
| `worker/fhs.sh <cmd...>` | bench | run one command inside the FHS build environment (depot_tools on PATH, ccache) |
| `worker/build.sh <out> <target...>` | bench | `autoninja -C out/<out> <targets>` in the Chromium checkout |
| `worker/wire.sh <worktree>` | bench | pristine tree, copy `shell/` to `src/views_shell/`, apply `shell/patches/series` with plain `git apply`, print `WIRE-OK` |
| `worker/headless.sh <outdir> <cmd...>` | bench | under `runtime-test`: headless scroll (pixman), run `<cmd>` with `WAYLAND_DEBUG=1`, capture the tree, a screenshot and the log, then stop everything |
| `worker/measure.sh <label> <seconds> <pid> <results.tsv>` | bench | PSS, RSS, CPU, context switches, threads and fds of a process tree, one row |

Bench layout (`~/views-bench/` on the host): `build-env` (a link to the FHS environment built from
`worker/flake.nix`), `scroll/` (stock scroll, the dotfiles pin), `logs/`,
`results/`, `marks/` (bootstrap markers). The Chromium checkout is
`~/chromium/src` at the tag in `shell/build/CHROMIUM_VERSION`, with `out/views`
(component, development) and `out/release` (non-component, for footprint).

The bench host name is `worker` (an ssh alias). Override with `BENCH_HOST`.
