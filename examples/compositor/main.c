#define _POSIX_C_SOURCE 199309L
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <inttypes.h>
#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/render.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles2.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <xkbcommon/xkbcommon.h>
#include <wlr/util/log.h>
#include "shared.h"
#include "compositor.h"

struct sample_state {
	struct wlr_renderer *renderer;
	struct wl_compositor_state compositor;
	struct wlr_wl_shell *wl_shell;
	struct wlr_xdg_shell_v6 *xdg_shell;
};

/*
 * Convert timespec to milliseconds
 */
static inline int64_t timespec_to_msec(const struct timespec *a) {
	return (int64_t)a->tv_sec * 1000 + a->tv_nsec / 1000000;
}

void output_frame_handle_surface(struct sample_state *sample,
		struct wlr_output *wlr_output, struct timespec *ts,
		struct wl_resource *_res) {
	struct wlr_surface *surface = wl_resource_get_user_data(_res);
	float matrix[16];
	float transform[16];
	wlr_surface_flush_damage(surface);
	if (surface->texture->valid) {
		wlr_matrix_translate(&transform, 200, 200, 0);
		wlr_surface_get_matrix(surface, &matrix,
			&wlr_output->transform_matrix, &transform);
		wlr_render_with_matrix(sample->renderer, surface->texture, &matrix);

		struct wlr_frame_callback *cb, *cnext;
		wl_list_for_each_safe(cb, cnext, &surface->frame_callback_list, link) {
			wl_callback_send_done(cb->resource, timespec_to_msec(ts));
			wl_resource_destroy(cb->resource);
		}
	}
}
void handle_output_frame(struct output_state *output, struct timespec *ts) {
	struct compositor_state *state = output->compositor;
	struct sample_state *sample = state->data;
	struct wlr_output *wlr_output = output->output;

	wlr_output_make_current(wlr_output);
	wlr_renderer_begin(sample->renderer, wlr_output);

	struct wlr_wl_shell_surface *wl_shell_surface;
	wl_list_for_each(wl_shell_surface, &sample->wl_shell->surfaces, link) {
		output_frame_handle_surface(sample, wlr_output, ts, wl_shell_surface->surface);
	}
	struct wlr_xdg_surface_v6 *xdg_surface;
	wl_list_for_each(xdg_surface, &sample->xdg_shell->surfaces, link) {
		output_frame_handle_surface(sample, wlr_output, ts, xdg_surface->surface);
	}

	wlr_renderer_end(sample->renderer);
	wlr_output_swap_buffers(wlr_output);
}

int main() {
	struct sample_state state = { 0 };
	struct compositor_state compositor = { 0,
		.data = &state,
		.output_frame_cb = handle_output_frame,
	};
	compositor_init(&compositor);

	state.renderer = wlr_gles2_renderer_init(compositor.backend);
	if (!state.renderer) {
		wlr_log(L_ERROR, "Could not start compositor, OOM");
		exit(EXIT_FAILURE);
	}
	wl_display_init_shm(compositor.display);
	wl_compositor_init(compositor.display, &state.compositor, state.renderer);
	state.wl_shell = wlr_wl_shell_create(compositor.display);
	state.xdg_shell = wlr_xdg_shell_v6_create(compositor.display);

	compositor_run(&compositor);

	wlr_wl_shell_destroy(state.wl_shell);
	wlr_xdg_shell_v6_destroy(state.xdg_shell);
}
