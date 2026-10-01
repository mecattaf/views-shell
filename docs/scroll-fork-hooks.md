# Upstream wishes for stock scroll

> **Status: asks, not code.** views-shell runs on stock, unpatched scroll (Tom,
> 2026-10-01: "start with no-patch scroll wm"). This repository carries no scroll
> patch, and no row of the adapter capability table depends on one. What follows is
> the short list of IPC additions that would make the scroll adapter better, each
> one shaped to be sent to dawsers as a single pull request and, until it is
> merged, degraded around.
>
> **Invariant, checked by the docs gate:** views-shell carries no scroll patch and
> runs against the stock scroll already installed on the bench. The filename of
> this page is kept only so the chapter's links resolve.

views-shell works on stock scroll, sway, niri and Hyprland through adapters. Nothing
in the core needs anything from scroll's maintainer: every ask below is a capability
the scroll adapter probes for, and a plugin entry that needs a missing capability is
simply not registered (rule R14).

## Ground rules for an ask

Each ask is:

- **Listener-guarded.** No behaviour changes for a client that does not subscribe to
  the event or send the request. `swaymsg`, `scrollmsg` and every existing sway
  client keep working unchanged, and Tom's scroll config keeps working.
- **On the existing i3 socket.** A new request type above 124, a new event bit above
  31, or a new `change` value on an existing event. No private Wayland global:
  per-toplevel thumbnails are already stock
  (`ext_foreign_toplevel_image_capture_source_manager_v1`).
- **Small, at a stable call site.** Each one is tens of lines in one file, listed
  below with its site, so a rebase after an upstream refactor is obvious.
- **Probed, never assumed.** Each ask, once merged, is reported in a `GET_VERSION`
  `features` array (ask H10), and the adapter declares the capability only when the
  probe reports it. Until then the capability is "no" in
  [`compositor-adapters.md`](compositor-adapters.md).
- **Degradeable.** Each ask names what views-shell does without it. A declined ask
  changes nothing here: scroll stays stock, and the degraded path is the shipping
  path.

## The asks

| Id | Ask | Shell gains | Size and site | Upstream chance | Degrades to |
|---|---|---|---|---|---|
| H1 | `scroller` change `jump` with `phase: begin\|end` and the label table | The core's only Lua hook (`jump_end`) goes; labels drawable in Views | ~60 lines, `layout.c` at the lines that set and clear `root->jumping` | good | capability `scroll.jump` only |
| H2 | `scroller` change `overview_workspaces` with per-output workspace rects | Rail and bar react to `scale_workspaces` | ~40 lines | good | niri: open/closed without geometry |
| H3 | One coalesced geometry event per applied transaction | Rail, minimap and Alt+Tab never poll `GET_TREE` | ~50 lines in the transaction apply loop | plausible | niri `WindowLayoutsChanged`; sway polls |
| H4 | Binding events for gestures and switches | `bindgesture … nop views-shell …` costs nothing; no process per swipe | ~25 lines, `ipc-json.c` default branch | good (or via sway) | `exec views-shell call` |
| H5 | ext-workspace ids and `REMOVE` | The generic adapter becomes first-class on scroll | ~20 lines | good (sway is the better home) | name-only join |
| H10 | `GET_VERSION` `features` array | The capability probe itself | ~15 lines | good | `variant` only |
| H11 | `window` change `app_id` | Late-set app ids (Chrome PWAs, Xwayland) fix the rail icon | ~3 lines | good | the next unrelated event |

Order of asking, most useful first: H4, H11, H1, H2, H10, H5, H3. Events go first,
because they are the shape sway already has and the cheapest to review.

Findings that need no ask: workspace reorder is `workspace swap` (stock); thumbnails
are stock per-toplevel capture; a theme event is not needed because views-shell
renders `scroll.conf` from the theme itself. Until a `jump` end event exists (H1), the
core uses one minimal scroll Lua callback for that single signal (rule R13).

## Deliberately not asked for

Three families of additions were considered and dropped, because they ask scroll to
carry desktop-shell policy rather than to report state, and views-shell can live
without each:

- **A synchronous placement hook** (the shell deciding where a window lands before
  its first frame). It blocks every map on a client reply with a deadline; the
  degraded path is a visible late move, which is what every other compositor does.
- **Per-window border colour and dynamic `for_window` matchers.** niri and Hyprland
  have them natively; on scroll, borders come from `client.*` and the corner radius
  from `default_decoration border_radius`. MacTahoe's prompt border is the one look
  scroll cannot express, and views-shell does not own other programs' decorations.
- **Background blur behind chosen surfaces.** Declined upstream twice already, and it
  needs a blur-capable renderer in scroll's vendored wlroots. MacTahoe's translucent
  GTK4 surfaces draw unfrosted over whatever is behind them; open decision P21 covers
  living with that or using MacTahoe's solid variant.

## Testing an ask

Nothing here needs a seat. If dawsers takes an ask, the scroll adapter gains one
recorded-transcript fixture under `shell/wm/adapters/scroll/testdata/`, and the
headless run gate (`tools/bench/worker/headless.sh`, under `runtime-test` on the
bench) exercises it against a nested stock scroll. A merged ask is a version bump of
the scroll pin in the dotfiles; this repository carries no scroll source.
