# Media (quick settings): `views-shell.qs-media`

Now-playing cards, one per MPRIS session, in the host's `card` slot, as a T1 plugin
with no code over the first-party `views-shell.media` source plugin. Each card is one
`mediaSession` node, which the renderer draws with the stock
`global_media_controls::MediaItemUIView` from `components/global_media_controls`
(it builds on Linux; Chrome uses it there). That node is part of the closed `ui`
vocabulary, so a third-party player plugin gets the same card: the vocabulary grows
by stock components, never by first-party privilege. Ash's media view is not
ported.

## Tier, contributions, capabilities, permissions

- **Tier:** T1 declarative, no commands of its own.
- **Contributes:** one `quickSettings` entry, `sessions` (slot `card`, shown
  `when: session.mediaActive`), bound to `views-shell.media/state`; tree
  [`ui/card.json`](ui/card.json), a `repeat` of `mediaSession` nodes whose
  action is `views-shell.media/control` with the session id.
- **Capabilities:** none. **Permissions:** `call:views-shell.media/control`.
  **Depends on:** `views-shell.media`.
- The `mediaSession` node is not drawn in chapter 2; the trace already carries
  its resolved props.

## Registry fixture

[`tools/fixtures/registry/views-shell.qs-media.json`](../../tools/fixtures/registry/views-shell.qs-media.json).
Render trace: `tools/fixtures/render/quick-settings-media.card.json`.
