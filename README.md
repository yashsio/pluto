# pluto - dynamic tiling window manager for river

Pluto is small, fast dynamic tiling window manager for the [river](https://isaacfreund.com/software/river/) wayland compositor.
It uses a persistent BSP tree layout inspired by [bspwm](https://github.com/baskerville/bspwm),
splitting the focused window in half on each new window with alternating orientation.

A monocle layout is also available, all other open windows are hidden or pushed to the background, leaving just the active window visible.


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
