# Example themes

Tom's three themes as Omarchy theme directories, converted from the dotfiles'
`home/themes/*.nix` by the private scoping note `themes/nix-palette-to-omarchy.py`.
Rendering the converted `noir` through Omarchy's own unmodified kitty template
reproduces Tom's live kitty palette exactly, apart from `color7`, which is kept as
the extra key `terminal_color7`.

`all-black` is not one of those three: it is the Chrome Web Store theme Tom wears
(**All Black**, id `mkplpffahhkjfocfbfapcemhhkgmljpn`) expressed as an Omarchy theme
directory, so the shell and Chrome wear the same black and white. Only what that
theme defines is taken verbatim — the black grounds, the white ink, the button tint
toward white (`accent`); the neutral greys and the ANSI hues it does not define are
marked `derived` in its `colors.toml` (the hues are `noir`'s, and
[`../../style/theme-map.json`](../../style/theme-map.json) leaves them unpinned).
See [`../../style/tokens/README.md`](../../style/tokens/README.md).

| Theme | Mode | Background | GTK theme (`gtk.theme`) | Icons (`icons.theme`) |
|---|---|---|---|---|
| [`noir/`](noir/) (the default) | dark | `#000000` | MacTahoe-Dark-grey | MacTahoe-dark |
| [`claude-dark/`](claude-dark/) | dark | `#1a1a1a` | MacTahoe-Claude-Dark-orange | MacTahoe-dark |
| [`claude-light/`](claude-light/) | light | `#f9f9f7` | MacTahoe-Claude-Light-orange | MacTahoe-light |
| [`all-black/`](all-black/) | dark | `#000000` | MacTahoe-Dark-grey | MacTahoe-dark |

No theme here ships `backgrounds/` yet. Whether the accent wallpapers are linked into each
theme's `backgrounds/` is open. The extra keys (`brand`, `desktop_background`,
`inactive_border`, `tab_indicator_inactive`, `terminal_color7`,
`nvim_catppuccin_flavour`, `claude_code_theme`, `hyprland_active_border`) and the
`gtk.theme` file are not Omarchy keys; whether they keep bare names or take a
prefix is open decision P18.

`tools/validate.sh` parses every `colors.toml` here and fails on a key that
[`../../style/theme-map.json`](../../style/theme-map.json) does not account for,
on a colour that is not `#rrggbb`, and on a directory missing `gtk.theme` or
`icons.theme`.
