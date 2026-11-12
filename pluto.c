/* See LICENSE file for copyright and license details. */
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <river-layer-shell-v1.h>
#include <river-libinput-config-v1.h>
#include <river-window-management-v1.h>
#include <river-xkb-bindings-v1.h>

/* macros */
#define LENGTH(X)		(sizeof (X) / sizeof (X)[0])
#define MAX(A, B)		((A) > (B) ? (A) : (B))
#define MIN(A, B)		((A) < (B) ? (A) : (B))
#define TAGMASK			((1 << LENGTH(tags)) - 1)
#define ISVISIBLE(C)		((C)->mon && ((C)->tags & \
				(C)->mon->tagset[(C)->mon->seltags]))
#define EDGES_ALL		(RIVER_WINDOW_V1_EDGES_TOP | \
				RIVER_WINDOW_V1_EDGES_BOTTOM | \
				RIVER_WINDOW_V1_EDGES_LEFT | \
				RIVER_WINDOW_V1_EDGES_RIGHT)

/* types */
typedef struct Monitor Monitor;
typedef struct Client Client;
typedef struct Seat Seat;

typedef struct {
	int32_t x, y, width, height;
} Rect;

typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

typedef struct {
	const char *symbol;
	void (*arrange)(Monitor *);
} Layout;

typedef struct {
	unsigned int mod;
	xkb_keysym_t key;
	void (*func)(const Arg *);
	const Arg arg;
} Key;

typedef struct {
	unsigned int mod;
	unsigned int button;
	void (*func)(const Arg *);
	const Arg arg;
} Button;

typedef struct {
	const char *app_id;	/* NULL matches anything */
	const char *title;	/* NULL matches anything */
	unsigned int tags;	/* 0 means "keep current tags" */
	int isfloating;
	int monitor;		/* -1 means "keep current monitor" */
} Rule;

struct Client {
	/* Two orders, as in dwm: clients decides the tiling order (head is the
	 * master), stack decides focus order (head is the most recently focused).
	 * Focusing must never change the layout, only zoom() may do that. */
	struct wl_list link;	/* clients */
	struct wl_list slink;	/* stack */
	struct river_window_v1 *obj;
	struct river_node_v1 *node;

	Monitor *mon;
	unsigned int tags;

	int isfloating;
	int wasfloating;
	int fullscreen;
	int geom_valid;
	int rules_applied;
	int has_parent;

	Rect geom;		/* content geometry in logical coordinates */
	Rect oldgeom;		/* geometry to restore when leaving fullscreen */
	Rect slot;		/* tiled slot (content + border) */
	int bw;

	char *app_id;
	char *title;

	/* Work deferred to the next manage sequence. */
	int need_capabilities;
	int need_ssd;
	int need_close;
	int need_fullscreen;
	int need_exit_fullscreen;
	int tiled;		/* last value passed to set_tiled, -1 = unknown */
	int32_t proposed_width;
	int32_t proposed_height;

	/* Cached rendering state, to avoid flooding the compositor. */
	int shown;
	int pos_valid;
	int32_t pos_x, pos_y;
	int border_valid;
	int border_focused;
	int border_width_set;
};

struct Monitor {
	struct wl_list link;	/* monitors, ordered by position */
	struct river_output_v1 *obj;
	struct river_layer_shell_output_v1 *ls;

	Rect real;		/* full output geometry */
	Rect windowed;		/* non-exclusive area (excludes bars, etc.) */

	unsigned int tagset[2];
	unsigned int seltags;
	float mfact;
	int nmaster;
	unsigned int sellt;
	const Layout *lt[2];

	Client *sel;
};

struct Seat {
	struct wl_list link;	/* seats */
	struct river_seat_v1 *obj;
	struct river_layer_shell_seat_v1 *ls;
	struct wl_list xkb_bindings;
	struct wl_list pointer_bindings;

	Client *entered;	/* window under the pointer */
	int ls_focused;		/* a layer surface (e.g. launcher) has focus */
	int locked;		/* session is locked: bindings are disabled */
	int bindings_dirty;

	/* Last value passed to river_seat_v1_focus_window. */
	Client *focus_client;
	int focus_cleared;

	/* Interactive move/resize, driven by op_delta events. */
	Client *op_client;
	int op_mode;
	int op_float_pending;
	int op_start_pending;
	int op_end_pending;
	int32_t op_dx, op_dy;
	Rect op_geom;
};

enum {
	OpNone,
	OpMove,
	OpResize,
};

/* variables */
static struct wl_display *display;
static struct wl_list monitors;
static struct wl_list clients;
static struct wl_list stack;
static struct wl_list seats;
static Monitor *selmon;
static Seat *current_seat;
static Monitor *default_monitor;

static struct river_window_manager_v1 *window_manager_v1;
static struct river_xkb_bindings_v1 *xkb_bindings_v1;
static struct river_layer_shell_v1 *layer_shell_v1;

/* function declarations */
static void die(const char *fmt, ...);
static void *ecalloc(size_t nmemb, size_t size);
static char *estrdup(const char *s);

static Monitor *monitor_at(int32_t x, int32_t y);
static void reorder_monitor(Monitor *m);
static Monitor *first_monitor(void);
static Monitor *next_monitor(Monitor *m, int dir);
static Client *nexttiled(Monitor *m, Client *c);
static Client *first_visible(Monitor *m);
static Client *visible_fullscreen(Monitor *m);
static Rect client_render_rect(Client *c);
static void center_client(Client *c);

static void manage_dirty(void);
static void arrange(Monitor *m);
static void arrange_all(void);
static void focus(Client *c);
static void restack(void);
static void render(void);
static void applyrules(Client *c);

static void place_tiled(Client *c, Rect r);
static void tile(Monitor *m);
static void monocle(Monitor *m);

static void setfullscreen(Client *c, int fullscreen);
static void spawn(const Arg *arg);
static void quit(const Arg *arg);
static void killclient(const Arg *arg);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void incnmaster(const Arg *arg);
static void zoom(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglefullscreen(const Arg *arg);
static void focusstack(const Arg *arg);
static void focusmon(const Arg *arg);
static void sendmon(Client *c, Monitor *m);
static void tagmon(const Arg *arg);
static void view(const Arg *arg);
static void tag(const Arg *arg);
static void toggleview(const Arg *arg);
static void toggletag(const Arg *arg);
static void movemouse(const Arg *arg);
static void resizemouse(const Arg *arg);

static void start_op(Client *c, int mode);
static void op_cancel(Seat *seat);
static void init_bindings(Seat *seat);
static void manage_bindings(Seat *seat);
static void remove_bindings(Seat *seat);
static void lock_bindings(Seat *seat);
static void unlock_bindings(Seat *seat);

static void init_input_config(struct wl_registry *registry, uint32_t name);

static Client *client_create(struct river_window_v1 *obj);
static void client_destroy(Client *c);
static Monitor *monitor_create(struct river_output_v1 *obj,
		struct river_layer_shell_output_v1 *ls);
static void monitor_destroy(Monitor *m);

/* configuration, allows nested code to access above variables */
#include "config.h"

/* compile-time check if all tags fit into an unsigned int bit array */
struct NumTags { char limitexceeded[LENGTH(tags) > 31 ? -1 : 1]; };

/* function implementations */
static void
die(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (fmt[0] && fmt[strlen(fmt) - 1] == ':') {
		fputc(' ', stderr);
		perror(NULL);
	} else {
		fputc('\n', stderr);
	}

	exit(1);
}

static void *
ecalloc(size_t nmemb, size_t size)
{
	void *p;

	if (!(p = calloc(nmemb, size)))
		die("pluto: calloc:");
	return p;
}

static char *
estrdup(const char *s)
{
	char *p;

	if (!(p = strdup(s)))
		die("pluto: strdup:");
	return p;
}

static Monitor *
monitor_at(int32_t x, int32_t y)
{
	Monitor *m;

	wl_list_for_each(m, &monitors, link)
		if (m->real.x <= x && m->real.x + m->real.width > x
				&& m->real.y <= y
				&& m->real.y + m->real.height > y)
			return m;
	return NULL;
}

/* Keep the monitor list ordered left to right, top to bottom. */
static void
reorder_monitor(Monitor *m)
{
	struct wl_list *pos;

	wl_list_remove(&m->link);
	for (pos = monitors.next; pos != &monitors; pos = pos->next) {
		Monitor *o = wl_container_of(pos, o, link);

		if (m->real.x != o->real.x) {
			if (m->real.x < o->real.x)
				break;
		} else if (m->real.y < o->real.y) {
			break;
		}
	}
	/* wl_list_insert() puts m after prev, i.e. before pos. */
	wl_list_insert(pos->prev, &m->link);
}

static Monitor *
first_monitor(void)
{
	Monitor *m;

	if (wl_list_empty(&monitors))
		return NULL;
	m = wl_container_of(monitors.next, m, link);
	return m;
}

static Monitor *
next_monitor(Monitor *m, int dir)
{
	Monitor *r;

	if (!m || wl_list_empty(&monitors))
		return NULL;
	if (dir > 0)
		r = m->link.next == &monitors
			? wl_container_of(monitors.next, r, link)
			: wl_container_of(m->link.next, r, link);
	else
		r = m->link.prev == &monitors
			? wl_container_of(monitors.prev, r, link)
			: wl_container_of(m->link.prev, r, link);
	return r;
}

static Client *
nexttiled(Monitor *m, Client *c)
{
	struct wl_list *pos = c ? c->link.next : clients.next;

	for (; pos != &clients; pos = pos->next) {
		Client *w = wl_container_of(pos, w, link);
		if (w->mon == m && ISVISIBLE(w) && !w->isfloating && !w->fullscreen)
			return w;
	}
	return NULL;
}

static Client *
first_visible(Monitor *m)
{
	Client *c;

	if (!m)
		return NULL;
	wl_list_for_each(c, &stack, slink)
		if (c->mon == m && ISVISIBLE(c))
			return c;
	return NULL;
}

static Client *
visible_fullscreen(Monitor *m)
{
	Client *c;

	wl_list_for_each(c, &stack, slink)
		if (c->mon == m && ISVISIBLE(c) && c->fullscreen)
			return c;
	return NULL;
}

/* The rectangle a client's content is actually drawn at.  For tiled clients
 * the slot assigned by arrange is authoritative, but the compositor may give
 * the window slightly different dimensions (e.g. terminals round down to a
 * whole number of cells), so center the content in its slot like dwm does
 * with size hints. */
static Rect
client_render_rect(Client *c)
{
	Rect r;

	if (c->isfloating || c->fullscreen || !c->mon
			|| !c->mon->lt[c->mon->sellt]
			|| !c->mon->lt[c->mon->sellt]->arrange
			|| c->slot.width <= 0)
		return c->geom;

	r = c->slot;
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

static void
center_client(Client *c)
{
	if (!c->mon)
		return;
	c->geom.x = c->mon->windowed.x
		+ (c->mon->windowed.width - c->geom.width) / 2;
	c->geom.y = c->mon->windowed.y
		+ (c->mon->windowed.height - c->geom.height) / 2;
	c->geom.x = MAX(c->geom.x, c->mon->windowed.x);
	c->geom.y = MAX(c->geom.y, c->mon->windowed.y);
}

static void
arrange(Monitor *m)
{
	Client *c;

	if (!m)
		return;

	/* Tell windows whether they are part of a tiled layout.  This is window
	 * management state and may only change during a manage sequence. */
	wl_list_for_each(c, &clients, link) {
		int edges;

		if (c->mon != m)
			continue;
		edges = (!m->lt[m->sellt] || !m->lt[m->sellt]->arrange
				|| c->isfloating || c->fullscreen) ? 0 : EDGES_ALL;
		if (c->tiled != edges) {
			river_window_v1_set_tiled(c->obj, edges);
			c->tiled = edges;
		}
	}

	if (m->lt[m->sellt] && m->lt[m->sellt]->arrange)
		m->lt[m->sellt]->arrange(m);
}

static void
arrange_all(void)
{
	Monitor *m;

	wl_list_for_each(m, &monitors, link)
		arrange(m);
}

/* Most river events are followed by a manage_start, but key and pointer
 * bindings are not.  Ask for one after a binding changed our own state. */
static void
manage_dirty(void)
{
	if (window_manager_v1)
		river_window_manager_v1_manage_dirty(window_manager_v1);
}

/* dwm's focus(): pick a visible client, make it selmon's selection and move it
 * to the top of the focus stack.  The tiling order is deliberately left alone:
 * focusing a window must not turn it into the master (only zoom does that). */
static void
focus(Client *c)
{
	Client *fs;

	if (!selmon)
		return;

	if (!c || !ISVISIBLE(c))
		c = first_visible(selmon);

	if (lockfullscreen && (fs = visible_fullscreen(selmon)))
		c = fs;

	if (c) {
		int changed = c != selmon->sel || c->mon != selmon;
		selmon = c->mon;
		selmon->sel = c;
		if (changed) {
			wl_list_remove(&c->slink);
			wl_list_insert(&stack, &c->slink);
		}
	} else {
		selmon->sel = NULL;
	}
}

/* Cached restack order, so an unchanged order does not issue place_top calls
 * on every frame. -1 means "unknown, restack unconditionally". */
static Client *stack_cache[256];
static int stack_cache_len = -1;

static void
restack(void)
{
	Client *order[256];
	Client *c;
	int n = 0;

	/* Bottom to top: tiled clients, then floating ones (which stay above the
	 * tiled layout), each group in focus order. */
	wl_list_for_each_reverse(c, &stack, slink) {
		if (n >= (int)LENGTH(order))
			break;
		if (ISVISIBLE(c) && !c->isfloating)
			order[n++] = c;
	}
	wl_list_for_each_reverse(c, &stack, slink) {
		if (n >= (int)LENGTH(order))
			break;
		if (ISVISIBLE(c) && c->isfloating)
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

static void
render(void)
{
	Client *c;

	wl_list_for_each(c, &clients, link) {
		int bw, focused, want = ISVISIBLE(c);
		Rect r;

		if (c->shown != want) {
			if (want)
				river_window_v1_show(c->obj);
			else
				river_window_v1_hide(c->obj);
			c->shown = want;
		}
		if (!want)
			continue;

		if (!c->fullscreen) {
			r = client_render_rect(c);
			if (!c->pos_valid || r.x != c->pos_x
					|| r.y != c->pos_y) {
				river_node_v1_set_position(c->node, r.x, r.y);
				c->pos_x = r.x;
				c->pos_y = r.y;
				c->pos_valid = 1;
			}
		}

		/* dwm removes the border in fullscreen. */
		bw = c->fullscreen ? 0 : c->bw;
		focused = c->mon == selmon && c->mon->sel == c;
		if (!c->border_valid || c->border_width_set != bw
				|| c->border_focused != focused) {
			const uint32_t *color = focused
				? border_color_focus : border_color_normal;
			river_window_v1_set_borders(c->obj, EDGES_ALL, bw,
					color[0], color[1], color[2], color[3]);
			c->border_width_set = bw;
			c->border_focused = focused;
			c->border_valid = 1;
		}
	}

	restack();
}

static void
applyrules(Client *c)
{
	const Rule *r;
	unsigned int i;

	if (c->rules_applied)
		return;

	c->isfloating = 0;
	c->tags = 0;

	for (i = 0; i < LENGTH(rules); i++) {
		r = &rules[i];
		if (r->title && (!c->title || !strstr(c->title, r->title)))
			continue;
		if (r->app_id && (!c->app_id || !strstr(c->app_id, r->app_id)))
			continue;

		c->isfloating = r->isfloating;
		c->tags |= r->tags;
		if (r->monitor >= 0) {
			Monitor *m = first_monitor();
			for (int j = 0; m && j < r->monitor; j++)
				m = next_monitor(m, 1);
			if (m)
				c->mon = m;
		}
	}

	if (!(c->tags & TAGMASK))
		c->tags = c->mon
			? c->mon->tagset[c->mon->seltags] : 1;
	c->isfloating = c->isfloating || c->has_parent;
	c->rules_applied = 1;

	if (c->isfloating && c->geom_valid)
		center_client(c);
}

/* Give a client the given slot (content + border) and, if necessary, propose
 * new content dimensions to the compositor. */
static void
place_tiled(Client *c, Rect slot)
{
	int32_t width, height;

	c->slot = slot;
	c->bw = borderpx;

	width = MAX(0, slot.width - 2 * c->bw);
	height = MAX(0, slot.height - 2 * c->bw);

	if (width != c->proposed_width || height != c->proposed_height) {
		river_window_v1_propose_dimensions(c->obj, width, height);
		c->proposed_width = width;
		c->proposed_height = height;
	}

	/* Remember where the content is so toggling to floating does not make
	 * the window jump to the top left corner. */
	c->geom.x = slot.x + c->bw + (width - c->geom.width) / 2;
	c->geom.y = slot.y + c->bw + (height - c->geom.height) / 2;
}

/* dwm's tile(): nmaster clients stacked in the master area on the left, the
 * rest stacked in the remaining area on the right. */
static void
tile(Monitor *m)
{
	unsigned int i, n, h, mw, my, ty;
	Client *c;

	for (n = 0, c = nexttiled(m, NULL); c; c = nexttiled(m, c), n++)
		;
	if (n == 0)
		return;

	if (n > (unsigned int)m->nmaster)
		mw = m->nmaster ? m->windowed.width * m->mfact : 0;
	else
		mw = m->windowed.width;

	for (i = my = ty = 0, c = nexttiled(m, NULL); c;
			c = nexttiled(m, c), i++) {
		if (i < (unsigned int)m->nmaster) {
			h = (m->windowed.height - my)
				/ (MIN(n, (unsigned int)m->nmaster) - i);
			place_tiled(c, (Rect){
				m->windowed.x,
				m->windowed.y + my,
				mw,
				h,
			});
			if (my + h < (unsigned int)m->windowed.height)
				my += h;
		} else {
			h = (m->windowed.height - ty) / (n - i);
			place_tiled(c, (Rect){
				m->windowed.x + mw,
				m->windowed.y + ty,
				m->windowed.width - mw,
				h,
			});
			if (ty + h < (unsigned int)m->windowed.height)
				ty += h;
		}
	}
}

/* dwm's monocle(): every tiled client gets the whole work area. */
static void
monocle(Monitor *m)
{
	Client *c;

	for (c = nexttiled(m, NULL); c; c = nexttiled(m, c))
		place_tiled(c, m->windowed);
}

static void
setfullscreen(Client *c, int fullscreen)
{
	if (!c || !c->mon || c->fullscreen == fullscreen)
		return;

	if (fullscreen) {
		c->wasfloating = c->isfloating;
		c->oldgeom = client_render_rect(c);
		c->isfloating = 1;
		c->fullscreen = 1;
		c->need_fullscreen = 1;
	} else {
		c->fullscreen = 0;
		c->need_exit_fullscreen = 1;
		c->isfloating = c->wasfloating;
		c->proposed_width = -1;
		c->proposed_height = -1;
	}
}

/* commands */
static void
spawn(const Arg *arg)
{
	struct sigaction sa;

	if (!arg || !arg->v)
		return;

	if (fork() == 0) {
		if (display)
			close(wl_display_get_fd(display));
		setsid();

		sigemptyset(&sa.sa_mask);
		sa.sa_flags = 0;
		sa.sa_handler = SIG_DFL;
		sigaction(SIGCHLD, &sa, NULL);

		execvp(((char *const *)arg->v)[0], (char *const *)arg->v);
		die("pluto: execvp '%s' failed:", ((char *const *)arg->v)[0]);
	}
}

static void
quit(const Arg *arg)
{
	if (window_manager_v1)
		river_window_manager_v1_exit_session(window_manager_v1);
}

static void
killclient(const Arg *arg)
{
	if (selmon && selmon->sel)
		selmon->sel->need_close = 1;
}

static void
setlayout(const Arg *arg)
{
	if (!selmon)
		return;

	if (!arg || !arg->v || arg->v != selmon->lt[selmon->sellt])
		selmon->sellt ^= 1;
	if (arg && arg->v)
		selmon->lt[selmon->sellt] = (const Layout *)arg->v;
}

/* arg > 1.0 will set mfact absolutely */
static void
setmfact(const Arg *arg)
{
	float f;

	if (!selmon || !arg || !selmon->lt[selmon->sellt]->arrange)
		return;
	f = arg->f < 1.0f ? arg->f + selmon->mfact : arg->f - 1.0f;
	if (f < 0.05f || f > 0.95f)
		return;
	selmon->mfact = f;
}

static void
incnmaster(const Arg *arg)
{
	if (!selmon || !arg)
		return;
	selmon->nmaster = MAX(selmon->nmaster + arg->i, 0);
}

/* dwm's zoom(): swap the focused client with the master by moving it to the
 * top of the tiling order. */
static void
zoom(const Arg *arg)
{
	Client *c;

	if (!selmon || !(c = selmon->sel))
		return;
	if (!selmon->lt[selmon->sellt]->arrange || c->isfloating)
		return;
	if (c == nexttiled(selmon, NULL) && !(c = nexttiled(selmon, c)))
		return;

	wl_list_remove(&c->link);
	wl_list_insert(&clients, &c->link);
	focus(c);
}

static void
togglefloating(const Arg *arg)
{
	Client *c;

	if (!selmon || !(c = selmon->sel) || c->fullscreen)
		return;

	if (!c->isfloating) {
		c->geom = client_render_rect(c);
		if (c->geom.width <= 0 || c->geom.height <= 0)
			center_client(c);
		c->isfloating = 1;
	} else {
		c->isfloating = 0;
		c->proposed_width = -1;
		c->proposed_height = -1;
	}
}

static void
togglefullscreen(const Arg *arg)
{
	if (!selmon || !selmon->sel)
		return;
	setfullscreen(selmon->sel, !selmon->sel->fullscreen);
}

static void
focusstack(const Arg *arg)
{
	Client *c = NULL;
	struct wl_list *pos;

	if (!selmon || !selmon->sel)
		return;
	if (selmon->sel->fullscreen && lockfullscreen)
		return;

	if (arg->i > 0) {
		for (pos = selmon->sel->link.next; pos != &clients;
				pos = pos->next) {
			Client *w = wl_container_of(pos, w, link);
			if (w->mon == selmon && ISVISIBLE(w)) {
				c = w;
				break;
			}
		}
		if (!c)
			for (pos = clients.next; pos != &clients; pos = pos->next) {
				Client *w = wl_container_of(pos, w, link);
				if (w->mon == selmon && ISVISIBLE(w)) {
					c = w;
					break;
				}
			}
	} else {
		for (pos = selmon->sel->link.prev; pos != &clients;
				pos = pos->prev) {
			Client *w = wl_container_of(pos, w, link);
			if (w->mon == selmon && ISVISIBLE(w)) {
				c = w;
				break;
			}
		}
		if (!c)
			for (pos = clients.prev; pos != &clients; pos = pos->prev) {
				Client *w = wl_container_of(pos, w, link);
				if (w->mon == selmon && ISVISIBLE(w)) {
					c = w;
					break;
				}
			}
	}

	if (c)
		focus(c);
}

static void
focusmon(const Arg *arg)
{
	Monitor *m;

	if (wl_list_length(&monitors) < 2)
		return;
	if ((m = next_monitor(selmon, arg->i)) == selmon)
		return;
	selmon = m;
	focus(NULL);
}

static void
sendmon(Client *c, Monitor *m)
{
	Monitor *old = c->mon;

	if (old == m)
		return;

	if (c->fullscreen) {
		c->fullscreen = 0;
		c->need_exit_fullscreen = 1;
		c->isfloating = c->wasfloating;
	}

	wl_list_remove(&c->link);
	wl_list_insert(&clients, &c->link);
	wl_list_remove(&c->slink);
	wl_list_insert(&stack, &c->slink);

	c->mon = m;
	c->tags = m->tagset[m->seltags];
	c->proposed_width = -1;
	c->proposed_height = -1;
	c->pos_valid = 0;

	if (selmon == old)
		focus(NULL);
}

static void
tagmon(const Arg *arg)
{
	if (!selmon || !selmon->sel || wl_list_length(&monitors) < 2)
		return;
	sendmon(selmon->sel, next_monitor(selmon, arg->i));
}

static void
view(const Arg *arg)
{
	if (!selmon)
		return;
	if ((arg->ui & TAGMASK) == selmon->tagset[selmon->seltags])
		return;
	selmon->seltags ^= 1;
	if (arg->ui & TAGMASK)
		selmon->tagset[selmon->seltags] = arg->ui & TAGMASK;
	focus(NULL);
}

static void
tag(const Arg *arg)
{
	if (!selmon || !selmon->sel || !(arg->ui & TAGMASK))
		return;
	selmon->sel->tags = arg->ui & TAGMASK;
	focus(NULL);
}

static void
toggleview(const Arg *arg)
{
	unsigned int newtagset;

	if (!selmon)
		return;
	newtagset = selmon->tagset[selmon->seltags] ^ (arg->ui & TAGMASK);
	if (newtagset) {
		selmon->tagset[selmon->seltags] = newtagset;
		focus(NULL);
	}
}

static void
toggletag(const Arg *arg)
{
	unsigned int newtags;

	if (!selmon || !selmon->sel)
		return;
	newtags = selmon->sel->tags ^ (arg->ui & TAGMASK);
	if (newtags) {
		selmon->sel->tags = newtags;
		focus(NULL);
	}
}

/* interactive move/resize */
static void
start_op(Client *c, int mode)
{
	Seat *seat = current_seat;

	if (!seat || seat->op_mode != OpNone)
		return;

	seat->op_client = c;
	seat->op_mode = mode;
	seat->op_geom = c->isfloating ? c->geom : client_render_rect(c);
	/* dwm only tears a tiled window out of the layout after the pointer has
	 * moved far enough; in the floating layout there is no layout to leave. */
	seat->op_float_pending = !c->isfloating && !c->fullscreen
			&& c->mon && c->mon->lt[c->mon->sellt]
			&& c->mon->lt[c->mon->sellt]->arrange;
	seat->op_dx = 0;
	seat->op_dy = 0;
	seat->op_start_pending = 1;
	seat->op_end_pending = 0;
}

static void
movemouse(const Arg *arg)
{
	Client *c;

	if (!selmon || !(c = selmon->sel) || c->fullscreen)
		return;

	start_op(c, OpMove);
}

static void
resizemouse(const Arg *arg)
{
	Client *c;

	if (!selmon || !(c = selmon->sel) || c->fullscreen)
		return;

	start_op(c, OpResize);
}

static void
op_cancel(Seat *seat)
{
	seat->op_client = NULL;
	seat->op_float_pending = 0;
	seat->op_start_pending = 0;
	seat->op_end_pending = 1;
	seat->op_dx = 0;
	seat->op_dy = 0;
}

/* binding plumbing */
struct XkbBinding {
	struct wl_list link;	/* Seat.xkb_bindings */
	struct river_xkb_binding_v1 *obj;
	Seat *seat;
	void (*func)(const Arg *);
	Arg arg;
	int enabled;
};

struct PointerBinding {
	struct wl_list link;	/* Seat.pointer_bindings */
	struct river_pointer_binding_v1 *obj;
	Seat *seat;
	void (*func)(const Arg *);
	Arg arg;
	int enabled;
};

static void
xkb_binding_handle_pressed(void *data, struct river_xkb_binding_v1 *obj)
{
	struct XkbBinding *b = data;

	current_seat = b->seat;
	if (b->func)
		b->func(&b->arg);
	manage_dirty();
}

static void
xkb_binding_handle_released(void *data, struct river_xkb_binding_v1 *obj)
{
}

static void
xkb_binding_handle_stop_repeat(void *data, struct river_xkb_binding_v1 *obj)
{
}

static const struct river_xkb_binding_v1_listener xkb_binding_listener = {
	.pressed = xkb_binding_handle_pressed,
	.released = xkb_binding_handle_released,
	.stop_repeat = xkb_binding_handle_stop_repeat,
};

static void
pointer_binding_handle_pressed(void *data,
		struct river_pointer_binding_v1 *obj)
{
	struct PointerBinding *b = data;

	current_seat = b->seat;
	if (b->func)
		b->func(&b->arg);
	manage_dirty();
}

static void
pointer_binding_handle_released(void *data,
		struct river_pointer_binding_v1 *obj)
{
}

static const struct river_pointer_binding_v1_listener pointer_binding_listener = {
	.pressed = pointer_binding_handle_pressed,
	.released = pointer_binding_handle_released,
};

static void
init_bindings(Seat *seat)
{
	unsigned int i;

	for (i = 0; i < LENGTH(keys); i++) {
		struct XkbBinding *b = ecalloc(1, sizeof(*b));

		b->obj = river_xkb_bindings_v1_get_xkb_binding(xkb_bindings_v1,
				seat->obj, keys[i].key, keys[i].mod);
		b->seat = seat;
		b->func = keys[i].func;
		b->arg = keys[i].arg;
		river_xkb_binding_v1_add_listener(b->obj,
				&xkb_binding_listener, b);
		wl_list_insert(&seat->xkb_bindings, &b->link);
	}

	for (i = 0; i < LENGTH(buttons); i++) {
		struct PointerBinding *b = ecalloc(1, sizeof(*b));

		b->obj = river_seat_v1_get_pointer_binding(seat->obj,
				buttons[i].button, buttons[i].mod);
		b->seat = seat;
		b->func = buttons[i].func;
		b->arg = buttons[i].arg;
		river_pointer_binding_v1_add_listener(b->obj,
				&pointer_binding_listener, b);
		wl_list_insert(&seat->pointer_bindings, &b->link);
	}
}

static void
manage_bindings(Seat *seat)
{
	struct XkbBinding *xb;
	struct PointerBinding *pb;

	wl_list_for_each(xb, &seat->xkb_bindings, link) {
		if (seat->locked && xb->enabled) {
			river_xkb_binding_v1_disable(xb->obj);
			xb->enabled = 0;
		} else if (!seat->locked && !xb->enabled) {
			river_xkb_binding_v1_enable(xb->obj);
			xb->enabled = 1;
		}
	}
	wl_list_for_each(pb, &seat->pointer_bindings, link) {
		if (seat->locked && pb->enabled) {
			river_pointer_binding_v1_disable(pb->obj);
			pb->enabled = 0;
		} else if (!seat->locked && !pb->enabled) {
			river_pointer_binding_v1_enable(pb->obj);
			pb->enabled = 1;
		}
	}
}

static void
remove_bindings(Seat *seat)
{
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

static void
lock_bindings(Seat *seat)
{
	seat->locked = 1;
	seat->bindings_dirty = 1;
}

static void
unlock_bindings(Seat *seat)
{
	seat->locked = 0;
	seat->bindings_dirty = 1;
}

/* libinput device configuration
 *
 * River deliberately delegates input device configuration to the window
 * manager.  We only touch the settings that matter for a touchpad; everything
 * else is left exactly as the device/library default. */
static void
input_handle_device(void *data, struct river_libinput_device_v1 *obj,
		struct river_input_device_v1 *device)
{
}

static void
input_handle_removed(void *data, struct river_libinput_device_v1 *obj)
{
	river_libinput_device_v1_destroy(obj);
}

static void
input_result_done(void *data, struct river_libinput_result_v1 *obj)
{
	river_libinput_result_v1_destroy(obj);
}

static const struct river_libinput_result_v1_listener input_result_listener = {
	.success = input_result_done,
	.unsupported = input_result_done,
	.invalid = input_result_done,
};

static void
apply_result(struct river_libinput_result_v1 *result)
{
	if (result)
		river_libinput_result_v1_add_listener(result,
				&input_result_listener, NULL);
}

static void
input_handle_tap_support(void *data, struct river_libinput_device_v1 *obj,
		int32_t finger_count)
{
	if (finger_count <= 0)
		return;

	apply_result(river_libinput_device_v1_set_tap(obj, tap_to_click
			? RIVER_LIBINPUT_DEVICE_V1_TAP_STATE_ENABLED
			: RIVER_LIBINPUT_DEVICE_V1_TAP_STATE_DISABLED));

	apply_result(river_libinput_device_v1_set_drag(obj, tap_drag
			? RIVER_LIBINPUT_DEVICE_V1_DRAG_STATE_ENABLED
			: RIVER_LIBINPUT_DEVICE_V1_DRAG_STATE_DISABLED));
}

static void
input_handle_natural_scroll_support(void *data,
		struct river_libinput_device_v1 *obj, int32_t supported)
{
	if (!supported)
		return;

	apply_result(river_libinput_device_v1_set_natural_scroll(obj,
			natural_scroll
			? RIVER_LIBINPUT_DEVICE_V1_NATURAL_SCROLL_STATE_ENABLED
			: RIVER_LIBINPUT_DEVICE_V1_NATURAL_SCROLL_STATE_DISABLED));
}

/* Every remaining event still needs a non-null listener: libwayland aborts
 * when it dispatches an event whose listener function is NULL. */
#define IGNORE_U32(NAME) \
	static void NAME(void *data, struct river_libinput_device_v1 *obj, \
			uint32_t value) {}
#define IGNORE_I32(NAME) \
	static void NAME(void *data, struct river_libinput_device_v1 *obj, \
			int32_t value) {}
#define IGNORE_ARRAY(NAME) \
	static void NAME(void *data, struct river_libinput_device_v1 *obj, \
			struct wl_array *value) {}

IGNORE_U32(input_handle_send_events_support)
IGNORE_U32(input_handle_send_events_default)
IGNORE_U32(input_handle_send_events_current)
IGNORE_U32(input_handle_tap_default)
IGNORE_U32(input_handle_tap_current)
IGNORE_U32(input_handle_tap_button_map_default)
IGNORE_U32(input_handle_tap_button_map_current)
IGNORE_U32(input_handle_drag_default)
IGNORE_U32(input_handle_drag_current)
IGNORE_U32(input_handle_drag_lock_default)
IGNORE_U32(input_handle_drag_lock_current)
IGNORE_I32(input_handle_three_finger_drag_support)
IGNORE_U32(input_handle_three_finger_drag_default)
IGNORE_U32(input_handle_three_finger_drag_current)
IGNORE_I32(input_handle_calibration_matrix_support)
IGNORE_ARRAY(input_handle_calibration_matrix_default)
IGNORE_ARRAY(input_handle_calibration_matrix_current)
IGNORE_U32(input_handle_accel_profiles_support)
IGNORE_U32(input_handle_accel_profile_default)
IGNORE_U32(input_handle_accel_profile_current)
IGNORE_ARRAY(input_handle_accel_speed_default)
IGNORE_ARRAY(input_handle_accel_speed_current)
IGNORE_U32(input_handle_natural_scroll_default)
IGNORE_U32(input_handle_natural_scroll_current)
IGNORE_I32(input_handle_left_handed_support)
IGNORE_U32(input_handle_left_handed_default)
IGNORE_U32(input_handle_left_handed_current)
IGNORE_U32(input_handle_click_method_support)
IGNORE_U32(input_handle_click_method_default)
IGNORE_U32(input_handle_click_method_current)
IGNORE_U32(input_handle_clickfinger_button_map_default)
IGNORE_U32(input_handle_clickfinger_button_map_current)
IGNORE_I32(input_handle_middle_emulation_support)
IGNORE_U32(input_handle_middle_emulation_default)
IGNORE_U32(input_handle_middle_emulation_current)
IGNORE_U32(input_handle_scroll_method_support)
IGNORE_U32(input_handle_scroll_method_default)
IGNORE_U32(input_handle_scroll_method_current)
IGNORE_U32(input_handle_scroll_button_default)
IGNORE_U32(input_handle_scroll_button_current)
IGNORE_U32(input_handle_scroll_button_lock_default)
IGNORE_U32(input_handle_scroll_button_lock_current)
IGNORE_I32(input_handle_dwt_support)
IGNORE_U32(input_handle_dwt_default)
IGNORE_U32(input_handle_dwt_current)
IGNORE_I32(input_handle_dwtp_support)
IGNORE_U32(input_handle_dwtp_default)
IGNORE_U32(input_handle_dwtp_current)
IGNORE_I32(input_handle_rotation_support)
IGNORE_U32(input_handle_rotation_default)
IGNORE_U32(input_handle_rotation_current)

#undef IGNORE_U32
#undef IGNORE_I32
#undef IGNORE_ARRAY

static const struct river_libinput_device_v1_listener input_device_listener = {
	.removed = input_handle_removed,
	.input_device = input_handle_device,
	.send_events_support = input_handle_send_events_support,
	.send_events_default = input_handle_send_events_default,
	.send_events_current = input_handle_send_events_current,
	.tap_support = input_handle_tap_support,
	.tap_default = input_handle_tap_default,
	.tap_current = input_handle_tap_current,
	.tap_button_map_default = input_handle_tap_button_map_default,
	.tap_button_map_current = input_handle_tap_button_map_current,
	.drag_default = input_handle_drag_default,
	.drag_current = input_handle_drag_current,
	.drag_lock_default = input_handle_drag_lock_default,
	.drag_lock_current = input_handle_drag_lock_current,
	.three_finger_drag_support = input_handle_three_finger_drag_support,
	.three_finger_drag_default = input_handle_three_finger_drag_default,
	.three_finger_drag_current = input_handle_three_finger_drag_current,
	.calibration_matrix_support = input_handle_calibration_matrix_support,
	.calibration_matrix_default = input_handle_calibration_matrix_default,
	.calibration_matrix_current = input_handle_calibration_matrix_current,
	.accel_profiles_support = input_handle_accel_profiles_support,
	.accel_profile_default = input_handle_accel_profile_default,
	.accel_profile_current = input_handle_accel_profile_current,
	.accel_speed_default = input_handle_accel_speed_default,
	.accel_speed_current = input_handle_accel_speed_current,
	.natural_scroll_support = input_handle_natural_scroll_support,
	.natural_scroll_default = input_handle_natural_scroll_default,
	.natural_scroll_current = input_handle_natural_scroll_current,
	.left_handed_support = input_handle_left_handed_support,
	.left_handed_default = input_handle_left_handed_default,
	.left_handed_current = input_handle_left_handed_current,
	.click_method_support = input_handle_click_method_support,
	.click_method_default = input_handle_click_method_default,
	.click_method_current = input_handle_click_method_current,
	.clickfinger_button_map_default = input_handle_clickfinger_button_map_default,
	.clickfinger_button_map_current = input_handle_clickfinger_button_map_current,
	.middle_emulation_support = input_handle_middle_emulation_support,
	.middle_emulation_default = input_handle_middle_emulation_default,
	.middle_emulation_current = input_handle_middle_emulation_current,
	.scroll_method_support = input_handle_scroll_method_support,
	.scroll_method_default = input_handle_scroll_method_default,
	.scroll_method_current = input_handle_scroll_method_current,
	.scroll_button_default = input_handle_scroll_button_default,
	.scroll_button_current = input_handle_scroll_button_current,
	.scroll_button_lock_default = input_handle_scroll_button_lock_default,
	.scroll_button_lock_current = input_handle_scroll_button_lock_current,
	.dwt_support = input_handle_dwt_support,
	.dwt_default = input_handle_dwt_default,
	.dwt_current = input_handle_dwt_current,
	.dwtp_support = input_handle_dwtp_support,
	.dwtp_default = input_handle_dwtp_default,
	.dwtp_current = input_handle_dwtp_current,
	.rotation_support = input_handle_rotation_support,
	.rotation_default = input_handle_rotation_default,
	.rotation_current = input_handle_rotation_current,
};

static void
input_handle_libinput_device(void *data,
		struct river_libinput_config_v1 *obj,
		struct river_libinput_device_v1 *device)
{
	river_libinput_device_v1_add_listener(device,
			&input_device_listener, NULL);
}

static void
input_handle_finished(void *data, struct river_libinput_config_v1 *obj)
{
	river_libinput_config_v1_destroy(obj);
}

static const struct river_libinput_config_v1_listener input_config_listener = {
	.libinput_device = input_handle_libinput_device,
	.finished = input_handle_finished,
};

static void
init_input_config(struct wl_registry *registry, uint32_t name)
{
	struct river_libinput_config_v1 *config;

	config = wl_registry_bind(registry, name,
			&river_libinput_config_v1_interface, 1);
	river_libinput_config_v1_add_listener(config,
			&input_config_listener, NULL);
}

/* object creation and destruction */
static Client *
client_create(struct river_window_v1 *obj)
{
	Client *c = ecalloc(1, sizeof(*c));

	c->obj = obj;
	c->node = river_window_v1_get_node(obj);
	river_window_v1_set_user_data(obj, c);
	c->tiled = -1;
	c->bw = borderpx;
	c->need_capabilities = 1;
	c->need_ssd = 1;
	c->mon = selmon ? selmon : first_monitor();
	c->tags = c->mon ? c->mon->tagset[c->mon->seltags] : 1;

	/* dwm focuses new windows. */
	if (c->mon) {
		c->mon->sel = c;
		selmon = c->mon;
	}

	wl_list_insert(&clients, &c->link);
	wl_list_insert(&stack, &c->slink);
	stack_cache_len = -1;
	return c;
}

static void
client_destroy(Client *c)
{
	Seat *seat;

	wl_list_for_each(seat, &seats, link) {
		if (seat->op_client == c)
			op_cancel(seat);
		if (seat->entered == c)
			seat->entered = NULL;
		if (seat->focus_client == c)
			seat->focus_client = NULL;
	}

	if (c->mon && c->mon->sel == c)
		c->mon->sel = NULL;

	wl_list_remove(&c->link);
	wl_list_remove(&c->slink);
	stack_cache_len = -1;
	river_window_v1_destroy(c->obj);
	free(c->app_id);
	free(c->title);
	free(c);
}

static Monitor *
monitor_create(struct river_output_v1 *obj,
		struct river_layer_shell_output_v1 *ls)
{
	Monitor *m = ecalloc(1, sizeof(*m));

	m->obj = obj;
	m->ls = ls;
	m->tagset[0] = m->tagset[1] = 1;
	m->seltags = 0;
	m->mfact = mfact;
	m->nmaster = nmaster;
	m->sellt = 0;
	m->lt[0] = &layouts[0];
	m->lt[1] = &layouts[1 % LENGTH(layouts)];

	wl_list_insert(&monitors, &m->link);
	return m;
}

static void
monitor_destroy(Monitor *m)
{
	Client *c;
	Monitor *other = NULL, *o;

	wl_list_for_each(o, &monitors, link) {
		if (o != m) {
			other = o;
			break;
		}
	}

	wl_list_for_each(c, &clients, link) {
		if (c->mon != m)
			continue;
		c->mon = other;
		/* River automatically exits fullscreen when the output goes away. */
		c->fullscreen = 0;
		c->isfloating = c->wasfloating;
		if (other)
			c->tags = other->tagset[other->seltags];
	}

	if (selmon == m) {
		selmon = other;
		if (selmon)
			selmon->sel = NULL;
	}
	if (default_monitor == m)
		default_monitor = NULL;

	river_layer_shell_output_v1_destroy(m->ls);
	river_output_v1_destroy(m->obj);
	wl_list_remove(&m->link);
	free(m);
}

/* output listeners */
static void
output_handle_removed(void *data, struct river_output_v1 *obj)
{
	monitor_destroy(data);
}

static void
output_handle_wl_output(void *data, struct river_output_v1 *obj, uint32_t name)
{
}

static void
output_handle_position(void *data, struct river_output_v1 *obj,
		int32_t x, int32_t y)
{
	Monitor *m = data;

	m->real.x = x;
	m->real.y = y;
	if (m->windowed.width == 0 && m->windowed.height == 0)
		m->windowed = m->real;
	reorder_monitor(m);
}

static void
output_handle_dimensions(void *data, struct river_output_v1 *obj,
		int32_t width, int32_t height)
{
	Monitor *m = data;

	m->real.width = width;
	m->real.height = height;
	if (m->windowed.width == 0 && m->windowed.height == 0)
		m->windowed = m->real;
	reorder_monitor(m);
}

static const struct river_output_v1_listener output_listener = {
	.removed = output_handle_removed,
	.wl_output = output_handle_wl_output,
	.position = output_handle_position,
	.dimensions = output_handle_dimensions,
};

static void
ls_output_handle_non_exclusive_area(void *data,
		struct river_layer_shell_output_v1 *ls, int32_t x, int32_t y,
		int32_t width, int32_t height)
{
	Monitor *m = data;

	m->windowed.x = x;
	m->windowed.y = y;
	m->windowed.width = width;
	m->windowed.height = height;
}

static const struct river_layer_shell_output_v1_listener ls_output_listener = {
	.non_exclusive_area = ls_output_handle_non_exclusive_area,
};

/* window listeners */
static void
window_handle_closed(void *data, struct river_window_v1 *obj)
{
	client_destroy(data);
}

static void
window_handle_dimensions(void *data, struct river_window_v1 *obj,
		int32_t width, int32_t height)
{
	Client *c = data;
	int first = !c->geom_valid;

	c->geom_valid = 1;
	c->geom.width = width;
	c->geom.height = height;

	if (first && c->isfloating && !c->fullscreen)
		center_client(c);
}

static void
window_handle_app_id(void *data, struct river_window_v1 *obj,
		const char *app_id)
{
	Client *c = data;

	free(c->app_id);
	c->app_id = app_id ? estrdup(app_id) : NULL;
}

static void
window_handle_title(void *data, struct river_window_v1 *obj,
		const char *title)
{
	Client *c = data;

	free(c->title);
	c->title = title ? estrdup(title) : NULL;
}

static void
window_handle_fullscreen_requested(void *data, struct river_window_v1 *obj,
		struct river_output_v1 *output)
{
	setfullscreen(data, 1);
}

static void
window_handle_exit_fullscreen_requested(void *data,
		struct river_window_v1 *obj)
{
	setfullscreen(data, 0);
}

/* dwm ignores maximize: it is not advertised in set_capabilities. */
static void
window_handle_maximize_requested(void *data, struct river_window_v1 *obj)
{
}

static void
window_handle_unmaximize_requested(void *data, struct river_window_v1 *obj)
{
}

/* ignored events */
static void
window_handle_decoration_hint(void *data, struct river_window_v1 *obj,
		uint32_t hint)
{
}

static void
window_handle_dimensions_hint(void *data, struct river_window_v1 *obj,
		int32_t min_width, int32_t min_height, int32_t max_width,
		int32_t max_height)
{
}

static void
window_handle_identifier(void *data, struct river_window_v1 *obj,
		const char *identifier)
{
}

static void
window_handle_minimize_requested(void *data, struct river_window_v1 *obj)
{
}

/* dwm floats transient windows; river reports the parent window here. */
static void
window_handle_parent(void *data, struct river_window_v1 *obj,
		struct river_window_v1 *parent)
{
	Client *c = data;

	if (parent) {
		c->has_parent = 1;
		if (!c->isfloating) {
			c->isfloating = 1;
			if (c->geom_valid)
				center_client(c);
		}
	}
}

/* Only relevant for client side decorations; we use server side decorations. */
static void
window_handle_pointer_move_requested(void *data, struct river_window_v1 *obj,
		struct river_seat_v1 *seat)
{
}

static void
window_handle_pointer_resize_requested(void *data, struct river_window_v1 *obj,
		struct river_seat_v1 *seat, uint32_t edges)
{
}

static void
window_handle_show_window_menu_requested(void *data,
		struct river_window_v1 *obj, int32_t x, int32_t y)
{
}

static void
window_handle_unreliable_pid(void *data, struct river_window_v1 *obj,
		int32_t unreliable_pid)
{
}

static void
window_handle_presentation_hint(void *data, struct river_window_v1 *obj,
		uint32_t hint)
{
}

static const struct river_window_v1_listener window_listener = {
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

/* seat listeners */
static void
seat_handle_removed(void *data, struct river_seat_v1 *obj)
{
	Seat *seat = data;

	remove_bindings(seat);
	river_layer_shell_seat_v1_destroy(seat->ls);
	river_seat_v1_destroy(seat->obj);
	wl_list_remove(&seat->link);
	if (current_seat == seat)
		current_seat = NULL;
	free(seat);
}

/* dwm is click-to-focus: pressing on a window focuses it and raises it. */
static void
seat_handle_window_interaction(void *data, struct river_seat_v1 *obj,
		struct river_window_v1 *river_window)
{
	Client *c = river_window_v1_get_user_data(river_window);

	if (!c || !c->mon)
		return;
	selmon = c->mon;
	focus(c);
}

static void
seat_handle_pointer_enter(void *data, struct river_seat_v1 *obj,
		struct river_window_v1 *river_window)
{
	Seat *seat = data;
	Client *c = river_window_v1_get_user_data(river_window);

	seat->entered = c;
	/* Update the selection right away (dwm's enternotify()), so a pointer
	 * binding handled before the next manage sequence acts on this window.
	 * The focus request itself is still sent in the manage sequence. */
	if (c && c->mon && !seat->ls_focused && seat->op_mode == OpNone)
		focus(c);
}

static void
seat_handle_pointer_leave(void *data, struct river_seat_v1 *obj)
{
	Seat *seat = data;

	seat->entered = NULL;
}

static void
seat_handle_pointer_position(void *data, struct river_seat_v1 *obj,
		int32_t x, int32_t y)
{
	Monitor *m = monitor_at(x, y);

	if (m)
		selmon = m;
}

static void
seat_handle_op_delta(void *data, struct river_seat_v1 *obj,
		int32_t dx, int32_t dy)
{
	Seat *seat = data;

	seat->op_dx = dx;
	seat->op_dy = dy;
}

static void
seat_handle_op_release(void *data, struct river_seat_v1 *obj)
{
	Seat *seat = data;

	seat->op_end_pending = 1;
}

static void
seat_handle_wl_seat(void *data, struct river_seat_v1 *obj, uint32_t id)
{
}

static void
seat_handle_shell_surface_interaction(void *data, struct river_seat_v1 *obj,
		struct river_shell_surface_v1 *river_shell_surface)
{
}

static const struct river_seat_v1_listener seat_listener = {
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

static void
ls_seat_handle_focus_exclusive(void *data,
		struct river_layer_shell_seat_v1 *ls)
{
	Seat *seat = data;

	seat->ls_focused = 1;
}

static void
ls_seat_handle_focus_non_exclusive(void *data,
		struct river_layer_shell_seat_v1 *ls)
{
	Seat *seat = data;

	seat->ls_focused = 1;
}

static void
ls_seat_handle_focus_none(void *data, struct river_layer_shell_seat_v1 *ls)
{
	Seat *seat = data;

	seat->ls_focused = 0;
}

static const struct river_layer_shell_seat_v1_listener ls_seat_listener = {
	.focus_exclusive = ls_seat_handle_focus_exclusive,
	.focus_non_exclusive = ls_seat_handle_focus_non_exclusive,
	.focus_none = ls_seat_handle_focus_none,
};

/* window manager events */
static void
wm_handle_output(void *data, struct river_window_manager_v1 *obj,
		struct river_output_v1 *river_output)
{
	struct river_layer_shell_output_v1 *ls;
	Monitor *m;
	Client *c;

	ls = river_layer_shell_v1_get_output(layer_shell_v1, river_output);
	m = monitor_create(river_output, ls);

	river_output_v1_add_listener(river_output, &output_listener, m);
	river_layer_shell_output_v1_add_listener(ls, &ls_output_listener, m);

	if (!selmon)
		selmon = m;

	/* Adopt clients that were left without an output. */
	wl_list_for_each(c, &clients, link)
		if (!c->mon) {
			c->mon = m;
			c->tags = m->tagset[m->seltags];
			c->fullscreen = 0;
			center_client(c);
		}

}

static void
wm_handle_seat(void *data, struct river_window_manager_v1 *obj,
		struct river_seat_v1 *river_seat)
{
	Seat *seat = ecalloc(1, sizeof(*seat));

	seat->obj = river_seat;
	seat->ls = river_layer_shell_v1_get_seat(layer_shell_v1, river_seat);
	seat->bindings_dirty = 1;
	wl_list_init(&seat->xkb_bindings);
	wl_list_init(&seat->pointer_bindings);

	wl_list_insert(&seats, &seat->link);
	init_bindings(seat);
	river_seat_v1_add_listener(river_seat, &seat_listener, seat);
	river_layer_shell_seat_v1_add_listener(seat->ls, &ls_seat_listener, seat);
}

static void
wm_handle_window(void *data, struct river_window_manager_v1 *obj,
		struct river_window_v1 *river_window)
{
	Client *c = client_create(river_window);

	river_window_v1_add_listener(river_window, &window_listener, c);
}

static void
wm_handle_manage_start(void *data, struct river_window_manager_v1 *obj)
{
	Client *c;
	Monitor *m;
	Seat *seat;

	wl_list_for_each(c, &clients, link) {
		if (c->need_capabilities) {
			river_window_v1_set_capabilities(c->obj,
					RIVER_WINDOW_V1_CAPABILITIES_FULLSCREEN);
			c->need_capabilities = 0;
		}
		if (c->need_ssd) {
			river_window_v1_use_ssd(c->obj);
			c->need_ssd = 0;
		}
		if (c->need_close) {
			river_window_v1_close(c->obj);
			c->need_close = 0;
		}
		if (c->need_fullscreen && c->mon) {
			river_window_v1_inform_fullscreen(c->obj);
			river_window_v1_fullscreen(c->obj, c->mon->obj);
			c->need_fullscreen = 0;
		}
		if (c->need_exit_fullscreen) {
			river_window_v1_inform_not_fullscreen(c->obj);
			river_window_v1_exit_fullscreen(c->obj);
			c->need_exit_fullscreen = 0;
			c->isfloating = c->wasfloating;
			/* River leaves the geometry undefined after exit_fullscreen;
			 * restore it in this same manage sequence. */
			c->geom = c->oldgeom;
			c->pos_valid = 0;
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

	/* Apply interactive move/resize deltas. */
	wl_list_for_each(seat, &seats, link) {
		if (seat->op_mode == OpNone)
			continue;
		if (seat->op_start_pending) {
			river_seat_v1_op_start_pointer(seat->obj);
			seat->op_start_pending = 0;
		}

		c = seat->op_client;
		if (c && (seat->op_dx != 0 || seat->op_dy != 0)) {
			int moved = 1;

			if (seat->op_float_pending) {
				if (abs(seat->op_dx) <= snap
						&& abs(seat->op_dy) <= snap)
					moved = 0;
				else {
					/* Like dwm, keep a tiled window in the layout
					 * until the pointer really moves. */
					c->geom = seat->op_geom;
					c->isfloating = 1;
					seat->op_float_pending = 0;
				}
			}

			if (moved && seat->op_mode == OpMove) {
				c->geom.x = seat->op_geom.x + seat->op_dx;
				c->geom.y = seat->op_geom.y + seat->op_dy;
			} else if (moved && seat->op_mode == OpResize) {
				c->geom.width = MAX(20,
						seat->op_geom.width + seat->op_dx);
				c->geom.height = MAX(20,
						seat->op_geom.height + seat->op_dy);
				if (c->geom.width != c->proposed_width
						|| c->geom.height != c->proposed_height) {
					river_window_v1_propose_dimensions(c->obj,
							c->geom.width,
							c->geom.height);
					c->proposed_width = c->geom.width;
					c->proposed_height = c->geom.height;
				}
			}
		}

		if (seat->op_end_pending) {
			river_seat_v1_op_end(seat->obj);
			seat->op_end_pending = 0;
			seat->op_client = NULL;
			seat->op_float_pending = 0;
			seat->op_mode = OpNone;
		}
	}

	/* Sloppy focus: the window the pointer entered takes focus for this
	 * manage sequence, exactly like dwm's enternotify(). */
	wl_list_for_each(seat, &seats, link)
		if (!seat->ls_focused && seat->op_mode == OpNone
				&& seat->entered && seat->entered->mon)
			focus(seat->entered);

	/* Arrange every monitor; this also proposes dimensions. */
	arrange_all();

	/* Make sure every monitor has a sensible selection. */
	wl_list_for_each(m, &monitors, link)
		if (!m->sel || !ISVISIBLE(m->sel) || m->sel->mon != m)
			m->sel = first_visible(m);

	if (!selmon)
		selmon = first_monitor();
	focus(selmon ? selmon->sel : NULL);

	/* Default layer shell output: only update when the selected monitor
	 * changes, otherwise every window gets placed on it again. */
	if (selmon && selmon != default_monitor) {
		river_layer_shell_output_v1_set_default(selmon->ls);
		default_monitor = selmon;
	}

	wl_list_for_each(seat, &seats, link) {
		if (seat->bindings_dirty) {
			manage_bindings(seat);
			seat->bindings_dirty = 0;
		}

		if (seat->ls_focused) {
			/* A layer surface owns keyboard focus; focus requests are
			 * ignored. */
			seat->focus_client = NULL;
			seat->focus_cleared = 0;
			seat->entered = NULL;
			continue;
		}

		if (selmon) {
			Client *sel = selmon->sel;

			if (sel) {
				if (seat->focus_client != sel) {
					river_seat_v1_focus_window(seat->obj,
							sel->obj);
					seat->focus_client = sel;
					seat->focus_cleared = 0;
				}
			} else if (!seat->focus_cleared) {
				river_seat_v1_clear_focus(seat->obj);
				seat->focus_client = NULL;
				seat->focus_cleared = 1;
			}
		}

		seat->entered = NULL;
	}

	river_window_manager_v1_manage_finish(window_manager_v1);
}

static void
wm_handle_render_start(void *data, struct river_window_manager_v1 *obj)
{
	render();
	river_window_manager_v1_render_finish(window_manager_v1);
}

static void
wm_handle_finished(void *data, struct river_window_manager_v1 *obj)
{
	river_window_manager_v1_destroy(window_manager_v1);
	exit(0);
}

static void
wm_handle_unavailable(void *data, struct river_window_manager_v1 *obj)
{
	die("pluto: window management unavailable (is another WM running?)");
}

static void
wm_handle_session_locked(void *data, struct river_window_manager_v1 *obj)
{
	Seat *seat;

	wl_list_for_each(seat, &seats, link)
		lock_bindings(seat);
}

static void
wm_handle_session_unlocked(void *data, struct river_window_manager_v1 *obj)
{
	Seat *seat;

	wl_list_for_each(seat, &seats, link)
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

/* startup */
static void
handle_global(void *data, struct wl_registry *registry, uint32_t name,
		const char *interface, uint32_t version)
{
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
	} else if (strcmp(interface,
			river_libinput_config_v1_interface.name) == 0) {
		init_input_config(registry, name);
	}
}

static void
handle_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	.global = handle_global,
	.global_remove = handle_global_remove,
};

static void
setup(void)
{
	struct sigaction sa;
	struct wl_registry *registry;

	unsetenv("WAYLAND_DEBUG");

	/* do not transform children into zombies when they terminate */
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_NOCLDSTOP | SA_NOCLDWAIT;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGCHLD, &sa, NULL);

	/* clean up any zombies (inherited from the session) immediately */
	while (waitpid(-1, NULL, WNOHANG) > 0)
		;

	wl_list_init(&monitors);
	wl_list_init(&clients);
	wl_list_init(&stack);
	wl_list_init(&seats);

	display = wl_display_connect(NULL);
	if (!display)
		die("pluto: cannot connect to Wayland server");

	registry = wl_display_get_registry(display);
	wl_registry_add_listener(registry, &registry_listener, NULL);
	if (wl_display_roundtrip(display) < 0)
		die("pluto: roundtrip failed");

	if (!window_manager_v1 || !xkb_bindings_v1 || !layer_shell_v1)
		die("pluto: required protocol not supported by the server");

	river_window_manager_v1_add_listener(window_manager_v1, &wm_listener,
			NULL);
}

static void
run(void)
{
	while (wl_display_dispatch(display) != -1)
		;
}

int
main(int argc, char *argv[])
{
	if (argc == 2 && !strcmp("-v", argv[1]))
		die("pluto-"VERSION);
	else if (argc != 1)
		die("usage: pluto [-v]");

	setup();
	run();
	return EXIT_SUCCESS;
}
