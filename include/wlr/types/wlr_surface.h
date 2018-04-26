#ifndef WLR_TYPES_WLR_SURFACE_H
#define WLR_TYPES_WLR_SURFACE_H

#include <pixman.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/types/wlr_output.h>

struct wlr_frame_callback {
	struct wl_resource *resource;
	struct wl_list link;
};

#define WLR_SURFACE_INVALID_BUFFER 1
#define WLR_SURFACE_INVALID_SURFACE_DAMAGE 2
#define WLR_SURFACE_INVALID_BUFFER_DAMAGE 4
#define WLR_SURFACE_INVALID_OPAQUE_REGION 8
#define WLR_SURFACE_INVALID_INPUT_REGION 16
#define WLR_SURFACE_INVALID_TRANSFORM 32
#define WLR_SURFACE_INVALID_SCALE 64
#define WLR_SURFACE_INVALID_SUBSURFACE_POSITION 128
#define WLR_SURFACE_INVALID_FRAME_CALLBACK_LIST 256

struct wlr_surface_state {
	uint32_t invalid;
	struct wl_resource *buffer;
	struct wl_listener buffer_destroy_listener;
	int32_t sx, sy;
	pixman_region32_t surface_damage, buffer_damage;
	pixman_region32_t opaque, input;
	enum wl_output_transform transform;
	int32_t scale;
	int width, height;
	int buffer_width, buffer_height;

	struct {
		int32_t x, y;
	} subsurface_position;

	struct wl_list frame_callback_list; // wl_surface.frame
};

struct wlr_subsurface {
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wlr_surface *parent;

	struct wlr_surface_state *cached;
	bool has_cache;

	bool synchronized;
	bool reordered;

	struct wl_list parent_link;
	struct wl_list parent_pending_link;

	struct wl_listener surface_destroy;
	struct wl_listener parent_destroy;

	struct {
		struct wl_signal destroy;
	} events;
};

struct wlr_surface {
	struct wl_resource *resource;
	struct wlr_renderer *renderer;
	struct wlr_texture *texture;
	struct wlr_surface_state *current, *pending;
	const char *role; // the lifetime-bound role or null

	float buffer_to_surface_matrix[9];
	float surface_to_buffer_matrix[9];

	struct {
		struct wl_signal commit;
		struct wl_signal new_subsurface;
		struct wl_signal destroy;
	} events;

	// surface commit callback for the role that runs before all others
	void (*role_committed)(struct wlr_surface *surface, void *role_data);
	void *role_data;

	struct wl_list subsurfaces; // wlr_subsurface::parent_link

	// wlr_subsurface::parent_pending_link
	struct wl_list subsurface_pending_list;

	struct wl_listener renderer_destroy;

	void *data;
};

typedef void (*wlr_surface_iterator_func_t)(struct wlr_surface *surface,
	int sx, int sy, void *data);

struct wlr_renderer;

/**
 * Create a new surface resource with the provided new ID. If `resource_list`
 * is non-NULL, adds the surface's resource to the list.
 */
struct wlr_surface *wlr_surface_create(struct wl_client *client,
		uint32_t version, uint32_t id, struct wlr_renderer *renderer,
		struct wl_list *resource_list);

/**
 * Set the lifetime role for this surface. Returns 0 on success or -1 if the
 * role cannot be set.
 */
int wlr_surface_set_role(struct wlr_surface *surface, const char *role,
		struct wl_resource *error_resource, uint32_t error_code);

/**
 * Whether or not this surface currently has an attached buffer. A surface has
 * an attached buffer when it commits with a non-null buffer in its pending
 * state. A surface will not have a buffer if it has never committed one, has
 * committed a null buffer, or something went wrong with uploading the buffer.
 */
bool wlr_surface_has_buffer(struct wlr_surface *surface);

/**
 * Create a new subsurface resource with the provided new ID. If `resource_list`
 * is non-NULL, adds the subsurface's resource to the list.
 */
struct wlr_subsurface *wlr_subsurface_create(struct wlr_surface *surface,
		struct wlr_surface *parent, uint32_t version, uint32_t id,
		struct wl_list *resource_list);

/**
 * Get the root of the subsurface tree for this surface.
 */
struct wlr_surface *wlr_surface_get_root_surface(struct wlr_surface *surface);

/**
 * Check if the surface accepts input events at the given surface-local
 * coordinates. Does not check the surface's subsurfaces.
 */
bool wlr_surface_point_accepts_input(struct wlr_surface *surface,
		double sx, double sy);

/**
 * Find a surface in this surface's tree that accepts input events at the given
 * surface-local coordinates. Returns the surface and coordinates in the leaf
 * surface coordinate system or NULL if no surface is found at that location.
 */
struct wlr_surface *wlr_surface_surface_at(struct wlr_surface *surface,
		double sx, double sy, double *sub_x, double *sub_y);

void wlr_surface_send_enter(struct wlr_surface *surface,
		struct wlr_output *output);

void wlr_surface_send_leave(struct wlr_surface *surface,
		struct wlr_output *output);

void wlr_surface_send_frame_done(struct wlr_surface *surface,
		const struct timespec *when);

/**
 * Set a callback for surface commit that runs before all the other callbacks.
 * This is intended for use by the surface role.
 */
void wlr_surface_set_role_committed(struct wlr_surface *surface,
		void (*role_committed)(struct wlr_surface *surface, void *role_data),
		void *role_data);

struct wlr_surface *wlr_surface_from_resource(struct wl_resource *resource);

/**
 * Call `iterator` on each surface in the surface tree, with the surface's
 * positon relative to the root surface. The function is called from root to
 * leaves (in rendering order).
 */
void wlr_surface_for_each_surface(struct wlr_surface *surface,
	wlr_surface_iterator_func_t iterator, void *user_data);

#endif
