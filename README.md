# vwm — a minimal dynamic tiling window manager

- **dynamic tiling** — each new window splits the focused slot,
  alternating vertical/horizontal based on the slot's aspect ratio
  (wide slot → split left/right, tall slot → split top/bottom)

## Keybindings

| Key          | Action                        |
|--------------|-------------------------------|
| Alt+Enter    | Launch Alacritty              |
| Alt+Q        | Close focused window          |
| Alt+Shift+Q  | Quit vwm / exit X11           |

## Build

    make

Requires only `libx11-dev` (Xlib).

## Install

    sudo make install     # installs to /usr/local/bin/vwm

## Run

Add to `~/.xinitrc`:

    exec vwm

Then start X with `startx`, or use a display manager and select *vwm* as
the session.

## Tuning (edit vwm.c top constants)

| Constant       | Default | Meaning                        |
|----------------|---------|--------------------------------|
| `BORDER_WIDTH` | 3       | Border thickness in pixels     |
| `BORDER_COLOR` | 0x00cc44| Focused border colour (RGB)    |
| `GAP`          | 6       | Gap between tiles in pixels    |
| `TERMINAL`     | alacritty | Command for Alt+Enter        |
