#include <wayland-server.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <wlr/util/log.h>

#include "rootston/xcursor.h"
#include "rootston/input.h"
#include "rootston/seat.h"
#include "rootston/keyboard.h"
#include "rootston/cursor.h"

static void handle_keyboard_key(struct wl_listener *listener, void *data) {
	struct roots_keyboard *keyboard =
		wl_container_of(listener, keyboard, keyboard_key);
	struct wlr_event_keyboard_key *event = data;
	roots_keyboard_handle_key(keyboard, event);
}

static void handle_keyboard_modifiers(struct wl_listener *listener,
		void *data) {
	struct roots_keyboard *keyboard =
		wl_container_of(listener, keyboard, keyboard_modifiers);
	roots_keyboard_handle_modifiers(keyboard);
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, motion);
	struct wlr_event_pointer_motion *event = data;
	roots_cursor_handle_motion(cursor, event);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, motion_absolute);
	struct wlr_event_pointer_motion_absolute *event = data;
	roots_cursor_handle_motion_absolute(cursor, event);
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, button);
	struct wlr_event_pointer_button *event = data;
	roots_cursor_handle_button(cursor, event);
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, axis);
	struct wlr_event_pointer_axis *event = data;
	roots_cursor_handle_axis(cursor, event);
}

static void handle_touch_down(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, touch_down);
	struct wlr_event_touch_down *event = data;
	roots_cursor_handle_touch_down(cursor, event);
}

static void handle_touch_up(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, touch_up);
	struct wlr_event_touch_up *event = data;
	roots_cursor_handle_touch_up(cursor, event);
}

static void handle_touch_motion(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, touch_motion);
	struct wlr_event_touch_motion *event = data;
	roots_cursor_handle_touch_motion(cursor, event);
}

static void handle_tool_axis(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, tool_axis);
	struct wlr_event_tablet_tool_axis *event = data;
	roots_cursor_handle_tool_axis(cursor, event);
}

static void handle_tool_tip(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, tool_tip);
	struct wlr_event_tablet_tool_tip *event = data;
	roots_cursor_handle_tool_tip(cursor, event);
}

static void handle_request_set_cursor(struct wl_listener *listener,
		void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, request_set_cursor);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	roots_cursor_handle_request_set_cursor(cursor, event);
}

static void handle_pointer_grab_begin(struct wl_listener *listener,
		void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, pointer_grab_begin);
	struct wlr_seat_pointer_grab *grab = data;
	roots_cursor_handle_pointer_grab_begin(cursor, grab);
}

static void handle_pointer_grab_end(struct wl_listener *listener,
		void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, pointer_grab_end);
	struct wlr_seat_pointer_grab *grab = data;
	roots_cursor_handle_pointer_grab_end(cursor, grab);
}

static void seat_reset_device_mappings(struct roots_seat *seat, struct wlr_input_device *device) {
	struct wlr_cursor *cursor = seat->cursor->cursor;
	struct roots_config *config = seat->input->config;

	wlr_cursor_map_input_to_output(cursor, device, NULL);
	struct roots_device_config *dconfig;
	if ((dconfig = roots_config_get_device(config, device))) {
		wlr_cursor_map_input_to_region(cursor, device, dconfig->mapped_box);
	}
}

static void seat_set_device_output_mappings(struct roots_seat *seat,
		struct wlr_input_device *device, struct wlr_output *output) {
	struct wlr_cursor *cursor = seat->cursor->cursor;
	struct roots_config *config = seat->input->config;
	struct roots_device_config *dconfig;
	dconfig = roots_config_get_device(config, device);
	if (dconfig && dconfig->mapped_output &&
			strcmp(dconfig->mapped_output, output->name) == 0) {
		wlr_cursor_map_input_to_output(cursor, device, output);
	}
}

void roots_seat_configure_cursor(struct roots_seat *seat) {
	struct roots_config *config = seat->input->config;
	struct roots_desktop *desktop = seat->input->server->desktop;
	struct wlr_cursor *cursor = seat->cursor->cursor;

	struct roots_pointer *pointer;
	struct roots_touch *touch;
	struct roots_tablet_tool *tablet_tool;
	struct roots_output *output;

	// reset mappings
	wlr_cursor_map_to_output(cursor, NULL);
	wl_list_for_each(pointer, &seat->pointers, link) {
		seat_reset_device_mappings(seat, pointer->device);
	}
	wl_list_for_each(touch, &seat->touch, link) {
		seat_reset_device_mappings(seat, touch->device);
	}
	wl_list_for_each(tablet_tool, &seat->tablet_tools, link) {
		seat_reset_device_mappings(seat, tablet_tool->device);
	}

	// configure device to output mappings
	const char *mapped_output = config->cursor.mapped_output;
	wl_list_for_each(output, &desktop->outputs, link) {
		if (mapped_output && strcmp(mapped_output, output->wlr_output->name) == 0) {
			wlr_cursor_map_to_output(cursor, output->wlr_output);
		}

		wl_list_for_each(pointer, &seat->pointers, link) {
			seat_set_device_output_mappings(seat, pointer->device, output->wlr_output);
		}
		wl_list_for_each(tablet_tool, &seat->tablet_tools, link) {
			seat_set_device_output_mappings(seat, tablet_tool->device, output->wlr_output);
		}
		wl_list_for_each(touch, &seat->touch, link) {
			seat_set_device_output_mappings(seat, touch->device, output->wlr_output);
		}
	}
}

static void roots_seat_init_cursor(struct roots_seat *seat) {
	seat->cursor = roots_cursor_create(seat);
	if (!seat->cursor) {
		return;
	}
	seat->cursor->seat = seat;
	struct wlr_cursor *wlr_cursor = seat->cursor->cursor;
	struct roots_desktop *desktop = seat->input->server->desktop;
	wlr_cursor_attach_output_layout(wlr_cursor, desktop->layout);

	roots_xcursor_theme_set_default(desktop->xcursor_theme, wlr_cursor);

	wl_list_init(&seat->cursor->touch_points);

	roots_seat_configure_cursor(seat);

	// add input signals
	wl_signal_add(&wlr_cursor->events.motion, &seat->cursor->motion);
	seat->cursor->motion.notify = handle_cursor_motion;

	wl_signal_add(&wlr_cursor->events.motion_absolute,
		&seat->cursor->motion_absolute);
	seat->cursor->motion_absolute.notify = handle_cursor_motion_absolute;

	wl_signal_add(&wlr_cursor->events.button, &seat->cursor->button);
	seat->cursor->button.notify = handle_cursor_button;

	wl_signal_add(&wlr_cursor->events.axis, &seat->cursor->axis);
	seat->cursor->axis.notify = handle_cursor_axis;

	wl_signal_add(&wlr_cursor->events.touch_down, &seat->cursor->touch_down);
	seat->cursor->touch_down.notify = handle_touch_down;

	wl_signal_add(&wlr_cursor->events.touch_up, &seat->cursor->touch_up);
	seat->cursor->touch_up.notify = handle_touch_up;

	wl_signal_add(&wlr_cursor->events.touch_motion, &seat->cursor->touch_motion);
	seat->cursor->touch_motion.notify = handle_touch_motion;

	wl_signal_add(&wlr_cursor->events.tablet_tool_axis, &seat->cursor->tool_axis);
	seat->cursor->tool_axis.notify = handle_tool_axis;

	wl_signal_add(&wlr_cursor->events.tablet_tool_tip, &seat->cursor->tool_tip);
	seat->cursor->tool_tip.notify = handle_tool_tip;

	wl_signal_add(&seat->seat->events.request_set_cursor,
			&seat->cursor->request_set_cursor);
	seat->cursor->request_set_cursor.notify = handle_request_set_cursor;

	wl_signal_add(&seat->seat->events.pointer_grab_begin,
			&seat->cursor->pointer_grab_begin);
	seat->cursor->pointer_grab_begin.notify = handle_pointer_grab_begin;

	wl_signal_add(&seat->seat->events.pointer_grab_end,
			&seat->cursor->pointer_grab_end);
	seat->cursor->pointer_grab_end.notify = handle_pointer_grab_end;
}

struct roots_seat *roots_seat_create(struct roots_input *input, char *name) {
	struct roots_seat *seat = calloc(1, sizeof(struct roots_seat));
	if (!seat) {
		return NULL;
	}

	wl_list_init(&seat->keyboards);
	wl_list_init(&seat->pointers);
	wl_list_init(&seat->touch);
	wl_list_init(&seat->tablet_tools);
	wl_list_init(&seat->drag_icons);

	seat->input = input;

	seat->seat = wlr_seat_create(input->server->wl_display, name);
	if (!seat->seat) {
		free(seat);
		roots_cursor_destroy(seat->cursor);
		return NULL;
	}

	roots_seat_init_cursor(seat);
	if (!seat->cursor) {
		wlr_seat_destroy(seat->seat);
		free(seat);
		return NULL;
	}

	wlr_seat_set_capabilities(seat->seat,
		WL_SEAT_CAPABILITY_KEYBOARD |
		WL_SEAT_CAPABILITY_POINTER |
		WL_SEAT_CAPABILITY_TOUCH);

	wl_list_insert(&input->seats, &seat->link);

	return seat;
}

void roots_seat_destroy(struct roots_seat *seat) {
	// TODO
}

static void seat_add_keyboard(struct roots_seat *seat, struct wlr_input_device *device) {
	assert(device->type == WLR_INPUT_DEVICE_KEYBOARD);
	struct roots_keyboard *keyboard = roots_keyboard_create(device, seat->input);
	if (keyboard == NULL) {
		wlr_log(L_ERROR, "could not allocate keyboard for seat");
		return;
	}

	keyboard->seat = seat;

	wl_list_insert(&seat->keyboards, &keyboard->link);

	keyboard->keyboard_key.notify = handle_keyboard_key;
	wl_signal_add(&keyboard->device->keyboard->events.key,
		&keyboard->keyboard_key);

	keyboard->keyboard_modifiers.notify = handle_keyboard_modifiers;
	wl_signal_add(&keyboard->device->keyboard->events.modifiers,
		&keyboard->keyboard_modifiers);
}

static void seat_add_pointer(struct roots_seat *seat, struct wlr_input_device *device) {
	struct roots_pointer *pointer = calloc(sizeof(struct roots_pointer), 1);
	if (!pointer) {
		wlr_log(L_ERROR, "could not allocate pointer for seat");
		return;
	}

	device->data = pointer;
	pointer->device = device;
	pointer->seat = seat;
	wl_list_insert(&seat->pointers, &pointer->link);
	wlr_cursor_attach_input_device(seat->cursor->cursor, device);
	roots_seat_configure_cursor(seat);
}

static void seat_add_touch(struct roots_seat *seat, struct wlr_input_device *device) {
	struct roots_touch *touch = calloc(sizeof(struct roots_touch), 1);
	if (!touch) {
		wlr_log(L_ERROR, "could not allocate touch for seat");
		return;
	}

	device->data = touch;
	touch->device = device;
	touch->seat = seat;
	wl_list_insert(&seat->touch, &touch->link);
	wlr_cursor_attach_input_device(seat->cursor->cursor, device);
	roots_seat_configure_cursor(seat);
}

static void seat_add_tablet_pad(struct roots_seat *seat, struct wlr_input_device *device) {
	// TODO
}

static void seat_add_tablet_tool(struct roots_seat *seat, struct wlr_input_device *device) {
	struct roots_tablet_tool *tablet_tool = calloc(sizeof(struct roots_tablet_tool), 1);
	if (!tablet_tool) {
		wlr_log(L_ERROR, "could not allocate tablet_tool for seat");
		return;
	}

	device->data = tablet_tool;
	tablet_tool->device = device;
	tablet_tool->seat = seat;
	wl_list_insert(&seat->tablet_tools, &tablet_tool->link);
	wlr_cursor_attach_input_device(seat->cursor->cursor, device);
	roots_seat_configure_cursor(seat);
}

void roots_seat_add_device(struct roots_seat *seat,
		struct wlr_input_device *device) {
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		seat_add_keyboard(seat, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		seat_add_pointer(seat, device);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		seat_add_touch(seat, device);
		break;
	case WLR_INPUT_DEVICE_TABLET_PAD:
		seat_add_tablet_pad(seat, device);
		break;
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		seat_add_tablet_tool(seat, device);
		break;
	}
}

static void seat_remove_keyboard(struct roots_seat *seat,
		struct wlr_input_device *device) {
	struct roots_keyboard *keyboard;
	wl_list_for_each(keyboard, &seat->keyboards, link) {
		if (keyboard->device == device) {
			roots_keyboard_destroy(keyboard);
			return;
		}
	}
}

static void seat_remove_pointer(struct roots_seat *seat,
		struct wlr_input_device *device) {
	struct roots_pointer *pointer;
	wl_list_for_each(pointer, &seat->pointers, link) {
		if (pointer->device == device) {
			wl_list_remove(&pointer->link);
			wlr_cursor_detach_input_device(seat->cursor->cursor, device);
			free(pointer);
			return;
		}
	}
}

static void seat_remove_touch(struct roots_seat *seat,
		struct wlr_input_device *device) {
	struct roots_touch *touch;
	wl_list_for_each(touch, &seat->touch, link) {
		if (touch->device == device) {
			wl_list_remove(&touch->link);
			wlr_cursor_detach_input_device(seat->cursor->cursor, device);
			free(touch);
			return;
		}
	}
}

static void seat_remove_tablet_pad(struct roots_seat *seat,
		struct wlr_input_device *device) {
	// TODO
}

static void seat_remove_tablet_tool(struct roots_seat *seat,
		struct wlr_input_device *device) {
	struct roots_tablet_tool *tablet_tool;
	wl_list_for_each(tablet_tool, &seat->tablet_tools, link) {
		if (tablet_tool->device == device) {
			wl_list_remove(&tablet_tool->link);
			wlr_cursor_detach_input_device(seat->cursor->cursor, device);
			free(tablet_tool);
			return;
		}
	}
}

void roots_seat_remove_device(struct roots_seat *seat,
		struct wlr_input_device *device) {
	switch (device->type) {
	case WLR_INPUT_DEVICE_KEYBOARD:
		seat_remove_keyboard(seat, device);
		break;
	case WLR_INPUT_DEVICE_POINTER:
		seat_remove_pointer(seat, device);
		break;
	case WLR_INPUT_DEVICE_TOUCH:
		seat_remove_touch(seat, device);
		break;
	case WLR_INPUT_DEVICE_TABLET_PAD:
		seat_remove_tablet_pad(seat, device);
		break;
	case WLR_INPUT_DEVICE_TABLET_TOOL:
		seat_remove_tablet_tool(seat, device);
		break;
	}
}

void roots_seat_configure_xcursor(struct roots_seat *seat) {
	roots_xcursor_theme_set_default(seat->input->server->desktop->xcursor_theme,
		seat->cursor->cursor);
	wlr_cursor_warp(seat->cursor->cursor, NULL, seat->cursor->cursor->x,
		seat->cursor->cursor->y);
}

bool roots_seat_has_meta_pressed(struct roots_seat *seat) {
	struct roots_keyboard *keyboard;
	wl_list_for_each(keyboard, &seat->keyboards, link) {
		if (!keyboard->config->meta_key) {
			continue;
		}

		uint32_t modifiers =
			wlr_keyboard_get_modifiers(keyboard->device->keyboard);
		if ((modifiers ^ keyboard->config->meta_key) == 0) {
			return true;
		}
	}

	return false;
}

void roots_seat_focus_view(struct roots_seat *seat, struct roots_view *view) {
	struct roots_desktop *desktop = seat->input->server->desktop;
	if (seat->focus == view) {
		return;
	}

	if (view && view->type == ROOTS_XWAYLAND_VIEW &&
			view->xwayland_surface->override_redirect) {
		return;
	}

	struct roots_view *prev_focus = seat->focus;
	seat->focus = view;

	// unfocus the old view if it is not focused by some other seat
	if (prev_focus && !input_view_has_focus(seat->input, prev_focus)) {
		view_activate(prev_focus, false);
	}

	if (!seat->focus) {
		seat->cursor->mode = ROOTS_CURSOR_PASSTHROUGH;
		return;
	}

	size_t index = 0;
	for (size_t i = 0; i < desktop->views->length; ++i) {
		struct roots_view *_view = desktop->views->items[i];
		if (_view == view) {
			index = i;
			break;
		}
	}

	view_activate(view, true);
	// TODO: list_swap
	wlr_list_del(desktop->views, index);
	wlr_list_add(desktop->views, view);
	wlr_seat_keyboard_notify_enter(seat->seat, view->wlr_surface);
}

void roots_seat_begin_move(struct roots_seat *seat, struct roots_view *view) {
	struct roots_cursor *cursor = seat->cursor;
	cursor->mode = ROOTS_CURSOR_MOVE;
	cursor->offs_x = cursor->cursor->x;
	cursor->offs_y = cursor->cursor->y;
	if (view->maximized) {
		cursor->view_x = view->saved.x;
		cursor->view_y = view->saved.y;
	} else {
		cursor->view_x = view->x;
		cursor->view_y = view->y;
	}
	view_maximize(view, false);
	wlr_seat_pointer_clear_focus(seat->seat);

	roots_xcursor_theme_set_move(seat->input->server->desktop->xcursor_theme,
		seat->cursor->cursor);
}

void roots_seat_begin_resize(struct roots_seat *seat, struct roots_view *view,
		uint32_t edges) {
	struct roots_cursor *cursor = seat->cursor;
	cursor->mode = ROOTS_CURSOR_RESIZE;
	cursor->offs_x = cursor->cursor->x;
	cursor->offs_y = cursor->cursor->y;
	if (view->maximized) {
		cursor->view_x = view->saved.x;
		cursor->view_y = view->saved.y;
		cursor->view_width = view->saved.width;
		cursor->view_height = view->saved.height;
	} else {
		cursor->view_x = view->x;
		cursor->view_y = view->y;
		struct wlr_box box;
		view_get_box(view, &box);
		cursor->view_width = box.width;
		cursor->view_height = box.height;
	}
	cursor->resize_edges = edges;
	view_maximize(view, false);
	wlr_seat_pointer_clear_focus(seat->seat);

	roots_xcursor_theme_set_resize(seat->input->server->desktop->xcursor_theme,
		seat->cursor->cursor, edges);
}

void roots_seat_begin_rotate(struct roots_seat *seat, struct roots_view *view) {
	struct roots_cursor *cursor = seat->cursor;
	cursor->mode = ROOTS_CURSOR_ROTATE;
	cursor->offs_x = cursor->cursor->x;
	cursor->offs_y = cursor->cursor->y;
	cursor->view_rotation = view->rotation;
	view_maximize(view, false);
	wlr_seat_pointer_clear_focus(seat->seat);

	roots_xcursor_theme_set_rotate(seat->input->server->desktop->xcursor_theme,
		seat->cursor->cursor);
}
