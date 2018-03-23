#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/util/log.h>
#include "backend/wayland.h"
#include "util/signal.h"
#include "xdg-shell-unstable-v6-client-protocol.h"

int os_create_anonymous_file(off_t size);

static struct wl_callback_listener frame_listener;

static void surface_frame_callback(void *data, struct wl_callback *cb,
		uint32_t time) {
	struct wlr_wl_backend_output *output = data;
	assert(output);
	wl_callback_destroy(cb);
	output->frame_callback = NULL;

	wlr_output_send_frame(&output->wlr_output);
}

static struct wl_callback_listener frame_listener = {
	.done = surface_frame_callback
};

static bool wlr_wl_output_set_custom_mode(struct wlr_output *_output,
		int32_t width, int32_t height, int32_t refresh) {
	struct wlr_wl_backend_output *output = (struct wlr_wl_backend_output *)_output;
	wl_egl_window_resize(output->egl_window, width, height, 0, 0);
	wlr_output_update_custom_mode(&output->wlr_output, width, height, 0);
	return true;
}

static bool wlr_wl_output_make_current(struct wlr_output *wlr_output,
		int *buffer_age) {
	struct wlr_wl_backend_output *output =
		(struct wlr_wl_backend_output *)wlr_output;
	return wlr_egl_make_current(&output->backend->egl, output->egl_surface,
		buffer_age);
}

static bool wlr_wl_output_swap_buffers(struct wlr_output *wlr_output,
		pixman_region32_t *damage) {
	struct wlr_wl_backend_output *output =
		(struct wlr_wl_backend_output *)wlr_output;

	if (output->frame_callback != NULL) {
		wlr_log(L_ERROR, "Skipping buffer swap");
		return false;
	}

	output->frame_callback = wl_surface_frame(output->surface);
	wl_callback_add_listener(output->frame_callback, &frame_listener, output);

	return wlr_egl_swap_buffers(&output->backend->egl, output->egl_surface,
		damage);
}

static void wlr_wl_output_transform(struct wlr_output *_output,
		enum wl_output_transform transform) {
	struct wlr_wl_backend_output *output = (struct wlr_wl_backend_output *)_output;
	output->wlr_output.transform = transform;
}

static bool wlr_wl_output_set_cursor(struct wlr_output *_output,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height,
		int32_t hotspot_x, int32_t hotspot_y, bool update_pixels) {
	struct wlr_wl_backend_output *output =
		(struct wlr_wl_backend_output *)_output;
	struct wlr_wl_backend *backend = output->backend;

	// TODO: use output->wlr_output.transform to transform pixels and hotpot
	output->cursor.hotspot_x = hotspot_x;
	output->cursor.hotspot_y = hotspot_y;

	if (!update_pixels) {
		// Update hotspot without changing cursor image
		wlr_wl_output_update_cursor(output);
		return true;
	}
	if (!buf) {
		// Hide cursor
		if (output->cursor.surface) {
			wl_surface_destroy(output->cursor.surface);
			munmap(output->cursor.data, output->cursor.buf_size);
			output->cursor.surface = NULL;
			output->cursor.buf_size = 0;
		}
		wlr_wl_output_update_cursor(output);
		return true;
	}

	stride *= 4; // stride is given in pixels, we need it in bytes

	if (!backend->shm || !backend->pointer) {
		wlr_log(L_INFO, "cannot set cursor, no shm or pointer");
		return false;
	}

	if (!output->cursor.surface) {
		output->cursor.surface =
			wl_compositor_create_surface(output->backend->compositor);
	}

	uint32_t size = stride * height;
	if (output->cursor.buf_size != size) {
		if (output->cursor.buffer) {
			wl_buffer_destroy(output->cursor.buffer);
		}

		if (size > output->cursor.buf_size) {
			if (output->cursor.pool) {
				wl_shm_pool_destroy(output->cursor.pool);
				output->cursor.pool = NULL;
				munmap(output->cursor.data, output->cursor.buf_size);
			}
		}

		if (!output->cursor.pool) {
			int fd = os_create_anonymous_file(size);
			if (fd < 0) {
				wlr_log_errno(L_INFO,
					"creating anonymous file for cursor buffer failed");
				return false;
			}

			output->cursor.data = mmap(NULL, size, PROT_READ | PROT_WRITE,
				MAP_SHARED, fd, 0);
			if (output->cursor.data == MAP_FAILED) {
				close(fd);
				wlr_log_errno(L_INFO, "mmap failed");
				return false;
			}

			output->cursor.pool = wl_shm_create_pool(backend->shm, fd, size);
			close(fd);
		}

		output->cursor.buffer = wl_shm_pool_create_buffer(output->cursor.pool,
			0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
		output->cursor.buf_size = size;
	}

	memcpy(output->cursor.data, buf, size);
	wl_surface_attach(output->cursor.surface, output->cursor.buffer, 0, 0);
	wl_surface_damage(output->cursor.surface, 0, 0, width, height);
	wl_surface_commit(output->cursor.surface);

	wlr_wl_output_update_cursor(output);
	return true;
}

static void wlr_wl_output_destroy(struct wlr_output *wlr_output) {
	struct wlr_wl_backend_output *output =
		(struct wlr_wl_backend_output *)wlr_output;
	if (output == NULL) {
		return;
	}

	wl_list_remove(&output->link);

	if (output->cursor.buf_size != 0) {
		assert(output->cursor.data);
		assert(output->cursor.buffer);
		assert(output->cursor.pool);

		wl_buffer_destroy(output->cursor.buffer);
		munmap(output->cursor.data, output->cursor.buf_size);
		wl_shm_pool_destroy(output->cursor.pool);
	}

	if (output->cursor.surface) {
		wl_surface_destroy(output->cursor.surface);
	}

	if (output->frame_callback) {
		wl_callback_destroy(output->frame_callback);
	}

	eglDestroySurface(output->backend->egl.display, output->surface);
	wl_egl_window_destroy(output->egl_window);
	zxdg_toplevel_v6_destroy(output->xdg_toplevel);
	zxdg_surface_v6_destroy(output->xdg_surface);
	wl_surface_destroy(output->surface);
	free(output);
}

void wlr_wl_output_update_cursor(struct wlr_wl_backend_output *output) {
	if (output->backend->pointer && output->enter_serial) {
		wl_pointer_set_cursor(output->backend->pointer, output->enter_serial,
			output->cursor.surface, output->cursor.hotspot_x,
			output->cursor.hotspot_y);
	}
}

bool wlr_wl_output_move_cursor(struct wlr_output *_output, int x, int y) {
	// TODO: only return true if x == current x and y == current y
	return true;
}

static struct wlr_output_impl output_impl = {
	.set_custom_mode = wlr_wl_output_set_custom_mode,
	.transform = wlr_wl_output_transform,
	.destroy = wlr_wl_output_destroy,
	.make_current = wlr_wl_output_make_current,
	.swap_buffers = wlr_wl_output_swap_buffers,
	.set_cursor = wlr_wl_output_set_cursor,
	.move_cursor = wlr_wl_output_move_cursor,
};

bool wlr_output_is_wl(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}

static void xdg_surface_handle_configure(void *data, struct zxdg_surface_v6 *xdg_surface,
		uint32_t serial) {
	struct wlr_wl_backend_output *output = data;
	assert(output && output->xdg_surface == xdg_surface);

	zxdg_surface_v6_ack_configure(xdg_surface, serial);

	// nothing else?
}

static struct zxdg_surface_v6_listener xdg_surface_listener = {
	.configure = xdg_surface_handle_configure,
};

static void xdg_toplevel_handle_configure(void *data, struct zxdg_toplevel_v6 *xdg_toplevel,
		int32_t width, int32_t height, struct wl_array *states) {
	struct wlr_wl_backend_output *output = data;
	assert(output && output->xdg_toplevel == xdg_toplevel);

	if (width == 0 && height == 0) {
		return;
	}
	// loop over states for maximized etc?
	wl_egl_window_resize(output->egl_window, width, height, 0, 0);
	wlr_output_update_custom_mode(&output->wlr_output, width, height, 0);
}

static void xdg_toplevel_handle_close(void *data, struct zxdg_toplevel_v6 *xdg_toplevel) {
	struct wlr_wl_backend_output *output = data;
	assert(output && output->xdg_toplevel == xdg_toplevel);

	wlr_output_destroy((struct wlr_output *)output);
}

static struct zxdg_toplevel_v6_listener xdg_toplevel_listener = {
	.configure = xdg_toplevel_handle_configure,
	.close = xdg_toplevel_handle_close,
};

struct wlr_output *wlr_wl_output_create(struct wlr_backend *_backend) {
	assert(wlr_backend_is_wl(_backend));
	struct wlr_wl_backend *backend = (struct wlr_wl_backend *)_backend;
	if (!backend->started) {
		++backend->requested_outputs;
		return NULL;
	}

	struct wlr_wl_backend_output *output;
	if (!(output = calloc(sizeof(struct wlr_wl_backend_output), 1))) {
		wlr_log(L_ERROR, "Failed to allocate wlr_wl_backend_output");
		return NULL;
	}
	wlr_output_init(&output->wlr_output, &backend->backend, &output_impl,
		backend->local_display);
	struct wlr_output *wlr_output = &output->wlr_output;

	wlr_output_update_custom_mode(wlr_output, 1280, 720, 0);
	strncpy(wlr_output->make, "wayland", sizeof(wlr_output->make));
	strncpy(wlr_output->model, "wayland", sizeof(wlr_output->model));
	snprintf(wlr_output->name, sizeof(wlr_output->name), "WL-%d",
		wl_list_length(&backend->outputs) + 1);

	output->backend = backend;

	output->surface = wl_compositor_create_surface(backend->compositor);
	if (!output->surface) {
		wlr_log_errno(L_ERROR, "Could not create output surface");
		goto error;
	}
	output->xdg_surface =
		zxdg_shell_v6_get_xdg_surface(backend->shell, output->surface);
	if (!output->xdg_surface) {
		wlr_log_errno(L_ERROR, "Could not get xdg surface");
		goto error;
	}
	output->xdg_toplevel =
		zxdg_surface_v6_get_toplevel(output->xdg_surface);
	if (!output->xdg_toplevel) {
		wlr_log_errno(L_ERROR, "Could not get xdg toplevel");
		goto error;
	}

	zxdg_toplevel_v6_set_app_id(output->xdg_toplevel, "wlroots");
	zxdg_toplevel_v6_set_title(output->xdg_toplevel, "wlroots");
	zxdg_surface_v6_add_listener(output->xdg_surface,
			&xdg_surface_listener, output);
	zxdg_toplevel_v6_add_listener(output->xdg_toplevel,
			&xdg_toplevel_listener, output);
	wl_surface_commit(output->surface);

	output->egl_window = wl_egl_window_create(output->surface,
			wlr_output->width, wlr_output->height);
	output->egl_surface = wlr_egl_create_surface(&backend->egl,
		output->egl_window);

	wl_display_roundtrip(output->backend->remote_display);

	// start rendering loop per callbacks by rendering first frame
	if (!wlr_egl_make_current(&output->backend->egl, output->egl_surface,
			NULL)) {
		goto error;
	}

	wlr_renderer_begin(backend->renderer, wlr_output->width, wlr_output->height);
	wlr_renderer_clear(backend->renderer, (float[]){ 1.0, 1.0, 1.0, 1.0 });
	wlr_renderer_end(backend->renderer);

	output->frame_callback = wl_surface_frame(output->surface);
	wl_callback_add_listener(output->frame_callback, &frame_listener, output);

	if (!wlr_egl_swap_buffers(&output->backend->egl, output->egl_surface,
			NULL)) {
		goto error;
	}

	wl_list_insert(&backend->outputs, &output->link);
	wlr_output_update_enabled(wlr_output, true);
	wlr_signal_emit_safe(&backend->backend.events.new_output, wlr_output);
	return wlr_output;

error:
	wlr_output_destroy(&output->wlr_output);
	return NULL;
}
