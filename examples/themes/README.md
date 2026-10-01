# Example themes

Tom's three themes as Omarchy theme directories, converted from the dotfiles'
`home/themes/*.nix` by the private scoping note `themes/nix-palette-to-omarchy.py`.
Rendering the converted `noir` through Omarchy's own unmodified kitty template
reproduces Tom's live kitty palette exactly, apart from `color7`, which is kept as
the extra key `terminal_color7`.

| Theme | Mode | Background | GTK theme (`gtk.theme`) | Icons (`icons.theme`) |
|---|---|---|---|---|
| [`noir/`](noir/) (the default) | dark | `#000000` | MacTahoe-Dark-grey | MacTahoe-dark |
| [`claude-dark/`](claude-dark/) | dark | `#1a1a1a` | MacTahoe-Claude-Dark-orange | MacTahoe-dark |
| [`claude-light/`](claude-light/) | light | `#f9f9f7` | MacTahoe-Claude-Light-orange | MacTahoe-light |

No theme here ships `backgrounds/` yet. Whether the accent wallpapers are linked into each
theme's `backgrounds/` is open. The extra keys (`brand`, `desktop_background`,
`inactive_border`, `tab_indicator_inactive`, `terminal_color7`,
`nvim_catppuccin_flavour`, `claude_code_theme`, `hyprland_active_border`) and the
`gtk.theme` file are not Omarchy keys; whether they keep bare names or take a
prefix is open decision P18.
