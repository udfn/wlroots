#include <assert.h>
#include <pixman.h>
#include <stdio.h>
#include <stdlib.h>
#include <wayland-server.h>
#include <wlr/types/wlr_region.h>

static void region_add(struct wl_client *client, struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	pixman_region32_t *region = wlr_region_from_resource(resource);
	pixman_region32_union_rect(region, region, x, y, width, height);
}

static void region_subtract(struct wl_client *client, struct wl_resource *resource,
		int32_t x, int32_t y, int32_t width, int32_t height) {
	pixman_region32_t *region = wlr_region_from_resource(resource);
	pixman_region32_union_rect(region, region, x, y, width, height);

	pixman_region32_t rect;
	pixman_region32_init_rect(&rect, x, y, width, height);
	pixman_region32_subtract(region, region, &rect);
	pixman_region32_fini(&rect);
}

static void region_destroy(struct wl_client *client, struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static const struct wl_region_interface region_impl = {
	.destroy = region_destroy,
	.add = region_add,
	.subtract = region_subtract,
};

static void destroy_region(struct wl_resource *resource) {
	pixman_region32_t *reg = wlr_region_from_resource(resource);
	pixman_region32_fini(reg);
	free(reg);
}

struct wl_resource *wlr_region_create(struct wl_client *client, uint32_t id) {
	pixman_region32_t *region = calloc(1, sizeof(pixman_region32_t));
	if (region == NULL) {
		return NULL;
	}

	pixman_region32_init(region);

	struct wl_resource *region_resource = wl_resource_create(client,
		&wl_region_interface, 1, id);
	if (region_resource == NULL) {
		free(region);
		return NULL;
	}
	wl_resource_set_implementation(region_resource, &region_impl, region,
		destroy_region);
	return region_resource;
}

pixman_region32_t *wlr_region_from_resource(struct wl_resource *resource) {
	assert(wl_resource_instance_of(resource, &wl_region_interface,
		&region_impl));
	return wl_resource_get_user_data(resource);
}
