# `chrome.storage.sync` and the views-shell extension

Status: probe and procedure written; the live two-seat run is Tom's (procedure
**H1** below, open question Q1 in [`../SPEC.md`](../SPEC.md)). The probe page is
[`../extension/sync-probe.html`](../extension/sync-probe.html), reachable from
the options page nav ("Storage sync probe"); it needs no views-shell process,
only the extension's `storage` permission.

The user configuration ([`../schemas/config.schema.json`](../schemas/config.schema.json))
is sized for one `chrome.storage.sync` item: 8,192 bytes serialised, inside a
102,400-byte area of at most 512 items. `tools/validate.py` checks every
example against that per-item budget.

## What is documented

From Chrome's own documentation of `chrome.storage.sync` and extension ids:

- **Sync data belongs to the extension id.** Two installs share storage only if
  they have the same id. An unpacked extension normally derives its id from its
  directory path, so the id changes when the directory moves — unless a `key`
  is pinned in `manifest.json`. `../extension/manifest.json` pins the `key`, so
  an unpacked checkout of `extension/` has a stable id on every machine
  (see [`naming.md`](naming.md) and `../extension/README.md`).
- **Sync requires a signed-in profile with sync on.** `chrome.storage.sync`
  only syncs when the Chrome profile is signed in, Chrome sync is enabled, and
  the Extensions sync option is on. Without sign-in the API still works but
  behaves as local storage: nothing is lost, nothing travels.
- **Quotas** (constants the probe page prints): `QUOTA_BYTES` 102,400 total,
  `QUOTA_BYTES_PER_ITEM` 8,192, `MAX_ITEMS` 512,
  `MAX_WRITE_OPERATIONS_PER_MINUTE` 120, `MAX_WRITE_OPERATIONS_PER_HOUR` 1,800.
- **Change events**: a write that syncs down arrives through
  `chrome.storage.onChanged` with `areaName` `"sync"` on the other profile.

## What is unverified

**Unverified:** that Chrome syncs an unpacked extension's
`chrome.storage.sync` data *at all*. Everything above says an unpacked
extension with a pinned `key` and a signed-in profile *should* sync — the API
is available to it, its id is stable, and nothing in the documentation limits
storage sync to Web Store installs — but this repository has not observed it,
and the store's own materials mostly discuss installed extensions. Until H1
runs, Q1 stands at its default: assumed yes. If H1 shows unpacked extensions
do not sync, the configuration falls back to `chrome.storage.local` plus the
dotfiles repository, and this document records that instead.

## H1: the two-seat procedure (human: Tom)

Settles Q1. Both seats stay in niri; this is ordinary signed-in Chrome, no
binary from this chapter touches a seat.

Preconditions, both seats:

1. Developer mode in `chrome://extensions`; "Load unpacked" the same checkout
   of `extension/` on both seats (same repository, same pinned `key`).
2. Confirm both seats show the **same extension id** on `chrome://extensions`.
3. Both profiles signed in to the same Google account, Chrome sync on,
   Extensions sync on (record the toggle state, not the account address).
4. Open `sync-probe.html` (options page → "Storage sync probe") on both seats.

Run:

5. Seat A: click **Write probe record + default config**. Note the `probe.at`
   timestamp the status line shows, and the `getBytesInUse(null)` value.
6. Seat B: watch the `onChanged` event log. Do not refresh; the page listens
   from load. Wait up to 5 minutes.
7. Seat B: if an event with area `sync` arrived, click **Read back** and
   compare `probe.at` with seat A's.
8. Seat B: click **Clear sync area**; confirm the clear echoes back to seat A's
   event log (deletions sync too).

Fields to record in the result table below:

| Field | Seat A | Seat B |
|---|---|---|
| Date (ISO) | | |
| Chrome version and channel | | |
| Extension id | | |
| Signed in / sync on / Extensions sync on | | |
| `probe.at` written (A) / observed (B) | | |
| First `onChanged` area + latency after the write | | |
| `getBytesInUse(null)` after the write | | |
| Clear echoed back to A? | | |
| Verdict (synced / not synced / degraded) | | |

## Result

Pending — **H1** has not been run. Record the table above here, then close Q1
in the spec's open questions with the verdict.
