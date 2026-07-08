// layout.c -- Core window layout+rendering logic for Pluto (fork of JrWM)
//
// This file is responsible for the layout and rendering of windows, making it
// responsible for much of the core "look and feel" of the WM.
//
// Copyright 2026 Isaac Freund, 2026 Jack Conger.  See LICENSE for details.
// Copyright 2026 Yash Sharma. See LICENSE for details.

#include <stdlib.h>

#include "pluto.h"

// Private functions for window management and rendering

static bool valid_rect(struct Rect r) {
	return r.width >= 0 && r.height >= 0;
}

// In-place shrink the boundaries of a Rect to accommodate the given border radius
static void subtract_border(struct Rect *rect, int32_t border) {
	rect->x = rect->x + border;
	rect->y = rect->y + border;
	rect->width  = (rect->width  - border * 2 < 0) ? 0 : rect->width  - border * 2;
	rect->height = (rect->height - border * 2 < 0) ? 0 : rect->height - border * 2;
}

static void render_border(struct Window *window, int thickness, uint32_t *color) {
	river_window_v1_set_borders(window->obj, 15, thickness,
			color[0], color[1], color[2], color[3]);
}

static void unfullscreen_window(struct Window *window) {
	river_window_v1_exit_fullscreen(window->obj);
	river_window_v1_inform_not_fullscreen(window->obj);
	window->fake_fullscreen = false;
	window->fullscreen = false;
}


// BSP tree management for Hyprland-style dwindle layout

static struct BspNode *bsp_create_leaf(struct Window *window) {
	struct BspNode *node = calloc(1, sizeof(struct BspNode));
	node->window = window;
	return node;
}

static struct BspNode *bsp_find_leaf(struct BspNode *node, struct Window *window) {
	if (node == NULL) return NULL;
	if (node->window == window) return node;
	struct BspNode *found = bsp_find_leaf(node->first, window);
	if (found != NULL) return found;
	return bsp_find_leaf(node->second, window);
}

extern void bsp_insert_window(struct Space *space,
		struct Window *new_window, struct Window *old_focused) {
	if (space->bsp_root == NULL) {
		space->bsp_root = bsp_create_leaf(new_window);
		return;
	}

	struct BspNode *leaf = bsp_find_leaf(space->bsp_root, old_focused);
	if (leaf == NULL)
		return;

	bool vertical;
	if (leaf->parent == NULL)
		vertical = false;
	else
		vertical = !leaf->parent->vertical;

	struct BspNode *split = calloc(1, sizeof(struct BspNode));
	split->parent = leaf->parent;
	split->vertical = vertical;
	split->ratio = 0.5f;

	split->first = leaf;
	struct BspNode *second = bsp_create_leaf(new_window);
	split->second = second;
	leaf->parent = split;
	second->parent = split;

	if (split->parent == NULL) {
		space->bsp_root = split;
	} else {
		if (split->parent->first == leaf)
			split->parent->first = split;
		else
			split->parent->second = split;
	}
}

extern void bsp_remove_window(struct Space *space, struct Window *window) {
	struct BspNode *leaf = bsp_find_leaf(space->bsp_root, window);
	if (leaf == NULL) return;

	if (leaf == space->bsp_root) {
		free(leaf);
		space->bsp_root = NULL;
		return;
	}

	struct BspNode *parent = leaf->parent;
	struct BspNode *sibling = (parent->first == leaf)
			? parent->second : parent->first;

	sibling->parent = parent->parent;

	if (parent->parent == NULL) {
		space->bsp_root = sibling;
	} else {
		if (parent->parent->first == parent)
			parent->parent->first = sibling;
		else
			parent->parent->second = sibling;
	}

	free(parent);
	free(leaf);
}

extern void bsp_free_tree(struct BspNode *node) {
	if (node == NULL) return;
	bsp_free_tree(node->first);
	bsp_free_tree(node->second);
	free(node);
}


// Handle creation/deletion of the core objects that all point to each other.
//  - These run before the relevant wl_list in wm is updated
//  - None of these run during a manage or render sequence

// Find a Space for this Output to activate
extern void place_output(struct Output *output) {
	struct Space *space;

	// If we have a focused, inactive Space, use that
	struct Seat *seat;
	wl_list_for_each(seat, &wm.seats, link) {
		space = seat->focused;
		if (space->output == NULL || space->output->active != space) {
			output->active = seat->focused;
			seat->focused->output = output;
			return;
		}
	}

	// Otherwise, just pick the first inactive Space
	wl_list_for_each(space, &wm.spaces, link) {
		if (space->output == NULL || space->output->active != space) {
			output->active = space;
			space->output = output;
			return;
		}
	}

	// Fallback: make a new space to use!  This may cause problems
	// with the "nth-space" bindings, but it's better than segfaults.
	space = create_space();
	output->active = space;
	space->output = output;
	wl_list_insert(&wm.spaces, &space->link);
}

// Replace this Output with any other where necessary
extern void replace_output(struct Output *output) {
	struct Output *replacement = NULL, *r;
	struct Space *space;
	struct Seat *seat;

	// Pick a random other Output, if there are any.
	wl_list_for_each(r, &wm.outputs, link)
		if (r != output)
			replacement = r;

	// Assign that Output (or NULL, if no other Outputs exist) to any Spaces
	wl_list_for_each(space, &wm.spaces, link) {
		if (space->output != output)
			continue;
		space->output = replacement;

		// Make the Space active on the new Output if it is focused
		if (space == output->active)
			wl_list_for_each(seat, &wm.seats, link)
				if (space == seat->focused && space->output != NULL)
					space->output->active = space;
	}
}

// Find a Space for this Window to be in
extern void place_window(struct Window *window) {
	struct Window *old_focused = NULL;
	struct Space *space = NULL;

	// If there is one, pick the first Seat's focused Space
	// TODO: Pick the correct Seat, once we have a way to know what that is
	if (!wl_list_empty(&wm.seats)) {
		struct Seat *seat = wl_container_of(wm.seats.next, seat, link);
		space = seat->focused;
		old_focused = space->focused;
		space->focused = window;
	}

	// Fallback: just pick the first Space
	if (space == NULL) {
		space = wl_container_of(wm.spaces.next, space, link);
		old_focused = space->focused;
		space->focused = window;
	}

	window->space = space;
	bsp_insert_window(space, window, old_focused);
}

// Replace this Window with any other where necessary
extern void replace_window(struct Window *window) {
	struct Seat *seat;
	// Unclear if this is even possible, but let's play it safe
	wl_list_for_each(seat, &wm.seats, link)
		if (seat->entered == window)
			seat->entered = NULL;

	bsp_remove_window(window->space, window);

	if (window->space->focused != window)
		return;
	struct Window *r, *replacement = NULL;

	// Focus the previous window in the space, else the first, else none
	wl_list_for_each_reverse(r, &window->link, link) {
		if (&r->link == &wm.windows)
			break;
		if (r->space == window->space) {
			replacement = r;
			break;
		}
	}
	if (replacement == NULL) {
		wl_list_for_each(r, &window->link, link) {
			if (&r->link == &wm.windows)
				break;
			if (r->space == window->space) {
				replacement = r;
				break;
			}
		}
	}
	window->space->focused = replacement;
}

// Find a Space for this Seat to focus on
extern void place_seat(struct Seat *seat) {
	seat->focused = wl_container_of(wm.spaces.next, seat->focused, link);
}


// Space layout functions

extern void monocle_layout(struct Space *space, struct Rect bounds) {
	struct Window *window;
	wl_list_for_each(window, &wm.windows, link) {
		if (window->space != space)
			continue;
		if (window->space->focused != NULL &&
				window->space->focused != window) {
			// HACK: Intentionally invalidate Rect to prevent rendering
			window->layout.width = window->layout.height = -1;
			continue;
		}
		if (!window->maximized) {
			river_window_v1_inform_maximized(window->obj);
			window->maximized = true;
		}
		subtract_border(&bounds, monocle_borderpx);
		window->layout = bounds;
	}
}

static void bsp_traverse(struct BspNode *node, struct Rect bounds, int gap) {
	if (node == NULL) return;

	if (node->window != NULL) {
		node->window->layout = bounds;
		return;
	}

	struct Rect first_bounds, second_bounds;
	if (!node->vertical) {
		int avail = bounds.width - gap;
		if (avail < 0) avail = 0;
		first_bounds = bounds;
		first_bounds.width = (int32_t)(avail * node->ratio);
		second_bounds = bounds;
		second_bounds.x = bounds.x + first_bounds.width + gap;
		second_bounds.width = bounds.width - first_bounds.width - gap;
		if (second_bounds.width < 0) second_bounds.width = 0;
	} else {
		int avail = bounds.height - gap;
		if (avail < 0) avail = 0;
		first_bounds = bounds;
		first_bounds.height = (int32_t)(avail * node->ratio);
		second_bounds = bounds;
		second_bounds.y = bounds.y + first_bounds.height + gap;
		second_bounds.height = bounds.height - first_bounds.height - gap;
		if (second_bounds.height < 0) second_bounds.height = 0;
	}

	bsp_traverse(node->first, first_bounds, gap);
	bsp_traverse(node->second, second_bounds, gap);
}

extern void bsp_layout(struct Space *space, struct Rect bounds) {
	subtract_border(&bounds, gaps_outer);

	if (space->bsp_root == NULL) return;

	struct Window *window;
	wl_list_for_each(window, &wm.windows, link) {
		if (window->space == space && window->maximized) {
			river_window_v1_inform_unmaximized(window->obj);
			window->maximized = false;
		}
	}

	int gap = -gaps_inner;
	if (gap < 0) gap = 0;

	bsp_traverse(space->bsp_root, bounds, gap);

	wl_list_for_each(window, &wm.windows, link) {
		if (window->space == space)
			subtract_border(&window->layout, border_width);
	}
}


// Manage sequence/render sequence functions

// Perform actions for a Window that have been called for by some event, but
// must be done during the manage sequence
extern void manage_window_deferred(struct Window *window) {
	if (window->set_capabilities) {
		river_window_v1_set_capabilities(window->obj,
				RIVER_WINDOW_V1_CAPABILITIES_MAXIMIZE |
				RIVER_WINDOW_V1_CAPABILITIES_FULLSCREEN);
		window->set_capabilities = false;
	}
	if (window->close) {
		river_window_v1_close(window->obj);
		window->close = false;
	}
	if (window->enter_fake_fullscreen) {
		river_window_v1_inform_fullscreen(window->obj);
		window->fake_fullscreen = true;
		window->enter_fake_fullscreen = false;
	}
	if (window->enter_fullscreen) {
		struct Space *space = window->space;
		if (space->output != NULL &&
				space->output->active == space) {
			river_window_v1_inform_fullscreen(window->obj);
			river_window_v1_fullscreen(window->obj,
					space->output->obj);
			window->fullscreen = true;
		}
		window->enter_fullscreen = false;
	}
	if (window->exit_fullscreen) {
		unfullscreen_window(window);
		window->exit_fullscreen = false;
	}
}

// Per-Space focus is technically just internal bookkeeping; this function
// propagates the "real", per-Seat, focus state to the compositor during each
// manage sequence.
// Called at the end of the sequence so that other functions can modify focus
extern void manage_seat_focus(struct Seat *seat) {
	// Change focus, if necessary
	if (seat->moved && seat->entered != NULL) {
		seat->focused = seat->entered->space;
		seat->focused->focused = seat->entered;
	}

	// Propagate focus information to River
	if (seat->focused->output != NULL)
		river_layer_shell_output_v1_set_default(seat->focused->output->ls);
	if (seat->focused->focused != NULL)
		river_seat_v1_focus_window(seat->obj, seat->focused->focused->obj);
	else
		river_seat_v1_clear_focus(seat->obj);

	// Un-fullscreen if there's a layer shell focused
	if (seat->focused->focused != NULL
			&& seat->focused->focused->fullscreen
			&& seat->ls_focused) {
		unfullscreen_window(seat->focused->focused);
	}

	// Warp the pointer to the focused window
	if (pointer_follows_focus && seat->warp && !seat->ls_focused) {
		struct Window *window = seat->focused->focused;
		if (window != NULL) {
			int32_t x = window->layout.x + window->layout.width/2;
			int32_t y = window->layout.y + window->layout.height/2;
			river_seat_v1_pointer_warp(seat->obj, x, y);
		}
	}
	seat->warp = false;
	seat->moved = false;
	seat->entered = NULL;
}

// Perform the main, per-Space, manage sequence logic
extern void manage_space(struct Space *space) {
	struct Output *output = active_on_output(space);
	if (output == NULL)
		return;

	if (space->layout != NULL)
		space->layout(space, output->windowed);

	struct Window *window;
	wl_list_for_each(window, &wm.windows, link) {
		if (window->space != output->active || !valid_rect(window->layout))
			continue;
		if (window->fullscreen && window->space->focused != window)
			unfullscreen_window(window);
		river_window_v1_use_ssd(window->obj);
		river_window_v1_set_tiled(window->obj, 15);
		river_window_v1_propose_dimensions(window->obj,
				window->layout.width,
				window->layout.height);
	}
}

// Perform the main, per-Space, render sequence logic
extern void render_space(struct Space *space) {
	struct Output *output = active_on_output(space);
	if (output == NULL)
		return;

	struct Window *window;
	wl_list_for_each(window, &wm.windows, link) {
		if (window->space != space || !valid_rect(window->layout))
			continue;
		river_window_v1_show(window->obj);
		river_node_v1_set_position(window->node,
				window->layout.x, window->layout.y);
		if (space->layout == monocle_layout)
			render_border(window, monocle_borderpx, border_color);
		else
			render_border(window, border_width, border_color);
	}
}

extern void render_seat_focus(struct Seat *seat) {
	struct Window *window = seat->focused->focused;
	if (window == NULL || seat->ls_focused)
		return;
	river_node_v1_place_top(window->node);
	if (seat->focused->layout == monocle_layout)
		render_border(window, monocle_borderpx, focused_color);
	else
		render_border(window, border_width, focused_color);
}
