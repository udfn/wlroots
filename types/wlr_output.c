#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <tgmath.h>
#include <time.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include <GLES2/gl2.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles2.h>
#include <wlr/render.h>

static void wl_output_send_to_resource(struct wl_resource *resource) {
	assert(resource);
	struct wlr_output *output = wl_resource_get_user_data(resource);
	assert(output);
	const uint32_t version = wl_resource_get_version(resource);
	if (version >= WL_OUTPUT_GEOMETRY_SINCE_VERSION) {
		wl_output_send_geometry(resource, output->lx, output->ly,
			output->phys_width, output->phys_height, output->subpixel,
			output->make, output->model, output->transform);
	}
	if (version >= WL_OUTPUT_MODE_SINCE_VERSION) {
		struct wlr_output_mode *mode;
		wl_list_for_each(mode, &output->modes, link) {
			uint32_t flags = mode->flags & WL_OUTPUT_MODE_PREFERRED;
			if (output->current_mode == mode) {
				flags |= WL_OUTPUT_MODE_CURRENT;
			}
			wl_output_send_mode(resource, flags, mode->width, mode->height,
				mode->refresh);
		}

		if (wl_list_length(&output->modes) == 0) {
			// Output has no mode, send the current width/height
			wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT,
				output->width, output->height, output->refresh);
		}
	}
	if (version >= WL_OUTPUT_SCALE_SINCE_VERSION) {
		wl_output_send_scale(resource, (uint32_t)ceil(output->scale));
	}
	if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
		wl_output_send_done(resource);
	}
}

static void wlr_output_send_current_mode_to_resource(
		struct wl_resource *resource) {
	assert(resource);
	struct wlr_output *output = wl_resource_get_user_data(resource);
	assert(output);
	const uint32_t version = wl_resource_get_version(resource);
	if (version < WL_OUTPUT_MODE_SINCE_VERSION) {
		return;
	}
	if (output->current_mode != NULL) {
		struct wlr_output_mode *mode = output->current_mode;
		uint32_t flags = mode->flags & WL_OUTPUT_MODE_PREFERRED;
		wl_output_send_mode(resource, flags | WL_OUTPUT_MODE_CURRENT,
			mode->width, mode->height, mode->refresh);
	} else {
		// Output has no mode
		wl_output_send_mode(resource, WL_OUTPUT_MODE_CURRENT, output->width,
			output->height, output->refresh);
	}
	if (version >= WL_OUTPUT_DONE_SINCE_VERSION) {
		wl_output_send_done(resource);
	}
}

static void wl_output_destroy(struct wl_resource *resource) {
	struct wlr_output *output = wl_resource_get_user_data(resource);
	struct wl_resource *_resource = NULL;
	wl_resource_for_each(_resource, &output->wl_resources) {
		if (_resource == resource) {
			struct wl_list *link = wl_resource_get_link(_resource);
			wl_list_remove(link);
			break;
		}
	}
}

static void wl_output_release(struct wl_client *client,
		struct wl_resource *resource) {
	wl_resource_destroy(resource);
}

static struct wl_output_interface wl_output_impl = {
	.release = wl_output_release,
};

static void wl_output_bind(struct wl_client *wl_client, void *data,
		uint32_t version, uint32_t id) {
	struct wlr_output *wlr_output = data;
	assert(wl_client && wlr_output);

	struct wl_resource *wl_resource = wl_resource_create(wl_client,
		&wl_output_interface, version, id);
	if (wl_resource == NULL) {
		wl_client_post_no_memory(wl_client);
		return;
	}
	wl_resource_set_implementation(wl_resource, &wl_output_impl, wlr_output,
		wl_output_destroy);
	wl_list_insert(&wlr_output->wl_resources,
		wl_resource_get_link(wl_resource));
	wl_output_send_to_resource(wl_resource);
}

static void wlr_output_create_global(struct wlr_output *output) {
	if (output->wl_global != NULL) {
		return;
	}
	struct wl_global *wl_global = wl_global_create(output->display,
		&wl_output_interface, 3, output, wl_output_bind);
	output->wl_global = wl_global;
}

static void wlr_output_destroy_global(struct wlr_output *output) {
	if (output->wl_global == NULL) {
		return;
	}
	struct wl_resource *resource, *tmp;
	wl_resource_for_each_safe(resource, tmp, &output->wl_resources) {
		wl_resource_destroy(resource);
	}
	wl_global_destroy(output->wl_global);
	output->wl_global = NULL;
}

void wlr_output_update_enabled(struct wlr_output *output, bool enabled) {
	if (output->enabled == enabled) {
		return;
	}

	output->enabled = enabled;

	if (enabled) {
		wlr_output_create_global(output);
	} else {
		wlr_output_destroy_global(output);
	}

	wl_signal_emit(&output->events.enable, output);
}

static void wlr_output_update_matrix(struct wlr_output *output) {
	wlr_matrix_texture(output->transform_matrix, output->width, output->height,
		output->transform);
}

void wlr_output_enable(struct wlr_output *output, bool enable) {
	if (output->impl->enable) {
		output->impl->enable(output, enable);
	}
}

bool wlr_output_set_mode(struct wlr_output *output,
		struct wlr_output_mode *mode) {
	if (!output->impl || !output->impl->set_mode) {
		return false;
	}
	return output->impl->set_mode(output, mode);
}

bool wlr_output_set_custom_mode(struct wlr_output *output, int32_t width,
		int32_t height, int32_t refresh) {
	if (!output->impl || !output->impl->set_custom_mode) {
		return false;
	}
	return output->impl->set_custom_mode(output, width, height, refresh);
}

void wlr_output_update_mode(struct wlr_output *output,
		struct wlr_output_mode *mode) {
	output->current_mode = mode;
	wlr_output_update_custom_mode(output, mode->width, mode->height,
		mode->refresh);
}

void wlr_output_update_custom_mode(struct wlr_output *output, int32_t width,
		int32_t height, int32_t refresh) {
	if (output->width == width && output->height == height &&
			output->refresh == refresh) {
		return;
	}

	output->width = width;
	output->height = height;
	wlr_output_update_matrix(output);

	output->refresh = refresh;

	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->wl_resources) {
		wlr_output_send_current_mode_to_resource(resource);
	}

	wl_signal_emit(&output->events.mode, output);
}

void wlr_output_set_transform(struct wlr_output *output,
		enum wl_output_transform transform) {
	output->impl->transform(output, transform);
	wlr_output_update_matrix(output);

	// TODO: only send geometry and done
	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->wl_resources) {
		wl_output_send_to_resource(resource);
	}

	wl_signal_emit(&output->events.transform, output);
}

void wlr_output_set_position(struct wlr_output *output, int32_t lx,
		int32_t ly) {
	if (lx == output->lx && ly == output->ly) {
		return;
	}

	output->lx = lx;
	output->ly = ly;

	// TODO: only send geometry and done
	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->wl_resources) {
		wl_output_send_to_resource(resource);
	}
}

void wlr_output_set_scale(struct wlr_output *output, float scale) {
	if (output->scale == scale) {
		return;
	}

	output->scale = scale;

	// TODO: only send mode and done
	struct wl_resource *resource;
	wl_resource_for_each(resource, &output->wl_resources) {
		wl_output_send_to_resource(resource);
	}

	wl_signal_emit(&output->events.scale, output);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_output *output =
		wl_container_of(listener, output, display_destroy);
	wlr_output_destroy_global(output);
}

void wlr_output_init(struct wlr_output *output, struct wlr_backend *backend,
		const struct wlr_output_impl *impl, struct wl_display *display) {
	assert(impl->make_current && impl->swap_buffers && impl->transform);
	output->backend = backend;
	output->impl = impl;
	output->display = display;
	wl_list_init(&output->modes);
	output->transform = WL_OUTPUT_TRANSFORM_NORMAL;
	output->scale = 1;
	wl_list_init(&output->cursors);
	wl_list_init(&output->wl_resources);
	wl_signal_init(&output->events.frame);
	wl_signal_init(&output->events.swap_buffers);
	wl_signal_init(&output->events.enable);
	wl_signal_init(&output->events.mode);
	wl_signal_init(&output->events.scale);
	wl_signal_init(&output->events.transform);
	wl_signal_init(&output->events.destroy);

	output->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &output->display_destroy);
}

void wlr_output_destroy(struct wlr_output *output) {
	if (!output) {
		return;
	}

	wl_list_remove(&output->display_destroy.link);
	wlr_output_destroy_global(output);
	wlr_output_set_fullscreen_surface(output, NULL);

	wl_signal_emit(&output->events.destroy, output);

	struct wlr_output_mode *mode, *tmp_mode;
	wl_list_for_each_safe(mode, tmp_mode, &output->modes, link) {
		wl_list_remove(&mode->link);
		free(mode);
	}

	struct wlr_output_cursor *cursor, *tmp_cursor;
	wl_list_for_each_safe(cursor, tmp_cursor, &output->cursors, link) {
		wlr_output_cursor_destroy(cursor);
	}

	if (output->impl && output->impl->destroy) {
		output->impl->destroy(output);
	} else {
		free(output);
	}
}

void wlr_output_effective_resolution(struct wlr_output *output,
		int *width, int *height) {
	if (output->transform % 2 == 1) {
		*width = output->height;
		*height = output->width;
	} else {
		*width = output->width;
		*height = output->height;
	}
	*width /= output->scale;
	*height /= output->scale;
}

void wlr_output_make_current(struct wlr_output *output) {
	output->impl->make_current(output);
}

static void output_fullscreen_surface_render(struct wlr_output *output,
		struct wlr_surface *surface, const struct timespec *when) {
	int width, height;
	wlr_output_effective_resolution(output, &width, &height);

	int x = (width - surface->current->width) / 2;
	int y = (height - surface->current->height) / 2;

	int render_x = x * output->scale;
	int render_y = y * output->scale;
	int render_width = surface->current->width * output->scale;
	int render_height = surface->current->height * output->scale;

	glViewport(0, 0, output->width, output->height);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);

	if (!wlr_surface_has_buffer(surface)) {
		return;
	}

	float translate[16];
	wlr_matrix_translate(&translate, render_x, render_y, 0);

	float scale[16];
	wlr_matrix_scale(&scale, render_width, render_height, 1);

	float matrix[16];
	wlr_matrix_mul(&translate, &scale, &matrix);
	wlr_matrix_mul(&output->transform_matrix, &matrix, &matrix);

	wlr_render_with_matrix(surface->renderer, surface->texture, &matrix);

	wlr_surface_send_frame_done(surface, when);
}

/**
 * Returns the cursor box, scaled for its output.
 */
static void output_cursor_get_box(struct wlr_output_cursor *cursor,
		struct wlr_box *box) {
	box->x = cursor->x - cursor->hotspot_x;
	box->y = cursor->y - cursor->hotspot_y;
	box->width = cursor->width;
	box->height = cursor->height;

	if (cursor->surface != NULL) {
		box->x += cursor->surface->current->sx * cursor->output->scale;
		box->y += cursor->surface->current->sy * cursor->output->scale;
	}
}

static void output_cursor_render(struct wlr_output_cursor *cursor,
		const struct timespec *when) {
	struct wlr_texture *texture = cursor->texture;
	struct wlr_renderer *renderer = cursor->renderer;
	if (cursor->surface != NULL) {
		texture = cursor->surface->texture;
		renderer = cursor->surface->renderer;
	}

	if (texture == NULL || renderer == NULL) {
		return;
	}

	glViewport(0, 0, cursor->output->width, cursor->output->height);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	struct wlr_box cursor_box;
	output_cursor_get_box(cursor, &cursor_box);

	float translate[16];
	wlr_matrix_translate(&translate, cursor_box.x, cursor_box.y, 0);

	float scale[16];
	wlr_matrix_scale(&scale, cursor_box.width, cursor_box.height, 1);

	float matrix[16];
	wlr_matrix_mul(&translate, &scale, &matrix);
	wlr_matrix_mul(&cursor->output->transform_matrix, &matrix, &matrix);

	wlr_render_with_matrix(renderer, texture, &matrix);

	if (cursor->surface != NULL) {
		wlr_surface_send_frame_done(cursor->surface, when);
	}
}

void wlr_output_swap_buffers(struct wlr_output *output) {
	wl_signal_emit(&output->events.swap_buffers, &output);

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	if (output->fullscreen_surface != NULL) {
		output_fullscreen_surface_render(output, output->fullscreen_surface,
			&now);
	}

	struct wlr_output_cursor *cursor;
	wl_list_for_each(cursor, &output->cursors, link) {
		if (!cursor->enabled || !cursor->visible ||
				output->hardware_cursor == cursor) {
			continue;
		}
		output_cursor_render(cursor, &now);
	}

	output->impl->swap_buffers(output);
	output->needs_swap = false;
}

void wlr_output_set_gamma(struct wlr_output *output,
	uint32_t size, uint16_t *r, uint16_t *g, uint16_t *b) {
	if (output->impl->set_gamma) {
		output->impl->set_gamma(output, size, r, g, b);
	}
}

uint32_t wlr_output_get_gamma_size(struct wlr_output *output) {
	if (!output->impl->get_gamma_size) {
		return 0;
	}
	return output->impl->get_gamma_size(output);
}

static void output_fullscreen_surface_reset(struct wlr_output *output) {
	if (output->fullscreen_surface != NULL) {
		wl_list_remove(&output->fullscreen_surface_commit.link);
		wl_list_remove(&output->fullscreen_surface_destroy.link);
		output->fullscreen_surface = NULL;
		output->needs_swap = true;
	}
}

static void output_fullscreen_surface_handle_commit(
		struct wl_listener *listener, void *data) {
	struct wlr_output *output = wl_container_of(listener, output,
		fullscreen_surface_commit);
	output->needs_swap = true;
}

static void output_fullscreen_surface_handle_destroy(
		struct wl_listener *listener, void *data) {
	struct wlr_output *output = wl_container_of(listener, output,
		fullscreen_surface_destroy);
	output_fullscreen_surface_reset(output);
}

void wlr_output_set_fullscreen_surface(struct wlr_output *output,
		struct wlr_surface *surface) {
	// TODO: hardware fullscreen

	if (output->fullscreen_surface == surface) {
		return;
	}

	output_fullscreen_surface_reset(output);

	output->fullscreen_surface = surface;
	output->needs_swap = true;

	if (surface == NULL) {
		return;
	}

	output->fullscreen_surface_commit.notify =
		output_fullscreen_surface_handle_commit;
	wl_signal_add(&surface->events.commit, &output->fullscreen_surface_commit);
	output->fullscreen_surface_destroy.notify =
		output_fullscreen_surface_handle_destroy;
	wl_signal_add(&surface->events.destroy,
		&output->fullscreen_surface_destroy);
}


static void output_cursor_reset(struct wlr_output_cursor *cursor) {
	if (cursor->output->hardware_cursor != cursor) {
		cursor->output->needs_swap = true;
	}
	if (cursor->surface != NULL) {
		wl_list_remove(&cursor->surface_commit.link);
		wl_list_remove(&cursor->surface_destroy.link);
		cursor->surface = NULL;
	}
}

bool wlr_output_cursor_set_image(struct wlr_output_cursor *cursor,
		const uint8_t *pixels, int32_t stride, uint32_t width, uint32_t height,
		int32_t hotspot_x, int32_t hotspot_y) {
	output_cursor_reset(cursor);

	cursor->width = width;
	cursor->height = height;
	cursor->hotspot_x = hotspot_x;
	cursor->hotspot_y = hotspot_y;

	struct wlr_output_cursor *hwcur = cursor->output->hardware_cursor;
	if (cursor->output->impl->set_cursor && (hwcur == NULL || hwcur == cursor)) {
		if (cursor->output->impl->move_cursor && hwcur != cursor) {
			cursor->output->impl->move_cursor(cursor->output,
				(int)cursor->x, (int)cursor->y);
		}
		int ok = cursor->output->impl->set_cursor(cursor->output, pixels,
			stride, width, height, hotspot_x, hotspot_y, true);
		if (ok) {
			cursor->output->hardware_cursor = cursor;
			return true;
		}
	}

	wlr_log(L_DEBUG, "Falling back to software cursor");
	cursor->output->needs_swap = true;

	cursor->enabled = pixels != NULL;
	if (!cursor->enabled) {
		return true;
	}

	if (cursor->renderer == NULL) {
		cursor->renderer = wlr_gles2_renderer_create(cursor->output->backend);
		if (cursor->renderer == NULL) {
			return false;
		}
	}

	if (cursor->texture == NULL) {
		cursor->texture = wlr_render_texture_create(cursor->renderer);
		if (cursor->texture == NULL) {
			return false;
		}
	}

	return wlr_texture_upload_pixels(cursor->texture, WL_SHM_FORMAT_ARGB8888,
		stride, width, height, pixels);
}

static void output_cursor_update_visible(struct wlr_output_cursor *cursor) {
	struct wlr_box output_box;
	output_box.x = output_box.y = 0;
	wlr_output_effective_resolution(cursor->output, &output_box.width,
		&output_box.height);
	output_box.width *= cursor->output->scale;
	output_box.height *= cursor->output->scale;

	struct wlr_box cursor_box;
	output_cursor_get_box(cursor, &cursor_box);

	struct wlr_box intersection;
	bool visible =
		wlr_box_intersection(&output_box, &cursor_box, &intersection);

	if (cursor->surface != NULL) {
		if (cursor->visible && !visible) {
			wlr_surface_send_leave(cursor->surface, cursor->output);
		}
		if (!cursor->visible && visible) {
			wlr_surface_send_enter(cursor->surface, cursor->output);
		}
	}

	cursor->visible = visible;
}

static void output_cursor_commit(struct wlr_output_cursor *cursor) {
	// Some clients commit a cursor surface with a NULL buffer to hide it.
	cursor->enabled = wlr_surface_has_buffer(cursor->surface);
	cursor->width = cursor->surface->current->width * cursor->output->scale;
	cursor->height = cursor->surface->current->height * cursor->output->scale;

	if (cursor->output->hardware_cursor != cursor) {
		cursor->output->needs_swap = true;
	} else {
		// TODO: upload pixels

		struct timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		wlr_surface_send_frame_done(cursor->surface, &now);
	}
}

static void output_cursor_handle_commit(struct wl_listener *listener,
		void *data) {
	struct wlr_output_cursor *cursor = wl_container_of(listener, cursor,
		surface_commit);
	output_cursor_commit(cursor);
}

static void output_cursor_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct wlr_output_cursor *cursor = wl_container_of(listener, cursor,
		surface_destroy);
	output_cursor_reset(cursor);
}

void wlr_output_cursor_set_surface(struct wlr_output_cursor *cursor,
		struct wlr_surface *surface, int32_t hotspot_x, int32_t hotspot_y) {
	if (surface && strcmp(surface->role, "wl_pointer-cursor") != 0) {
		return;
	}

	cursor->hotspot_x = hotspot_x * cursor->output->scale;
	cursor->hotspot_y = hotspot_y * cursor->output->scale;

	if (surface && surface == cursor->surface) {
		if (cursor->output->hardware_cursor == cursor &&
				cursor->output->impl->set_cursor) {
			// If the surface hasn't changed and it's an hardware cursor, only
			// update the hotspot
			cursor->output->impl->set_cursor(cursor->output, NULL, 0, 0, 0,
				hotspot_x, hotspot_y, false);
		}
		return;
	}

	output_cursor_reset(cursor);

	// Disable hardware cursor for surfaces
	// TODO: support hardware cursors
	if (cursor->output->hardware_cursor == cursor &&
			cursor->output->impl->set_cursor) {
		cursor->output->impl->set_cursor(cursor->output, NULL, 0, 0, 0, 0, 0,
			true);
		cursor->output->hardware_cursor = NULL;
	}

	cursor->surface = surface;

	if (surface != NULL) {
		wl_signal_add(&surface->events.commit, &cursor->surface_commit);
		wl_signal_add(&surface->events.destroy, &cursor->surface_destroy);
		output_cursor_commit(cursor);

		cursor->visible = false;
		output_cursor_update_visible(cursor);
	} else {
		cursor->enabled = false;
		cursor->width = 0;
		cursor->height = 0;

		// TODO: if hardware cursor, disable cursor
	}
}

bool wlr_output_cursor_move(struct wlr_output_cursor *cursor,
		double x, double y) {
	x *= cursor->output->scale;
	y *= cursor->output->scale;
	cursor->x = x;
	cursor->y = y;
	output_cursor_update_visible(cursor);

	if (cursor->output->hardware_cursor != cursor) {
		cursor->output->needs_swap = true;
		return true;
	}

	if (!cursor->output->impl->move_cursor) {
		return false;
	}
	return cursor->output->impl->move_cursor(cursor->output, (int)x, (int)y);
}

struct wlr_output_cursor *wlr_output_cursor_create(struct wlr_output *output) {
	struct wlr_output_cursor *cursor =
		calloc(1, sizeof(struct wlr_output_cursor));
	if (cursor == NULL) {
		return NULL;
	}
	cursor->output = output;
	wl_list_init(&cursor->surface_commit.link);
	cursor->surface_commit.notify = output_cursor_handle_commit;
	wl_list_init(&cursor->surface_destroy.link);
	cursor->surface_destroy.notify = output_cursor_handle_destroy;
	wl_list_insert(&output->cursors, &cursor->link);
	return cursor;
}

void wlr_output_cursor_destroy(struct wlr_output_cursor *cursor) {
	if (cursor == NULL) {
		return;
	}
	output_cursor_reset(cursor);
	if (cursor->output->hardware_cursor == cursor) {
		// If this cursor was the hardware cursor, disable it
		if (cursor->output->impl->set_cursor) {
			cursor->output->impl->set_cursor(cursor->output, NULL, 0, 0, 0, 0,
				0, true);
		}
		cursor->output->hardware_cursor = NULL;
	}
	if (cursor->texture != NULL) {
		wlr_texture_destroy(cursor->texture);
	}
	if (cursor->renderer != NULL) {
		wlr_renderer_destroy(cursor->renderer);
	}
	wl_list_remove(&cursor->link);
	free(cursor);
}


enum wl_output_transform wlr_output_transform_invert(
		enum wl_output_transform tr) {
	if ((tr & WL_OUTPUT_TRANSFORM_90) && !(tr & WL_OUTPUT_TRANSFORM_FLIPPED)) {
		tr ^= WL_OUTPUT_TRANSFORM_180;
	}
	return tr;
}

enum wl_output_transform wlr_output_transform_compose(
		enum wl_output_transform tr_a, enum wl_output_transform tr_b) {
	uint32_t flipped = (tr_a ^ tr_b) & WL_OUTPUT_TRANSFORM_FLIPPED;
	uint32_t rotated =
		(tr_a + tr_b) & (WL_OUTPUT_TRANSFORM_90 | WL_OUTPUT_TRANSFORM_180);
	if ((tr_a & WL_OUTPUT_TRANSFORM_FLIPPED) && (tr_b & WL_OUTPUT_TRANSFORM_FLIPPED)) {
		rotated = wlr_output_transform_invert(rotated);
	}
	return flipped | rotated;
}
