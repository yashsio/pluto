// layout.c -- dwm's master/stack and monocle layouts for Pluto.
//
// The layout functions only assign slots and propose dimensions; the actual
// set_position/set_borders/show/hide calls happen in the render sequence.  All
// of this runs inside a manage sequence, as required by
// river-window-management-v1.
//
// Copyright 2026 Yash Sharma.  See LICENSE for details.

#include "pluto.h"

// Give a client the given slot (content + border) and, if necessary, propose
// new content dimensions to the compositor.
static void place_tiled(struct Client *c, struct Rect slot) {
	c->slot = slot;
	c->bw = borderpx;

	int32_t width = MAX(0, slot.width - 2 * c->bw);
	int32_t height = MAX(0, slot.height - 2 * c->bw);

	if (width != c->proposed_width || height != c->proposed_height) {
		river_window_v1_propose_dimensions(c->obj, width, height);
		c->proposed_width = width;
		c->proposed_height = height;
	}

	// Remember where the content is so toggling to floating does not make
	// the window jump to the top left corner.
	c->geom.x = slot.x + c->bw
		+ (width - c->geom.width) / 2;
	c->geom.y = slot.y + c->bw
		+ (height - c->geom.height) / 2;
}

static int tiled_count(struct Monitor *m) {
	int n = 0;
	struct Client *c;
	wl_list_for_each(c, &wm.clients, link)
		if (c->mon == m && is_visible(c)
				&& !c->isfloating && !c->fullscreen)
			n++;
	return n;
}

// The work area with the outer gap removed.  If the gap would leave no room,
// it is ignored rather than producing degenerate geometry.
static struct Rect gap_area(struct Monitor *m) {
	struct Rect w = m->windowed;
	int g = MAX(0, gap_outer);

	if (w.width - 2 * g < 1 || w.height - 2 * g < 1)
		g = 0;

	w.x += g;
	w.y += g;
	w.width -= 2 * g;
	w.height -= 2 * g;
	return w;
}

// dwm's tile(): nmaster clients stacked in the master area on the left, the
// rest stacked in the remaining area on the right.
void tile(struct Monitor *m) {
	struct Client *c;
	struct Rect w = gap_area(m);

	int n = tiled_count(m);
	if (n == 0)
		return;

	int gi = MAX(0, gap_inner);
	int nmaster = MIN(n, m->nmaster);
	int stack_count = n - nmaster;

	// Never let the gaps eat the clients; fall back to gapless for this
	// arrange if there is not at least one pixel per client.
	if ((nmaster > 1 && w.height - (nmaster - 1) * gi < nmaster)
			|| (stack_count > 1
				&& w.height - (stack_count - 1) * gi < stack_count))
		gi = 0;

	int mw;
	if (stack_count > 0 && m->nmaster > 0 && w.width > gi)
		mw = (int32_t)((w.width - gi) * m->mfact);
	else if (stack_count > 0)
		mw = 0;
	else
		mw = w.width;

	int master_height = nmaster > 0
		? (w.height - (nmaster - 1) * gi) / nmaster : 0;
	int stack_height = stack_count > 0
		? (w.height - (stack_count - 1) * gi) / stack_count : 0;

	int split = stack_count > 0 && mw > 0;
	int stack_x = w.x + mw + (split ? gi : 0);
	int stack_width = MAX(1, w.width - mw - (split ? gi : 0));

	int i = 0;
	int y = w.y;
	for (c = next_tiled(m, NULL); c != NULL; c = next_tiled(m, c), i++) {
		struct Rect slot;
		if (i < nmaster) {
			// Give the last master client the leftover pixels so the
			// column exactly fills the work area.
			int h = (i == nmaster - 1)
				? (w.y + w.height - y) : master_height;
			slot = (struct Rect){ w.x, y, mw, h };
			y += h + gi;
		} else {
			int idx = i - nmaster;
			int yy = w.y + idx * (stack_height + gi);
			int h = (idx == stack_count - 1)
				? (w.y + w.height - yy) : stack_height;
			slot = (struct Rect){ stack_x, yy, stack_width, h };
		}
		place_tiled(c, slot);
	}
}

// dwm's monocle(): every tiled client gets the whole work area; the focused
// one is placed on top by restack().
void monocle(struct Monitor *m) {
	if (tiled_count(m) == 0)
		return;

	struct Rect w = gap_area(m);
	struct Client *c;
	for (c = next_tiled(m, NULL); c != NULL; c = next_tiled(m, c))
		place_tiled(c, w);
}
