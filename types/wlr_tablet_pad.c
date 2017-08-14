#include <stdlib.h>
#include <string.h>
#include <wayland-server.h>
#include <wlr/types/wlr_tablet_pad.h>
#include <wlr/interfaces/wlr_tablet_pad.h>

void wlr_tablet_pad_init(struct wlr_tablet_pad *pad,
		struct wlr_tablet_pad_impl *impl) {
	pad->impl = impl;
	wl_signal_init(&pad->events.button);
	wl_signal_init(&pad->events.ring);
	wl_signal_init(&pad->events.strip);
}

void wlr_tablet_pad_destroy(struct wlr_tablet_pad *pad) {
	if (!pad) return;
	if (pad->impl && pad->impl->destroy) {
		pad->impl->destroy(pad);
	} else {
		free(pad);
	}
}
