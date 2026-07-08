// config.def.h -- Default configuration for Pluto
// Copy this to config.h and edit to customize.
//
// Copyright 2026 Isaac Freund, 2026 Jack Conger.  See LICENSE for details.
// Copyright 2026 Yash Sharma. See LICENSE for details.

// How many static Spaces are allocated on startup.  More spaces than this may
// be created dynamically as needed, but there will always be at least this many
int static_spaces = 9;

// The default layout for a newly-created Space
void (*default_layout)(struct Space *, struct Rect) = bsp_layout;


// "Aesthetics", insofar as they exist: Colors and borders

#define	COLOR(hex)	{ ((hex >> 24) & 0xFF) * (UINT32_MAX / 255), \
			  ((hex >> 16) & 0xFF) * (UINT32_MAX / 255), \
			  ((hex >>  8) & 0xFF) * (UINT32_MAX / 255), \
			  ( hex        & 0xFF) * (UINT32_MAX / 255) }

uint32_t border_color[4]  = COLOR(0x333333ff);
uint32_t focused_color[4] = COLOR(0x77aa99ff);

int monocle_borderpx = 0;
int border_width   = 2;


// Tiled layout config

int gaps_inner           = -2;	// Gap between windows (negative = positive gap)
int gaps_outer           =  0;	// Gap around windows

// Pointer behavior

bool focus_follows_pointer = true;
bool pointer_follows_focus = false;


// Keybinds and spawns

static char *spawn_foot[] = {"foot", NULL};
static char *spawn_menu[] = {"fuzzel", NULL};

#define alt	RIVER_SEAT_V1_MODIFIERS_MOD1
#define ctrl	RIVER_SEAT_V1_MODIFIERS_CTRL
#define super	RIVER_SEAT_V1_MODIFIERS_MOD4
#define shift	RIVER_SEAT_V1_MODIFIERS_SHIFT
#define none	RIVER_SEAT_V1_MODIFIERS_NONE

// The key codes of the form XKB_KEY_* are declared in xkbcommon.h
// The binding functions are declared in pluto.h and defined in bindings.c
struct Binddef binds[] = {
	{super,       XKB_KEY_q, binding_close,                  {0}},
	{super|shift, XKB_KEY_e, binding_exit,                   {0}},
	{super,       XKB_KEY_m, binding_toggle_monocle,         {0}},
	{super|shift, XKB_KEY_f, binding_toggle_fullscreen,      {0}},
	{super|shift, XKB_KEY_m, binding_toggle_fake_fullscreen, {0}},

	// Bindings for window movement
	{super,       XKB_KEY_j, binding_focus_next, {0}},
	{super|shift, XKB_KEY_j, binding_move_next,  {0}},
	{super,       XKB_KEY_k, binding_focus_prev, {0}},
	{super|shift, XKB_KEY_k, binding_move_prev,  {0}},

	// Bindings for relative motion between spaces
	{super,       XKB_KEY_h, binding_activate_prev_busy_space, {0}},
	{super,       XKB_KEY_l, binding_activate_next_busy_space, {0}},
	{super|alt,   XKB_KEY_h, binding_activate_prev_space,      {0}},
	{super|alt,   XKB_KEY_l, binding_activate_next_space,      {0}},
	{super,       XKB_KEY_o, binding_activate_next_idle_space, {0}},
	{super|ctrl,  XKB_KEY_o, binding_activate_prev_idle_space, {0}},

	// Bindings to refer to spaces by number; best used with static_spaces = 9
	{super,       XKB_KEY_1, binding_activate_space, {.i = 1}},
	{super|shift, XKB_KEY_1, binding_move_to_space,  {.i = 1}},
	{super,       XKB_KEY_2, binding_activate_space, {.i = 2}},
	{super|shift, XKB_KEY_2, binding_move_to_space,  {.i = 2}},
	{super,       XKB_KEY_3, binding_activate_space, {.i = 3}},
	{super|shift, XKB_KEY_3, binding_move_to_space,  {.i = 3}},
	{super,       XKB_KEY_4, binding_activate_space, {.i = 4}},
	{super|shift, XKB_KEY_4, binding_move_to_space,  {.i = 4}},
	{super,       XKB_KEY_5, binding_activate_space, {.i = 5}},
	{super|shift, XKB_KEY_5, binding_move_to_space,  {.i = 5}},
	{super,       XKB_KEY_6, binding_activate_space, {.i = 6}},
	{super|shift, XKB_KEY_6, binding_move_to_space,  {.i = 6}},
	{super,       XKB_KEY_7, binding_activate_space, {.i = 7}},
	{super|shift, XKB_KEY_7, binding_move_to_space,  {.i = 7}},
	{super,       XKB_KEY_8, binding_activate_space, {.i = 8}},
	{super|shift, XKB_KEY_8, binding_move_to_space,  {.i = 8}},
	{super,       XKB_KEY_9, binding_activate_space, {.i = 9}},
	{super|shift, XKB_KEY_9, binding_move_to_space,  {.i = 9}},

#define spawn_binding(mod, key, argv)	{mod, key, binding_spawn, {.v = argv}}

	spawn_binding(super, XKB_KEY_Return, spawn_foot),
	spawn_binding(super, XKB_KEY_space,  spawn_menu),

#undef spawn_binding

	{0, 0, NULL, {0}}
};

#undef alt
#undef ctrl
#undef super
#undef shift
#undef none
