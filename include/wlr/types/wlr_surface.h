#ifndef WLR_TYPES_WLR_SURFACE_H
#define WLR_TYPES_WLR_SURFACE_H

#include <wayland-server.h>
#include <pixman.h>
#include <stdint.h>
#include <stdbool.h>

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

struct wlr_surface_state {
	uint32_t invalid;
	struct wl_resource *buffer;
	int32_t sx, sy;
	pixman_region32_t surface_damage, buffer_damage;
	pixman_region32_t opaque, input;
	enum wl_output_transform transform;
	int32_t scale;
	int width, height;
	int buffer_width, buffer_height;
};

struct wlr_subsurface {
	struct wl_resource *resource;
	struct wlr_surface *surface;
	struct wlr_surface *parent;

	struct wlr_surface_state cached;

	struct {
		int32_t x, y;
	} position;

	struct {
		int32_t x, y;
		bool set;
	} pending_position;

	bool synchronized;
};

struct wlr_surface {
	struct wl_resource *resource;
	struct wlr_renderer *renderer;
	struct wlr_texture *texture;
	struct wlr_surface_state current, pending;
	const char *role; // the lifetime-bound role or null

	float buffer_to_surface_matrix[16];
	float surface_to_buffer_matrix[16];
	bool reupload_buffer;

	struct {
		struct wl_signal commit;
		struct wl_signal destroy;
	} signals;

	struct wl_list frame_callback_list; // wl_surface.frame
	// destroy listener used by compositor
	struct wl_listener compositor_listener;
	void *compositor_data;

	// subsurface properties
	struct wlr_subsurface *subsurface;

	void *data;
};

struct wlr_renderer;
struct wlr_surface *wlr_surface_create(struct wl_resource *res,
		struct wlr_renderer *renderer);
void wlr_surface_flush_damage(struct wlr_surface *surface);
/**
 * Gets a matrix you can pass into wlr_render_with_matrix to display this
 * surface. `matrix` is the output matrix, `projection` is the wlr_output
 * projection matrix, and `transform` is any additional transformations you want
 * to perform on the surface (or NULL/the identity matrix if you don't).
 * `transform` is used before the surface is scaled, so its geometry extends
 * from 0 to 1 in both dimensions.
 */
void wlr_surface_get_matrix(struct wlr_surface *surface,
		float (*matrix)[16],
		const float (*projection)[16],
		const float (*transform)[16]);


/**
 * Set the lifetime role for this surface. Returns 0 on success or -1 if the
 * role cannot be set.
 */
int wlr_surface_set_role(struct wlr_surface *surface, const char *role,
		struct wl_resource *error_resource, uint32_t error_code);

/**
 * Create the subsurface implementation for this surface.
 */
void wlr_surface_make_subsurface(struct wlr_surface *surface,
		struct wlr_surface *parent, uint32_t id);

#endif
