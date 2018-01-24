#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/config.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/util/log.h>
#include <wlr/types/wlr_idle.h>
#include "rootston/xcursor.h"
#include "rootston/input.h"
#include "rootston/seat.h"
#include "rootston/keyboard.h"
#include "rootston/cursor.h"

static void handle_keyboard_key(struct wl_listener *listener, void *data) {
	struct roots_keyboard *keyboard =
		wl_container_of(listener, keyboard, keyboard_key);
	struct roots_desktop *desktop = keyboard->input->server->desktop;
	wlr_idle_notify_activity(desktop->idle, keyboard->seat->seat);
	struct wlr_event_keyboard_key *event = data;
	roots_keyboard_handle_key(keyboard, event);
}

static void handle_keyboard_modifiers(struct wl_listener *listener,
		void *data) {
	struct roots_keyboard *keyboard =
		wl_container_of(listener, keyboard, keyboard_modifiers);
	struct roots_desktop *desktop = keyboard->input->server->desktop;
	wlr_idle_notify_activity(desktop->idle, keyboard->seat->seat);
	roots_keyboard_handle_modifiers(keyboard);
}

static void handle_cursor_motion(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, motion);
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_pointer_motion *event = data;
	roots_cursor_handle_motion(cursor, event);
}

static void handle_cursor_motion_absolute(struct wl_listener *listener,
		void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, motion_absolute);
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_pointer_motion_absolute *event = data;
	roots_cursor_handle_motion_absolute(cursor, event);
}

static void handle_cursor_button(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, button);
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_pointer_button *event = data;
	roots_cursor_handle_button(cursor, event);
}

static void handle_cursor_axis(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, axis);
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_pointer_axis *event = data;
	roots_cursor_handle_axis(cursor, event);
}

static void handle_touch_down(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, touch_down);
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_touch_down *event = data;
	roots_cursor_handle_touch_down(cursor, event);
}

static void handle_touch_up(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, touch_up);
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_touch_up *event = data;
	roots_cursor_handle_touch_up(cursor, event);
}

static void handle_touch_motion(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, touch_motion);
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_touch_motion *event = data;
	roots_cursor_handle_touch_motion(cursor, event);
}

static void handle_tool_axis(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, tool_axis);
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_tablet_tool_axis *event = data;
	roots_cursor_handle_tool_axis(cursor, event);
}

static void handle_tool_tip(struct wl_listener *listener, void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, tool_tip);
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_event_tablet_tool_tip *event = data;
	roots_cursor_handle_tool_tip(cursor, event);
}

static void handle_request_set_cursor(struct wl_listener *listener,
		void *data) {
	struct roots_cursor *cursor =
		wl_container_of(listener, cursor, request_set_cursor);
	struct roots_desktop *desktop = cursor->seat->input->server->desktop;
	wlr_idle_notify_activity(desktop->idle, cursor->seat->seat);
	struct wlr_seat_pointer_request_set_cursor_event *event = data;
	roots_cursor_handle_request_set_cursor(cursor, event);
}

static void seat_reset_device_mappings(struct roots_seat *seat,
		struct wlr_input_device *device) {
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
	const char *mapped_output = NULL;
	struct roots_cursor_config *cc =
		roots_config_get_cursor(config, seat->seat->name);
	if (cc != NULL) {
		mapped_output = cc->mapped_output;
	}
	wl_list_for_each(output, &desktop->outputs, link) {
		if (mapped_output &&
				strcmp(mapped_output, output->wlr_output->name) == 0) {
			wlr_cursor_map_to_output(cursor, output->wlr_output);
		}

		wl_list_for_each(pointer, &seat->pointers, link) {
			seat_set_device_output_mappings(seat, pointer->device,
				output->wlr_output);
		}
		wl_list_for_each(tablet_tool, &seat->tablet_tools, link) {
			seat_set_device_output_mappings(seat, tablet_tool->device,
				output->wlr_output);
		}
		wl_list_for_each(touch, &seat->touch, link) {
			seat_set_device_output_mappings(seat, touch->device,
				output->wlr_output);
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

	roots_seat_configure_cursor(seat);
	roots_seat_configure_xcursor(seat);

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

	wl_signal_add(&wlr_cursor->events.touch_motion,
		&seat->cursor->touch_motion);
	seat->cursor->touch_motion.notify = handle_touch_motion;

	wl_signal_add(&wlr_cursor->events.tablet_tool_axis,
		&seat->cursor->tool_axis);
	seat->cursor->tool_axis.notify = handle_tool_axis;

	wl_signal_add(&wlr_cursor->events.tablet_tool_tip, &seat->cursor->tool_tip);
	seat->cursor->tool_tip.notify = handle_tool_tip;

	wl_signal_add(&seat->seat->events.request_set_cursor,
		&seat->cursor->request_set_cursor);
	seat->cursor->request_set_cursor.notify = handle_request_set_cursor;
}

static void roots_seat_handle_new_drag_icon(struct wl_listener *listener,
		void *data) {
	struct roots_seat *seat = wl_container_of(listener, seat, new_drag_icon);
	struct wlr_drag_icon *wlr_drag_icon = data;

	struct roots_drag_icon *icon = calloc(1, sizeof(struct roots_drag_icon));
	if (icon == NULL) {
		return;
	}
	icon->seat = seat;
	icon->wlr_drag_icon = wlr_drag_icon;

	icon->surface_commit.notify = roots_drag_icon_handle_surface_commit;
	wl_signal_add(&wlr_drag_icon->events.surface_commit, &icon->surface_commit);
	icon->map.notify = roots_drag_icon_handle_map;
	wl_signal_add(&wlr_drag_icon->events.map, &icon->map);
	icon->destroy.notify = roots_drag_icon_handle_destroy;
	wl_signal_add(&wlr_drag_icon->events.destroy, &icon->destroy);

	wl_list_insert(&seat->drag_icons, &icon->link);
}

static void seat_view_destroy(struct roots_seat_view *seat_view);

static void roots_seat_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_seat *seat = wl_container_of(listener, seat, destroy);

	// TODO: probably more to be freed here
	wl_list_remove(&seat->seat_destroy.link);

	struct roots_seat_view *view, *nview;
	wl_list_for_each_safe(view, nview, &seat->views, link) {
		seat_view_destroy(view);
	}
}

void roots_seat_destroy(struct roots_seat *seat) {
	roots_seat_handle_destroy(&seat->destroy, seat->seat);
	wlr_seat_destroy(seat->seat);
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
	wl_list_init(&seat->views);
	wl_list_init(&seat->drag_icons);

	seat->input = input;

	seat->seat = wlr_seat_create(input->server->wl_display, name);
	if (!seat->seat) {
		free(seat);
		return NULL;
	}

	roots_seat_init_cursor(seat);
	if (!seat->cursor) {
		wlr_seat_destroy(seat->seat);
		free(seat);
		return NULL;
	}

	wl_list_insert(&input->seats, &seat->link);

	seat->new_drag_icon.notify = roots_seat_handle_new_drag_icon;
	wl_signal_add(&seat->seat->events.new_drag_icon, &seat->new_drag_icon);
	seat->destroy.notify = roots_seat_handle_seat_destroy;
	wl_signal_add(&seat->seat->events.destroy, &seat->destroy);

	return seat;
}

static void seat_update_capabilities(struct roots_seat *seat) {
	uint32_t caps = 0;
	if (!wl_list_empty(&seat->keyboards)) {
		caps |= WL_SEAT_CAPABILITY_KEYBOARD;
	}
	if (!wl_list_empty(&seat->pointers) || !wl_list_empty(&seat->tablet_tools)) {
		caps |= WL_SEAT_CAPABILITY_POINTER;
	}
	if (!wl_list_empty(&seat->touch)) {
		caps |= WL_SEAT_CAPABILITY_TOUCH;
	}
	wlr_seat_set_capabilities(seat->seat, caps);

	// Hide cursor if seat doesn't have pointer capability
	if ((caps & WL_SEAT_CAPABILITY_POINTER) == 0) {
		wlr_cursor_set_image(seat->cursor->cursor, NULL, 0, 0, 0, 0, 0, 0);
	} else {
		wlr_xcursor_manager_set_cursor_image(seat->cursor->xcursor_manager,
			seat->cursor->default_xcursor, seat->cursor->cursor);
	}
}

static void seat_add_keyboard(struct roots_seat *seat,
		struct wlr_input_device *device) {
	assert(device->type == WLR_INPUT_DEVICE_KEYBOARD);
	struct roots_keyboard *keyboard =
		roots_keyboard_create(device, seat->input);
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

	wlr_seat_set_keyboard(seat->seat, device);
}

static void seat_add_pointer(struct roots_seat *seat,
		struct wlr_input_device *device) {
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

static void seat_add_touch(struct roots_seat *seat,
		struct wlr_input_device *device) {
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

static void seat_add_tablet_pad(struct roots_seat *seat,
		struct wlr_input_device *device) {
	// TODO
}

static void seat_add_tablet_tool(struct roots_seat *seat,
		struct wlr_input_device *device) {
	struct roots_tablet_tool *tablet_tool =
		calloc(sizeof(struct roots_tablet_tool), 1);
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

	seat_update_capabilities(seat);
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

	seat_update_capabilities(seat);
}

void roots_seat_configure_xcursor(struct roots_seat *seat) {
	const char *cursor_theme = NULL;
	struct roots_cursor_config *cc =
		roots_config_get_cursor(seat->input->config, seat->seat->name);
	if (cc != NULL) {
		cursor_theme = cc->theme;
		if (cc->default_image != NULL) {
			seat->cursor->default_xcursor = cc->default_image;
		}
	}

	if (!seat->cursor->xcursor_manager) {
		seat->cursor->xcursor_manager =
			wlr_xcursor_manager_create(cursor_theme, ROOTS_XCURSOR_SIZE);
		if (seat->cursor->xcursor_manager == NULL) {
			wlr_log(L_ERROR, "Cannot create XCursor manager for theme %s",
					cursor_theme);
			return;
		}
	}

	struct roots_output *output;
	wl_list_for_each(output, &seat->input->server->desktop->outputs, link) {
		float scale = output->wlr_output->scale;
		if (wlr_xcursor_manager_load(seat->cursor->xcursor_manager, scale)) {
			wlr_log(L_ERROR, "Cannot load xcursor theme for output '%s' "
				"with scale %f", output->wlr_output->name, scale);
		}
	}

	wlr_xcursor_manager_set_cursor_image(seat->cursor->xcursor_manager,
		seat->cursor->default_xcursor, seat->cursor->cursor);
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

struct roots_view *roots_seat_get_focus(struct roots_seat *seat) {
	if (!seat->has_focus || wl_list_empty(&seat->views)) {
		return NULL;
	}
	struct roots_seat_view *seat_view =
		wl_container_of(seat->views.next, seat_view, link);
	return seat_view->view;
}

static void seat_view_destroy(struct roots_seat_view *seat_view) {
	struct roots_seat *seat = seat_view->seat;

	if (seat_view->view == roots_seat_get_focus(seat)) {
		seat->has_focus = false;
		seat->cursor->mode = ROOTS_CURSOR_PASSTHROUGH;
	}

	if (seat_view == seat->cursor->pointer_view) {
		seat->cursor->pointer_view = NULL;
	}

	wl_list_remove(&seat_view->view_destroy.link);
	wl_list_remove(&seat_view->link);
	free(seat_view);

	// Focus first view
	if (!wl_list_empty(&seat->views)) {
		struct roots_seat_view *first_seat_view = wl_container_of(
			seat->views.next, first_seat_view, link);
		roots_seat_set_focus(seat, first_seat_view->view);
	}
}

static void seat_view_handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_seat_view *seat_view =
		wl_container_of(listener, seat_view, view_destroy);
	seat_view_destroy(seat_view);
}

static struct roots_seat_view *seat_add_view(struct roots_seat *seat,
		struct roots_view *view) {
	struct roots_seat_view *seat_view =
		calloc(1, sizeof(struct roots_seat_view));
	if (seat_view == NULL) {
		return NULL;
	}
	seat_view->seat = seat;
	seat_view->view = view;

	wl_list_insert(seat->views.prev, &seat_view->link);

	seat_view->view_destroy.notify = seat_view_handle_destroy;
	wl_signal_add(&view->events.destroy, &seat_view->view_destroy);

	return seat_view;
}

struct roots_seat_view *roots_seat_view_from_view(
		struct roots_seat *seat, struct roots_view *view) {
	if (view == NULL) {
		return NULL;
	}

	bool found = false;
	struct roots_seat_view *seat_view = NULL;
	wl_list_for_each(seat_view, &seat->views, link) {
		if (seat_view->view == view) {
			found = true;
			break;
		}
	}
	if (!found) {
		seat_view = seat_add_view(seat, view);
		if (seat_view == NULL) {
			wlr_log(L_ERROR, "Allocation failed");
			return NULL;
		}
	}

	return seat_view;
}

void roots_seat_set_focus(struct roots_seat *seat, struct roots_view *view) {
	// Make sure the view will be rendered on top of others, even if it's
	// already focused in this seat
	if (view != NULL) {
		wl_list_remove(&view->link);
		wl_list_insert(&seat->input->server->desktop->views, &view->link);
	}

	struct roots_view *prev_focus = roots_seat_get_focus(seat);
	if (view == prev_focus) {
		return;
	}

	if (view && view->type == ROOTS_XWAYLAND_VIEW &&
			view->xwayland_surface->override_redirect) {
		return;
	}
	struct roots_seat_view *seat_view = NULL;
	if (view != NULL) {
		seat_view = roots_seat_view_from_view(seat, view);
		if (seat_view == NULL) {
			return;
		}
	}

	seat->has_focus = false;

	// Deactivate the old view if it is not focused by some other seat
	if (prev_focus != NULL && !input_view_has_focus(seat->input, prev_focus)) {
		view_activate(prev_focus, false);
	}

	if (view == NULL) {
		seat->cursor->mode = ROOTS_CURSOR_PASSTHROUGH;
		return;
	}

	view_activate(view, true);

	seat->has_focus = true;
	wl_list_remove(&seat_view->link);
	wl_list_insert(&seat->views, &seat_view->link);

	struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat->seat);
	if (keyboard != NULL) {
		wlr_seat_keyboard_notify_enter(seat->seat, view->wlr_surface,
			keyboard->keycodes, keyboard->num_keycodes,
			&keyboard->modifiers);
	} else {
		wlr_seat_keyboard_notify_enter(seat->seat, view->wlr_surface,
			NULL, 0, NULL);
	}
}

void roots_seat_cycle_focus(struct roots_seat *seat) {
	if (wl_list_empty(&seat->views)) {
		return;
	}

	struct roots_seat_view *first_seat_view = wl_container_of(
		seat->views.next, first_seat_view, link);
	if (!seat->has_focus) {
		roots_seat_set_focus(seat, first_seat_view->view);
		return;
	}
	if (wl_list_length(&seat->views) < 2) {
		return;
	}

	// Focus the next view
	struct roots_seat_view *next_seat_view = wl_container_of(
		first_seat_view->link.next, next_seat_view, link);
	roots_seat_set_focus(seat, next_seat_view->view);

	// Move the first view to the end of the list
	wl_list_remove(&first_seat_view->link);
	wl_list_insert(seat->views.prev, &first_seat_view->link);
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

	wlr_xcursor_manager_set_cursor_image(seat->cursor->xcursor_manager,
		ROOTS_XCURSOR_MOVE, seat->cursor->cursor);
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

	const char *resize_name = wlr_xcursor_get_resize_name(edges);
	wlr_xcursor_manager_set_cursor_image(seat->cursor->xcursor_manager,
		resize_name, seat->cursor->cursor);
}

void roots_seat_begin_rotate(struct roots_seat *seat, struct roots_view *view) {
	struct roots_cursor *cursor = seat->cursor;
	cursor->mode = ROOTS_CURSOR_ROTATE;
	cursor->offs_x = cursor->cursor->x;
	cursor->offs_y = cursor->cursor->y;
	cursor->view_rotation = view->rotation;
	view_maximize(view, false);
	wlr_seat_pointer_clear_focus(seat->seat);

	wlr_xcursor_manager_set_cursor_image(seat->cursor->xcursor_manager,
		ROOTS_XCURSOR_ROTATE, seat->cursor->cursor);
}
