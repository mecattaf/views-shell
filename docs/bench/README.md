# Bench records

Every run the bench produces is written up here, with the screenshot that proves
it. All runs are headless stock scroll under `runtime-test` on the bench
(`tools/bench/worker/headless.sh`), Chromium `154.0.8037.92`, one 1920x1080
output. Unless a row says otherwise the compositor renders with pixman and the
client passes `--use-gl=angle --use-angle=swiftshader` (finding F2), so the
footprint numbers are indicative; the GPU-fair rows (`runtime-test-gpu
--allow-dri`, gles2 on `renderD128`) are the exception. Every row is N=1. A
drawn frame is `attaches: N` > 0 and `colours: N` >= 2 in the run's
`summary.txt` plus the screenshot.

## Records

| Record | Chapter, item | What it proves (claims) |
|---|---|---|
| [`views-examples-154.md`](views-examples-154.md) | 1, T2 | Chromium at the tag with `views_examples` built content-free (C2.1–C2.5); findings F1 (V8 through the Automation bindings) and F2 (stock `views_examples` never attaches: the in-process test sink mints its own `LocalSurfaceId`s; `--use-gl=angle --use-angle=swiftshader` under `runtime-test`); the first footprint row |
| [`views-shell-154.md`](views-shell-154.md) | 1, T3 and T4, FD census by w1c | `views_shell` builds with no `//v8`, `//content`, Blink renderer or `//chrome` path (C3.3) and draws beside `views_examples` (C3.4, C3.5); finding F3 (the two Blink edges cut); the layer surface and the popup (C4.1–C4.3); the FD census that explains chapter 1's 344 descriptors (C11.1, finding F8) |
| [`views-shell-154-production.md`](views-shell-154-production.md) | 2, w1a | debt D1 paid: the production in-process viz host acks its configures, software and GPU compositing both draw, software is the default (C9.1–C9.4, findings F4–F7); 107.1 MB / 15 threads / 352 FDs software, 129.0 MB / 33 / 364 GPU |
| [`views-shell-154-release.md`](views-shell-154-release.md) | 2, w1b | the non-component `out/release` build, its size (63.3 MB stripped), the dependency census (6,417 targets; WebRTC and XNNPACK through viz/service), `runtime_deps`, and the release footprint beside the component build and the `views_examples` control (C10.1–C10.5, finding F9) |
| [`views-shell-154-gpu.md`](views-shell-154-gpu.md) | 2, w1c | the four GPU-fair rows for open decision P23: pixman + SwiftShader, gles2 + SwiftShader, gles2 + default client GL over linux-dmabuf, and the `views_examples` control (C11.2–C11.4, finding F10) |
| [`views-shell-154-theme.md`](views-shell-154-theme.md) | 2, w2b | the style kit and the bar with its clock wear an Omarchy theme directory: six runs, the bar's ground is the theme's `background` (C15.1–C15.5); rule R1's exit code; stock menus pick up the pins |
| [`views-shell-154-adapter.md`](views-shell-154-adapter.md) | 2, w2a | the scroll adapter, `WmModel` and `wm_probe` on stock scroll 1.13-dev: the recorded 97-frame transcript, the switch and its echo, the capability set, the barrier facts (C14.1–C14.3, finding F11) |
| [`views-shell-154-assembled.md`](views-shell-154-assembled.md) | 2, w3c | the assembled program: the workspace strip follows the model's echo, a `notify-send` becomes an overlay layer surface, `wtype` types into a `Textfield` on an exclusive overlay surface, orderly exit rc 0, and the footprint of the whole program in component, GPU-fair and release builds (C21.1–C21.5, findings F13 and F14) |
| [`verification-chapter2.md`](verification-chapter2.md) | 2, fable-verify | the adversarial re-run of chapter 2's claims from main on the bench: 73 fresh PROVE rows, three overclaims refuted, the superseded numbers dated |

The chapter-1 record of the tab strip (`--left-tabs`, PRs #18 and #19) lives in
[`../tabs.md`](../tabs.md), with its two screenshots below.

## Screenshots

| PNG | Record | What it shows |
|---|---|---|
| [`views-examples-154-headless-scroll.png`](views-examples-154-headless-scroll.png) | `views-examples-154.md` | the full `views_examples` window drawn once the F2 test config is applied (44,521 bytes against 6,121 blank) |
| [`views-shell-154-headless-scroll.png`](views-shell-154-headless-scroll.png) | `views-shell-154.md` | chapter 1's `views_shell` label in an `xdg_toplevel` (the window mode, since superseded by rule R1's exit) |
| [`views-shell-154-bar.png`](views-shell-154-bar.png) | `views-shell-154.md` (T4, recorded by w1c) | the 32 px top layer surface with the exclusive zone (C4.2) |
| [`views-shell-154-popup.png`](views-shell-154-popup.png) | `views-shell-154.md` (T4, recorded by w1c) | the menu as an `xdg_popup` parented to the bar through `zwlr_layer_surface_v1.get_popup` (C4.3) |
| [`views-shell-154-production-bar.png`](views-shell-154-production-bar.png) | `views-shell-154-production.md` | the bar drawn by the production viz host in software compositing (C9.2) |
| [`views-shell-154-production-popup.png`](views-shell-154-production-popup.png) | `views-shell-154-production.md` | the popup with its rounded corners and shadow by the software renderer (C9.3) |
| [`views-shell-154-release-bar.png`](views-shell-154-release-bar.png) | `views-shell-154-release.md` | the release build's bar (C10.3) |
| [`views-shell-154-gpu-bar.png`](views-shell-154-gpu-bar.png) | `views-shell-154-gpu.md` | the same bar drawn with the default client GL over linux-dmabuf on a gles2 compositor; byte-identical to the pixman capture (C11.3) |
| [`views-shell-154-theme-claude-dark.png`](views-shell-154-theme-claude-dark.png) | `views-shell-154-theme.md` | the bar in `claude-dark`: `#1a1a1a` ground, the clock in `#eaecf0` (C15.2) |
| [`views-shell-154-theme-claude-light-popup.png`](views-shell-154-theme-claude-light-popup.png) | `views-shell-154-theme.md` | a light theme with the menu drawn from the same pins (C15.3) |
| [`views-shell-154-assembled-switch.png`](views-shell-154-assembled-switch.png) | `views-shell-154-assembled.md` | workspace 3 focused in the bar's strip after the echo, the notification top right (C21.1) |
| [`views-shell-154-assembled-notify.png`](views-shell-154-assembled-notify.png) | `views-shell-154-assembled.md` | the notification popup on the overlay layer (C21.2) |
| [`views-shell-154-assembled-keyboard.png`](views-shell-154-assembled-keyboard.png) | `views-shell-154-assembled.md` | the keyboard probe centred on the overlay layer with `hello` typed (C21.3) |
| [`tabstrip-154-headless-scroll.png`](tabstrip-154-headless-scroll.png) | `../tabs.md` | `--left-tabs` from a static workspace list (PR #18) |
| [`tabstrip-154-headless-scroll-niri.png`](tabstrip-154-headless-scroll-niri.png) | `../tabs.md` | `--left-tabs` fed by `niri msg -j workspaces` (PR #19) |
| [`left-tabs-154-all-black.png`](left-tabs-154-all-black.png) | `../tabs.md` | the ported strip under `--theme examples/themes/all-black`, static workspaces (C25.3) |
| [`left-tabs-154-all-black-switch.png`](left-tabs-154-all-black-switch.png) | `../tabs.md` | the 220 px rail on scroll after `--demo-workspace-switch`: workspace 3 from the echo (C25.1) |

## The footprint across the chapter

The same `--bar` program, measured by `measure.sh` over 10 s after the client
settled (PSS from `smaps_rollup`, the client's process tree including the FHS
`bwrap`):

| Row | Build | Context factory | Compositor / client GL | PSS MB | CPU % | Threads | FDs | Record |
|---|---|---|---|---|---|---|---|---|
| chapter 1 `--bar` | component | test | pixman / SwiftShader | 141 | 0 | 30 | 344 | `views-shell-154.md` |
| w1a `--bar` | component | production, software | pixman / GL disabled | 107.1 | 0 | 15 | 352 | `views-shell-154-production.md` |
| w1a `--bar --gpu-compositing` | component | production, GPU | pixman / SwiftShader Vulkan | 129.0 | 0 | 33 | 364 | `views-shell-154-production.md` |
| w1b `--bar` | release | test | pixman / SwiftShader | 81.1 | 0 | 30 | 55 | `views-shell-154-release.md` |
| w1c `--bar` | component | test | gles2 / default GL (dmabuf) | 158.7 | 0 | 45 | 358 | `views-shell-154-gpu.md` |
| w2b `--bar --theme claude-dark` | component | production, software | pixman | 108.8 | 0 to 0.1 | 15 | 353 | `views-shell-154-theme.md` |
| w3c assembled `--bar` | component | production, software | pixman | 108.7 | 0.00 | 17 | 362 | `views-shell-154-assembled.md` |
| w3c assembled `--bar`, GPU-fair | component | production, software | gles2 / default GL | 128.4 | 0.00 | 16 | 360 | `views-shell-154-assembled.md` |
| w3c assembled `--bar` | release | production, software | pixman | 59.7 | 0.00 | 17 | 69 | `views-shell-154-assembled.md` |

The release `views_examples` control (never draws, finding F2) is 111.8 MB at
4.6 % CPU, 61 threads and 200 context switches a second.

## Writing a record

One file per item, named `views-shell-<tag>-<item or subject>.md`: the harness
and flags, what runs, the build (`WIRE-OK`, `ENSURE-OK`, the gn checks), the
unit tests with their filter, a table of runs with first attach, attaches,
colours, protocol facts, PSS, threads and FDs, the findings, and the caveat.
Copy the screenshot that proves a claim here as a small PNG and name it in the
record and in the table above.
