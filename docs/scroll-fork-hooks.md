# A scroll fork with hooks (experimental)

> **Status: experimental scoping.** Nothing here is built, packaged or sent
> upstream. It records what a scroll-based fork of Tom's own, with typed hooks for
> the Views shell, would give views-shell. The full research is in
> the private scoping note `scroll-fork-hooks.md` (scroll's hook inventory at
> `097ac369`, each hook's payload, patch site and rebase cost) and
> the private scoping note `agency-wm-spec-reread.md` (the Agency-era
> window-manager spec, mined for contracts).

views-shell works on stock scroll, sway, niri and Hyprland through adapters. Nothing in
the core needs a fork. A fork is a richer adapter, not a different plugin model:
every hook below is a capability that the scroll adapter probes, and a plugin
entry that needs a missing capability is not registered (rule R14).

## What the fork is, and is not

- **A strict superset of scroll.** Every scroll feature, its i3 command grammar,
  criteria, marks, modes, jump, trails, spaces and Tom's ported config keep
  working. `swaymsg`, `scrollmsg` and every sway client keep working unchanged.
- **Typed hooks on the i3 socket.** New request types above 124, new event bits
  above 31 and new `change` values on existing events, each listed in a
  `GET_VERSION` `features` array (hook H10) so the adapter can probe it. No private
  Wayland global: per-toplevel thumbnails are already stock
  (`ext_foreign_toplevel_image_capture_source_manager_v1`).
- **A patch queue, not a long-lived branch.** Each hook is one patch file with an
  upstream-status header (`sent as PR #n`, `merged in <sha>`, `declined: <reason>`,
  `local-only`), applied by the dotfiles' pinned `pkgs/scroll` derivation. A merged
  patch is deleted at the next bump. A workbench branch is rebased onto scroll
  master at each bump and regenerates the queue. scroll itself tracks wlroots and
  sway by hand cherry-picks, with no merge commits, so a queue rebases cleanly.
- **Not umbriel.** umbriel (`noctalia-dev/umbriel`) is an independent C++23
  compositor on wlroots 0.20 with its own SceneFX fork, not a sway fork. Taking it
  would discard everything scroll is. Its effects are of no importance here. Three
  of its ideas are worth carrying: full-snapshot event families, stable workspace
  ids, and its one-page `SCOPE.md`, whose four questions this fork borrows (does it
  need the compositor; does it extend or open a front; does it fall out of what
  exists; who maintains it in a year).
- **Not Aura.** No Chromium scene graph in the compositor, no Aura as window
  manager. The Agency corpus is mined for its contracts, not its architecture.
- **Not a Lua replacement by deletion.** The fork gives the shell typed hooks so the
  core never calls Lua. Whether Lua is then compiled out (H12) is a separate
  maintenance choice.

## The hooks

Hook ids follow the private scoping note `scroll-fork-hooks.md`; H13 to H22 are the Agency-spec
additions from the private scoping note `agency-wm-spec-reread.md` §3.2, numbered here so the two
notes share one list.

| Id | Hook | Shell gains | Patch size | Upstream chance | Degrades to |
|---|---|---|---|---|---|
| H1 | `scroller` change `jump` with `phase: begin\|end` and the label table | The core's only Lua hook (`jump_end`) goes; labels drawable in Views | ~60 lines, `layout.c` at the lines that set and clear `root->jumping` | good | capability `scroll.jump` only |
| H2 | `scroller` change `overview_workspaces` with per-output workspace rects | Rail and bar react to `scale_workspaces` | ~40 lines | good | niri: open/closed without geometry |
| H3 | One coalesced geometry event per applied transaction | Rail, minimap and Alt+Tab never poll `GET_TREE` | ~50 lines in the transaction apply loop | plausible | niri `WindowLayoutsChanged`; sway polls |
| H4 | Binding events for gestures and switches | `bindgesture … nop views-shell …` costs nothing; no process per swipe | ~25 lines, `ipc-json.c` default branch | good (or via sway) | `exec views-shell call` |
| H5 | ext-workspace ids and `REMOVE` | The generic adapter becomes first-class on scroll | ~20 lines | good (sway is the better home) | name-only join |
| H10 | `GET_VERSION` `features` array | The capability probe itself | ~15 lines | good | `variant` only |
| H11 | `window` change `app_id` | Late-set app ids (Chrome PWAs, Xwayland) fix the rail icon | ~3 lines | good | the next unrelated event |
| H14 | A `generation` counter on events and replies | Snapshot and event ordering without guesswork | small | open | tick barrier |
| H15 | `bindsym --title` (returned by `GET_BINDINGS`) and `--cooldown` | The cheat sheet and shortcuts page from one query | small | open | the `# @title` comment convention |
| H16 | `SUBSCRIBE` with an initial burst (snapshot, then events) | No race between subscribe and query | small | open | subscribe-then-query with the tick barrier |
| H17 | A real `SYNC` (reply after processing) | Command echoes without a tick round trip | small | open | `SEND_TICK` |
| H18 | An `atomic` flag on `RUN_COMMAND` (one transaction, no intermediate frame) | Multi-step choreography (herdr chords) without a visible jump | moderate | open | separate commands |
| H19 | `ipc_permit` by peer uid | An agent unit cannot `exit` the seat compositor | small | open | trust every local client |
| H20 | `focus layer <ns> --after-release` | A bubble owns the keyboard the instant a chord is released | small | open | surface-requested exclusive keyboard |
| H21 | `layer_rule <ns> capture_exclude` | The bar never appears in screenshots or screencasts | moderate | open | bars in captures |
| H7 | **Synchronous placement hook** (Agency D03.FR-057 / D02.FR-036, verbatim): one registered client, a 100 ms deadline, a candidate record, `pass` or `apply` with a placement; timeout or disconnect falls through to ordinary placement | Pre-first-frame placement decided by the shell: open beside the spawner, a race-free herdr projector landing | ~150 lines, blocking variant at `view.c:1057` | unlikely | a visible late move on every compositor |
| H9 | Per-window border colour (`for_window … border_color …`) | The orange prompt border; the one niri look scroll cannot express | ~80 lines | ask once | none (niri and Hyprland have it natively) |
| H22 | `for_window --live` with dynamic matchers | Focus-dependent borders and opacity | moderate | open | static `for_window` |
| H13 | Background blur behind chosen surfaces (`ext-background-effect`) on a blur-capable scene | MacTahoe's translucent GTK surfaces frosted, as under niri | large: needs a blur-capable renderer in the vendored wlroots | declined upstream (#289, #226) | unfrosted translucency, or MacTahoe's solid variant |
| H12 | A Lua-free build (`-Dlua=disabled`) | `LUA_EVAL` gone from the socket | ~30 lines of meson and ~15 `#ifdef`s | maybe | Lua compiled and unused |

Findings that need no hook: workspace reorder is `workspace swap` (stock); thumbnails
are stock per-toplevel capture; a theme event is not needed because views-shell renders
`scroll.conf` from the theme itself.

## Rungs, lowest first

Order and dependencies only.

| Rung | Content | Character |
|---|---|---|
| 0 | stock scroll | what the dotfiles branch runs today |
| 1 | events: H1, H2, H3, H4, H5, H10, H11, plus H14 and H15 | small, sway-shaped, listener-guarded; sent upstream one PR at a time, carried until merged. This alone is not a fork. |
| 2 | requests: H16 to H21 | still on the i3 socket; some may be upstreamable |
| 3 | H7, the placement hook | fork-only; after H18 so an `apply` commits as one transaction |
| 4 | H9 and H22, per-window looks | fork-only unless dawsers takes the explicit-override framing |
| 5 | H13, blur | only if Tom wants MacTahoe's translucent variant frosted on scroll, and only on a blur-capable scene; no contortions |

Inside the queue, events go first and fork-only patches last, so a declined or
conflicting fork-only patch can be dropped without re-threading the rest.

## MacTahoe and the niri look

MacTahoe is non-negotiable and no libadwaita CSS is generated (rule R26). niri's
rules did four things: corner radius with clipping, border colours, blur behind
kitty and Nautilus, and the orange prompt border. On stock scroll, corner radius is
`default_decoration border_radius 4` and borders are `client.*`. What is left is
blur (H13) and the per-window border (H9), and Tom has placed exactly that kind of
functionality in his own superset fork rather than in a patched scroll. Until
then, MacTahoe's translucent GTK4 surfaces draw unfrosted over whatever is behind
them. Whether to live with that, use MacTahoe's solid variant, or climb to rung 5
is open decision P21.

## Testing the fork

Inside the dotfiles flake, with no clocks: `checks.scroll-patches-apply` applies
the queue to the pinned source and fails on any fuzz; `checks.scroll-hooks-tests`
runs scroll's own pytest suite (ASan, headless) plus one test per hook in the build
sandbox. Anything that starts a compositor outside the sandbox runs under
`~/.local/bin/runtime-test`. The H7 deadline needs a headless measurement with a
deliberately slow hook client before it is trusted on the 5K seats.
