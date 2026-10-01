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
