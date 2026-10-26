// pluto.h -- Header file for Pluto, a dwm-like window manager for river.
//
// Copyright 2026 Isaac Freund, 2026 Jack Conger.  See LICENSE for details.
// Copyright 2026 Yash Sharma.  See LICENSE for details.
//
// The client/monitor model deliberately mirrors dwm: clients carry a tag
// bitmask, each monitor has two tag sets (so Mod+Tab can flip back), and the
// layout is master/stack instead of a BSP tree.

#ifndef PLUTO_H
#define PLUTO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <river-layer-shell-v1.h>
#include <river-libinput-config-v1.h>
#include <river-window-management-v1.h>
#include <river-xkb-bindings-v1.h>

#define LENGTH(X) (sizeof (X) / sizeof (X)[0])
#define MAX(A, B) ((A) > (B) ? (A) : (B))
#define MIN(A, B) ((A) < (B) ? (A) : (B))

#define EDGES_ALL (RIVER_WINDOW_V1_EDGES_TOP | \
		RIVER_WINDOW_V1_EDGES_BOTTOM | \
		RIVER_WINDOW_V1_EDGES_LEFT | \
		RIVER_WINDOW_V1_EDGES_RIGHT)


// Types

struct Rect {
	int32_t x, y, width, height;
};

struct Monitor;
struct Client;

// A layout is identified by its symbol and the function that arranges the
// tiled clients of a monitor.  A null arrange function means "floating":
// clients keep whatever geometry they have.
typedef struct {
	const char *symbol;
	void (*arrange)(struct Monitor *);
} Layout;

// Argument passing, exactly like dwm.
typedef union {
	int i;
	unsigned int ui;
	float f;
	const void *v;
} Arg;

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

// A rule is applied to a client once its app_id/title is known.
typedef struct {
	const char *app_id;   // NULL matches anything
	const char *title;    // NULL matches anything
	unsigned int tags;    // 0 means "keep current tags"
	bool isfloating;
	int monitor;          // -1 means "keep current monitor"
} Rule;

// A Client is a window under management.
struct Client {
	// dwm keeps two orders: the client list decides the tiling order (head is
	// the master), the stack list decides focus/stacking order (head is the
	// most recently focused).  Focusing must not change the layout, so they
	// have to stay separate.
	struct wl_list link;            // WindowManager.clients, head = master
	struct wl_list slink;           // WindowManager.stack, head = most recently focused
	struct river_window_v1 *obj;
	struct river_node_v1 *node;

	struct Monitor *mon;            // May be null briefly (e.g. no outputs)
	unsigned int tags;              // Tag bitmask, like dwm

	bool isfloating;
	bool wasfloating;               // Floating state to restore on unfullscreen
	bool fullscreen;
	bool geom_valid;                // We have received dimensions at least once
	bool rules_applied;

	struct Rect geom;               // Content geometry in logical coordinates
	struct Rect oldgeom;            // Geometry to restore when leaving fullscreen
	struct Rect slot;               // Tiled slot (content + border) assigned by arrange
	int bw;                         // Border width

	char *app_id;
	char *title;

	// Work deferred to the next manage sequence.
	bool need_capabilities;
	bool need_ssd;
	bool need_close;
	bool need_fullscreen;
	bool need_exit_fullscreen;
	int tiled;                      // Last value passed to set_tiled, -1 = unknown
	int32_t proposed_width;         // Last propose_dimensions, to avoid protocol loops
	int32_t proposed_height;

	// Cached rendering state.  Only send a request when the value changes,
	// otherwise every render sequence floods the compositor with no-op
	// requests (the main source of input lag).
	bool shown;                     // Last show/hide state
	bool pos_valid;
	int32_t pos_x, pos_y;           // Last set_position
	bool border_valid;
	bool border_focused;
	int border_width_set;
};

// A Monitor is an output.
struct Monitor {
	struct wl_list link;            // WindowManager.monitors, ordered by position
	struct river_output_v1 *obj;
	struct river_layer_shell_output_v1 *ls;
	struct wl_output *wl_output;    // for the name, exposed over IPC
	char name[64];

	struct Rect real;               // Full output geometry
	struct Rect windowed;           // Non-exclusive area (excludes bars, etc.)

	unsigned int tagset[2];         // Selected tags, like dwm
	unsigned int seltags;
	float mfact;
	int nmaster;
	const Layout *lt;

	struct Client *sel;
};

// A Seat is a collection of input devices.
struct Seat {
	struct wl_list link;            // WindowManager.seats
	struct river_seat_v1 *obj;
	struct river_layer_shell_seat_v1 *ls;
	struct wl_list xkb_bindings;    // XkbBinding
	struct wl_list pointer_bindings; // PointerBinding

	struct Client *entered;         // Window under the pointer
	bool warp;                      // Warp the pointer to the focused window
	bool ls_focused;                // A layer shell surface (e.g. a launcher) has focus
	bool locked;                    // Session is locked: bindings are disabled
	bool bindings_dirty;            // Binding enable/disable state needs syncing

	// Last value passed to river_seat_v1_focus_window.  Re-sending it on
	// every manage sequence needlessly resets keyboard focus.
	struct Client *focus_client;
	bool focus_cleared;

	// Interactive move/resize, driven by op_delta events.
	struct Client *op_client;
	int op_mode;
	bool op_start_pending;
	bool op_end_pending;
	int32_t op_dx, op_dy;
	struct Rect op_geom;
};

enum {
	OpNone,
	OpMove,
	OpResize,
};

struct WindowManager {
	struct wl_list monitors;        // Monitor
	struct wl_list clients;         // Client, tiling order
	struct wl_list stack;           // Client, focus order
	struct wl_list seats;           // Seat
};


// pluto.c

extern struct WindowManager wm;
extern struct Monitor *selmon;
extern struct Seat *current_seat;   // Seat dispatching a binding

extern struct river_window_manager_v1 *window_manager_v1;
extern struct river_xkb_bindings_v1 *xkb_bindings_v1;
extern struct river_layer_shell_v1 *layer_shell_v1;

struct Monitor *first_monitor(void);
struct Monitor *next_monitor(struct Monitor *m, int dir);
struct Client *next_tiled(struct Monitor *m, struct Client *c);
struct Client *first_visible(struct Monitor *m);
struct Client *visible_fullscreen(struct Monitor *m);
struct Rect client_render_rect(struct Client *c);

bool is_visible(struct Client *c);

void arrange(struct Monitor *m);
void arrange_all(void);
void focus(struct Client *c);
void restack(void);
void render(void);
void applyrules(struct Client *c);
void center_client(struct Client *c);
void manage_dirty(void);

// layout.c: the layouts referenced from config.h
void tile(struct Monitor *m);
void monocle(struct Monitor *m);


// bindings.c: dwm's key/button handlers

void setfullscreen(struct Client *c, bool fullscreen);

void spawn(const Arg *arg);
void quit(const Arg *arg);
void killclient(const Arg *arg);
void setlayout(const Arg *arg);
void setmfact(const Arg *arg);
void incnmaster(const Arg *arg);
void zoom(const Arg *arg);
void togglefloating(const Arg *arg);
void togglefullscreen(const Arg *arg);
void focusstack(const Arg *arg);
void focusmon(const Arg *arg);
void tagmon(const Arg *arg);
void view(const Arg *arg);
void tag(const Arg *arg);
void toggleview(const Arg *arg);
void toggletag(const Arg *arg);
void movemouse(const Arg *arg);
void resizemouse(const Arg *arg);

void init_bindings(struct Seat *seat);
void manage_bindings(struct Seat *seat);
void remove_bindings(struct Seat *seat);
void lock_bindings(struct Seat *seat);
void unlock_bindings(struct Seat *seat);
void op_cancel(struct Seat *seat);


// input.c: libinput device configuration (river delegates it to the WM)
//
// The global is optional: older river versions do not have it.

void init_input_config(struct wl_registry *registry, uint32_t name);


// config.c (config.h, included only by pluto.c)

extern int borderpx;
extern uint32_t border_color_normal[4];
extern uint32_t border_color_focus[4];

extern float mfact;
extern int nmaster;
extern int gap_inner;
extern int gap_outer;
extern bool lockfullscreen;
extern bool focus_follows_pointer;
extern bool pointer_follows_focus;

/* libinput: applied to every device that supports the setting */
extern bool tap_to_click;
extern bool tap_drag;
extern bool natural_scroll;

extern const Layout layouts[];
extern const int layouts_len;
extern const Rule rules[];
extern const int rules_len;
extern const Key keys[];
extern const Button buttons[];
extern const unsigned int tagmask;

#endif
