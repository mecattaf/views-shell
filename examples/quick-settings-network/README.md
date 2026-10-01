# Network (quick settings): `views-shell.qs-network`

The first-party network tile and its drill-in page, written as a T1 plugin with no
code. It is exactly what a third party would write: the core has no private path
for it.

- **State** comes from the first-party `views-shell.network` source plugin
  ([`../network-source/`](../network-source/)), a built-in producer over
  NetworkManager that draws nothing. The entries bind to it with
  `"state": "views-shell.network/state"`.
- **Actions** are `views-shell.network/*` commands, declared with the
  `call:views-shell.network/*` permission, which is shown at enable and checked by
  `tools/validate.py`.
- **The tile** sits in the host's `tile` slot. It becomes one `ui::ActionItem`, so
  "Wi-Fi" is also a palette and launcher row with no extra declaration.
- **The page** is a `page` entry: the host pushes it onto its own stack and draws
  the title and back button.
- **Credentials** never pass through here. Activating a secured network makes
  NetworkManager ask its registered secret agent, which is the core credential
  modal (rule R25).
