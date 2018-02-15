#include <assert.h>
#include <stdlib.h>
#include <wlr/types/wlr_server_decoration.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/util/log.h>
#include "server-decoration-protocol.h"
#include "util/signal.h"

static void server_decoration_handle_release(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void server_decoration_handle_request_mode(struct wl_client *client,
		struct wl_resource *resource, uint32_t mode) {
	struct wlr_server_decoration *decoration =
		wl_resource_get_user_data(resource);
	if (decoration->mode == mode) {
		return;
	}
	decoration->mode = mode;
	wlr_signal_emit_safe(&decoration->events.mode, decoration);
	org_kde_kwin_server_decoration_send_mode(decoration->resource,
		decoration->mode);
}

static void server_decoration_destroy(
		struct wlr_server_decoration *decoration) {
	wlr_signal_emit_safe(&decoration->events.destroy, decoration);
	wl_list_remove(&decoration->surface_destroy_listener.link);
	wl_resource_set_user_data(decoration->resource, NULL);
	wl_list_remove(&decoration->link);
	free(decoration);
}

static void server_decoration_destroy_resource(struct wl_resource *resource) {
	struct wlr_server_decoration *decoration =
		wl_resource_get_user_data(resource);
	if (decoration != NULL) {
		server_decoration_destroy(decoration);
	}
}

static void server_decoration_handle_surface_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_server_decoration *decoration =
		wl_container_of(listener, decoration, surface_destroy_listener);
	server_decoration_destroy(decoration);
}

static const struct org_kde_kwin_server_decoration_interface
server_decoration_impl = {
	.release = server_decoration_handle_release,
	.request_mode = server_decoration_handle_request_mode,
};

static void server_decoration_manager_handle_create(struct wl_client *client,
		struct wl_resource *manager_resource, uint32_t id,
		struct wl_resource *surface_resource) {
	struct wlr_server_decoration_manager *manager =
		wl_resource_get_user_data(manager_resource);
	struct wlr_surface *surface = wl_resource_get_user_data(surface_resource);

	struct wlr_server_decoration *decoration =
		calloc(1, sizeof(struct wlr_server_decoration));
	if (decoration == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	decoration->surface = surface;
	decoration->mode = manager->default_mode;

	int version = wl_resource_get_version(manager_resource);
	decoration->resource = wl_resource_create(client,
		&org_kde_kwin_server_decoration_interface, version, id);
	if (decoration->resource == NULL) {
		free(decoration);
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(decoration->resource,
		&server_decoration_impl, decoration,
		server_decoration_destroy_resource);

	wlr_log(L_DEBUG, "new server_decoration %p (res %p)", decoration,
		decoration->resource);

	wl_signal_init(&decoration->events.destroy);
	wl_signal_init(&decoration->events.mode);

	wl_signal_add(&surface->events.destroy,
		&decoration->surface_destroy_listener);
	decoration->surface_destroy_listener.notify =
		server_decoration_handle_surface_destroy;

	wl_list_insert(&manager->decorations, &decoration->link);

	org_kde_kwin_server_decoration_send_mode(decoration->resource,
		decoration->mode);

	wlr_signal_emit_safe(&manager->events.new_decoration, decoration);
}

static const struct org_kde_kwin_server_decoration_manager_interface
server_decoration_manager_impl = {
	.create = server_decoration_manager_handle_create,
};

void wlr_server_decoration_manager_set_default_mode(
		struct wlr_server_decoration_manager *manager, uint32_t default_mode) {
	manager->default_mode = default_mode;

	struct wl_resource *resource;
	wl_resource_for_each(resource, &manager->wl_resources) {
		org_kde_kwin_server_decoration_manager_send_default_mode(resource,
			manager->default_mode);
	}
}

void server_decoration_manager_destroy_resource(struct wl_resource *resource) {
	wl_list_remove(wl_resource_get_link(resource));
}

static void server_decoration_manager_bind(struct wl_client *client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_server_decoration_manager *manager = data;
	assert(client && manager);

	struct wl_resource *resource = wl_resource_create(client,
		&org_kde_kwin_server_decoration_manager_interface, version, id);
	if (resource == NULL) {
		wl_client_post_no_memory(client);
		return;
	}
	wl_resource_set_implementation(resource, &server_decoration_manager_impl,
		manager, server_decoration_manager_destroy_resource);

	wl_list_insert(&manager->wl_resources, wl_resource_get_link(resource));

	org_kde_kwin_server_decoration_manager_send_default_mode(resource,
		manager->default_mode);
}

void wlr_server_decoration_manager_destroy(
		struct wlr_server_decoration_manager *manager) {
	if (manager == NULL) {
		return;
	}
	wl_list_remove(&manager->display_destroy.link);
	struct wlr_server_decoration *decoration, *tmp_decoration;
	wl_list_for_each_safe(decoration, tmp_decoration, &manager->decorations,
			link) {
		server_decoration_destroy(decoration);
	}
	struct wl_resource *resource, *tmp_resource;
	wl_resource_for_each_safe(resource, tmp_resource, &manager->wl_resources) {
		server_decoration_manager_destroy_resource(resource);
	}
	wl_global_destroy(manager->wl_global);
	free(manager);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_server_decoration_manager *manager =
		wl_container_of(listener, manager, display_destroy);
	wlr_server_decoration_manager_destroy(manager);
}

struct wlr_server_decoration_manager *wlr_server_decoration_manager_create(
		struct wl_display *display) {
	struct wlr_server_decoration_manager *manager =
		calloc(1, sizeof(struct wlr_server_decoration_manager));
	if (manager == NULL) {
		return NULL;
	}
	manager->wl_global = wl_global_create(display,
		&org_kde_kwin_server_decoration_manager_interface, 1, manager,
		server_decoration_manager_bind);
	if (manager->wl_global == NULL) {
		free(manager);
		return NULL;
	}
	manager->default_mode = ORG_KDE_KWIN_SERVER_DECORATION_MANAGER_MODE_NONE;
	wl_list_init(&manager->wl_resources);
	wl_list_init(&manager->decorations);
	wl_signal_init(&manager->events.new_decoration);

	manager->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &manager->display_destroy);

	return manager;
}
