// pluto.c -- Protocol layer for Pluto, a dwm-like window manager for river.
//
// This file is meant to have as little "interesting" logic as possible; it
// handles the river-window-management-v1 protocol, keeps the core data
// structures alive and dispatches to layout.c/bindings.c.
//
// Copyright 2026 Isaac Freund, 2026 Jack Conger.  See LICENSE for details.
// Copyright 2026 Yash Sharma.  See LICENSE for details.

#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pluto.h"
#include "config.h"
#include "ipc.h"

struct WindowManager wm;
struct Monitor *selmon;
struct Seat *current_seat;

// Last monitor passed to river_layer_shell_output_v1_set_default.
static struct Monitor *default_monitor;

// Needed to bind the wl_output behind a river_output_v1 and learn its name.
static struct wl_registry *wl_registry;

struct river_window_manager_v1 *window_manager_v1;
struct river_xkb_bindings_v1 *xkb_bindings_v1;
struct river_layer_shell_v1 *layer_shell_v1;


// Utility functions

static bool rect_contains(struct Rect r, int32_t x, int32_t y) {
	return r.x <= x && r.x + r.width > x && r.y <= y && r.y + r.height > y;
}

static struct Monitor *monitor_at(int32_t x, int32_t y) {
	struct Monitor *m;
	wl_list_for_each(m, &wm.monitors, link)
		if (rect_contains(m->real, x, y))
			return m;
	return NULL;
}

// Keep the monitor list ordered from left to right / top to bottom.  River
// guarantees that outputs do not overlap.
static void reorder_monitor(struct Monitor *m) {
	struct Monitor *o;
	struct wl_list *location = &wm.monitors;

	wl_list_remove(&m->link);
	wl_list_for_each(o, &wm.monitors, link) {
		if (o->real.x > m->real.x + m->real.width)
			break;
		if (o->real.y > m->real.y + m->real.height)
			break;
		location = &o->link;
	}
	wl_list_insert(location, &m->link);
}

bool is_visible(struct Client *c) {
	if (c->mon == NULL)
		return false;
	return c->tags & c->mon->tagset[c->mon->seltags];
}

struct Client *next_tiled(struct Monitor *m, struct Client *c) {
	struct wl_list *pos = c ? c->link.next : wm.clients.next;
	for (; pos != &wm.clients; pos = pos->next) {
		struct Client *w = wl_container_of(pos, w, link);
		if (w->mon == m && is_visible(w) && !w->isfloating && !w->fullscreen)
			return w;
	}
	return NULL;
}

struct Client *first_visible(struct Monitor *m) {
	if (m == NULL)
		return NULL;
	struct Client *c;
	// dwm walks the stack (focus) order here, so the most recently focused
	// visible client wins.
	wl_list_for_each(c, &wm.stack, slink)
		if (c->mon == m && is_visible(c))
			return c;
	return NULL;
}

struct Client *visible_fullscreen(struct Monitor *m) {
	struct Client *c;
	wl_list_for_each(c, &wm.stack, slink)
		if (c->mon == m && is_visible(c) && c->fullscreen)
			return c;
	return NULL;
}

struct Monitor *first_monitor(void) {
	struct Monitor *m;
	if (wl_list_empty(&wm.monitors))
		return NULL;
	m = wl_container_of(wm.monitors.next, m, link);
	return m;
}

struct Monitor *next_monitor(struct Monitor *m, int dir) {
	struct wl_list *pos;
	if (m == NULL || wl_list_empty(&wm.monitors))
		return NULL;
	if (dir > 0) {
		for (pos = m->link.next; pos != &wm.monitors; pos = pos->next)
			return wl_container_of(pos, m, link);
		return wl_container_of(wm.monitors.next, m, link);
	}
	for (pos = m->link.prev; pos != &wm.monitors; pos = pos->prev)
		return wl_container_of(pos, m, link);
	return wl_container_of(wm.monitors.prev, m, link);
}

// The rectangle a client's content is actually drawn at.  For tiled clients
// the slot assigned by arrange is authoritative, but the compositor may give
// the window slightly different dimensions (e.g. terminals round down to a
// whole number of cells), so center the content in its slot like dwm does
// with size hints.
struct Rect client_render_rect(struct Client *c) {
	if (c->isfloating || c->fullscreen || c->slot.width <= 0)
		return c->geom;

	struct Rect r = c->slot;
	r.x += c->bw;
	r.y += c->bw;
	r.width -= 2 * c->bw;
	r.height -= 2 * c->bw;
	if (r.width < 0)
		r.width = 0;
	if (r.height < 0)
		r.height = 0;
	r.x += (r.width - c->geom.width) / 2;
	r.y += (r.height - c->geom.height) / 2;
	return r;
}

void center_client(struct Client *c) {
	if (c->mon == NULL)
		return;
	c->geom.x = c->mon->windowed.x
		+ (c->mon->windowed.width - c->geom.width) / 2;
	c->geom.y = c->mon->windowed.y
		+ (c->mon->windowed.height - c->geom.height) / 2;
	c->geom.x = MAX(c->geom.x, c->mon->windowed.x);
	c->geom.y = MAX(c->geom.y, c->mon->windowed.y);
}

void manage_dirty(void) {
	if (window_manager_v1 != NULL)
		river_window_manager_v1_manage_dirty(window_manager_v1);
}

void arrange(struct Monitor *m) {
	if (m == NULL || m->lt == NULL)
		return;

	// Tell windows whether they are part of a tiled layout.  This is window
	// management state and may only change during a manage sequence.
	struct Client *c;
	wl_list_for_each(c, &wm.clients, link) {
		if (c->mon != m)
			continue;
		int edges = (c->isfloating || c->fullscreen) ? 0 : EDGES_ALL;
		if (c->tiled != edges) {
			river_window_v1_set_tiled(c->obj, edges);
			c->tiled = edges;
		}
	}

	if (m->lt->arrange != NULL)
		m->lt->arrange(m);
}

void arrange_all(void) {
	struct Monitor *m;
	wl_list_for_each(m, &wm.monitors, link)
		arrange(m);
}

// dwm's focus(): pick a visible client, make it selmon's selection and move it
// to the top of the focus stack.  The tiling order is deliberately left alone:
// focusing a window must not turn it into the master (only zoom does that).
void focus(struct Client *c) {
	if (selmon == NULL)
		return;

	if (c == NULL || !is_visible(c)) {
		struct Client *fs = lockfullscreen
			? visible_fullscreen(selmon) : NULL;
		c = fs ? fs : first_visible(selmon);
	}

	if (c != NULL) {
		if (lockfullscreen) {
			struct Client *fs = visible_fullscreen(c->mon);
			if (fs != NULL)
				c = fs;
		}
		bool changed = c != selmon->sel || c->mon != selmon;
		selmon = c->mon;
		selmon->sel = c;
		if (changed) {
			wl_list_remove(&c->slink);
			wl_list_insert(&wm.stack, &c->slink);
		}
	} else {
		selmon->sel = NULL;
	}
}

// Cached restack order, so an unchanged order does not issue place_top calls
// on every frame. -1 means "unknown, restack unconditionally".
static struct Client *stack_cache[256];
static int stack_cache_len = -1;

void restack(void) {
	struct Client *order[256];
	struct Client *c;
	int n = 0;

	// Bottom to top: tiled clients, then floating ones (which stay above the
	// tiled layout), each group in focus order so the most recently focused
	// visible client ends up on top of its group.
	wl_list_for_each_reverse(c, &wm.stack, slink) {
		if (n >= (int)LENGTH(order))
			break;
		if (is_visible(c) && !c->isfloating)
			order[n++] = c;
	}
	wl_list_for_each_reverse(c, &wm.stack, slink) {
		if (n >= (int)LENGTH(order))
			break;
		if (is_visible(c) && c->isfloating)
			order[n++] = c;
	}

	if (n == stack_cache_len
			&& (n == 0 || memcmp(order, stack_cache,
					n * sizeof(*order)) == 0))
		return;

	for (int i = 0; i < n; i++)
		river_node_v1_place_top(order[i]->node);
	memcpy(stack_cache, order, n * sizeof(*order));
	stack_cache_len = n;
}

void render(void) {
	struct Client *c;

	wl_list_for_each(c, &wm.clients, link) {
		bool want = is_visible(c);
		if (c->shown != want) {
			if (want)
				river_window_v1_show(c->obj);
			else
				river_window_v1_hide(c->obj);
			c->shown = want;
		}
		if (!want || c->fullscreen)
			continue;

		struct Rect r = client_render_rect(c);
		if (!c->pos_valid || r.x != c->pos_x || r.y != c->pos_y) {
			river_node_v1_set_position(c->node, r.x, r.y);
			c->pos_x = r.x;
			c->pos_y = r.y;
			c->pos_valid = true;
		}

		bool focused = c->mon != NULL && c->mon->sel == c;
		if (!c->border_valid || c->border_width_set != c->bw
				|| c->border_focused != focused) {
			uint32_t *color = focused
				? border_color_focus : border_color_normal;
			river_window_v1_set_borders(c->obj,
					EDGES_ALL,
					c->bw,
					color[0], color[1], color[2], color[3]);
			c->border_width_set = c->bw;
			c->border_focused = focused;
			c->border_valid = true;
		}
	}

	restack();
}

void applyrules(struct Client *c) {
	if (c->rules_applied || c->app_id == NULL)
		return;

	for (int i = 0; i < rules_len; i++) {
		const Rule *r = &rules[i];
		bool match = true;

		if (r->app_id != NULL
				&& (c->app_id == NULL || strcmp(r->app_id, c->app_id) != 0))
			match = false;
		if (r->title != NULL
				&& (c->title == NULL || strcmp(r->title, c->title) != 0))
			match = false;
		if (!match)
			continue;

		if (r->tags != 0)
			c->tags = r->tags & tagmask;
		bool wasfloating = c->isfloating;
		c->isfloating = r->isfloating;
		if (c->isfloating && !wasfloating && c->geom_valid)
			center_client(c);
		if (r->monitor >= 0) {
			struct Monitor *m = first_monitor();
			for (int j = 0; m != NULL && j < r->monitor; j++)
				m = next_monitor(m, 1);
			if (m != NULL)
				c->mon = m;
		}
	}

	c->rules_applied = true;
}


// Object creation and destruction

static struct Client *client_create(struct river_window_v1 *obj) {
	struct Client *c = calloc(1, sizeof(*c));
	if (c == NULL)
		return NULL;

	c->obj = obj;
	c->node = river_window_v1_get_node(obj);
	c->tiled = -1;
	c->bw = borderpx;
	c->need_capabilities = true;
	c->need_ssd = true;
	c->mon = selmon ? selmon : first_monitor();
	c->tags = c->mon ? c->mon->tagset[c->mon->seltags] : 1;

	// dwm focuses new windows.
	if (c->mon != NULL) {
		c->mon->sel = c;
		selmon = c->mon;
	}

	wl_list_insert(&wm.clients, &c->link);
	wl_list_insert(&wm.stack, &c->slink);
	stack_cache_len = -1;
	return c;
}

static void client_destroy(struct Client *c) {
	struct Seat *seat;
	wl_list_for_each(seat, &wm.seats, link) {
		if (seat->op_client == c)
			op_cancel(seat);
		if (seat->entered == c)
			seat->entered = NULL;
		if (seat->focus_client == c)
			seat->focus_client = NULL;
	}

	if (c->mon != NULL && c->mon->sel == c)
		c->mon->sel = NULL;

	wl_list_remove(&c->link);
	wl_list_remove(&c->slink);
	stack_cache_len = -1;
	river_window_v1_destroy(c->obj);
	free(c->app_id);
	free(c->title);
	free(c);
}

static struct Monitor *monitor_create(struct river_output_v1 *obj,
		struct river_layer_shell_output_v1 *ls) {
	struct Monitor *m = calloc(1, sizeof(*m));
	if (m == NULL)
		return NULL;

	m->obj = obj;
	m->ls = ls;
	m->tagset[0] = m->tagset[1] = 1;
	m->seltags = 0;
	m->mfact = mfact;
	m->nmaster = nmaster;
	m->lt = &layouts[0];

	wl_list_insert(&wm.monitors, &m->link);
	return m;
}

static void monitor_destroy(struct Monitor *m) {
	struct Client *c;
	struct Monitor *other = NULL, *o;

	wl_list_for_each(o, &wm.monitors, link) {
		if (o != m) {
			other = o;
			break;
		}
	}

	wl_list_for_each(c, &wm.clients, link) {
		if (c->mon != m)
			continue;
		c->mon = other;
		// River automatically exits fullscreen when the output goes away.
		c->fullscreen = false;
		c->isfloating = c->wasfloating;
		if (other != NULL)
			c->tags = other->tagset[other->seltags];
	}

	if (selmon == m) {
		selmon = other;
		if (selmon != NULL)
			selmon->sel = NULL;
	}
	if (default_monitor == m)
		default_monitor = NULL;

	river_layer_shell_output_v1_destroy(m->ls);
	river_output_v1_destroy(m->obj);
	if (m->wl_output != NULL)
		wl_output_destroy(m->wl_output);
	wl_list_remove(&m->link);
	free(m);
}


// Listeners

static void output_handle_removed(void *data, struct river_output_v1 *obj) {
	monitor_destroy(data);
}

static void output_handle_position(void *data, struct river_output_v1 *obj,
		int32_t x, int32_t y) {
	struct Monitor *m = data;
	m->real.x = x;
	m->real.y = y;
	if (m->windowed.width == 0 && m->windowed.height == 0)
		m->windowed = m->real;
	reorder_monitor(m);
	manage_dirty();
}

static void output_handle_dimensions(void *data, struct river_output_v1 *obj,
		int32_t width, int32_t height) {
	struct Monitor *m = data;
	m->real.width = width;
	m->real.height = height;
	if (m->windowed.width == 0 && m->windowed.height == 0)
		m->windowed = m->real;
	reorder_monitor(m);
	manage_dirty();
}

static void wl_output_handle_geometry(void *data, struct wl_output *output,
		int32_t x, int32_t y, int32_t physical_width,
		int32_t physical_height, int32_t subpixel, const char *make,
		const char *model, int32_t transform) {}
static void wl_output_handle_mode(void *data, struct wl_output *output,
		uint32_t flags, int32_t width, int32_t height,
		int32_t refresh) {}
static void wl_output_handle_done(void *data, struct wl_output *output) {}
static void wl_output_handle_scale(void *data, struct wl_output *output,
		int32_t factor) {}
static void wl_output_handle_description(void *data, struct wl_output *output,
		const char *description) {}

static void wl_output_handle_name(void *data, struct wl_output *output,
		const char *name) {
	struct Monitor *m = data;

	snprintf(m->name, sizeof(m->name), "%s", name);
	ipc_update();
}

static const struct wl_output_listener wl_output_listener = {
	.geometry = wl_output_handle_geometry,
	.mode = wl_output_handle_mode,
	.done = wl_output_handle_done,
	.scale = wl_output_handle_scale,
	.name = wl_output_handle_name,
	.description = wl_output_handle_description,
};

static void output_handle_wl_output(void *data, struct river_output_v1 *obj,
		uint32_t name) {
	struct Monitor *m = data;

	if (m->wl_output != NULL)
		return;
	m->wl_output = wl_registry_bind(wl_registry, name,
			&wl_output_interface, 4);
	if (m->wl_output != NULL)
		wl_output_add_listener(m->wl_output, &wl_output_listener, m);
}

const struct river_output_v1_listener river_output_listener = {
	.removed = output_handle_removed,
	.wl_output = output_handle_wl_output,
	.position = output_handle_position,
	.dimensions = output_handle_dimensions,
};

static void ls_output_handle_non_exclusive_area(void *data,
		struct river_layer_shell_output_v1 *ls, int32_t x, int32_t y,
		int32_t width, int32_t height) {
	struct Monitor *m = data;
	m->windowed.x = x;
	m->windowed.y = y;
	m->windowed.width = width;
	m->windowed.height = height;
	manage_dirty();
}

const struct river_layer_shell_output_v1_listener ls_output_listener = {
	.non_exclusive_area = ls_output_handle_non_exclusive_area,
};

static void window_handle_closed(void *data, struct river_window_v1 *obj) {
	client_destroy(data);
}

static void window_handle_dimensions(void *data, struct river_window_v1 *obj,
		int32_t width, int32_t height) {
	struct Client *c = data;

	bool first = !c->geom_valid;
	c->geom_valid = true;
	c->geom.width = width;
	c->geom.height = height;

	if (first && c->isfloating && !c->fullscreen)
		center_client(c);
}

static void window_handle_app_id(void *data, struct river_window_v1 *obj,
		const char *app_id) {
	struct Client *c = data;
	free(c->app_id);
	c->app_id = app_id ? strdup(app_id) : NULL;
	applyrules(c);
}

static void window_handle_title(void *data, struct river_window_v1 *obj,
		const char *title) {
	struct Client *c = data;
	free(c->title);
	c->title = title ? strdup(title) : NULL;
	applyrules(c);
}

static void window_handle_fullscreen_requested(void *data,
		struct river_window_v1 *obj, struct river_output_v1 *output) {
	setfullscreen(data, true);
}

static void window_handle_exit_fullscreen_requested(void *data,
		struct river_window_v1 *obj) {
	setfullscreen(data, false);
}

// dwm ignores maximize: it is not advertised in set_capabilities.
static void window_handle_maximize_requested(void *data,
		struct river_window_v1 *obj) {}
static void window_handle_unmaximize_requested(void *data,
		struct river_window_v1 *obj) {}

// Ignored events
static void window_handle_decoration_hint(void *data,
		struct river_window_v1 *obj, uint32_t hint) {}
static void window_handle_dimensions_hint(void *data,
		struct river_window_v1 *obj, int32_t min_width, int32_t min_height,
		int32_t max_width, int32_t max_height) {}
static void window_handle_identifier(void *data, struct river_window_v1 *obj,
		const char *identifier) {}
static void window_handle_minimize_requested(void *data,
		struct river_window_v1 *obj) {}
static void window_handle_parent(void *data, struct river_window_v1 *obj,
		struct river_window_v1 *parent) {}
// Only relevant for client side decorations; we use server side decorations.
static void window_handle_pointer_move_requested(void *data,
		struct river_window_v1 *obj, struct river_seat_v1 *seat) {}
static void window_handle_pointer_resize_requested(void *data,
		struct river_window_v1 *obj, struct river_seat_v1 *seat,
		uint32_t edges) {}
static void window_handle_show_window_menu_requested(void *data,
		struct river_window_v1 *obj, int32_t x, int32_t y) {}
static void window_handle_unreliable_pid(void *data,
		struct river_window_v1 *obj, int32_t unreliable_pid) {}
static void window_handle_presentation_hint(void *data,
		struct river_window_v1 *obj, uint32_t hint) {}

const struct river_window_v1_listener river_window_listener = {
	.closed = window_handle_closed,
	.dimensions_hint = window_handle_dimensions_hint,
	.dimensions = window_handle_dimensions,
	.app_id = window_handle_app_id,
	.title = window_handle_title,
	.parent = window_handle_parent,
	.decoration_hint = window_handle_decoration_hint,
	.pointer_move_requested = window_handle_pointer_move_requested,
	.pointer_resize_requested = window_handle_pointer_resize_requested,
	.show_window_menu_requested = window_handle_show_window_menu_requested,
	.maximize_requested = window_handle_maximize_requested,
	.unmaximize_requested = window_handle_unmaximize_requested,
	.fullscreen_requested = window_handle_fullscreen_requested,
	.exit_fullscreen_requested = window_handle_exit_fullscreen_requested,
	.minimize_requested = window_handle_minimize_requested,
	.unreliable_pid = window_handle_unreliable_pid,
	.presentation_hint = window_handle_presentation_hint,
	.identifier = window_handle_identifier,
};

static void seat_handle_removed(void *data, struct river_seat_v1 *obj) {
	struct Seat *seat = data;

	remove_bindings(seat);
	river_layer_shell_seat_v1_destroy(seat->ls);
	river_seat_v1_destroy(seat->obj);
	wl_list_remove(&seat->link);
	free(seat);
}

// dwm is click-to-focus: pressing on a window focuses it and raises it.
static void seat_handle_window_interaction(void *data,
		struct river_seat_v1 *obj, struct river_window_v1 *river_window) {
	struct Client *c = river_window_v1_get_user_data(river_window);
	if (c == NULL || c->mon == NULL)
		return;
	selmon = c->mon;
	focus(c);
}

static void seat_handle_pointer_enter(void *data, struct river_seat_v1 *obj,
		struct river_window_v1 *river_window) {
	struct Seat *seat = data;
	if (focus_follows_pointer)
		seat->entered = river_window_v1_get_user_data(river_window);
}

static void seat_handle_pointer_position(void *data, struct river_seat_v1 *obj,
		int32_t x, int32_t y) {
	// dwm selects the monitor under the pointer.  Note that motion alone
	// never starts a manage sequence, so this only runs when something else
	// needs managing.
	struct Monitor *m = monitor_at(x, y);
	if (m != NULL)
		selmon = m;
}

static void seat_handle_op_delta(void *data, struct river_seat_v1 *obj,
		int32_t dx, int32_t dy) {
	struct Seat *seat = data;
	seat->op_dx = dx;
	seat->op_dy = dy;
}

static void seat_handle_op_release(void *data, struct river_seat_v1 *obj) {
	struct Seat *seat = data;
	seat->op_end_pending = true;
}

static void seat_handle_pointer_leave(void *data, struct river_seat_v1 *obj) {
	struct Seat *seat = data;
	seat->entered = NULL;
}
static void seat_handle_shell_surface_interaction(void *data,
		struct river_seat_v1 *obj,
		struct river_shell_surface_v1 *river_shell_surface) {}
static void seat_handle_wl_seat(void *data, struct river_seat_v1 *obj,
		uint32_t id) {}

const struct river_seat_v1_listener river_seat_listener = {
	.removed = seat_handle_removed,
	.wl_seat = seat_handle_wl_seat,
	.pointer_enter = seat_handle_pointer_enter,
	.pointer_leave = seat_handle_pointer_leave,
	.window_interaction = seat_handle_window_interaction,
	.shell_surface_interaction = seat_handle_shell_surface_interaction,
	.op_delta = seat_handle_op_delta,
	.op_release = seat_handle_op_release,
	.pointer_position = seat_handle_pointer_position,
};

static void ls_seat_handle_focus_exclusive(void *data,
		struct river_layer_shell_seat_v1 *ls) {
	struct Seat *seat = data;
	seat->ls_focused = true;
}

static void ls_seat_handle_focus_non_exclusive(void *data,
		struct river_layer_shell_seat_v1 *ls) {
	struct Seat *seat = data;
	seat->ls_focused = true;
}

static void ls_seat_handle_focus_none(void *data,
		struct river_layer_shell_seat_v1 *ls) {
	struct Seat *seat = data;
	seat->ls_focused = false;
}

const struct river_layer_shell_seat_v1_listener ls_seat_listener = {
	.focus_exclusive = ls_seat_handle_focus_exclusive,
	.focus_non_exclusive = ls_seat_handle_focus_non_exclusive,
	.focus_none = ls_seat_handle_focus_none,
};


// Window manager events

static void wm_handle_output(void *data, struct river_window_manager_v1 *obj,
		struct river_output_v1 *river_output) {
	struct river_layer_shell_output_v1 *ls =
		river_layer_shell_v1_get_output(layer_shell_v1, river_output);
	struct Monitor *m = monitor_create(river_output, ls);
	if (m == NULL)
		return;

	river_output_v1_add_listener(river_output, &river_output_listener, m);
	river_layer_shell_output_v1_add_listener(ls, &ls_output_listener, m);

	if (selmon == NULL)
		selmon = m;

	// Adopt clients that were left without an output.
	struct Client *c;
	wl_list_for_each(c, &wm.clients, link)
		if (c->mon == NULL) {
			c->mon = m;
			c->tags = m->tagset[m->seltags];
			c->fullscreen = false;
			center_client(c);
		}

	manage_dirty();
}

static void wm_handle_seat(void *data, struct river_window_manager_v1 *obj,
		struct river_seat_v1 *river_seat) {
	struct Seat *seat = calloc(1, sizeof(*seat));
	if (seat == NULL)
		return;

	seat->obj = river_seat;
	seat->ls = river_layer_shell_v1_get_seat(layer_shell_v1, river_seat);
	wl_list_init(&seat->xkb_bindings);
	wl_list_init(&seat->pointer_bindings);
	seat->bindings_dirty = true;

	wl_list_insert(&wm.seats, &seat->link);
	init_bindings(seat);
	river_seat_v1_add_listener(river_seat, &river_seat_listener, seat);
	river_layer_shell_seat_v1_add_listener(seat->ls, &ls_seat_listener, seat);
	manage_dirty();
}

static void wm_handle_window(void *data, struct river_window_manager_v1 *obj,
		struct river_window_v1 *river_window) {
	struct Client *c = client_create(river_window);
	if (c == NULL)
		return;
	river_window_v1_add_listener(river_window, &river_window_listener, c);
	applyrules(c);
	manage_dirty();
}

static void wm_handle_manage_start(void *data,
		struct river_window_manager_v1 *obj) {
	struct Client *c;
	struct Monitor *m;
	struct Seat *seat;

	wl_list_for_each(c, &wm.clients, link) {
		if (c->need_capabilities) {
			river_window_v1_set_capabilities(c->obj,
					RIVER_WINDOW_V1_CAPABILITIES_FULLSCREEN);
			c->need_capabilities = false;
		}
		if (c->need_ssd) {
			river_window_v1_use_ssd(c->obj);
			c->need_ssd = false;
		}
		if (c->need_close) {
			river_window_v1_close(c->obj);
			c->need_close = false;
		}
		if (c->need_fullscreen && c->mon != NULL) {
			river_window_v1_inform_fullscreen(c->obj);
			river_window_v1_fullscreen(c->obj, c->mon->obj);
			c->need_fullscreen = false;
		}
		if (c->need_exit_fullscreen) {
			river_window_v1_inform_not_fullscreen(c->obj);
			river_window_v1_exit_fullscreen(c->obj);
			c->need_exit_fullscreen = false;
			c->isfloating = c->wasfloating;
			// River leaves the geometry undefined after exit_fullscreen;
			// restore it in this same manage sequence.
			c->geom = c->oldgeom;
			c->pos_valid = false;
			c->proposed_width = -1;
			c->proposed_height = -1;
			if (c->isfloating && c->oldgeom.width > 0
					&& c->oldgeom.height > 0) {
				river_window_v1_propose_dimensions(c->obj,
						c->oldgeom.width,
						c->oldgeom.height);
				c->proposed_width = c->oldgeom.width;
				c->proposed_height = c->oldgeom.height;
			}
		}
		if (!c->rules_applied)
			applyrules(c);
	}

	// Apply interactive move/resize deltas.
	wl_list_for_each(seat, &wm.seats, link) {
		if (seat->op_mode == OpNone)
			continue;
		if (seat->op_start_pending) {
			river_seat_v1_op_start_pointer(seat->obj);
			seat->op_start_pending = false;
		}

		c = seat->op_client;
		if (c != NULL && (seat->op_dx != 0 || seat->op_dy != 0)) {
			if (seat->op_mode == OpMove) {
				c->geom.x = seat->op_geom.x + seat->op_dx;
				c->geom.y = seat->op_geom.y + seat->op_dy;
			} else if (seat->op_mode == OpResize) {
				c->geom.width = MAX(20, seat->op_geom.width + seat->op_dx);
				c->geom.height = MAX(20, seat->op_geom.height + seat->op_dy);
				if (c->geom.width != c->proposed_width
						|| c->geom.height != c->proposed_height) {
					river_window_v1_propose_dimensions(c->obj,
							c->geom.width, c->geom.height);
					c->proposed_width = c->geom.width;
					c->proposed_height = c->geom.height;
				}
			}
		}

		if (seat->op_end_pending) {
			river_seat_v1_op_end(seat->obj);
			seat->op_end_pending = false;
			seat->op_client = NULL;
			seat->op_mode = OpNone;
		}
	}

	// Sloppy focus: the window the pointer entered takes focus for this
	// manage sequence.  pointer_enter is always followed by manage_start.
	if (focus_follows_pointer) {
		wl_list_for_each(seat, &wm.seats, link)
			if (!seat->ls_focused && seat->op_mode == OpNone
					&& seat->entered != NULL
					&& seat->entered->mon != NULL)
				focus(seat->entered);
	}

	// Arrange every monitor; this also proposes dimensions.
	arrange_all();

	// Make sure every monitor has a sensible selection.
	wl_list_for_each(m, &wm.monitors, link)
		if (m->sel == NULL || !is_visible(m->sel) || m->sel->mon != m)
			m->sel = first_visible(m);

	// Make sure the selected client is visible and focused.
	if (selmon == NULL)
		selmon = first_monitor();
	focus(selmon != NULL ? selmon->sel : NULL);

	// Default layer shell output: only update when the selected monitor
	// changes, otherwise every window gets placed on it again.
	if (selmon != NULL && selmon != default_monitor) {
		river_layer_shell_output_v1_set_default(selmon->ls);
		default_monitor = selmon;
	}

	wl_list_for_each(seat, &wm.seats, link) {
		if (seat->bindings_dirty) {
			manage_bindings(seat);
			seat->bindings_dirty = false;
		}

		if (seat->ls_focused) {
			// A layer surface owns keyboard focus; focus requests are
			// ignored.  Forget what we last pushed so focus is restored
			// to the window once the surface goes away.
			seat->focus_client = NULL;
			seat->focus_cleared = false;
			seat->warp = false;
			seat->entered = NULL;
			continue;
		}

		if (selmon != NULL) {
			struct Client *sel = selmon->sel;
			if (sel != NULL) {
				if (seat->focus_client != sel) {
					river_seat_v1_focus_window(seat->obj,
							sel->obj);
					seat->focus_client = sel;
					seat->focus_cleared = false;
				}
				if (pointer_follows_focus && seat->warp) {
					struct Rect r = client_render_rect(sel);
					river_seat_v1_pointer_warp(seat->obj,
							r.x + r.width / 2,
							r.y + r.height / 2);
				}
			} else if (!seat->focus_cleared) {
				river_seat_v1_clear_focus(seat->obj);
				seat->focus_client = NULL;
				seat->focus_cleared = true;
			}
		}

		seat->warp = false;
		seat->entered = NULL;
	}

	river_window_manager_v1_manage_finish(window_manager_v1);
	ipc_update();
}

static void wm_handle_render_start(void *data,
		struct river_window_manager_v1 *obj) {
	render();
	river_window_manager_v1_render_finish(window_manager_v1);
}

static void wm_handle_finished(void *data,
		struct river_window_manager_v1 *obj) {
	river_window_manager_v1_destroy(window_manager_v1);
	exit(0);
}

static void wm_handle_unavailable(void *data,
		struct river_window_manager_v1 *obj) {
	fprintf(stderr, "window management unavailable (is another WM running?)\n");
	exit(1);
}

static void wm_handle_session_locked(void *data,
		struct river_window_manager_v1 *obj) {
	struct Seat *seat;
	wl_list_for_each(seat, &wm.seats, link)
		lock_bindings(seat);
}

static void wm_handle_session_unlocked(void *data,
		struct river_window_manager_v1 *obj) {
	struct Seat *seat;
	wl_list_for_each(seat, &wm.seats, link)
		unlock_bindings(seat);
}

static const struct river_window_manager_v1_listener wm_listener = {
	.unavailable = wm_handle_unavailable,
	.finished = wm_handle_finished,
	.manage_start = wm_handle_manage_start,
	.render_start = wm_handle_render_start,
	.session_locked = wm_handle_session_locked,
	.session_unlocked = wm_handle_session_unlocked,
	.window = wm_handle_window,
	.output = wm_handle_output,
	.seat = wm_handle_seat,
};


// Startup

static void wm_init(void) {
	unsetenv("WAYLAND_DEBUG");

	struct sigaction sa;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	// Clean up inherited zombie children, as dwm does.
	while (waitpid(-1, NULL, WNOHANG) > 0);

	wl_list_init(&wm.monitors);
	wl_list_init(&wm.clients);
	wl_list_init(&wm.stack);
	wl_list_init(&wm.seats);
}

static void handle_global(void *data, struct wl_registry *registry,
		uint32_t name, const char *interface, uint32_t version) {
	if (strcmp(interface, river_window_manager_v1_interface.name) == 0
			&& version >= 4) {
		window_manager_v1 = wl_registry_bind(registry, name,
				&river_window_manager_v1_interface, 4);
	} else if (strcmp(interface, river_xkb_bindings_v1_interface.name) == 0) {
		xkb_bindings_v1 = wl_registry_bind(registry, name,
				&river_xkb_bindings_v1_interface, 1);
	} else if (strcmp(interface, river_layer_shell_v1_interface.name) == 0) {
		layer_shell_v1 = wl_registry_bind(registry, name,
				&river_layer_shell_v1_interface, 1);
	} else if (strcmp(interface, river_libinput_config_v1_interface.name) == 0) {
		init_input_config(registry, name);
	}
}

static void handle_global_remove(void *data, struct wl_registry *registry,
		uint32_t name) {}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

int main(void) {
	struct wl_display *display = wl_display_connect(NULL);
	if (display == NULL) {
		fprintf(stderr, "failed to connect to Wayland server\n");
		return 1;
	}

	struct wl_registry *registry = wl_display_get_registry(display);
	wl_registry = registry;
	wl_registry_add_listener(registry, &registry_listener, NULL);
	if (wl_display_roundtrip(display) < 0) {
		fprintf(stderr, "roundtrip failed\n");
		return 1;
	}

	if (window_manager_v1 == NULL || xkb_bindings_v1 == NULL
			|| layer_shell_v1 == NULL) {
		fprintf(stderr, "required protocol not supported by the server\n");
		return 1;
	}

	wm_init();
	ipc_init();
	river_window_manager_v1_add_listener(window_manager_v1, &wm_listener, NULL);

	struct pollfd fds[16];

	while (true) {
		// The usual Wayland prepare_read dance, so we can poll the IPC
		// socket at the same time without a second thread.
		while (wl_display_prepare_read(display) != 0)
			if (wl_display_dispatch_pending(display) < 0) {
				fprintf(stderr, "dispatch failed\n");
				return 1;
			}

		if (wl_display_flush(display) < 0 && errno != EAGAIN) {
			wl_display_cancel_read(display);
			fprintf(stderr, "flush failed\n");
			return 1;
		}

		int nfds = 1;
		fds[0].fd = wl_display_get_fd(display);
		fds[0].events = POLLIN;
		nfds = ipc_add_pollfds(fds, nfds, LENGTH(fds));

		if (poll(fds, nfds, -1) < 0) {
			wl_display_cancel_read(display);
			if (errno == EINTR)
				continue;
			fprintf(stderr, "poll failed\n");
			return 1;
		}

		if (fds[0].revents & POLLIN) {
			if (wl_display_read_events(display) < 0) {
				fprintf(stderr, "read failed\n");
				return 1;
			}
			if (wl_display_dispatch_pending(display) < 0) {
				fprintf(stderr, "dispatch failed\n");
				return 1;
			}
		} else {
			wl_display_cancel_read(display);
		}

		ipc_handle_pollfds(fds, nfds);
	}
}
