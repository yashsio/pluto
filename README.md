# Pluto - a dwm-like window manager for river

Pluto is a small dynamic tiling window manager for the
[river](https://isaacfreund.com/software/river/) Wayland compositor.  It is a
port of dwm's policy to river: tags instead of workspaces, a master/stack
layout, a focus stack, rules, and the same key bindings.  The whole window
manager lives in one C file and is configured by editing `config.def.h`, just
like dwm.

The status bar is deliberately left out.  Pluto only manages windows; use any
river layer-shell bar you like.

## Requirements

- a C99 compiler
- GNU or BSD make
- wayland-scanner
- wayland-client
- libxkbcommon

## Installation

Edit `config.mk` to match your local setup, then run:

	make clean install

## Configuration

Copy `config.def.h` to `config.h`, edit it, and rebuild:

	cp config.def.h config.h
	make clean install

The default configuration uses Mod4 (the Windows key) as `MODKEY`, keeps
dwm's key bindings and tag model, and starts `foot` and `fuzzel`.

## License

Licensed under [GPL-3.0](LICENSE).
