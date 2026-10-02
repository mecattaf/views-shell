# Network source: `views-shell.network`

A first-party built-in (T0) **source plugin**: a producer over NetworkManager that
draws nothing. It publishes one full snapshot (`contributes.sources`, id `state`)
and a handful of commands, and mounts `views-shell network …` on the CLI. The
quick-settings tile ([`../quick-settings-network/`](../quick-settings-network/)),
a bar pill and the CLI all consume it.

Disabling `views-shell.qs-network` removes the tile while the pill and the CLI keep
working. Disabling `views-shell.network` removes the whole domain: a feature is dropped
by not registering it (rule R10). The NetworkManager secret agent is not here; it
belongs to the core credential modal (rule R25).

The C++ target named in `runtime.target` does not exist yet.

## Tier, contributions, capabilities, permissions

- **Tier:** T0 built-in, target `//views_shell/producers/network:network` (not
  built yet). Built-in mode is reserved for `views-shell.*` ids.
- **Contributes:** the source `state` (published as `views-shell.network/state`),
  seven commands (`toggle-wifi`, `set-wifi-enabled`, `activate`, `deactivate`,
  `forget` with `confirm`, `open-editor`, `state` as `result: json`), and the CLI
  verbs `views-shell network wifi|connect|disconnect|forget|state`. No surfaces:
  it draws nothing.
- **Capabilities:** none. NetworkManager is a system daemon, not the compositor.
- **Permissions:** `dbus:system:org.freedesktop.NetworkManager` and
  `exec:nm-connection-editor` (the optional helper `open-editor` starts). They are
  declared even though a built-in runs in process, so the enable dialog and the
  Chrome plugins page show the same list for every tier.

## Registry fixture

[`tools/fixtures/registry/views-shell.network.json`](../../tools/fixtures/registry/views-shell.network.json):
activation is `onSource:views-shell.network/state` plus one `onCommand:` per
command, so the producer starts when a consumer binds its source or a command
is called, never at start-up for its own sake.
