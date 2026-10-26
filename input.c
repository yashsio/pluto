// input.c -- libinput device configuration for Pluto.
//
// River deliberately leaves input device configuration to the window manager
// through river-libinput-config-v1.  A WM that does not use the protocol gets
// libinput's defaults, which means tap-to-click is off on most touchpads.
//
// We only touch the settings that matter for a touchpad.  Everything else is
// left exactly as the device/library default.
//
// Copyright 2026 Yash Sharma.  See LICENSE for details.

#include "pluto.h"

static void handle_input_device(void *data, struct river_libinput_device_v1 *obj,
		struct river_input_device_v1 *device) {}

static void handle_removed(void *data, struct river_libinput_device_v1 *obj) {
	river_libinput_device_v1_destroy(obj);
}

// A result object is destroyed by us as soon as the server reports the
// outcome.  We do not need the outcome for anything.
static void result_handle_done(void *data, struct river_libinput_result_v1 *obj) {
	river_libinput_result_v1_destroy(obj);
}

static const struct river_libinput_result_v1_listener result_listener = {
	.success = result_handle_done,
	.unsupported = result_handle_done,
	.invalid = result_handle_done,
};

static void apply_result(struct river_libinput_result_v1 *result) {
	if (result != NULL)
		river_libinput_result_v1_add_listener(result,
				&result_listener, NULL);
}

static void handle_tap_support(void *data,
		struct river_libinput_device_v1 *obj, int32_t finger_count) {
	if (finger_count <= 0)
		return;

	apply_result(river_libinput_device_v1_set_tap(obj, tap_to_click
			? RIVER_LIBINPUT_DEVICE_V1_TAP_STATE_ENABLED
			: RIVER_LIBINPUT_DEVICE_V1_TAP_STATE_DISABLED));

	apply_result(river_libinput_device_v1_set_drag(obj, tap_drag
			? RIVER_LIBINPUT_DEVICE_V1_DRAG_STATE_ENABLED
			: RIVER_LIBINPUT_DEVICE_V1_DRAG_STATE_DISABLED));
}

static void handle_natural_scroll_support(void *data,
		struct river_libinput_device_v1 *obj, int32_t supported) {
	if (!supported)
		return;

	apply_result(river_libinput_device_v1_set_natural_scroll(obj,
			natural_scroll
			? RIVER_LIBINPUT_DEVICE_V1_NATURAL_SCROLL_STATE_ENABLED
			: RIVER_LIBINPUT_DEVICE_V1_NATURAL_SCROLL_STATE_DISABLED));
}

// Every remaining event must still have a non-null listener: libwayland aborts
// when it dispatches an event whose listener function is NULL.
#define IGNORE_U32(NAME) \
	static void NAME(void *data, struct river_libinput_device_v1 *obj, \
			uint32_t value) {}
#define IGNORE_I32(NAME) \
	static void NAME(void *data, struct river_libinput_device_v1 *obj, \
			int32_t value) {}
#define IGNORE_ARRAY(NAME) \
	static void NAME(void *data, struct river_libinput_device_v1 *obj, \
			struct wl_array *value) {}

IGNORE_U32(handle_send_events_support)
IGNORE_U32(handle_send_events_default)
IGNORE_U32(handle_send_events_current)
IGNORE_U32(handle_tap_default)
IGNORE_U32(handle_tap_current)
IGNORE_U32(handle_tap_button_map_default)
IGNORE_U32(handle_tap_button_map_current)
IGNORE_U32(handle_drag_default)
IGNORE_U32(handle_drag_current)
IGNORE_U32(handle_drag_lock_default)
IGNORE_U32(handle_drag_lock_current)
IGNORE_I32(handle_three_finger_drag_support)
IGNORE_U32(handle_three_finger_drag_default)
IGNORE_U32(handle_three_finger_drag_current)
IGNORE_I32(handle_calibration_matrix_support)
IGNORE_ARRAY(handle_calibration_matrix_default)
IGNORE_ARRAY(handle_calibration_matrix_current)
IGNORE_U32(handle_accel_profiles_support)
IGNORE_U32(handle_accel_profile_default)
IGNORE_U32(handle_accel_profile_current)
IGNORE_ARRAY(handle_accel_speed_default)
IGNORE_ARRAY(handle_accel_speed_current)
IGNORE_U32(handle_natural_scroll_default)
IGNORE_U32(handle_natural_scroll_current)
IGNORE_I32(handle_left_handed_support)
IGNORE_U32(handle_left_handed_default)
IGNORE_U32(handle_left_handed_current)
IGNORE_U32(handle_click_method_support)
IGNORE_U32(handle_click_method_default)
IGNORE_U32(handle_click_method_current)
IGNORE_U32(handle_clickfinger_button_map_default)
IGNORE_U32(handle_clickfinger_button_map_current)
IGNORE_I32(handle_middle_emulation_support)
IGNORE_U32(handle_middle_emulation_default)
IGNORE_U32(handle_middle_emulation_current)
IGNORE_U32(handle_scroll_method_support)
IGNORE_U32(handle_scroll_method_default)
IGNORE_U32(handle_scroll_method_current)
IGNORE_U32(handle_scroll_button_default)
IGNORE_U32(handle_scroll_button_current)
IGNORE_U32(handle_scroll_button_lock_default)
IGNORE_U32(handle_scroll_button_lock_current)
IGNORE_I32(handle_dwt_support)
IGNORE_U32(handle_dwt_default)
IGNORE_U32(handle_dwt_current)
IGNORE_I32(handle_dwtp_support)
IGNORE_U32(handle_dwtp_default)
IGNORE_U32(handle_dwtp_current)
IGNORE_I32(handle_rotation_support)
IGNORE_U32(handle_rotation_default)
IGNORE_U32(handle_rotation_current)

#undef IGNORE_U32
#undef IGNORE_I32
#undef IGNORE_ARRAY

static const struct river_libinput_device_v1_listener device_listener = {
	.removed = handle_removed,
	.input_device = handle_input_device,
	.send_events_support = handle_send_events_support,
	.send_events_default = handle_send_events_default,
	.send_events_current = handle_send_events_current,
	.tap_support = handle_tap_support,
	.tap_default = handle_tap_default,
	.tap_current = handle_tap_current,
	.tap_button_map_default = handle_tap_button_map_default,
	.tap_button_map_current = handle_tap_button_map_current,
	.drag_default = handle_drag_default,
	.drag_current = handle_drag_current,
	.drag_lock_default = handle_drag_lock_default,
	.drag_lock_current = handle_drag_lock_current,
	.three_finger_drag_support = handle_three_finger_drag_support,
	.three_finger_drag_default = handle_three_finger_drag_default,
	.three_finger_drag_current = handle_three_finger_drag_current,
	.calibration_matrix_support = handle_calibration_matrix_support,
	.calibration_matrix_default = handle_calibration_matrix_default,
	.calibration_matrix_current = handle_calibration_matrix_current,
	.accel_profiles_support = handle_accel_profiles_support,
	.accel_profile_default = handle_accel_profile_default,
	.accel_profile_current = handle_accel_profile_current,
	.accel_speed_default = handle_accel_speed_default,
	.accel_speed_current = handle_accel_speed_current,
	.natural_scroll_support = handle_natural_scroll_support,
	.natural_scroll_default = handle_natural_scroll_default,
	.natural_scroll_current = handle_natural_scroll_current,
	.left_handed_support = handle_left_handed_support,
	.left_handed_default = handle_left_handed_default,
	.left_handed_current = handle_left_handed_current,
	.click_method_support = handle_click_method_support,
	.click_method_default = handle_click_method_default,
	.click_method_current = handle_click_method_current,
	.clickfinger_button_map_default = handle_clickfinger_button_map_default,
	.clickfinger_button_map_current = handle_clickfinger_button_map_current,
	.middle_emulation_support = handle_middle_emulation_support,
	.middle_emulation_default = handle_middle_emulation_default,
	.middle_emulation_current = handle_middle_emulation_current,
	.scroll_method_support = handle_scroll_method_support,
	.scroll_method_default = handle_scroll_method_default,
	.scroll_method_current = handle_scroll_method_current,
	.scroll_button_default = handle_scroll_button_default,
	.scroll_button_current = handle_scroll_button_current,
	.scroll_button_lock_default = handle_scroll_button_lock_default,
	.scroll_button_lock_current = handle_scroll_button_lock_current,
	.dwt_support = handle_dwt_support,
	.dwt_default = handle_dwt_default,
	.dwt_current = handle_dwt_current,
	.dwtp_support = handle_dwtp_support,
	.dwtp_default = handle_dwtp_default,
	.dwtp_current = handle_dwtp_current,
	.rotation_support = handle_rotation_support,
	.rotation_default = handle_rotation_default,
	.rotation_current = handle_rotation_current,
};

static void handle_libinput_device(void *data,
		struct river_libinput_config_v1 *obj,
		struct river_libinput_device_v1 *device) {
	river_libinput_device_v1_add_listener(device, &device_listener, NULL);
}

static void handle_finished(void *data, struct river_libinput_config_v1 *obj) {
	river_libinput_config_v1_destroy(obj);
}

static const struct river_libinput_config_v1_listener config_listener = {
	.libinput_device = handle_libinput_device,
	.finished = handle_finished,
};

void init_input_config(struct wl_registry *registry, uint32_t name) {
	struct river_libinput_config_v1 *config = wl_registry_bind(registry, name,
			&river_libinput_config_v1_interface, 1);

	// Attach the listener immediately: river sends the initial state of every
	// device as soon as the global is bound, and those events must not be
	// dispatched before we are listening for them.
	river_libinput_config_v1_add_listener(config, &config_listener, NULL);
}
