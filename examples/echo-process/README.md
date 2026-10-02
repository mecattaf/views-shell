# Echo (protocol reference): `example.echo`

The reference T2 process plugin. `plugin.py` is python3 with the standard library
only, and it exists to exercise every row of
[`../../schemas/plugin-protocol.md`](../../schemas/plugin-protocol.md), including
the rows settled by the conformance runner. The C++ plugin host
([`../../shell/plugins/`](../../shell/plugins/README.md)) runs it as its first
subject: `ProcessPluginTest.EchoReferencePluginSession` and `PluginHostTest`
drive a whole session against it on the bench, and
`plugin_probe --plugin <this directory> --invoke ping --args '{"text":"hi"}'` is
the host's run gate. `tools/plugin-conformance.py` checks it in
`tools/validate.sh`.

| Part | What it does |
|---|---|
| `button` (bar widget) | A button whose label binds `/counter`; its action is `set-counter` with `value` bound to `/next`, so a click adds one. |
| `panel` | The greeting (`/greeting`), the counter, the last event's name (`/lastEvent/name`), and Reset and Add one buttons. |
| snapshot | `{counter, next, greeting, lastEvent: {name, data}}`, always whole, re-sent after every change. |
| `ping` | Echoes its args as a `json` result and sends a `notify` request, which the manifest's `notifications` permission allows. |
| `set-counter` | Sets the counter (an integer, else `-32602`) and re-sends the snapshot. |
| `try-exec` | Asks the broker to run `true`. The manifest declares no `exec:` permission on purpose, so the broker answers `-32001` and the command's result carries that answer: `{program, answer: {error: {code: -32001, ...}}}`. The plugin stays alive. |
| `config/changed` | Applies `greeting` (default `hello`). |
| `event` | Subscribed to `workspace`; records `{name, data}` as `lastEvent`. |
| everything else | Unknown notifications are ignored, unknown requests answer `-32601`, malformed lines are dropped with a note on stderr, `shutdown` answers `{}` and exits 0 at once. |

Run the conformance session by hand from the repository root, with a python3 that
has `jsonschema`:

```sh
nix shell nixpkgs#python3Packages.jsonschema -c python3 tools/plugin-conformance.py examples/echo-process
```

It prints one line per check and `CONFORMANCE-OK example.echo`. The runner never
executes what a plugin asks the broker for; it answers declared requests with a
canned success and undeclared ones with `-32001`.
