#ifndef TYPES_WLR_DATA_DEVICE_H
#define TYPES_WLR_DATA_DEVICE_H

#include <wayland-server.h>

#define DATA_DEVICE_ALL_ACTIONS (WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY | \
	WL_DATA_DEVICE_MANAGER_DND_ACTION_MOVE | \
	WL_DATA_DEVICE_MANAGER_DND_ACTION_ASK)

struct wlr_client_data_source {
	struct wlr_data_source source;
	struct wlr_data_source_impl impl;
	struct wl_resource *resource;
	bool finalized;
};

extern const struct wlr_surface_role drag_icon_surface_role;

struct wlr_data_offer *data_offer_create(struct wl_resource *device_resource,
	struct wlr_data_source *source);
void data_offer_update_action(struct wlr_data_offer *offer);
void data_offer_destroy(struct wlr_data_offer *offer);

struct wlr_client_data_source *client_data_source_create(
	struct wl_client *client, uint32_t version, uint32_t id,
	struct wl_list *resource_list);
struct wlr_client_data_source *client_data_source_from_resource(
	struct wl_resource *resource);
void data_source_notify_finish(struct wlr_data_source *source);

struct wlr_seat_client *seat_client_from_data_device_resource(
	struct wl_resource *resource);
bool seat_client_start_drag(struct wlr_seat_client *client,
	struct wlr_data_source *source, struct wlr_surface *icon_surface,
	struct wlr_surface *origin, uint32_t serial);

#endif
