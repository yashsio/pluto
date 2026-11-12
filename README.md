# Pluto - a dwm-like window manager for river

Pluto is a small dynamic tiling window manager for the
[river](https://isaacfreund.com/software/river/) Wayland compositor.  It is a
port of dwm's policy to river: tags instead of workspaces, a master/stack
layout, a focus stack, rules, and the same key bindings.  The whole window
manager lives in one C file and is configured by editing `config.def.h`, just
like dwm.

The status bar is deliberately left out.  Pluto only manages windows; use any
layer-shell bar you like. If you want full dwm vibe try [Pluto-Bar](https://github.com/yashsio/pluto-bar).

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

## Optional patches

Optional patches are kept in /patches:

- `patches/pluto-gaps.patch` - dwm-style inner and outer gaps
- `patches/pluto-ipc.patch` - status bar IPC for pluto-bar
- `patches/pluto-followtag.patch` - tag the focused window and switch to that tag

Apply patches from the repository root:

	make clean
	git apply patches/pluto-gaps.patch
	git apply patches/pluto-ipc.patch
	make clean install

## License

Licensed under [GPL-3.0](LICENSE).
