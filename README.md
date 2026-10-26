# Pluto - A dynamic tiling window manager for river

Pluto is small, fast dynamic tiling window manager for the [river](https://isaacfreund.com/software/river/) wayland compositor.
It have 2 layouts dwidle (or BSP) and master stack like dwm.
It also provides a tiny status IPC so bars such as [pluto-bar](https://github.com/yashsio/pluto-bar) can show tags, layout and the focused title, and switch tags with a click.

# Requirements

In order to build pluto you need:
- a C99 compiler
- GNU or BSD make
- wayland-scanner
- libxkbcommon

# Installation

Edit config.mk to match your local setup.
Afterwards enter the following command to build and install (if necessary as root):

	make clean install


# Configuration

The configuration of pluto is done by creating a custom config.h
and (re)compiling the source code.


# Credits

Pluto is a fork of [JrWM](https://github.com/jpco/jrwm).


# License

Licensed under [GPL-3.0](LICENSE).

