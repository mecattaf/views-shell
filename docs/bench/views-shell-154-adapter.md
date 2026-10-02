# The scroll adapter and wm_probe on headless scroll (Chromium 154.0.8037.92)

Measured on the bench on 2026-10-02 by item w2a (chapter 2, the open chapter-1
T5). One build: `out/views` (component, `tools/bench/args.views.gn`), wired from
`w/w2a` and built by `tools/bench/seq/w2a.sh` under the bench lock. Every run is
`tools/bench/worker/headless.sh`: headless stock scroll 1.13-dev under
runtime-test, one 1920x1080 output, `SCROLLSOCK` exported by the harness.
`wm_probe` is a console program, so the harness never sees a Wayland attach:
it waits out its attach timeout (about 15 s), captures, and reports `client
alive at capture: 0`. The evidence is the client log, the `PROBE-RC` line the
sequence echoes, and the harness's `tree.json` (`scrollmsg -t get_tree` after
the client).

## Build

`gn check out/views '//views_shell/wm/*'` is clean. The seam adds three targets
reachable from `//views_shell`: `//views_shell/wm:wm` (linked into
`views_shell`), `//views_shell/wm:wm_probe` and `//views_shell/wm:unittests`
(collected into `views_shell_unittests`). `:wm` and `:wm_probe` assert no
dependency on `//ui/views`, `//content`, `//chrome`, `//v8` or Blink's renderer:
the adapter needs `//base` and `gfx::Rect` only.

## Unit tests

`out/views/views_shell_unittests` prints `SUCCESS: all tests passed.`; with
`--gtest_filter='Scroll*:WmModel*'` 18 tests are OK:

| Suite | Tests |
|---|---|
| `ScrollIpcClientTest` (scripted server, real socket) | ReassemblesSplitAndCoalescedEventFrames, AnswersPipelinedRequestsInOrder, AnswersPendingRequestsWhenTheServerCloses, ReportsAMissingSocketAsDisconnected, ResolvesTheSocketInScrollOrder |
| `ScrollAdapterTest` (fake compositor replaying the transcript) | SubscribesFirstThenPublishesTheRecordedState, EveryCommandLandsAfterItsEcho, ReconnectsMidSnapshotAndResyncs, FailsWithNoEchoWhenTheTickNeverComes, DispatchesOnlyViewsShellBindings, RefusesWhatItCannotSpell |
| `ScrollAdapterStaticTest` | ProbesCapabilitiesFromGetVersion, SpellsCommandsAndQuotesNames |
| `WmModelTest` (hand-written snapshots) | FirstSnapshotAddsEverythingInOrder, DiffsRemovalsMovesAndAdditions, MirrorFollowsEverySequenceOfSnapshots, ReportsItemFocusOutputAndMruChanges, RequestsChangeNothingUntilTheEcho |

The fake compositor replays `shell/wm/adapters/scroll/testdata/scroll-transcript.jsonl`
by epoch and checks every `RUN_COMMAND` string byte for byte against the
recording. Each replayed event is written in two halves, so every adapter test
also exercises the frame decoder.

## The transcript

Recorded by `testdata/record_transcript.py` as the client of `headless.sh`
(stage `record`), with views_shell (no flags) as the window: 97 frames, the
window id 5 on workspace 1 (id 4). The seven command epochs all reached their
tick; `scratchpad show` was refused with `Scratchpad is empty`; `exit` produced
no reply, the `shutdown` event (`change: exit`) and the end of the stream:

```
RECORD window: 5 app_id views-shell
RECORD epoch 1: 'workspace --no-auto-back-and-forth "3"' -> [{"success": true}] (tick)
RECORD epoch 2: 'rename workspace "3" to "web"' -> [{"success": true}] (tick)
RECORD epoch 3: '[con_id=5] move container to workspace "web"' -> [{"success": true}] (tick)
RECORD epoch 4: '[con_id=5] focus' -> [{"success": true}] (tick)
RECORD epoch 5: 'scratchpad show' -> [{"success": false, "parse_error": true, "error": "Scratchpad is empty"}] (tick)
RECORD epoch 6: 'workspace --no-auto-back-and-forth "1"' -> [{"success": true}] (tick)
RECORD epoch 7: 'reload' -> [{"success": true}] (tick)
RECORD exit: eof
RECORD-DONE 97 frames
```

Observed on stock scroll, and relied on by the adapter: the `SUBSCRIBE` reply is
followed at once by a `tick` with `first: true`; a command's events come before
its `SEND_TICK`'s `tick` on the events connection; `reload`'s `workspace` event
(`change: reload`, `current: null`) also comes before that tick; `GET_VERSION`
carries `"variant": "scroll"` and no `features` array; scroll's tree puts each
window in a column container (`type: con`, `layout: vertical`) under the
workspace.

## Probe runs

| Run | Client command | `PROBE-RC` | What the log shows |
|---|---|---|---|
| `w2a-dump` | `wm_probe --dump` | 0 | `CONNECTED scroll 1.13-dev`, then one JSON object with `capabilities`, `compositor`, `mru` (empty), `outputs` (HEADLESS-1, 1920x1080, scale 1.0, focused), `version`, `windows` (empty), `workspaces` (id 4, name 1, focused, active) |
| `w2a-dump-window` | views_shell (no flags), then `wm_probe --dump` | 0 | the same with `windows: [{id 5, app_id views-shell, title views-shell, workspace 4, column 0, focused}]` and `mru: ["5"]` |
| `w2a-switch` | `wm_probe --switch 3 --timeout-seconds 20` | 0 | `SWITCH workspace 3 (by name)`, `ECHO workspace 3`; `tree.json` holds `"name": "3"` |
| `w2a-timeout` | `SCROLLSOCK=/nonexistent wm_probe --switch 3 --timeout-seconds 3` | 1 | four connect attempts 250, 500 and 1000 ms apart (the backoff), then `TIMEOUT after 3 s` |
| `w2a-watch` | views_shell, then `wm_probe --watch 7` while two other probes switch to 3 and back to 1 | 0 | `ADDED parent=- id=7 index=1`, `CHANGED id=4`, `CHANGED id=5`, `FOCUS workspace=7 window=-`, then on the way back `REMOVED parent=- id=7`, `FOCUS workspace=4 window=5`; both switches print their `ECHO` |

The capability set the probe reported on headless stock scroll (22, sorted):

```
bindings.events bindings.list config.include-slot config.reload outputs.list
overview.toggle scratchpad.toggle scroll.jump scroll.lua scroll.overview
scroll.scroller scroll.spaces scroll.trails session.exit windows.focus
windows.fullscreen-state windows.list windows.move-to-workspace windows.urgency
workspaces.focus workspaces.list workspaces.rename
```

No hook row (`overview.events`, `windows.geometry-events`,
`bindings.gesture-events`): stock scroll has no `features` array (hook H10).

## Reproduce

```
R=$(tools/bench/sync.sh)
# then, from the worktree, the transient unit of tools/bench/README.md with
#   flock -w 14400 ~/views-bench/bench.lock bash $R/tools/bench/seq/w2a.sh
tools/bench/job.sh wait w2a-seq 14400
tools/bench/job.sh log w2a-seq 300
```

`w2a.sh` takes stage names (`wire build record tests probes`) to run a subset.
The `record` stage writes a fresh transcript to
`~/views-bench/results/w2a-record/transcript.jsonl`; node ids and pids may
differ between recordings, and the tests take ids from the snapshots.
