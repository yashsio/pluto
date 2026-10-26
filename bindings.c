// bindings.c -- dwm's key/button bindings and the actions they trigger.
//
// Copyright 2026 Isaac Freund, 2026 Jack Conger.  See LICENSE for details.
// Copyright 2026 Yash Sharma.  See LICENSE for details.

#include <signal.h>
#include <stdlib.h>
#include <unistd.h>

#include "pluto.h"

struct XkbBinding {
	struct wl_list link;            // Seat.xkb_bindings
	struct river_xkb_binding_v1 *obj;
	struct Seat *seat;
	void (*func)(const Arg *);
	Arg arg;
	bool enabled;
};

struct PointerBinding {
	struct wl_list link;            // Seat.pointer_bindings
	struct river_pointer_binding_v1 *obj;
	struct Seat *seat;
	void (*func)(const Arg *);
	Arg arg;
	bool enabled;
};


// Commands

void spawn(const Arg *arg) {
	if (arg == NULL || arg->v == NULL)
		return;

	if (fork() == 0) {
		struct sigaction sa;
		setsid();

		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);

		execvp(((char *const *)arg->v)[0], (char *const *)arg->v);
		exit(EXIT_FAILURE);
	}
}

void quit(const Arg *arg) {
	if (window_manager_v1 != NULL)
		river_window_manager_v1_exit_session(window_manager_v1);
}

void killclient(const Arg *arg) {
	if (selmon != NULL && selmon->sel != NULL)
		selmon->sel->need_close = true;
}

void setlayout(const Arg *arg) {
	if (selmon == NULL)
		return;

	if (arg != NULL && arg->v != NULL) {
		selmon->lt = arg->v;
		return;
	}

	// Mod+Space cycles through the layouts, as is customary.
	int i;
	for (i = 0; i < layouts_len; i++)
		if (selmon->lt == &layouts[i])
			break;
	selmon->lt = &layouts[(i + 1) % layouts_len];
}

void setmfact(const Arg *arg) {
	if (selmon == NULL || arg == NULL || selmon->lt->arrange == NULL)
		return;

	float f = arg->f < 1.0f ? selmon->mfact + arg->f : arg->f;
	if (f < 0.05f || f > 0.95f)
		return;
	selmon->mfact = f;
}

void incnmaster(const Arg *arg) {
	if (selmon == NULL || arg == NULL)
		return;
	selmon->nmaster = MAX(0, selmon->nmaster + arg->i);
}

// dwm's zoom(): swap the focused client with the master by moving it to the top
// of the tiling order.  Focusing alone never does this.
void zoom(const Arg *arg) {
	if (selmon == NULL || selmon->sel == NULL)
		return;

	struct Client *c = selmon->sel;
	if (!is_visible(c) || selmon->lt->arrange == NULL)
		return;

	// dwm: if the focused window is already master, zoom the next tiled
	// client instead, so Mod+Return always does something.
	if (c == next_tiled(selmon, NULL)) {
		c = next_tiled(selmon, c);
		if (c == NULL)
			return;
	}

	wl_list_remove(&c->link);
	wl_list_insert(&wm.clients, &c->link);
}

void togglefloating(const Arg *arg) {
	if (selmon == NULL || selmon->sel == NULL)
		return;

	struct Client *c = selmon->sel;
	c->isfloating = !c->isfloating || c->fullscreen;

	if (c->isfloating) {
		c->geom = client_render_rect(c);
		if (c->geom.width <= 0 || c->geom.height <= 0)
			center_client(c);
	} else {
		// Force arrange() to propose dimensions again.
		c->proposed_width = -1;
		c->proposed_height = -1;
	}
}

void setfullscreen(struct Client *c, bool fullscreen) {
	if (c == NULL || c->mon == NULL || c->fullscreen == fullscreen)
		return;

	if (fullscreen) {
		c->wasfloating = c->isfloating;
		c->oldgeom = client_render_rect(c);
		c->isfloating = true;
		c->fullscreen = true;
		c->need_fullscreen = true;
	} else {
		c->fullscreen = false;
		c->need_exit_fullscreen = true;
		c->isfloating = c->wasfloating;
		c->proposed_width = -1;
		c->proposed_height = -1;
	}
}

void togglefullscreen(const Arg *arg) {
	if (selmon == NULL || selmon->sel == NULL)
		return;
	setfullscreen(selmon->sel, !selmon->sel->fullscreen);
}

static struct Client *stack_next(struct Client *c, int dir) {
	struct wl_list *start = &c->slink;
	struct wl_list *pos = dir > 0 ? start->next : start->prev;

	while (pos != start) {
		struct Client *w = wl_container_of(pos, w, slink);
		if (w->mon == selmon && is_visible(w))
			return w;
		pos = dir > 0 ? pos->next : pos->prev;
	}
	return NULL;
}

void focusstack(const Arg *arg) {
	if (selmon == NULL)
		return;

	struct Client *c = selmon->sel;
	if (c == NULL) {
		focus(NULL);
		return;
	}

	// Walk the focus stack (not the tiling order), exactly like dwm.
	struct Client *w = stack_next(c, arg->i);
	if (w != NULL) {
		focus(w);
		if (current_seat != NULL)
			current_seat->warp = true;
	}
}

void focusmon(const Arg *arg) {
	if (selmon == NULL || wl_list_empty(&wm.monitors))
		return;

	struct Monitor *m = next_monitor(selmon, arg->i);
	if (m == NULL || m == selmon)
		return;

	selmon = m;
	m->sel = first_visible(m);
	focus(m->sel);
}

void tagmon(const Arg *arg) {
	if (selmon == NULL || selmon->sel == NULL)
		return;

	struct Client *c = selmon->sel;
	struct Monitor *m = next_monitor(selmon, arg->i);
	if (m == NULL || m == c->mon)
		return;

	if (c->fullscreen) {
		c->fullscreen = false;
		c->need_exit_fullscreen = true;
		c->isfloating = c->wasfloating;
	}

	c->mon = m;
	c->tags = m->tagset[m->seltags];
	c->proposed_width = -1;
	c->proposed_height = -1;
	c->pos_valid = false;

	selmon = m;
	wl_list_remove(&c->link);
	wl_list_insert(&wm.clients, &c->link);
	wl_list_remove(&c->slink);
	wl_list_insert(&wm.stack, &c->slink);
	m->sel = c;
	focus(c);
}

void view(const Arg *arg) {
	if (selmon == NULL)
		return;

	if ((arg->ui & tagmask) == selmon->tagset[selmon->seltags])
		return;

	selmon->seltags ^= 1;
	if (arg->ui & tagmask)
		selmon->tagset[selmon->seltags] = arg->ui & tagmask;
}

void tag(const Arg *arg) {
	if (selmon == NULL || selmon->sel == NULL)
		return;
	if (arg->ui & tagmask)
		selmon->sel->tags = arg->ui & tagmask;
}

void toggleview(const Arg *arg) {
	if (selmon == NULL)
		return;
	selmon->tagset[selmon->seltags] ^= arg->ui & tagmask;
}

void toggletag(const Arg *arg) {
	if (selmon == NULL || selmon->sel == NULL)
		return;
	selmon->sel->tags ^= arg->ui & tagmask;
}


// Interactive move/resize

static void start_op(struct Client *c, int mode) {
	struct Seat *seat = current_seat;
	if (seat == NULL || seat->op_mode != OpNone)
		return;

	seat->op_client = c;
	seat->op_mode = mode;
	seat->op_geom = c->geom;
	seat->op_dx = 0;
	seat->op_dy = 0;
	seat->op_start_pending = true;
	seat->op_end_pending = false;
}

void movemouse(const Arg *arg) {
	if (selmon == NULL || selmon->sel == NULL
			|| selmon->sel->fullscreen)
		return;

	struct Client *c = selmon->sel;

	// Dragging a tiled window tears it out of the layout, like the
	// tile_drag patch for dwm.
	if (!c->isfloating) {
		c->geom = client_render_rect(c);
		c->isfloating = true;
	}
	start_op(c, OpMove);
}

void resizemouse(const Arg *arg) {
	if (selmon == NULL || selmon->sel == NULL
			|| selmon->sel->fullscreen)
		return;

	struct Client *c = selmon->sel;

	if (!c->isfloating) {
		c->geom = client_render_rect(c);
		c->isfloating = true;
	}
	start_op(c, OpResize);
}

void op_cancel(struct Seat *seat) {
	seat->op_client = NULL;
	seat->op_start_pending = false;
	seat->op_end_pending = true;
	seat->op_dx = 0;
	seat->op_dy = 0;
}


// Binding plumbing

static void xkb_binding_handle_pressed(void *data,
		struct river_xkb_binding_v1 *obj) {
	struct XkbBinding *b = data;
	current_seat = b->seat;
	if (b->func != NULL)
		b->func(&b->arg);
	manage_dirty();
}

static void xkb_binding_handle_released(void *data,
		struct river_xkb_binding_v1 *obj) {}

const struct river_xkb_binding_v1_listener river_xkb_binding_listener = {
	.pressed = xkb_binding_handle_pressed,
	.released = xkb_binding_handle_released,
};

static void pointer_binding_handle_pressed(void *data,
		struct river_pointer_binding_v1 *obj) {
	struct PointerBinding *b = data;
	current_seat = b->seat;
	if (b->func != NULL)
		b->func(&b->arg);
	manage_dirty();
}

static void pointer_binding_handle_released(void *data,
		struct river_pointer_binding_v1 *obj) {}

const struct river_pointer_binding_v1_listener river_pointer_binding_listener = {
	.pressed = pointer_binding_handle_pressed,
	.released = pointer_binding_handle_released,
};

void init_bindings(struct Seat *seat) {
	size_t i;

	for (i = 0; keys[i].func != NULL; i++) {
		struct XkbBinding *b = calloc(1, sizeof(*b));
		if (b == NULL)
			return;
		b->obj = river_xkb_bindings_v1_get_xkb_binding(xkb_bindings_v1,
				seat->obj, keys[i].key, keys[i].mod);
		b->seat = seat;
		b->func = keys[i].func;
		b->arg = keys[i].arg;
		river_xkb_binding_v1_add_listener(b->obj,
				&river_xkb_binding_listener, b);
		wl_list_insert(&seat->xkb_bindings, &b->link);
	}

	for (i = 0; buttons[i].func != NULL; i++) {
		struct PointerBinding *b = calloc(1, sizeof(*b));
		if (b == NULL)
			return;
		b->obj = river_seat_v1_get_pointer_binding(seat->obj,
				buttons[i].button, buttons[i].mod);
		b->seat = seat;
		b->func = buttons[i].func;
		b->arg = buttons[i].arg;
		river_pointer_binding_v1_add_listener(b->obj,
				&river_pointer_binding_listener, b);
		wl_list_insert(&seat->pointer_bindings, &b->link);
	}
}

void manage_bindings(struct Seat *seat) {
	struct XkbBinding *xb;
	struct PointerBinding *pb;

	wl_list_for_each(xb, &seat->xkb_bindings, link) {
		if (seat->locked && xb->enabled) {
			river_xkb_binding_v1_disable(xb->obj);
			xb->enabled = false;
		} else if (!seat->locked && !xb->enabled) {
			river_xkb_binding_v1_enable(xb->obj);
			xb->enabled = true;
		}
	}
	wl_list_for_each(pb, &seat->pointer_bindings, link) {
		if (seat->locked) {
			if (!pb->enabled)
				continue;
			river_pointer_binding_v1_disable(pb->obj);
			pb->enabled = false;
		} else {
			if (pb->enabled)
				continue;
			river_pointer_binding_v1_enable(pb->obj);
			pb->enabled = true;
		}
	}
}

void remove_bindings(struct Seat *seat) {
	struct XkbBinding *xb, *xtmp;
	struct PointerBinding *pb, *ptmp;

	wl_list_for_each_safe(xb, xtmp, &seat->xkb_bindings, link) {
		river_xkb_binding_v1_destroy(xb->obj);
		wl_list_remove(&xb->link);
		free(xb);
	}
	wl_list_for_each_safe(pb, ptmp, &seat->pointer_bindings, link) {
		river_pointer_binding_v1_destroy(pb->obj);
		wl_list_remove(&pb->link);
		free(pb);
	}
}

void lock_bindings(struct Seat *seat) {
	// The actual disable requests happen during the next manage sequence.
	seat->locked = true;
	seat->bindings_dirty = true;
	manage_dirty();
}

void unlock_bindings(struct Seat *seat) {
	seat->locked = false;
	seat->bindings_dirty = true;
	manage_dirty();
}
