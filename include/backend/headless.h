#ifndef BACKEND_HEADLESS_H
#define BACKEND_HEADLESS_H

#include <wlr/backend/interface.h>
#include <wlr/backend/headless.h>
#include <wlr/types/wlr_output.h>

struct wlr_headless_backend {
	struct wlr_backend backend;
	struct wlr_egl egl;
	struct wl_display *display;
	struct wl_list outputs;
	struct wl_listener display_destroy;
};

struct wlr_headless_backend_output {
	struct wlr_output wlr_output;

	struct wlr_headless_backend *backend;
	struct wl_list link;

	void *egl_surface;
};

#endif
