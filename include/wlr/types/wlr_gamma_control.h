#ifndef WLR_TYPES_WLR_GAMMA_CONTROL_H
#define WLR_TYPES_WLR_GAMMA_CONTROL_H

#include <wayland-server.h>

struct wlr_gamma_control_manager {
	struct wl_global *wl_global;

	void *data;
};

struct wlr_gamma_control {
	struct wl_resource *resource;
	struct wl_resource *output;

	void* data;
};

struct wlr_gamma_control_manager *wlr_gamma_control_manager_create(struct wl_display *display);
void wlr_gamma_control_manager_destroy(struct wlr_gamma_control_manager *gamma_control_manager);

#endif
