#define _XOPEN_SOURCE 500
#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/interfaces/wlr_input_device.h>
#include <wlr/interfaces/wlr_keyboard.h>
#include <wlr/interfaces/wlr_pointer.h>
#include <wlr/interfaces/wlr_touch.h>
#include <wlr/interfaces/wlr_tablet_tool.h>
#include <wlr/interfaces/wlr_tablet_pad.h>
#include <wlr/util/log.h>

void wlr_input_device_init(struct wlr_input_device *dev,
		enum wlr_input_device_type type,
		struct wlr_input_device_impl *impl,
		const char *name, int vendor, int product) {
	dev->type = type;
	dev->impl = impl;
	dev->name = strdup(name);
	dev->vendor = vendor;
	dev->product = product;

	wl_signal_init(&dev->events.destroy);
}

void wlr_input_device_destroy(struct wlr_input_device *dev) {
	if (!dev) {
		return;
	}

	wl_signal_emit(&dev->events.destroy, dev);
	
	if (dev->_device) {
		switch (dev->type) {
		case WLR_INPUT_DEVICE_KEYBOARD:
			wlr_keyboard_destroy(dev->keyboard);
			break;
		case WLR_INPUT_DEVICE_POINTER:
			wlr_pointer_destroy(dev->pointer);
			break;
		case WLR_INPUT_DEVICE_TOUCH:
			wlr_touch_destroy(dev->touch);
			break;
		case WLR_INPUT_DEVICE_TABLET_TOOL:
			wlr_tablet_tool_destroy(dev->tablet_tool);
			break;
		case WLR_INPUT_DEVICE_TABLET_PAD:
			wlr_tablet_pad_destroy(dev->tablet_pad);
			break;
		default:
			wlr_log(L_DEBUG, "Warning: leaking memory %p %p %d",
					dev->_device, dev, dev->type);
			break;
		}
	}
	free(dev->name);
	if (dev->impl && dev->impl->destroy) {
		dev->impl->destroy(dev);
	} else {
		free(dev);
	}
}
