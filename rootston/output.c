#define _POSIX_C_SOURCE 200809L
#include <time.h>
#include <stdlib.h>
#include <stdbool.h>
#include <GLES2/gl2.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/render/matrix.h>
#include <wlr/util/log.h>
#include "rootston/server.h"
#include "rootston/desktop.h"
#include "rootston/config.h"

/**
 * Rotate a child's position relative to a parent. The parent size is (pw, ph),
 * the child position is (*sx, *sy) and its size is (sw, sh).
 */
static void rotate_child_position(double *sx, double *sy, double sw, double sh,
		double pw, double ph, float rotation) {
	if (rotation != 0.0) {
		// Coordinates relative to the center of the subsurface
		double ox = *sx - pw/2 + sw/2,
			oy = *sy - ph/2 + sh/2;
		// Rotated coordinates
		double rx = cos(-rotation)*ox - sin(-rotation)*oy,
			ry = cos(-rotation)*oy + sin(-rotation)*ox;
		*sx = rx + pw/2 - sw/2;
		*sy = ry + ph/2 - sh/2;
	}
}

static void render_surface(struct wlr_surface *surface,
		struct roots_desktop *desktop, struct wlr_output *wlr_output,
		struct timespec *when, double lx, double ly, float rotation) {
	if (!wlr_surface_has_buffer(surface)) {
		return;
	}

	int width = surface->current->width;
	int height = surface->current->height;
	int render_width = width * wlr_output->scale;
	int render_height = height * wlr_output->scale;
	double ox = lx, oy = ly;
	wlr_output_layout_output_coords(desktop->layout, wlr_output, &ox, &oy);
	ox *= wlr_output->scale;
	oy *= wlr_output->scale;

	struct wlr_box render_box = {
		.x = lx, .y = ly,
		.width = render_width, .height = render_height,
	};
	if (wlr_output_layout_intersects(desktop->layout, wlr_output, &render_box)) {
		float matrix[16];
		wlr_output_get_box_matrix(wlr_output, ox, oy, render_width,
			render_height, surface->current->transform, rotation, &matrix);
		wlr_render_with_matrix(desktop->server->renderer, surface->texture,
			&matrix);

		wlr_surface_send_frame_done(surface, when);
	}

	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &surface->subsurface_list, parent_link) {
		struct wlr_surface_state *state = subsurface->surface->current;
		double sx = state->subsurface_position.x;
		double sy = state->subsurface_position.y;
		double sw = state->buffer_width / state->scale;
		double sh = state->buffer_height / state->scale;
		rotate_child_position(&sx, &sy, sw, sh, width, height, rotation);

		render_surface(subsurface->surface, desktop, wlr_output, when,
			lx + sx,
			ly + sy,
			rotation);
	}
}

static void render_xdg_v6_popups(struct wlr_xdg_surface_v6 *surface,
		struct roots_desktop *desktop, struct wlr_output *wlr_output,
		struct timespec *when, double base_x, double base_y, float rotation) {
	double width = surface->surface->current->width;
	double height = surface->surface->current->height;

	struct wlr_xdg_surface_v6 *popup;
	wl_list_for_each(popup, &surface->popups, popup_link) {
		if (!popup->configured) {
			continue;
		}

		double popup_width = popup->surface->current->width;
		double popup_height = popup->surface->current->height;

		double popup_sx, popup_sy;
		wlr_xdg_surface_v6_popup_get_position(popup, &popup_sx, &popup_sy);
		rotate_child_position(&popup_sx, &popup_sy, popup_width, popup_height,
			width, height, rotation);

		render_surface(popup->surface, desktop, wlr_output, when,
			base_x + popup_sx, base_y + popup_sy, rotation);
		render_xdg_v6_popups(popup, desktop, wlr_output, when,
			base_x + popup_sx, base_y + popup_sy, rotation);
	}
}

static void render_wl_shell_surface(struct wlr_wl_shell_surface *surface,
		struct roots_desktop *desktop, struct wlr_output *wlr_output,
		struct timespec *when, double lx, double ly, float rotation,
		bool is_child) {
	if (is_child || surface->state != WLR_WL_SHELL_SURFACE_STATE_POPUP) {
		render_surface(surface->surface, desktop, wlr_output, when,
			lx, ly, rotation);

		double width = surface->surface->current->width;
		double height = surface->surface->current->height;

		struct wlr_wl_shell_surface *popup;
		wl_list_for_each(popup, &surface->popups, popup_link) {
			double popup_width = popup->surface->current->width;
			double popup_height = popup->surface->current->height;

			double popup_x = popup->transient_state->x;
			double popup_y = popup->transient_state->y;
			rotate_child_position(&popup_x, &popup_y, popup_width, popup_height,
				width, height, rotation);

			render_wl_shell_surface(popup, desktop, wlr_output, when,
				lx + popup_x, ly + popup_y, rotation, true);
		}
	}
}

static void render_xwayland_children(struct wlr_xwayland_surface *surface,
		struct roots_desktop *desktop, struct wlr_output *wlr_output,
		struct timespec *when) {
	struct wlr_xwayland_surface *child;
	wl_list_for_each(child, &surface->children, parent_link) {
		if (child->surface != NULL && child->added) {
			render_surface(child->surface, desktop, wlr_output, when,
				child->x, child->y, 0);
		}
		render_xwayland_children(child, desktop, wlr_output, when);
	}
}

static void render_view(struct roots_view *view, struct roots_desktop *desktop,
		struct wlr_output *wlr_output, struct timespec *when) {
	switch (view->type) {
	case ROOTS_XDG_SHELL_V6_VIEW:
		render_surface(view->wlr_surface, desktop, wlr_output, when,
			view->x, view->y, view->rotation);
		render_xdg_v6_popups(view->xdg_surface_v6, desktop, wlr_output,
			when, view->x, view->y, view->rotation);
		break;
	case ROOTS_WL_SHELL_VIEW:
		render_wl_shell_surface(view->wl_shell_surface, desktop, wlr_output,
			when, view->x, view->y, view->rotation, false);
		break;
	case ROOTS_XWAYLAND_VIEW:
		render_surface(view->wlr_surface, desktop, wlr_output, when,
			view->x, view->y, view->rotation);
		break;
	}
}

static bool has_standalone_surface(struct roots_view *view) {
	if (!wl_list_empty(&view->wlr_surface->subsurface_list)) {
		return false;
	}

	switch (view->type) {
	case ROOTS_XDG_SHELL_V6_VIEW:
		return wl_list_empty(&view->xdg_surface_v6->popups);
	case ROOTS_WL_SHELL_VIEW:
		return wl_list_empty(&view->wl_shell_surface->popups);
	case ROOTS_XWAYLAND_VIEW:
		return wl_list_empty(&view->xwayland_surface->children);
	}
	return true;
}

static void output_frame_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct roots_output *output = wl_container_of(listener, output, frame);
	struct roots_desktop *desktop = output->desktop;
	struct roots_server *server = desktop->server;

	if (!wlr_output->enabled) {
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	wlr_output_make_current(wlr_output);
	wlr_renderer_begin(server->renderer, wlr_output);

	if (output->fullscreen_view != NULL) {
		struct roots_view *view = output->fullscreen_view;

		// Make sure the view is centered on screen
		const struct wlr_box *output_box =
			wlr_output_layout_get_box(desktop->layout, wlr_output);
		struct wlr_box view_box;
		view_get_box(view, &view_box);
		double view_x = (double)(output_box->width - view_box.width) / 2 +
			output_box->x;
		double view_y = (double)(output_box->height - view_box.height) / 2 +
			output_box->y;
		view_move(view, view_x, view_y);

		if (has_standalone_surface(view)) {
			wlr_output_set_fullscreen_surface(wlr_output, view->wlr_surface);
		} else {
			wlr_output_set_fullscreen_surface(wlr_output, NULL);

			glClearColor(0, 0, 0, 0);
			glClear(GL_COLOR_BUFFER_BIT);

			render_view(view, desktop, wlr_output, &now);

			// During normal rendering the xwayland window tree isn't traversed
			// because all windows are rendered. Here we only want to render
			// the fullscreen window's children so we have to traverse the tree.
			if (view->type == ROOTS_XWAYLAND_VIEW) {
				render_xwayland_children(view->xwayland_surface, desktop,
					wlr_output, &now);
			}
		}
		wlr_renderer_end(server->renderer);
		wlr_output_swap_buffers(wlr_output);
		output->last_frame = desktop->last_frame = now;
		return;
	} else {
		wlr_output_set_fullscreen_surface(wlr_output, NULL);
	}

	struct roots_view *view;
	wl_list_for_each_reverse(view, &desktop->views, link) {
		render_view(view, desktop, wlr_output, &now);
	}

	struct wlr_drag_icon *drag_icon = NULL;
	struct roots_seat *seat = NULL;
	wl_list_for_each(seat, &server->input->seats, link) {
		wl_list_for_each(drag_icon, &seat->seat->drag_icons, link) {
			if (!drag_icon->mapped) {
				continue;
			}
			struct wlr_surface *icon = drag_icon->surface;
			struct wlr_cursor *cursor = seat->cursor->cursor;
			double icon_x = 0, icon_y = 0;
			if (drag_icon->is_pointer) {
				icon_x = cursor->x + drag_icon->sx;
				icon_y = cursor->y + drag_icon->sy;
				render_surface(icon, desktop, wlr_output, &now, icon_x, icon_y, 0);
			} else {
				struct wlr_touch_point *point =
					wlr_seat_touch_get_point(seat->seat, drag_icon->touch_id);
				if (point) {
					icon_x = seat->touch_x + drag_icon->sx;
					icon_y = seat->touch_y + drag_icon->sy;
					render_surface(icon, desktop, wlr_output, &now, icon_x, icon_y, 0);
				}
			}
		}
	}

	wlr_renderer_end(server->renderer);
	wlr_output_swap_buffers(wlr_output);

	output->last_frame = desktop->last_frame = now;
}

static void set_mode(struct wlr_output *output,
		struct roots_output_config *oc) {
	int mhz = (int)(oc->mode.refresh_rate * 1000);

	if (wl_list_empty(&output->modes)) {
		// Output has no mode, try setting a custom one
		wlr_output_set_custom_mode(output, oc->mode.width, oc->mode.height, mhz);
		return;
	}

	struct wlr_output_mode *mode, *best = NULL;
	wl_list_for_each(mode, &output->modes, link) {
		if (mode->width == oc->mode.width && mode->height == oc->mode.height) {
			if (mode->refresh == mhz) {
				best = mode;
				break;
			}
			best = mode;
		}
	}
	if (!best) {
		wlr_log(L_ERROR, "Configured mode for %s not available", output->name);
	} else {
		wlr_log(L_DEBUG, "Assigning configured mode to %s", output->name);
		wlr_output_set_mode(output, best);
	}
}

void output_add_notify(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop = wl_container_of(listener, desktop,
		output_add);
	struct wlr_output *wlr_output = data;
	struct roots_input *input = desktop->server->input;
	struct roots_config *config = desktop->config;

	wlr_log(L_DEBUG, "Output '%s' added", wlr_output->name);
	wlr_log(L_DEBUG, "'%s %s %s' %"PRId32"mm x %"PRId32"mm", wlr_output->make,
		wlr_output->model, wlr_output->serial, wlr_output->phys_width,
		wlr_output->phys_height);

	if (wl_list_length(&wlr_output->modes) > 0) {
		struct wlr_output_mode *mode =
			wl_container_of((&wlr_output->modes)->prev, mode, link);
		wlr_output_set_mode(wlr_output, mode);
	}

	struct roots_output *output = calloc(1, sizeof(struct roots_output));
	clock_gettime(CLOCK_MONOTONIC, &output->last_frame);
	output->desktop = desktop;
	output->wlr_output = wlr_output;
	output->frame.notify = output_frame_notify;
	wl_signal_add(&wlr_output->events.frame, &output->frame);
	wl_list_insert(&desktop->outputs, &output->link);

	struct roots_output_config *output_config =
		roots_config_get_output(config, wlr_output);
	if (output_config) {
		if (output_config->enable) {
			if (output_config->mode.width) {
				set_mode(wlr_output, output_config);
			}
			wlr_output_set_scale(wlr_output, output_config->scale);
			wlr_output_set_transform(wlr_output, output_config->transform);
			wlr_output_layout_add(desktop->layout, wlr_output, output_config->x,
				output_config->y);
		} else {
			wlr_output_enable(wlr_output, false);
		}
	} else {
		wlr_output_layout_add_auto(desktop->layout, wlr_output);
	}

	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		roots_seat_configure_cursor(seat);
		roots_seat_configure_xcursor(seat);
	}
}

void output_remove_notify(struct wl_listener *listener, void *data) {
	struct wlr_output *wlr_output = data;
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, output_remove);

	struct roots_output *output = NULL, *_output;
	wl_list_for_each(_output, &desktop->outputs, link) {
		if (_output->wlr_output == wlr_output) {
			output = _output;
			break;
		}
	}
	if (!output) {
		return; // We are unfamiliar with this output
	}

	wlr_output_layout_remove(desktop->layout, output->wlr_output);

	// TODO: cursor
	//example_config_configure_cursor(sample->config, sample->cursor,
	//	sample->compositor);

	wl_list_remove(&output->link);
	wl_list_remove(&output->frame.link);
	free(output);
}
