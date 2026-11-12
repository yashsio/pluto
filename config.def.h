/* See LICENSE file for copyright and license details. */
#include <linux/input-event-codes.h>

/* appearance */
static const unsigned int borderpx = 1;
static const int snap = 32; /* snap pixel */

#define COLOR(hex)	{ ((hex >> 24) & 0xFF) * (UINT32_MAX / 255), \
			  ((hex >> 16) & 0xFF) * (UINT32_MAX / 255), \
			  ((hex >>  8) & 0xFF) * (UINT32_MAX / 255), \
			  ( hex        & 0xFF) * (UINT32_MAX / 255) }

/* dwm's col_gray2 and col_cyan */
static const uint32_t border_color_normal[4] = COLOR(0x444444ff);
static const uint32_t border_color_focus[4]  = COLOR(0x005577ff);

/* tagging */
static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9" };

static const Rule rules[] = {
	/* app_id            title   tags mask   isfloating   monitor */
	{ "org.gimp.GIMP",   NULL,   0,          1,           -1 },
	{ "firefox",         NULL,   1 << 8,     0,           -1 },
};

/* layout(s) */
static const float mfact     = 0.55f; /* factor of master area size [0.05..0.95] */
static const int nmaster     = 1;     /* number of clients in master area */
static const int lockfullscreen = 1;  /* 1 will force focus on the fullscreen window */

/* touchpad / libinput: river leaves device configuration to the WM */
static const int tap_to_click = 1;    /* tap the touchpad to click */
static const int tap_drag     = 1;    /* tap-and-drag */
static const int natural_scroll = 0;  /* reverse the scroll direction */

static const Layout layouts[] = {
	/* symbol     arrange function */
	{ "[]=",      tile },    /* first entry is default */
	{ "><>",      NULL },    /* no layout function means floating behavior */
	{ "[M]",      monocle },
};

/* key definitions */
#define Mod1Mask	RIVER_SEAT_V1_MODIFIERS_MOD1
#define Mod4Mask	RIVER_SEAT_V1_MODIFIERS_MOD4
#define ShiftMask	RIVER_SEAT_V1_MODIFIERS_SHIFT
#define ControlMask	RIVER_SEAT_V1_MODIFIERS_CTRL

/* dwm's default is Mod1Mask; most river users prefer the Windows key. */
#define MODKEY Mod4Mask

#define TAGKEYS(KEY,TAG) \
	{ MODKEY,                       KEY, view,           {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask,           KEY, toggleview,     {.ui = 1 << TAG} }, \
	{ MODKEY|ShiftMask,             KEY, tag,            {.ui = 1 << TAG} }, \
	{ MODKEY|ControlMask|ShiftMask, KEY, toggletag,      {.ui = 1 << TAG} }

/* commands */
static const char *menucmd[] = { "fuzzel", NULL };
static const char *termcmd[] = { "foot", NULL };

static const Key keys[] = {
	/* modifier                     key              function          argument */
	{ MODKEY,                       XKB_KEY_p,       spawn,            {.v = menucmd} },
	{ MODKEY|ShiftMask,             XKB_KEY_Return,  spawn,            {.v = termcmd} },
	{ MODKEY,                       XKB_KEY_j,       focusstack,       {.i = +1} },
	{ MODKEY,                       XKB_KEY_k,       focusstack,       {.i = -1} },
	{ MODKEY,                       XKB_KEY_i,       incnmaster,       {.i = +1} },
	{ MODKEY,                       XKB_KEY_d,       incnmaster,       {.i = -1} },
	{ MODKEY,                       XKB_KEY_h,       setmfact,         {.f = -0.05f} },
	{ MODKEY,                       XKB_KEY_l,       setmfact,         {.f = +0.05f} },
	{ MODKEY,                       XKB_KEY_Return,  zoom,             {0} },
	{ MODKEY,                       XKB_KEY_Tab,     view,             {0} },
	{ MODKEY|ShiftMask,             XKB_KEY_c,       killclient,       {0} },
	{ MODKEY,                       XKB_KEY_t,       setlayout,        {.v = &layouts[0]} },
	{ MODKEY,                       XKB_KEY_f,       setlayout,        {.v = &layouts[1]} },
	{ MODKEY,                       XKB_KEY_m,       setlayout,        {.v = &layouts[2]} },
	{ MODKEY,                       XKB_KEY_space,   setlayout,        {0} },
	{ MODKEY|ShiftMask,             XKB_KEY_space,   togglefloating,   {0} },
	{ MODKEY|ShiftMask,             XKB_KEY_f,       togglefullscreen, {0} },
	{ MODKEY,                       XKB_KEY_0,       view,             {.ui = ~0u} },
	{ MODKEY|ShiftMask,             XKB_KEY_0,       tag,              {.ui = ~0u} },
	{ MODKEY,                       XKB_KEY_comma,   focusmon,         {.i = -1} },
	{ MODKEY,                       XKB_KEY_period,  focusmon,         {.i = +1} },
	{ MODKEY|ShiftMask,             XKB_KEY_comma,   tagmon,           {.i = -1} },
	{ MODKEY|ShiftMask,             XKB_KEY_period,  tagmon,           {.i = +1} },
	TAGKEYS(                        XKB_KEY_1,                        0),
	TAGKEYS(                        XKB_KEY_2,                        1),
	TAGKEYS(                        XKB_KEY_3,                        2),
	TAGKEYS(                        XKB_KEY_4,                        3),
	TAGKEYS(                        XKB_KEY_5,                        4),
	TAGKEYS(                        XKB_KEY_6,                        5),
	TAGKEYS(                        XKB_KEY_7,                        6),
	TAGKEYS(                        XKB_KEY_8,                        7),
	TAGKEYS(                        XKB_KEY_9,                        8),
	{ MODKEY|ShiftMask,             XKB_KEY_q,       quit,             {0} },
};

/* button definitions */
static const Button buttons[] = {
	/* modifier   button      function          argument */
	{ MODKEY,     BTN_LEFT,   movemouse,        {0} },
	{ MODKEY,     BTN_MIDDLE, togglefloating,   {0} },
	{ MODKEY,     BTN_RIGHT,  resizemouse,      {0} },
};

#undef Mod1Mask
#undef Mod4Mask
#undef ShiftMask
#undef ControlMask
#undef MODKEY
#undef TAGKEYS
#undef COLOR
