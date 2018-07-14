#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <wlr/types/wlr_tablet_v2.h>

#include <wlr/util/log.h>
#include <assert.h>
#include <libinput.h>
#include <string.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/config.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_tablet_tool.h>

#include "tablet-unstable-v2-protocol.h"

struct wlr_tablet_seat_v2 {
	struct wl_list link;
	struct wlr_seat *wlr_seat;
	struct wlr_tablet_manager_v2 *manager;

	struct wl_list tablets; // wlr_tablet_v2_tablet::link
	struct wl_list tools;
	struct wl_list pads;

	struct wl_list clients; //wlr_tablet_seat_v2_client::link;

	struct wl_listener seat_destroy;
};

struct wlr_tablet_manager_client_v2 {
	struct wl_list link;
	struct wl_client *client;
	struct wl_resource *resource;

	struct wlr_tablet_manager_v2 *manager;

	struct wl_list tablet_seats; // wlr_tablet_seat_client_v2::link
};

struct wlr_tablet_seat_client_v2 {
	struct wl_list seat_link;
	struct wl_list client_link;
	struct wl_client *wl_client;
	struct wl_resource *resource;

	struct wlr_tablet_manager_client_v2 *client;
	struct wlr_seat_client *seat;

	struct wl_listener seat_client_destroy;

	struct wl_list tools;
	struct wl_list tablets;
	struct wl_list pads; //wlr_tablet_pad_client_v2::link
};

struct wlr_tablet_client_v2 {
	struct wl_list seat_link; // wlr_tablet_seat_client_v2::tablet
	struct wl_list tablet_link; // wlr_tablet_v2_tablet::clients
	struct wl_client *client;
	struct wl_resource *resource;

	struct wl_listener device_destroy;
};

struct wlr_tablet_tool_client_v2 {
	struct wl_list seat_link;
	struct wl_list tool_link;
	struct wl_client *client;
	struct wl_resource *resource;

	struct wlr_surface *cursor;
	struct wl_listener cursor_destroy;

	struct wl_listener tool_destroy;
};

struct wlr_tablet_pad_client_v2 {
	struct wl_list seat_link;
	struct wl_list pad_link;
	struct wl_client *client;
	struct wl_resource *resource;

	size_t button_count;

	size_t group_count;
	struct wl_resource **groups;

	size_t ring_count;
	struct wl_resource **rings;

	size_t strip_count;
	struct wl_resource **strips;

	struct wl_listener device_destroy;
};

static struct zwp_tablet_v2_interface tablet_impl;

static struct wlr_tablet_client_v2 *tablet_client_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_tablet_v2_interface,
		&tablet_impl));
	return wl_resource_get_user_data(resource);
}

static void destroy_tablet_v2(struct wl_resource *resource) {
	struct wlr_tablet_client_v2 *tablet = tablet_client_from_resource(resource);

	wl_list_remove(&tablet->seat_link);
	wl_list_remove(&tablet->tablet_link);

	//wl_list_remove(tablet->device_destroy.link);
	//wl_list_remove(tablet->client_destroy.link);
}

static void handle_tablet_v2_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_tablet_v2_interface tablet_impl = {
	.destroy = handle_tablet_v2_destroy,
};

static struct wlr_tablet_seat_v2 *make_tablet_seat(
		struct wlr_tablet_manager_v2 *manager,
		struct wlr_seat *wlr_seat) {
	struct wlr_tablet_seat_v2 *tablet_seat =
		calloc(1, sizeof(struct wlr_tablet_seat_v2));
	if (!tablet_seat) {
		return NULL;
	}

	tablet_seat->manager = manager;
	tablet_seat->wlr_seat = wlr_seat;

	wl_list_init(&tablet_seat->clients);

	wl_list_init(&tablet_seat->tablets);
	wl_list_init(&tablet_seat->tools);
	wl_list_init(&tablet_seat->pads);

	wl_list_insert(&manager->seats, &tablet_seat->link);
	return tablet_seat;
}

static struct wlr_tablet_seat_v2 *get_or_make_tablet_seat(
		struct wlr_tablet_manager_v2 *manager,
		struct wlr_seat *wlr_seat) {
	struct wlr_tablet_seat_v2 *pos;
	wl_list_for_each(pos, &manager->seats, link) {
		if (pos->wlr_seat == wlr_seat) {
			return pos;
		}
	}

	return make_tablet_seat(manager, wlr_seat);
}

static void add_tablet_client(struct wlr_tablet_seat_client_v2 *seat,
		struct wlr_tablet_v2_tablet *tablet) {
	struct wlr_tablet_client_v2 *client =
		calloc(1, sizeof(struct wlr_tablet_client_v2));
	if (!client) {
		return;
	}

	client->resource =
		wl_resource_create(seat->wl_client, &zwp_tablet_v2_interface, 1, 0);
	if (!client->resource) {
		free(client);
		return;
	}
	wl_resource_set_implementation(client->resource, &tablet_impl,
		client, destroy_tablet_v2);
	zwp_tablet_seat_v2_send_tablet_added(seat->resource, client->resource);

	// Send the expected events
	if (tablet->wlr_tool->name) {
		zwp_tablet_v2_send_name(client->resource, tablet->wlr_tool->name);
	}
	zwp_tablet_v2_send_id(client->resource,
		tablet->wlr_device->vendor, tablet->wlr_device->product);
	struct wlr_tablet_path *path;
	wl_list_for_each(path, &tablet->wlr_tool->paths, link) {
		zwp_tablet_v2_send_path(client->resource, path->path);
	}
	zwp_tablet_v2_send_done(client->resource);

	client->client = seat->wl_client;
	wl_list_insert(&seat->tablets, &client->seat_link);
	wl_list_insert(&tablet->clients, &client->tablet_link);
}

static void handle_wlr_tablet_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tablet_v2_tablet *tablet =
		wl_container_of(listener, tablet, tool_destroy);

	struct wlr_tablet_client_v2 *pos;
	struct wlr_tablet_client_v2 *tmp;
	wl_list_for_each_safe(pos, tmp, &tablet->clients, tablet_link) {
		// XXX: Add a timer/flag to destroy if client is slow?
		zwp_tablet_v2_send_removed(pos->resource);
	}

	wl_list_remove(&tablet->clients);
	wl_list_remove(&tablet->link);
	wl_list_remove(&tablet->tool_destroy.link);
	free(tablet);
}

static void handle_tablet_tool_v2_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}
static struct zwp_tablet_tool_v2_interface tablet_tool_impl = {
	.set_cursor = NULL,
	.destroy = handle_tablet_tool_v2_destroy,
};

static enum zwp_tablet_tool_v2_type tablet_type_from_wlr_type(
		enum wlr_tablet_tool_type wlr_type) {
	switch(wlr_type) {
	case WLR_TABLET_TOOL_TYPE_PEN:
		return ZWP_TABLET_TOOL_V2_TYPE_PEN;
	case WLR_TABLET_TOOL_TYPE_ERASER:
		return ZWP_TABLET_TOOL_V2_TYPE_ERASER;
	case WLR_TABLET_TOOL_TYPE_BRUSH:
		return ZWP_TABLET_TOOL_V2_TYPE_BRUSH;
	case WLR_TABLET_TOOL_TYPE_PENCIL:
		return ZWP_TABLET_TOOL_V2_TYPE_PENCIL;
	case WLR_TABLET_TOOL_TYPE_AIRBRUSH:
		return ZWP_TABLET_TOOL_V2_TYPE_AIRBRUSH;
	case WLR_TABLET_TOOL_TYPE_MOUSE:
		return ZWP_TABLET_TOOL_V2_TYPE_MOUSE;
	case WLR_TABLET_TOOL_TYPE_LENS:
		return ZWP_TABLET_TOOL_V2_TYPE_LENS;
	}

	assert(false && "Unreachable");
}

static void add_tablet_tool_client(struct wlr_tablet_seat_client_v2 *seat,
		struct wlr_tablet_v2_tablet_tool *tool) {
	struct wlr_tablet_tool_client_v2 *client =
		calloc(1, sizeof(struct wlr_tablet_tool_client_v2));
	if (!client) {
		return;
	}

	client->resource =
		wl_resource_create(seat->wl_client, &zwp_tablet_tool_v2_interface, 1, 0);
	if (!client->resource) {
		free(client);
		return;
	}
	wl_resource_set_implementation(client->resource, &tablet_tool_impl,
		client, NULL);
	zwp_tablet_seat_v2_send_tool_added(seat->resource, client->resource);

	// Send the expected events
	if (tool->wlr_tool->hardware_serial) {
			zwp_tablet_tool_v2_send_hardware_serial(
			client->resource, 
			tool->wlr_tool->hardware_serial >> 32,
			tool->wlr_tool->hardware_serial & 0xFFFFFFFF);
	}
	if (tool->wlr_tool->hardware_wacom) {
			zwp_tablet_tool_v2_send_hardware_id_wacom(
			client->resource, 
			tool->wlr_tool->hardware_wacom >> 32,
			tool->wlr_tool->hardware_wacom & 0xFFFFFFFF);
	}
	zwp_tablet_tool_v2_send_type(client->resource, 
		tablet_type_from_wlr_type(tool->wlr_tool->type));

	if (tool->wlr_tool->tilt) {
		zwp_tablet_tool_v2_send_capability(client->resource,
			ZWP_TABLET_TOOL_V2_CAPABILITY_TILT);
	}

	if (tool->wlr_tool->pressure) {
		zwp_tablet_tool_v2_send_capability(client->resource,
			ZWP_TABLET_TOOL_V2_CAPABILITY_PRESSURE);
	}

	if (tool->wlr_tool->distance) {
		zwp_tablet_tool_v2_send_capability(client->resource,
			ZWP_TABLET_TOOL_V2_CAPABILITY_DISTANCE);
	}

	if (tool->wlr_tool->rotation) {
		zwp_tablet_tool_v2_send_capability(client->resource,
			ZWP_TABLET_TOOL_V2_CAPABILITY_ROTATION);
	}

	if (tool->wlr_tool->slider) {
		zwp_tablet_tool_v2_send_capability(client->resource,
			ZWP_TABLET_TOOL_V2_CAPABILITY_SLIDER);
	}

	if (tool->wlr_tool->wheel) {
		zwp_tablet_tool_v2_send_capability(client->resource,
			ZWP_TABLET_TOOL_V2_CAPABILITY_WHEEL);
	}

	zwp_tablet_tool_v2_send_done(client->resource);

	client->client = seat->wl_client;
	wl_list_insert(&seat->tools, &client->seat_link);
	wl_list_insert(&tool->clients, &client->tool_link);
}

struct wlr_tablet_v2_tablet *wlr_make_tablet(
		struct wlr_tablet_manager_v2 *manager,
		struct wlr_seat *wlr_seat,
		struct wlr_input_device *wlr_device) {
	assert(wlr_device->type == WLR_INPUT_DEVICE_TABLET_TOOL);
	struct wlr_tablet_seat_v2 *seat = get_or_make_tablet_seat(manager, wlr_seat);
	if (!seat) {
		return NULL;
	}
	struct wlr_tablet_tool *tool = wlr_device->tablet_tool;
	struct wlr_tablet_v2_tablet *tablet = calloc(1, sizeof(struct wlr_tablet_v2_tablet));
	if (!tablet) {
		return NULL;
	}

	tablet->wlr_tool = tool;
	tablet->wlr_device = wlr_device;
	wl_list_init(&tablet->clients);


	tablet->tool_destroy.notify = handle_wlr_tablet_destroy;
	wl_signal_add(&wlr_device->events.destroy, &tablet->tool_destroy);
	wl_list_insert(&seat->tablets, &tablet->link);

	// We need to create a tablet client for all clients on the seat
	struct wlr_tablet_seat_client_v2 *pos;
	wl_list_for_each(pos, &seat->clients, seat_link) {
		// Tell the clients about the new tool
		add_tablet_client(pos, tablet);
	}

	return tablet;
}

static void handle_wlr_tablet_tool_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tablet_v2_tablet_tool *tool =
		wl_container_of(listener, tool, tool_destroy);

	struct wlr_tablet_tool_client_v2 *pos;
	struct wlr_tablet_tool_client_v2 *tmp;
	wl_list_for_each_safe(pos, tmp, &tool->clients, tool_link) {
		// XXX: Add a timer/flag to destroy if client is slow?
		zwp_tablet_tool_v2_send_removed(pos->resource);
	}

	wl_list_remove(&tool->clients);
	wl_list_remove(&tool->link);
	wl_list_remove(&tool->tool_destroy.link);
	free(tool);
}

struct wlr_tablet_v2_tablet_tool *wlr_make_tablet_tool(
		struct wlr_tablet_manager_v2 *manager,
		struct wlr_seat *wlr_seat,
		struct wlr_tablet_tool_tool *wlr_tool) {
	struct wlr_tablet_seat_v2 *seat = get_or_make_tablet_seat(manager, wlr_seat);
	if (!seat) {
		return NULL;
	}
	struct wlr_tablet_v2_tablet_tool *tool =
		calloc(1, sizeof(struct wlr_tablet_v2_tablet_tool));
	if (!tool) {
		return NULL;
	}

	tool->wlr_tool = wlr_tool;
	wl_list_init(&tool->clients);


	tool->tool_destroy.notify = handle_wlr_tablet_tool_destroy;
	wl_signal_add(&wlr_tool->events.destroy, &tool->tool_destroy);
	wl_list_insert(&seat->tools, &tool->link);

	// We need to create a tablet client for all clients on the seat
	struct wlr_tablet_seat_client_v2 *pos;
	wl_list_for_each(pos, &seat->clients, seat_link) {
		// Tell the clients about the new tool
		add_tablet_tool_client(pos, tool);
	}

	return tool;
}

static struct zwp_tablet_pad_v2_interface tablet_pad_impl;

static struct wlr_tablet_pad_client_v2 *tablet_pad_client_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_tablet_pad_v2_interface,
		&tablet_pad_impl));
	return wl_resource_get_user_data(resource);
}


static void destroy_tablet_pad_v2(struct wl_resource *resource) {
	struct wlr_tablet_pad_client_v2 *pad =
		tablet_pad_client_from_resource(resource);

	wl_list_remove(&pad->seat_link);
	wl_list_remove(&pad->pad_link);

	//wl_list_remove(tablet->device_destroy.link);
	//wl_list_remove(tablet->client_destroy.link);
}

static void handle_tablet_pad_v2_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void destroy_tablet_pad_ring_v2(struct wl_resource *resource) {
	struct wlr_tablet_pad_client_v2 *client = wl_resource_get_user_data(resource);

	for (size_t i = 0; i < client->ring_count; ++i) {
		if (client->rings[i] == resource) {
			client->rings[i] = NULL;
			return;
		}
	}
}

static void handle_tablet_pad_ring_v2_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_tablet_pad_ring_v2_interface tablet_pad_ring_impl = {
	.set_feedback = NULL,
	.destroy = handle_tablet_pad_ring_v2_destroy,
};

static void destroy_tablet_pad_strip_v2(struct wl_resource *resource) {
	struct wlr_tablet_pad_client_v2 *client = wl_resource_get_user_data(resource);

	for (size_t i = 0; i < client->strip_count; ++i) {
		if (client->strips[i] == resource) {
			client->strips[i] = NULL;
			return;
		}
	}
}

static void handle_tablet_pad_strip_v2_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_tablet_pad_strip_v2_interface tablet_pad_strip_impl = {
	.set_feedback = NULL,
	.destroy = handle_tablet_pad_strip_v2_destroy,
};

static struct zwp_tablet_pad_v2_interface tablet_pad_impl = {
	.set_feedback = NULL,
	.destroy = handle_tablet_pad_v2_destroy,
};

static void destroy_tablet_pad_group_v2(struct wl_resource *resource) {
	struct wlr_tablet_pad_client_v2 *client = wl_resource_get_user_data(resource);

	for (size_t i = 0; i < client->group_count; ++i) {
		if (client->groups[i] == resource) {
			client->groups[i] = NULL;
			return;
		}
	}
}

static void handle_tablet_pad_group_v2_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_tablet_pad_group_v2_interface tablet_pad_group_impl = {
	.destroy = handle_tablet_pad_group_v2_destroy,
};

static void add_tablet_pad_group(struct wlr_tablet_v2_tablet_pad *pad,
		struct wlr_tablet_pad_client_v2 *client,
		struct wlr_tablet_pad_group *group, size_t index) {
	client->groups[index] =
		wl_resource_create(client->client, &zwp_tablet_pad_group_v2_interface, 1, 0);
	if (!client->groups[index]) {
		wl_client_post_no_memory(client->client);
		return;
	}
	wl_resource_set_implementation(client->groups[index], &tablet_pad_group_impl,
		client, destroy_tablet_pad_group_v2);

	zwp_tablet_pad_v2_send_group(client->resource, client->groups[index]);
	zwp_tablet_pad_group_v2_send_modes(client->groups[index], group->mode_count);

	struct wl_array button_array;
	wl_array_init(&button_array);
	wl_array_add(&button_array, group->button_count * sizeof(int));
	memcpy(button_array.data, group->buttons, group->button_count * sizeof(int));
	zwp_tablet_pad_group_v2_send_buttons(client->groups[index], &button_array);

	client->strip_count = group->strip_count;
	for (size_t i = 0; i < group->strip_count; ++i) {
		size_t strip = group->strips[i];
		client->strips[strip] =
			wl_resource_create(client->client, &zwp_tablet_pad_strip_v2_interface, 1, 0);
		wl_resource_set_implementation(client->strips[strip],
			&tablet_pad_strip_impl,
			client, destroy_tablet_pad_strip_v2);
		zwp_tablet_pad_group_v2_send_strip(client->groups[index], client->strips[strip]);
	}

	client->ring_count = group->ring_count;
	for (size_t i = 0; i < group->ring_count; ++i) {
		size_t ring = group->rings[i];
		client->rings[ring] =
			wl_resource_create(client->client, &zwp_tablet_pad_ring_v2_interface, 1, 0);
		wl_resource_set_implementation(client->rings[ring],
			&tablet_pad_ring_impl,
			client, destroy_tablet_pad_ring_v2);
		zwp_tablet_pad_group_v2_send_ring(client->groups[index], client->rings[ring]);
	}

	zwp_tablet_pad_group_v2_send_done(client->groups[index]);
}

static void add_tablet_pad_client(struct wlr_tablet_seat_client_v2 *seat,
		struct wlr_tablet_v2_tablet_pad *pad) {
	struct wlr_tablet_pad_client_v2 *client =
		calloc(1, sizeof(struct wlr_tablet_pad_client_v2));
	if (!client) {
		wl_client_post_no_memory(seat->wl_client);
		return;
	}

	client->groups = calloc(sizeof(int), wl_list_length(&pad->wlr_pad->groups));
	if (!client->groups) {
		wl_client_post_no_memory(seat->wl_client);
		free(client);
		return;
	}

	client->rings = calloc(sizeof(struct wl_resource*), pad->wlr_pad->ring_count);
	if (!client->rings) {
		wl_client_post_no_memory(seat->wl_client);
		free(client->groups);
		free(client);
		return;
	}

	client->strips = calloc(sizeof(struct wl_resource*), pad->wlr_pad->strip_count);
	if (!client->strips) {
		wl_client_post_no_memory(seat->wl_client);
		free(client->groups);
		free(client->rings);
		free(client);
		return;
	}

	client->resource =
		wl_resource_create(seat->wl_client, &zwp_tablet_pad_v2_interface, 1, 0);
	if (!client->resource) {
		wl_client_post_no_memory(seat->wl_client);
		free(client->groups);
		free(client->rings);
		free(client->strips);
		free(client);
		return;
	}
	wl_resource_set_implementation(client->resource, &tablet_pad_impl,
		client, destroy_tablet_pad_v2);
	zwp_tablet_seat_v2_send_pad_added(seat->resource, client->resource);
	client->client = seat->wl_client;

	// Send the expected events
	if (pad->wlr_pad->button_count) {
		zwp_tablet_pad_v2_send_buttons(client->resource, pad->wlr_pad->button_count);
	}
	struct wlr_tablet_path *path;
	wl_list_for_each(path, &pad->wlr_pad->paths, link) {
		zwp_tablet_pad_v2_send_path(client->resource, path->path);
	}
	size_t i = 0;
	struct wlr_tablet_pad_group *group;
	wl_list_for_each(group, &pad->wlr_pad->groups, link) {
		add_tablet_pad_group(pad, client, group, i++);
	}
	zwp_tablet_pad_v2_send_done(client->resource);

	wl_list_insert(&seat->pads, &client->seat_link);
	wl_list_insert(&pad->clients, &client->pad_link);
}

static void handle_wlr_tablet_pad_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tablet_v2_tablet_pad *pad =
		wl_container_of(listener, pad, pad_destroy);

	struct wlr_tablet_pad_client_v2 *pos;
	struct wlr_tablet_pad_client_v2 *tmp;
	wl_list_for_each_safe(pos, tmp, &pad->clients, pad_link) {
		// XXX: Add a timer/flag to destroy if client is slow?
		zwp_tablet_pad_v2_send_removed(pos->resource);
	}

	wl_list_remove(&pad->clients);
	wl_list_remove(&pad->link);
	wl_list_remove(&pad->pad_destroy.link);
	free(pad);
}

struct wlr_tablet_v2_tablet_pad *wlr_make_tablet_pad(
		struct wlr_tablet_manager_v2 *manager,
		struct wlr_seat *wlr_seat,
		struct wlr_input_device *wlr_device) {
	assert(wlr_device->type == WLR_INPUT_DEVICE_TABLET_PAD);
	struct wlr_tablet_seat_v2 *seat = get_or_make_tablet_seat(manager, wlr_seat);
	if (!seat) {
		return NULL;
	}
	struct wlr_tablet_pad *wlr_pad = wlr_device->tablet_pad;
	struct wlr_tablet_v2_tablet_pad *pad = calloc(1, sizeof(struct wlr_tablet_v2_tablet_pad));
	if (!pad) {
		return NULL;
	}

	pad->wlr_pad = wlr_pad;
	wl_list_init(&pad->clients);

	pad->pad_destroy.notify = handle_wlr_tablet_pad_destroy;
	wl_signal_add(&wlr_device->events.destroy, &pad->pad_destroy);
	wl_list_insert(&seat->pads, &pad->link);

	// We need to create a tablet client for all clients on the seat
	struct wlr_tablet_seat_client_v2 *pos;
	wl_list_for_each(pos, &seat->clients, seat_link) {
		// Tell the clients about the new tool
		add_tablet_pad_client(pos, pad);
	}

	wlr_log(L_DEBUG, "Created tablet v2 pad:");
	struct wlr_tablet_path *path;
	wl_list_for_each(path, &wlr_pad->paths, link) {
		wlr_log(L_DEBUG, "%s", path->path);
	}

	return pad;
}

void wlr_tablet_v2_destroy(struct wlr_tablet_manager_v2 *manager);
static struct wlr_tablet_manager_client_v2 *tablet_manager_client_from_resource(struct wl_resource *resource);

static void tablet_seat_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct zwp_tablet_seat_v2_interface seat_impl = {
	.destroy = tablet_seat_destroy,
};

static struct wlr_tablet_seat_client_v2 *tablet_seat_from_resource (
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_tablet_seat_v2_interface,
		&seat_impl));
	return wl_resource_get_user_data(resource);
}

static void wlr_tablet_seat_client_v2_destroy(struct wl_resource *resource) {
	struct wlr_tablet_seat_client_v2 *seat = tablet_seat_from_resource(resource);

	seat->resource = NULL;
	/* We can't just destroy the struct, because we may need to iterate it
	 * on display->destroy/manager_destroy
	 */
	// TODO: Implement the free() check
}

static void handle_seat_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tablet_seat_client_v2 *seat =
		wl_container_of(listener, seat, seat_client_destroy);

	seat->seat = NULL;
	wl_list_remove(&seat->seat_client_destroy.link);
	/* Remove leaves it in a defunct state, we will remove again in the
	 * actual destroy sequence
	 */
	wl_list_init(&seat->seat_client_destroy.link);
}

static void tablet_manager_destroy(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static void get_tablet_seat(struct wl_client *wl_client, struct wl_resource *resource,
		uint32_t id, struct wl_resource *seat_resource)
{
	struct wlr_tablet_manager_client_v2 *manager = tablet_manager_client_from_resource(resource);
	struct wlr_seat_client *seat = wlr_seat_client_from_resource(seat_resource);
	struct wlr_tablet_seat_v2 *tablet_seat =
		get_or_make_tablet_seat(manager->manager, seat->seat);

	if (!tablet_seat) {// This can only happen when we ran out of memory
		wl_client_post_no_memory(wl_client);
		return;
	}

	struct wlr_tablet_seat_client_v2 *seat_client =
		calloc(1, sizeof(struct wlr_tablet_seat_client_v2));
	if (tablet_seat == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	seat_client->resource =
		wl_resource_create(wl_client, &zwp_tablet_seat_v2_interface, 1, id);
	if (seat_client->resource == NULL) {
		free(seat_client);
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(seat_client->resource, &seat_impl, seat_client,
		wlr_tablet_seat_client_v2_destroy);


	seat_client->seat = seat;
	seat_client->client = manager;
	seat_client->wl_client = wl_client;
	wl_list_init(&seat_client->tools);
	wl_list_init(&seat_client->tablets);
	wl_list_init(&seat_client->pads);

	seat_client->seat_client_destroy.notify = handle_seat_destroy;
	wl_signal_add(&seat->events.destroy, &seat_client->seat_client_destroy);

	wl_list_insert(&manager->tablet_seats, &seat_client->client_link);
	wl_list_insert(&tablet_seat->clients, &seat_client->seat_link);

	// We need to emmit the devices allready on the seat
	struct wlr_tablet_v2_tablet *tablet_pos;
	wl_list_for_each(tablet_pos, &tablet_seat->tablets, link) {
		add_tablet_client(seat_client, tablet_pos);
	}

	struct wlr_tablet_v2_tablet_pad *pad_pos;
	wl_list_for_each(pad_pos, &tablet_seat->pads, link) {
		add_tablet_pad_client(seat_client, pad_pos);
	}

	struct wlr_tablet_v2_tablet_tool *tool_pos;
	wl_list_for_each(tool_pos, &tablet_seat->tools, link) {
		add_tablet_tool_client(seat_client, tool_pos);
	}
}

static struct zwp_tablet_manager_v2_interface manager_impl = {
	.get_tablet_seat = get_tablet_seat,
	.destroy = tablet_manager_destroy,
};

static struct wlr_tablet_manager_client_v2 *tablet_manager_client_from_resource (
		struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &zwp_tablet_manager_v2_interface,
		&manager_impl));
	return wl_resource_get_user_data(resource);
}

static void wlr_tablet_manager_v2_destroy(struct wl_resource *resource) {
	struct wlr_tablet_manager_client_v2 *client = tablet_manager_client_from_resource(resource);

	client->resource = NULL;
	/* We can't just destroy the struct, because we may need to iterate it
	 * on display->destroy/manager_destroy
	 */
	// TODO: Implement the free() check
}

static void tablet_v2_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_tablet_manager_v2 *manager = data;
	assert(wl_client && manager);

	struct wlr_tablet_manager_client_v2 *client =
		calloc(1, sizeof(struct wlr_tablet_manager_client_v2));
	if (client == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}

	wl_list_init(&client->tablet_seats);

	client->resource =
		wl_resource_create(wl_client, &zwp_tablet_manager_v2_interface, version, id);
	if (client->resource == NULL) {
		free(client);
		wl_client_post_no_memory(wl_client);
		return;
	}
	client->client = wl_client;
	client->manager = manager;

	wl_resource_set_implementation(client->resource, &manager_impl, client,
		wlr_tablet_manager_v2_destroy);
	wl_list_insert(&manager->clients, &client->link);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_tablet_manager_v2 *tablet =
		wl_container_of(listener, tablet, display_destroy);
	wlr_tablet_v2_destroy(tablet);
}

void wlr_tablet_v2_destroy(struct wlr_tablet_manager_v2 *manager) {
	struct wlr_tablet_manager_client_v2 *tmp;
	struct wlr_tablet_manager_client_v2 *pos;

	wl_list_for_each_safe(pos, tmp, &manager->clients, link) {
		wl_resource_destroy(pos->resource);
	}

	wl_global_destroy(manager->wl_global);
	free(manager);
}

struct wlr_tablet_manager_v2 *wlr_tablet_v2_create(struct wl_display *display) {
	struct wlr_tablet_manager_v2 *tablet =
		calloc(1, sizeof(struct wlr_tablet_manager_v2));
	if (!tablet) {
		return NULL;
	}

	wl_list_init(&tablet->clients);
	wl_list_init(&tablet->seats);

	tablet->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &tablet->display_destroy);

	tablet->wl_global = wl_global_create(display,
		&zwp_tablet_manager_v2_interface, 1, tablet, tablet_v2_bind);
	if (tablet->wl_global == NULL) {
		free(tablet);
		return NULL;
	}

	return tablet;
}
