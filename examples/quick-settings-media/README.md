# Media (quick settings): `views-shell.qs-media`

Now-playing cards, one per MPRIS session, in the host's `card` slot, as a T1 plugin
with no code over the first-party `views-shell.media` source plugin. Each card is one
`mediaSession` node, which the renderer draws with the stock
`global_media_controls::MediaItemUIView` from `components/global_media_controls`
(it builds on Linux; Chrome uses it there). That node is part of the closed `ui`
vocabulary, so a third-party player plugin gets the same card: the vocabulary grows
by stock components, never by first-party privilege. Ash's media view is not
ported.
