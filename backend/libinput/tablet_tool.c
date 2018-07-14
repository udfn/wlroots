#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <string.h>
#include <assert.h>
#include <libinput.h>
#include <stdlib.h>
#include <wayland-util.h>
#include <wlr/backend/session.h>
#include <wlr/interfaces/wlr_tablet_tool.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/util/log.h>
#include "backend/libinput.h"
#include "util/signal.h"

//TODO: Move out
static void add_tablet_path(struct wl_list *list, const char *path) {
	struct wlr_tablet_path *tablet_path = calloc(1, sizeof(struct wlr_tablet_path));

	if (!tablet_path) {
		return;
	}

	tablet_path->path = strdup(path);
	wl_list_insert(list, &tablet_path->link);
}

struct wlr_libinput_tablet_tool {
	struct wlr_tablet_tool_tool wlr_tool;

	struct libinput_tablet_tool *libinput_tool;

	bool unique;
	// Refcount for destroy + release
	size_t pad_refs;
};

// TODO: Maybe this should be a wlr_list? Do we keep it, or want to get rid of
// it?
struct tablet_tool_list_elem {
	struct wl_list link;

	struct wlr_libinput_tablet_tool *tool;
};

struct wlr_libinput_tablet {
	struct wlr_tablet_tool wlr_tool;

	struct wl_list tools; // tablet_tool_list_elem::link
};

static void destroy_tool_tool(struct wlr_libinput_tablet_tool *tool) {
	wlr_signal_emit_safe(&tool->wlr_tool.events.destroy, &tool->wlr_tool);
	libinput_tablet_tool_ref(tool->libinput_tool);
	libinput_tablet_tool_set_user_data(tool->libinput_tool, NULL);
	free(tool);
}


static void libinput_tablet_tool_destroy(struct wlr_tablet_tool *tool) {
	struct wlr_libinput_tablet *tablet =
		wl_container_of(tool, tablet, wlr_tool);

	struct tablet_tool_list_elem *pos;
	struct tablet_tool_list_elem *tmp;
	wl_list_for_each_safe(pos, tmp, &tablet->tools, link) {
		struct wlr_libinput_tablet_tool *tool = pos->tool;
		wl_list_remove(&pos->link);
		free(pos);

		if (--tool->pad_refs == 0) {
			destroy_tool_tool(tool);
		}
	}

	free(tablet);
}

static struct wlr_tablet_tool_impl tool_impl = {
	.destroy = libinput_tablet_tool_destroy,
};

struct wlr_tablet_tool *create_libinput_tablet_tool(
		struct libinput_device *libinput_dev) {
	assert(libinput_dev);
	struct wlr_libinput_tablet *libinput_tablet_tool =
		calloc(1, sizeof(struct wlr_libinput_tablet));
	if (!libinput_tablet_tool) {
		wlr_log(WLR_ERROR, "Unable to allocate wlr_tablet_tool");
		return NULL;
	}
	struct wlr_tablet_tool *wlr_tablet_tool = &libinput_tablet_tool->wlr_tool;

	wl_list_init(&wlr_tablet_tool->paths);
	struct udev_device *udev = libinput_device_get_udev_device(libinput_dev);
	add_tablet_path(&wlr_tablet_tool->paths, udev_device_get_syspath(udev));
	wlr_tablet_tool->name = strdup(libinput_device_get_name(libinput_dev));
	wl_list_init(&libinput_tablet_tool->tools);

	wlr_tablet_tool_init(wlr_tablet_tool, &tool_impl);
	return wlr_tablet_tool;
}

static enum wlr_tablet_tool_type wlr_type_from_libinput_type(
		enum libinput_tablet_tool_type value) {
	switch (value) {
	case LIBINPUT_TABLET_TOOL_TYPE_PEN:
		return WLR_TABLET_TOOL_TYPE_PEN;
	case LIBINPUT_TABLET_TOOL_TYPE_ERASER:
		return WLR_TABLET_TOOL_TYPE_ERASER;
	case LIBINPUT_TABLET_TOOL_TYPE_BRUSH:
		return WLR_TABLET_TOOL_TYPE_BRUSH;
	case LIBINPUT_TABLET_TOOL_TYPE_PENCIL:
		return WLR_TABLET_TOOL_TYPE_PENCIL;
	case LIBINPUT_TABLET_TOOL_TYPE_AIRBRUSH:
		return WLR_TABLET_TOOL_TYPE_AIRBRUSH;
	case LIBINPUT_TABLET_TOOL_TYPE_MOUSE:
		return WLR_TABLET_TOOL_TYPE_MOUSE;
	case LIBINPUT_TABLET_TOOL_TYPE_LENS:
		return WLR_TABLET_TOOL_TYPE_LENS;
	}

	assert(false && "UNREACHABLE");
}

static struct wlr_libinput_tablet_tool *get_wlr_tablet_tool(
		struct libinput_tablet_tool *tool) {
	struct wlr_libinput_tablet_tool *ret =
		libinput_tablet_tool_get_user_data(tool);

	if (ret) {
		return ret;
	}

	ret = calloc(1, sizeof(struct wlr_libinput_tablet_tool));
	if (!ret) {
		return NULL;
	}

	ret->libinput_tool = libinput_tablet_tool_ref(tool);
	ret->wlr_tool.pressure = libinput_tablet_tool_has_pressure(tool);
	ret->wlr_tool.distance = libinput_tablet_tool_has_distance(tool);
	ret->wlr_tool.tilt = libinput_tablet_tool_has_tilt(tool);
	ret->wlr_tool.rotation = libinput_tablet_tool_has_rotation(tool);
	ret->wlr_tool.slider = libinput_tablet_tool_has_slider(tool);
	ret->wlr_tool.wheel = libinput_tablet_tool_has_wheel(tool);

	ret->wlr_tool.hardware_serial = libinput_tablet_tool_get_serial(tool);
	ret->wlr_tool.hardware_wacom = libinput_tablet_tool_get_tool_id(tool);
	ret->wlr_tool.type = wlr_type_from_libinput_type(
		libinput_tablet_tool_get_type(tool));

	ret->unique = libinput_tablet_tool_is_unique(tool);

	wl_signal_init(&ret->wlr_tool.events.destroy);

	libinput_tablet_tool_set_user_data(tool, ret);
	return ret;
}

static void ensure_tool_reference(struct wlr_libinput_tablet_tool *tool,
		struct wlr_tablet_tool *wlr_dev) {
	struct tablet_tool_list_elem *pos;
	struct wlr_libinput_tablet *tablet = wl_container_of(wlr_dev, tablet, wlr_tool);

	wl_list_for_each(pos, &tablet->tools, link) {
		if (pos->tool == tool) { // We already have a ref
			// XXX: We *could* optimize the tool to the front of
			// the list here, since we will probably get the next
			// couple of events from the same tool.
			// BUT the list should always be rather short (probably
			// single digit amount of tools) so it might be more
			// work than it saves
			return;
		}
	}

	struct tablet_tool_list_elem *new =
		calloc(1, sizeof(struct tablet_tool_list_elem));
	if (!new) {// TODO: Should we at least log?
		return;
	}

	new->tool = tool;
	wl_list_insert(&tablet->tools, &new->link);
	++tool->pad_refs;
}

void handle_tablet_tool_axis(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_TOOL, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a tablet tool event for a device with no tablet tools?");
		return;
	}
	struct libinput_event_tablet_tool *tevent =
		libinput_event_get_tablet_tool_event(event);
	struct wlr_event_tablet_tool_axis wlr_event = { 0 };
	struct wlr_libinput_tablet_tool *tool = get_wlr_tablet_tool(
		libinput_event_tablet_tool_get_tool(tevent));
	ensure_tool_reference(tool, wlr_dev->tablet_tool);

	wlr_event.device = wlr_dev;
	wlr_event.tool = &tool->wlr_tool;
	wlr_event.time_msec =
		usec_to_msec(libinput_event_tablet_tool_get_time_usec(tevent));
	if (libinput_event_tablet_tool_x_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_X;
		wlr_event.x = libinput_event_tablet_tool_get_x_transformed(tevent, 1);
	}
	if (libinput_event_tablet_tool_y_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_Y;
		wlr_event.y = libinput_event_tablet_tool_get_y_transformed(tevent, 1);
	}
	if (libinput_event_tablet_tool_pressure_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_PRESSURE;
		wlr_event.pressure = libinput_event_tablet_tool_get_pressure(tevent);
	}
	if (libinput_event_tablet_tool_distance_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_DISTANCE;
		wlr_event.distance = libinput_event_tablet_tool_get_distance(tevent);
	}
	if (libinput_event_tablet_tool_tilt_x_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_TILT_X;
		wlr_event.tilt_x = libinput_event_tablet_tool_get_tilt_x(tevent);
	}
	if (libinput_event_tablet_tool_tilt_y_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_TILT_Y;
		wlr_event.tilt_y = libinput_event_tablet_tool_get_tilt_y(tevent);
	}
	if (libinput_event_tablet_tool_rotation_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_ROTATION;
		wlr_event.rotation = libinput_event_tablet_tool_get_rotation(tevent);
	}
	if (libinput_event_tablet_tool_slider_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_SLIDER;
		wlr_event.slider = libinput_event_tablet_tool_get_slider_position(tevent);
	}
	if (libinput_event_tablet_tool_wheel_has_changed(tevent)) {
		wlr_event.updated_axes |= WLR_TABLET_TOOL_AXIS_WHEEL;
		wlr_event.wheel_delta = libinput_event_tablet_tool_get_wheel_delta(tevent);
	}
	wlr_signal_emit_safe(&wlr_dev->tablet_tool->events.axis, &wlr_event);
}

void handle_tablet_tool_proximity(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_TOOL, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a tablet tool event for a device with no tablet tools?");
		return;
	}
	struct libinput_event_tablet_tool *tevent =
		libinput_event_get_tablet_tool_event(event);
	struct wlr_event_tablet_tool_proximity wlr_event = { 0 };
	struct wlr_libinput_tablet_tool *tool = get_wlr_tablet_tool(
		libinput_event_tablet_tool_get_tool(tevent));
	ensure_tool_reference(tool, wlr_dev->tablet_tool);

	wlr_event.tool = &tool->wlr_tool;
	wlr_event.device = wlr_dev;
	wlr_event.time_msec =
		usec_to_msec(libinput_event_tablet_tool_get_time_usec(tevent));
	switch (libinput_event_tablet_tool_get_proximity_state(tevent)) {
	case LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT:
		wlr_event.state = WLR_TABLET_TOOL_PROXIMITY_OUT;
		break;
	case LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN:
		wlr_event.state = WLR_TABLET_TOOL_PROXIMITY_IN;
		break;
	}
	wlr_signal_emit_safe(&wlr_dev->tablet_tool->events.proximity, &wlr_event);

	if (libinput_event_tablet_tool_get_proximity_state(tevent) == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_IN) {
		handle_tablet_tool_axis(event, libinput_dev);
	}

	// If the tool is not unique, libinput will not find it again after the
	// proximity out, so we should destroy it
	if (!tool->unique &&
			libinput_event_tablet_tool_get_proximity_state(tevent) == LIBINPUT_TABLET_TOOL_PROXIMITY_STATE_OUT) {
		// The tool isn't unique, it can't be on multiple tablets
		assert(tool->pad_refs == 1);
		struct wlr_libinput_tablet *tablet =
			wl_container_of(wlr_dev->tablet_tool, tablet, wlr_tool);
		struct tablet_tool_list_elem *pos;
		struct tablet_tool_list_elem *tmp;

		wl_list_for_each_safe(pos, tmp, &tablet->tools, link) {
			if (pos->tool == tool) {
				wl_list_remove(&pos->link);
				free(pos);
				break;
			}
		}

		destroy_tool_tool(tool);
	}
}

void handle_tablet_tool_tip(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_TOOL, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a tablet tool event for a device with no tablet tools?");
		return;
	}
	handle_tablet_tool_axis(event, libinput_dev);
	struct libinput_event_tablet_tool *tevent =
		libinput_event_get_tablet_tool_event(event);
	struct wlr_event_tablet_tool_tip wlr_event = { 0 };
	struct wlr_libinput_tablet_tool *tool = get_wlr_tablet_tool(
		libinput_event_tablet_tool_get_tool(tevent));
	ensure_tool_reference(tool, wlr_dev->tablet_tool);

	wlr_event.device = wlr_dev;
	wlr_event.tool = &tool->wlr_tool;
	wlr_event.time_msec =
		usec_to_msec(libinput_event_tablet_tool_get_time_usec(tevent));
	switch (libinput_event_tablet_tool_get_tip_state(tevent)) {
	case LIBINPUT_TABLET_TOOL_TIP_UP:
		wlr_event.state = WLR_TABLET_TOOL_TIP_UP;
		break;
	case LIBINPUT_TABLET_TOOL_TIP_DOWN:
		wlr_event.state = WLR_TABLET_TOOL_TIP_DOWN;
		break;
	}
	wlr_signal_emit_safe(&wlr_dev->tablet_tool->events.tip, &wlr_event);
}

void handle_tablet_tool_button(struct libinput_event *event,
		struct libinput_device *libinput_dev) {
	struct wlr_input_device *wlr_dev =
		get_appropriate_device(WLR_INPUT_DEVICE_TABLET_TOOL, libinput_dev);
	if (!wlr_dev) {
		wlr_log(WLR_DEBUG, "Got a tablet tool event for a device with no tablet tools?");
		return;
	}
	handle_tablet_tool_axis(event, libinput_dev);
	struct libinput_event_tablet_tool *tevent =
		libinput_event_get_tablet_tool_event(event);
	struct wlr_event_tablet_tool_button wlr_event = { 0 };
	struct wlr_libinput_tablet_tool *tool = get_wlr_tablet_tool(
		libinput_event_tablet_tool_get_tool(tevent));
	ensure_tool_reference(tool, wlr_dev->tablet_tool);

	wlr_event.device = wlr_dev;
	wlr_event.tool = &tool->wlr_tool;
	wlr_event.time_msec =
		usec_to_msec(libinput_event_tablet_tool_get_time_usec(tevent));
	wlr_event.button = libinput_event_tablet_tool_get_button(tevent);
	switch (libinput_event_tablet_tool_get_button_state(tevent)) {
	case LIBINPUT_BUTTON_STATE_RELEASED:
		wlr_event.state = WLR_BUTTON_RELEASED;
		break;
	case LIBINPUT_BUTTON_STATE_PRESSED:
		wlr_event.state = WLR_BUTTON_PRESSED;
		break;
	}
	wlr_signal_emit_safe(&wlr_dev->tablet_tool->events.button, &wlr_event);
}
