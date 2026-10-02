# Recorded scroll i3-ipc transcript

`scroll-transcript.jsonl` is a recording of one headless stock scroll session
(1.13-dev, `WLR_BACKENDS=headless`, one 1920x1080 output) on the bench, made
by [`record_transcript.py`](record_transcript.py) as the client of
`tools/bench/worker/headless.sh` under runtime-test. `tools/bench/seq/w2a.sh`
(stage `record`) runs it; the result lands in
`~/views-bench/results/w2a-record/transcript.jsonl` and is copied here.

The session, in order:

1. A window client (views_shell with no flags: one xdg_toplevel) is started
   and waited for, so the tree holds one window from the start.
2. Events connection: `SUBSCRIBE ["workspace","window","output","binding",
   "shutdown","tick"]`, its reply and the `tick` with `first: true`.
3. Requests connection: `GET_VERSION`, then `GET_OUTPUTS`, `GET_WORKSPACES`,
   `GET_TREE`.
4. One epoch per command, each `RUN_COMMAND`, its reply, `SEND_TICK` with a
   unique payload and its reply, every event up to the matching `tick`, then
   `GET_OUTPUTS`, `GET_WORKSPACES`, `GET_TREE`:
   - `workspace --no-auto-back-and-forth "3"` (creates and focuses 3)
   - `rename workspace "3" to "web"`
   - `[con_id=N] move container to workspace "web"`
   - `[con_id=N] focus`
   - `scratchpad show` (refused: `Scratchpad is empty`)
   - `workspace --no-auto-back-and-forth "1"`
   - `reload` (the `workspace` event with change `reload`)
5. `RUN_COMMAND exit`: no reply, the `shutdown` event with change `exit`, then
   the end of the stream.

The command strings are the ones `scroll_adapter.cc` spells, so the fake
compositor in `scroll_adapter_unittest.cc` checks the adapter's commands byte
for byte against this file.

## Format

JSON Lines, one protocol frame per line:

```json
{"dir":"c2s"|"s2c","conn":"requests"|"events","type":<uint32>,"payload":<json>}
```

- `type` is the i3-ipc message type (events carry the `0x80000000` bit).
- `payload` is the frame payload, JSON-decoded, with two exceptions that are
  faithful to the wire: `null` means an empty payload (the queries), and a
  `RUN_COMMAND` or `SEND_TICK` payload is a JSON string because on the wire it
  is raw text, not JSON.
- Every `/home/<user>/` prefix is written as `~/` by the recorder
  (`loaded_config_file_name` carries the scratch HOME of the run), so the file
  passes `tools/check-fences.sh`.

## Re-recording

Run `tools/bench/seq/w2a.sh record` on the bench under the bench lock (it needs
a built `out/views/views_shell` as the window), then copy
`~/views-bench/results/w2a-record/transcript.jsonl` over
`scroll-transcript.jsonl`. Node ids and pids may change between recordings;
the tests take ids from the replayed snapshots, never from constants.
