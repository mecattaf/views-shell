# views_shell --bar with and without a render node (Chromium 154.0.8037.92)

Measured on the bench on 2026-10-02 (01:14 to 01:19 CEST) by item w1c for open
decision P23 (a GPU-fair footprint measurement). One build: `out/views`
restored to `tools/bench/args.views.gn` (component build) and rebuilt from
origin/main by `tools/bench/seq/w1c.sh` under the bench lock. Every run is
`tools/bench/worker/headless.sh` (headless stock scroll, one 1920x1080 output,
FD census on).

## The wrapper

`runtime-test` gives the test an otherwise empty `/dev` (finding F2), so the
compositor can only use pixman and the client only software GL.
`tools/bench/worker/runtime-test-gpu` is the bench's own `runtime-test` with one
added flag, `--allow-dri`, which binds `/dev/dri` and nothing else. It is
installed on the bench only, at `~/views-bench/bin/runtime-test-gpu`; the seats'
and the bench's `~/.local/bin/runtime-test` are untouched. `headless.sh` picks it
with `BENCH_RUNTIME_TEST`, `BENCH_RUNTIME_TEST_ARGS=--allow-dri`,
`BENCH_WLR_RENDERER=gles2` and `WLR_RENDER_DRM_DEVICE=/dev/dri/renderD128`.
Inside it, `/dev/dri` lists `by-path card1 renderD128`.

The bench's GPU also serves another workload. Before each run the sequence read
`gpu_busy_percent` and the VRAM in use: `busy=0% vram_used=301 MB` every time.
Nothing was stopped, restarted or reserved.

## Runs

| Run | Compositor | Client GL | First attach | Attaches | Colours | Buffers | Render-node FDs |
|---|---|---|---|---|---|---|---|
| `w1c-fd` (chapter-1 baseline) | pixman | SwiftShader (`--use-gl=angle --use-angle=swiftshader`) | 5771 ms | 1 | 3 | 2 `wl_shm.create_pool` | 0 |
| `w1c-gles2-ss` (the fair row) | gles2 on renderD128 | SwiftShader | 2155 ms | 1 | 3 | 2 `wl_shm.create_pool` | 4 |
| `w1c-gpu` | gles2 on renderD128 | default (no GL switch) | 2134 ms | 1 | 3 | 1 `zwp_linux_dmabuf_v1.create_params`, 9 `/dmabuf:` FDs, 1 `sync_file` | 5 |
| `w1c-gpu-examples` (control) | gles2 on renderD128 | default | 27210 ms | 0 | 1 | 3 `zwp_linux_dmabuf_v1.create_params` | 5 |

Measure rows (`measure.sh`, 10 s window, the `build-env` tree, `procs=2`):

```
w1c-fd            client	dur=10	procs=2	threads=30	cpu%=0.00	PSS_MB=125.2	RSS_MB=135.3	ctxsw/s=0.0	FDs=345
w1c-gles2-ss      client	dur=10	procs=2	threads=37	cpu%=0.00	PSS_MB=152.9	RSS_MB=183.3	ctxsw/s=0.0	FDs=349
w1c-gpu           client	dur=10	procs=2	threads=45	cpu%=0.00	PSS_MB=158.7	RSS_MB=197.4	ctxsw/s=0.0	FDs=358
w1c-gpu-examples  client	dur=10	procs=2	threads=44	cpu%=16.70	PSS_MB=181.8	RSS_MB=221.4	ctxsw/s=204.5	FDs=40
```

FD census (`fd kinds` / `fd other`):

```
w1c-gles2-ss  fd kinds: socket=2 anon_inode:[eventfd]=5 pipe=2 memfd=3 /dev/shm=0 file=329 other=8
              fd other: /dev/dri/renderDN=4 /dev/null=2 anon_inode:[signalfd]=1 anon_inode:[eventpoll]=1
w1c-gpu       fd kinds: socket=2 anon_inode:[eventfd]=6 pipe=2 memfd=0 /dev/shm=0 file=338 other=10
              fd other: /dev/dri/renderDN=5 /dev/null=2 anon_inode:[signalfd]=1 anon_inode:[eventpoll]=1 anon_inode:sync_file=1
```

All three `views_shell` runs drew the same bar: the screenshots are
byte-identical (6970 bytes; `views-shell-154-gpu-bar.png` is the `w1c-gpu`
capture and matches `views-shell-154-bar.png`). With the default client GL the
frame reaches the compositor as a linux-dmabuf buffer instead of `wl_shm`, so
the hardware path works end to end inside the wrapper: no `--use-gl` switch is
needed once `/dev/dri` is present. Finding F2's two switches are a consequence of
the empty `/dev`, not of the shell.

Client GL initialisation lines: none. At the default log level `client.log`
holds only the `WAYLAND_DEBUG` trace and two lines of the FHS wrapper
(`depot_tools`, `ccache`); the sequence's `gl.txt` per run is empty of GL lines.
The GL path is read from the protocol (`wl_shm` against
`zwp_linux_dmabuf_v1.create_params`) and from the FD census (render-node,
`/dmabuf:` and `sync_file` descriptors). A rerun with `--enable-logging=stderr
--v=1` would name the ANGLE back end and the driver.

The control draws nothing: `views_examples` here is the stock target (the
bench's earlier F2 one-line edit is gone after the pristine wire), so it
creates its buffers but never attaches one (finding F2), on pixman and on gles2
alike. Its row is a cost control, not a drawing control.

## Reading

- PSS grows with the render node: 125.2 MB on the baseline, 152.9 MB with only
  the compositor on the GPU (the SwiftShader client still opens renderD128
  four times once the node exists; this census does not attribute the 27 MB),
  158.7 MB with the client on hardware GL. On this component build the GPU path
  costs memory rather than saving it. Threads go 30, 37, 45.
- Idle CPU stays at 0.00 % in every `views_shell` row; nothing animates.
- First attach falls from 5771 ms to about 2150 ms with the gles2 compositor,
  whatever the client uses. The baseline's 5771 ms came first in the sequence,
  right after the rebuild (a cold page cache for 270 libraries), so it is not a
  clean renderer comparison.
- The fair rows for a footprint decision are `w1c-gles2-ss` (same client as
  chapter 1, real compositor renderer) and `w1c-gpu` (the configuration a seat
  runs). The numbers stay indicative: a component build, a headless output, and
  PSS that counts every shared library. The `out/release` footprint build is the
  next measurement.
