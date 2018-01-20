#include <stdlib.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include "backend/headless.h"

static EGLSurface egl_create_surface(struct wlr_egl *egl, unsigned int width,
		unsigned int height) {
	EGLint attribs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};

	EGLSurface surf = eglCreatePbufferSurface(egl->display, egl->config, attribs);
	if (surf == EGL_NO_SURFACE) {
		wlr_log(L_ERROR, "Failed to create EGL surface: %s", egl_error());
		return EGL_NO_SURFACE;
	}
	return surf;
}

static bool output_set_custom_mode(struct wlr_output *wlr_output, int32_t width,
		int32_t height, int32_t refresh) {
	struct wlr_headless_output *output =
		(struct wlr_headless_output *)wlr_output;
	struct wlr_headless_backend *backend = output->backend;

	if (output->egl_surface) {
		eglDestroySurface(backend->egl.display, output->egl_surface);
	}

	output->egl_surface = egl_create_surface(&backend->egl, width, height);
	if (output->egl_surface == EGL_NO_SURFACE) {
		wlr_log(L_ERROR, "Failed to recreate EGL surface");
		wlr_output_destroy(wlr_output);
		return false;
	}

	output->frame_delay = 1000000 / refresh;

	wlr_output_update_custom_mode(&output->wlr_output, width, height, refresh);
	return true;
}

static void output_transform(struct wlr_output *wlr_output,
		enum wl_output_transform transform) {
	struct wlr_headless_output *output =
		(struct wlr_headless_output *)wlr_output;
	output->wlr_output.transform = transform;
}

static bool output_make_current(struct wlr_output *wlr_output, int *buffer_age) {
	struct wlr_headless_output *output =
		(struct wlr_headless_output *)wlr_output;
	return wlr_egl_make_current(&output->backend->egl, output->egl_surface,
		buffer_age);
}

static bool output_swap_buffers(struct wlr_output *wlr_output) {
	return true; // No-op
}

static void output_destroy(struct wlr_output *wlr_output) {
	struct wlr_headless_output *output =
		(struct wlr_headless_output *)wlr_output;
	wl_signal_emit(&output->backend->backend.events.output_remove,
		&output->wlr_output);

	wl_list_remove(&output->link);

	eglDestroySurface(output->backend->egl.display, output->egl_surface);
	free(output);
}

static const struct wlr_output_impl output_impl = {
	.set_custom_mode = output_set_custom_mode,
	.transform = output_transform,
	.destroy = output_destroy,
	.make_current = output_make_current,
	.swap_buffers = output_swap_buffers,
};

bool wlr_output_is_headless(struct wlr_output *wlr_output) {
	return wlr_output->impl == &output_impl;
}

static int signal_frame(void *data) {
	struct wlr_headless_output *output = data;
	wl_signal_emit(&output->wlr_output.events.frame, &output->wlr_output);
	wl_event_source_timer_update(output->frame_timer, output->frame_delay);
	return 0;
}

struct wlr_output *wlr_headless_add_output(struct wlr_backend *wlr_backend,
		unsigned int width, unsigned int height) {
	struct wlr_headless_backend *backend =
		(struct wlr_headless_backend *)wlr_backend;

	struct wlr_headless_output *output =
		calloc(1, sizeof(struct wlr_headless_output));
	if (output == NULL) {
		wlr_log(L_ERROR, "Failed to allocate wlr_headless_output");
		return NULL;
	}
	output->backend = backend;
	wlr_output_init(&output->wlr_output, &backend->backend, &output_impl,
		backend->display);
	struct wlr_output *wlr_output = &output->wlr_output;

	output->egl_surface = egl_create_surface(&backend->egl, width, height);
	if (output->egl_surface == EGL_NO_SURFACE) {
		wlr_log(L_ERROR, "Failed to create EGL surface");
		goto error;
	}

	output_set_custom_mode(wlr_output, width, height, 60*1000);
	strncpy(wlr_output->make, "headless", sizeof(wlr_output->make));
	strncpy(wlr_output->model, "headless", sizeof(wlr_output->model));
	snprintf(wlr_output->name, sizeof(wlr_output->name), "HEADLESS-%d",
		wl_list_length(&backend->outputs) + 1);

	if (!eglMakeCurrent(output->backend->egl.display,
			output->egl_surface, output->egl_surface,
			output->backend->egl.context)) {
		wlr_log(L_ERROR, "eglMakeCurrent failed: %s", egl_error());
		goto error;
	}

	glViewport(0, 0, wlr_output->width, wlr_output->height);
	glClearColor(1.0, 1.0, 1.0, 1.0);
	glClear(GL_COLOR_BUFFER_BIT);

	struct wl_event_loop *ev = wl_display_get_event_loop(backend->display);
	output->frame_timer = wl_event_loop_add_timer(ev, signal_frame, output);

	wl_list_insert(&backend->outputs, &output->link);

	if (backend->started) {
		wl_event_source_timer_update(output->frame_timer, output->frame_delay);
		wlr_output_update_enabled(wlr_output, true);
		wl_signal_emit(&backend->backend.events.output_add, wlr_output);
	}

	return wlr_output;

error:
	wlr_output_destroy(&output->wlr_output);
	return NULL;
}
