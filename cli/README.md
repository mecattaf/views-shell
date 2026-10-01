# cli

- [`SPEC.md`](SPEC.md): the `views-shell` command line, every verb.
- [`views-shell`](views-shell): a dispatch skeleton in Python. It is a placeholder for the real
  client, which will ship with the views-shell binary. Offline verbs work today:
  `views-shell plugin render`, `views-shell plugin validate`, `views-shell version`. Every other verb
  is forwarded as JSON-RPC to `$XDG_RUNTIME_DIR/views-shell/views-shell.sock`, and exits 69
  because no views-shell process exists yet.
- [`testdata/`](testdata/): golden slot files rendered from `examples/` for scroll,
  niri and Hyprland. `tools/validate.sh` re-renders and compares them.

```
cli/views-shell plugin render --compositor scroll --out /tmp/slots --plugin examples/power-menu
```
