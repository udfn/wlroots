#ifndef WLR_TYPES_WLR_LAYER_SHELL_H
#define WLR_TYPES_WLR_LAYER_SHELL_H
#include <stdbool.h>
#include <stdint.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include "wlr-layer-shell-unstable-v1-protocol.h"

/**
 * wlr_layer_shell allows clients to arrange themselves in "layers" on the
 * desktop in accordance with the wlr-layer-shell protocol. When a client is
 * added, the new_surface signal will be raised and passed a reference to our
 * wlr_layer_surface. At this time, the client will have configured the surface
 * as it desires, including information like desired anchors and margins. The
 * compositor should use this information to decide how to arrange the layer
 * on-screen, then determine the dimensions of the layer and call
 * wlr_layer_surface_configure. The client will then attach a buffer and commit
 * the surface, at which point the wlr_layer_surface map signal is raised and
 * the compositor should begin rendering the surface.
 */
struct wlr_layer_shell {
	struct wl_global *wl_global;
	struct wl_list client_resources; // wl_resource
	struct wl_list surfaces; // wl_layer_surface

	struct wl_listener display_destroy;

	struct {
		 // struct wlr_layer_surface *
		 // Note: the output may be NULL. In this case, it is your
		 // responsibility to assign an output before returning.
		struct wl_signal new_surface;
	} events;

	void *data;
};

struct wlr_layer_surface_state {
	uint32_t anchor;
	int32_t exclusive_zone;
	struct {
		uint32_t top, right, bottom, left;
	} margin;
	bool keyboard_interactive;
	uint32_t desired_width, desired_height;
	uint32_t actual_width, actual_height;
};

struct wlr_layer_surface_configure {
	struct wl_list link; // wlr_layer_surface::configure_list
	uint32_t serial;
	struct wlr_layer_surface_state state;
};

struct wlr_layer_surface {
	struct wl_list link; // wlr_layer_shell::surfaces
	struct wlr_surface *surface;
	struct wlr_output *output;
	struct wl_resource *resource;
	struct wlr_layer_shell *shell;
	struct wl_list popups; // wlr_xdg_popup::link

	const char *namespace;
	enum zwlr_layer_shell_v1_layer layer;

	bool added, configured, mapped, closed;
	uint32_t configure_serial;
	struct wl_event_source *configure_idle;
	uint32_t configure_next_serial;
	struct wl_list configure_list;

	struct wlr_layer_surface_configure *acked_configure;

	struct wlr_layer_surface_state client_pending;
	struct wlr_layer_surface_state server_pending;
	struct wlr_layer_surface_state current;

	struct wl_listener surface_destroy_listener;

	struct {
		struct wl_signal destroy;
		struct wl_signal map;
		struct wl_signal unmap;
		struct wl_signal new_popup;
	} events;

	void *data;
};

struct wlr_layer_shell *wlr_layer_shell_create(struct wl_display *display);
void wlr_layer_shell_destroy(struct wlr_layer_shell *layer_shell);

/**
 * Notifies the layer surface to configure itself with this width/height. The
 * layer_surface will signal its map event when the surface is ready to assume
 * this size.
 */
void wlr_layer_surface_configure(struct wlr_layer_surface *surface,
		uint32_t width, uint32_t height);

/**
 * Unmaps this layer surface and notifies the client that it has been closed.
 */
void wlr_layer_surface_close(struct wlr_layer_surface *surface);

bool wlr_surface_is_layer_surface(struct wlr_surface *surface);

struct wlr_layer_surface *wlr_layer_surface_from_wlr_surface(
		struct wlr_surface *surface);

/* Calls the iterator function for each sub-surface and popup of this surface */
void wlr_layer_surface_for_each_surface(struct wlr_layer_surface *surface,
		wlr_surface_iterator_func_t iterator, void *user_data);

#endif
