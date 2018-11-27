/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_TYPES_WLR_GTK_PRIMARY_SELECTION_H
#define WLR_TYPES_WLR_GTK_PRIMARY_SELECTION_H

#include <wayland-server.h>
#include <wlr/types/wlr_seat.h>

struct wlr_gtk_primary_selection_device_manager {
	struct wl_global *global;
	struct wl_list resources; // wl_resource_get_link
	struct wl_list devices; // wlr_gtk_primary_selection_device::link

	struct wl_listener display_destroy;

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

/**
 * A device is a per-seat object used to set and get the current selection.
 */
struct wlr_gtk_primary_selection_device {
	struct wlr_gtk_primary_selection_device_manager *manager;
	struct wlr_seat *seat;
	struct wl_list link; // wlr_gtk_primary_selection_device_manager::devices
	struct wl_list resources; // wl_resource_get_link

	struct wlr_gtk_primary_selection_source *source;
	struct wl_list offers; // wlr_gtk_primary_selection_offer::link

	struct wl_listener seat_destroy;
	struct wl_listener seat_focus_change;
	struct wl_listener source_destroy;

	void *data;
};

/**
 * A source is the sending side of a selection.
 */
struct wlr_gtk_primary_selection_source {
	// source metadata
	struct wl_array mime_types;

	// source implementation
	void (*send)(struct wlr_gtk_primary_selection_source *source,
		const char *mime_type, int32_t fd);
	void (*cancel)(struct wlr_gtk_primary_selection_source *source);

	struct {
		struct wl_signal destroy;
	} events;

	void *data;
};

/**
 * An offer is the receiving side of a selection. When the selection is set,
 * offers are created for the currently focused client and can be used to
 * initiate the data transfer.
 */
struct wlr_gtk_primary_selection_offer {
	struct wl_resource *resource;
	struct wlr_gtk_primary_selection_source *source;
	struct wl_list link; // wlr_gtk_primary_selection_device::offers

	struct wl_listener source_destroy;

	void *data;
};

struct wlr_gtk_primary_selection_device_manager *
	wlr_gtk_primary_selection_device_manager_create(struct wl_display *display);
void wlr_gtk_primary_selection_device_manager_destroy(
	struct wlr_gtk_primary_selection_device_manager *manager);

void wlr_gtk_primary_selection_device_manager_set_selection(
	struct wlr_gtk_primary_selection_device_manager *manager,
	struct wlr_seat *seat,
	struct wlr_gtk_primary_selection_source *source);

void wlr_gtk_primary_selection_source_init(
	struct wlr_gtk_primary_selection_source *source);
void wlr_gtk_primary_selection_source_finish(
	struct wlr_gtk_primary_selection_source *source);

#endif
