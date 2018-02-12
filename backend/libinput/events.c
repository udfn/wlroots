#include <stdlib.h>
#include <assert.h>
#include <libinput.h>
#include <wlr/backend/session.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/util/log.h>
#include <wayland-util.h>
#include <wlr/util/signal.h>
#include "backend/libinput.h"

struct wlr_input_device *get_appropriate_device(
		enum wlr_input_device_type desired_type,
		struct libinput_device *libinput_dev) {
	struct wl_list *wlr_devices = libinput_device_get_user_data(libinput_dev);
	if (!wlr_devices) {
		return NULL;
	}
	struct wlr_input_device *dev;
	wl_list_for_each(dev, wlr_devices, link) {
		if (dev->type == desired_type) {
			return dev;
		}
	}
	return NULL;
}

static void wlr_libinput_device_destroy(struct wlr_input_device *_dev) {
	struct wlr_libinput_input_device *dev = (struct wlr_libinput_input_device *)_dev;
	libinput_device_unref(dev->handle);
	wl_list_remove(&dev->wlr_input_device.link);
	free(dev);
}

static struct wlr_input_device_impl input_device_impl = {
	.destroy = wlr_libinput_device_destroy
};

static struct wlr_input_device *allocate_device(
		struct wlr_libinput_backend *backend, struct libinput_device *libinput_dev,
		struct wl_list *wlr_devices, enum wlr_input_device_type type) {
	int vendor = libinput_device_get_id_vendor(libinput_dev);
	int product = libinput_device_get_id_product(libinput_dev);
	const char *name = libinput_device_get_name(libinput_dev);
	struct wlr_libinput_input_device *wlr_libinput_dev;
	if (!(wlr_libinput_dev = calloc(1, sizeof(struct wlr_libinput_input_device)))) {
		return NULL;
	}
	struct wlr_input_device *wlr_dev = &wlr_libinput_dev->wlr_input_device;
	wl_list_insert(wlr_devices, &wlr_dev->link);
	wlr_libinput_dev->handle = libinput_dev;
	libinput_device_ref(libinput_dev);
	wlr_input_device_init(wlr_dev, type, &input_device_impl,
			name, vendor, product);
	return wlr_dev;
}

bool wlr_input_device_is_libinput(struct wlr_input_device *wlr_dev) {
        return wlr_dev->impl == &input_device_impl;
}

static void handle_device_added(struct wlr_libinput_backend *backend,
		struct libinput_device *libinput_dev) {
	assert(backend && libinput_dev);
	/*
	 * Note: the wlr API exposes only devices with a single capability, because
	 * that meshes better with how Wayland does things and is a bit simpler.
	 * However, libinput devices often have multiple capabilities - in such
	 * cases we have to create several devices.
	 */
	int vendor = libinput_device_get_id_vendor(libinput_dev);
	int product = libinput_device_get_id_product(libinput_dev);
	const char *name = libinput_device_get_name(libinput_dev);
	struct wl_list *wlr_devices = calloc(1, sizeof(struct wl_list));
	if (!wlr_devices) {
		wlr_log(L_ERROR, "Allocation failed");
		return;
	}
	wl_list_init(wlr_devices);
	wlr_log(L_DEBUG, "Added %s [%d:%d]", name, vendor, product);

	if (libinput_device_has_capability(libinput_dev, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
		struct wlr_input_device *wlr_dev = allocate_device(backend,
				libinput_dev, wlr_devices, WLR_INPUT_DEVICE_KEYBOARD);
		if (!wlr_dev) {
			goto fail;
		}
		wlr_dev->keyboard = wlr_libinput_keyboard_create(libinput_dev);
		if (!wlr_dev->keyboard) {
			free(wlr_dev);
			goto fail;
		}
		wlr_signal_emit_safe(&backend->backend.events.new_input, wlr_dev);
	}
	if (libinput_device_has_capability(libinput_dev, LIBINPUT_DEVICE_CAP_POINTER)) {
		struct wlr_input_device *wlr_dev = allocate_device(backend,
				libinput_dev, wlr_devices, WLR_INPUT_DEVICE_POINTER);
		if (!wlr_dev) {
			goto fail;
		}
		wlr_dev->pointer = wlr_libinput_pointer_create(libinput_dev);
		if (!wlr_dev->pointer) {
			free(wlr_dev);
			goto fail;
		}
		wlr_signal_emit_safe(&backend->backend.events.new_input, wlr_dev);
	}
	if (libinput_device_has_capability(libinput_dev, LIBINPUT_DEVICE_CAP_TOUCH)) {
		struct wlr_input_device *wlr_dev = allocate_device(backend,
				libinput_dev, wlr_devices, WLR_INPUT_DEVICE_TOUCH);
		if (!wlr_dev) {
			goto fail;
		}
		wlr_dev->touch = wlr_libinput_touch_create(libinput_dev);
		if (!wlr_dev->touch) {
			free(wlr_dev);
			goto fail;
		}
		wlr_signal_emit_safe(&backend->backend.events.new_input, wlr_dev);
	}
	if (libinput_device_has_capability(libinput_dev, LIBINPUT_DEVICE_CAP_TABLET_TOOL)) {
		struct wlr_input_device *wlr_dev = allocate_device(backend,
				libinput_dev, wlr_devices, WLR_INPUT_DEVICE_TABLET_TOOL);
		if (!wlr_dev) {
			goto fail;
		}
		wlr_dev->tablet_tool = wlr_libinput_tablet_tool_create(libinput_dev);
		if (!wlr_dev->tablet_tool) {
			free(wlr_dev);
			goto fail;
		}
		wlr_signal_emit_safe(&backend->backend.events.new_input, wlr_dev);
	}
	if (libinput_device_has_capability(libinput_dev, LIBINPUT_DEVICE_CAP_TABLET_PAD)) {
		struct wlr_input_device *wlr_dev = allocate_device(backend,
				libinput_dev, wlr_devices, WLR_INPUT_DEVICE_TABLET_PAD);
		if (!wlr_dev) {
			goto fail;
		}
		wlr_dev->tablet_pad = wlr_libinput_tablet_pad_create(libinput_dev);
		if (!wlr_dev->tablet_pad) {
			free(wlr_dev);
			goto fail;
		}
		wlr_signal_emit_safe(&backend->backend.events.new_input, wlr_dev);
	}
	if (libinput_device_has_capability(libinput_dev, LIBINPUT_DEVICE_CAP_GESTURE)) {
		// TODO
	}
	if (libinput_device_has_capability(libinput_dev, LIBINPUT_DEVICE_CAP_SWITCH)) {
		// TODO
	}

	if (wl_list_length(wlr_devices) > 0) {
		libinput_device_set_user_data(libinput_dev, wlr_devices);
		wlr_list_push(&backend->wlr_device_lists, wlr_devices);
	} else {
		free(wlr_devices);
	}
	return;

fail:
	wlr_log(L_ERROR, "Could not allocate new device");
	struct wlr_input_device *dev, *tmp_dev;
	wl_list_for_each_safe(dev, tmp_dev, wlr_devices, link) {
		free(dev);
	}
	free(wlr_devices);
}

static void handle_device_removed(struct wlr_libinput_backend *backend,
		struct libinput_device *libinput_dev) {
	struct wl_list *wlr_devices = libinput_device_get_user_data(libinput_dev);
	int vendor = libinput_device_get_id_vendor(libinput_dev);
	int product = libinput_device_get_id_product(libinput_dev);
	const char *name = libinput_device_get_name(libinput_dev);
	wlr_log(L_DEBUG, "Removing %s [%d:%d]", name, vendor, product);
	if (!wlr_devices) {
		return;
	}
	struct wlr_input_device *dev, *tmp_dev;
	wl_list_for_each_safe(dev, tmp_dev, wlr_devices, link) {
		wlr_input_device_destroy(dev);
	}
	for (size_t i = 0; i < backend->wlr_device_lists.length; i++) {
		if (backend->wlr_device_lists.items[i] == wlr_devices) {
			wlr_list_del(&backend->wlr_device_lists, i);
			break;
		}
	}
	free(wlr_devices);
}

void wlr_libinput_event(struct wlr_libinput_backend *backend,
		struct libinput_event *event) {
	assert(backend && event);
	struct libinput_device *libinput_dev = libinput_event_get_device(event);
	enum libinput_event_type event_type = libinput_event_get_type(event);
	switch (event_type) {
	case LIBINPUT_EVENT_DEVICE_ADDED:
		handle_device_added(backend, libinput_dev);
		break;
	case LIBINPUT_EVENT_DEVICE_REMOVED:
		handle_device_removed(backend, libinput_dev);
		break;
	case LIBINPUT_EVENT_KEYBOARD_KEY:
		handle_keyboard_key(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_POINTER_MOTION:
		handle_pointer_motion(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE:
		handle_pointer_motion_abs(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_POINTER_BUTTON:
		handle_pointer_button(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_POINTER_AXIS:
		handle_pointer_axis(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TOUCH_DOWN:
		handle_touch_down(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TOUCH_UP:
		handle_touch_up(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TOUCH_MOTION:
		handle_touch_motion(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TOUCH_CANCEL:
		handle_touch_cancel(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TOUCH_FRAME:
		// no-op (at least for now)
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_AXIS:
		handle_tablet_tool_axis(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_PROXIMITY:
		handle_tablet_tool_proximity(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_TIP:
		handle_tablet_tool_tip(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TABLET_TOOL_BUTTON:
		handle_tablet_tool_button(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TABLET_PAD_BUTTON:
		handle_tablet_pad_button(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TABLET_PAD_RING:
		handle_tablet_pad_ring(event, libinput_dev);
		break;
	case LIBINPUT_EVENT_TABLET_PAD_STRIP:
		handle_tablet_pad_strip(event, libinput_dev);
		break;
	default:
		wlr_log(L_DEBUG, "Unknown libinput event %d", event_type);
		break;
	}
}
