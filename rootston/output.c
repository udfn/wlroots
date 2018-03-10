#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <time.h>
#include <wlr/render/matrix.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_wl_shell.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/log.h>
#include <wlr/util/region.h>
#include "rootston/config.h"
#include "rootston/output.h"
#include "rootston/server.h"

typedef void (*surface_iterator_func_t)(struct wlr_surface *surface,
	double lx, double ly, float rotation, void *data);

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

static void surface_for_each_surface(struct wlr_surface *surface, double lx,
		double ly, float rotation, surface_iterator_func_t iterator,
		void *user_data) {
	iterator(surface, lx, ly, rotation, user_data);

	struct wlr_subsurface *subsurface;
	wl_list_for_each(subsurface, &surface->subsurface_list, parent_link) {
		struct wlr_surface_state *state = subsurface->surface->current;
		double sx = state->subsurface_position.x;
		double sy = state->subsurface_position.y;
		rotate_child_position(&sx, &sy, state->width, state->height,
			surface->current->width, surface->current->height, rotation);

		surface_for_each_surface(subsurface->surface, lx + sx, ly + sy,
			rotation, iterator, user_data);
	}
}

static void xdg_surface_v6_for_each_surface(struct wlr_xdg_surface_v6 *surface,
		double base_x, double base_y, float rotation,
		surface_iterator_func_t iterator, void *user_data) {
	double width = surface->surface->current->width;
	double height = surface->surface->current->height;

	struct wlr_xdg_popup_v6 *popup_state;
	wl_list_for_each(popup_state, &surface->popups, link) {
		struct wlr_xdg_surface_v6 *popup = popup_state->base;
		if (!popup->configured) {
			continue;
		}

		double popup_width = popup->surface->current->width;
		double popup_height = popup->surface->current->height;

		double popup_sx, popup_sy;
		wlr_xdg_surface_v6_popup_get_position(popup, &popup_sx, &popup_sy);
		rotate_child_position(&popup_sx, &popup_sy, popup_width, popup_height,
			width, height, rotation);

		surface_for_each_surface(popup->surface, base_x + popup_sx,
			base_y + popup_sy, rotation, iterator, user_data);
		xdg_surface_v6_for_each_surface(popup, base_x + popup_sx,
			base_y + popup_sy, rotation, iterator, user_data);
	}
}

static void xdg_surface_for_each_surface(struct wlr_xdg_surface *surface,
		double base_x, double base_y, float rotation,
		surface_iterator_func_t iterator, void *user_data) {
	double width = surface->surface->current->width;
	double height = surface->surface->current->height;

	struct wlr_xdg_popup *popup_state;
	wl_list_for_each(popup_state, &surface->popups, link) {
		struct wlr_xdg_surface *popup = popup_state->base;
		if (!popup->configured) {
			continue;
		}

		double popup_width = popup->surface->current->width;
		double popup_height = popup->surface->current->height;

		double popup_sx, popup_sy;
		wlr_xdg_surface_popup_get_position(popup, &popup_sx, &popup_sy);
		rotate_child_position(&popup_sx, &popup_sy, popup_width, popup_height,
			width, height, rotation);

		surface_for_each_surface(popup->surface, base_x + popup_sx,
			base_y + popup_sy, rotation, iterator, user_data);
		xdg_surface_for_each_surface(popup, base_x + popup_sx,
			base_y + popup_sy, rotation, iterator, user_data);
	}
}

static void wl_shell_surface_for_each_surface(
		struct wlr_wl_shell_surface *surface, double lx, double ly,
		float rotation, bool is_child, surface_iterator_func_t iterator,
		void *user_data) {
	if (is_child || surface->state != WLR_WL_SHELL_SURFACE_STATE_POPUP) {
		surface_for_each_surface(surface->surface, lx, ly, rotation, iterator,
			user_data);

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

			wl_shell_surface_for_each_surface(popup, lx + popup_x, ly + popup_y,
				rotation, true, iterator, user_data);
		}
	}
}

static void view_for_each_surface(struct roots_view *view,
		surface_iterator_func_t iterator, void *user_data) {
	switch (view->type) {
	case ROOTS_XDG_SHELL_V6_VIEW:
		surface_for_each_surface(view->wlr_surface, view->x, view->y,
			view->rotation, iterator, user_data);
		xdg_surface_v6_for_each_surface(view->xdg_surface_v6, view->x, view->y,
			view->rotation, iterator, user_data);
		break;
	case ROOTS_XDG_SHELL_VIEW:
		surface_for_each_surface(view->wlr_surface, view->x, view->y,
			view->rotation, iterator, user_data);
		xdg_surface_for_each_surface(view->xdg_surface, view->x, view->y,
			view->rotation, iterator, user_data);
		break;
	case ROOTS_WL_SHELL_VIEW:
		wl_shell_surface_for_each_surface(view->wl_shell_surface, view->x,
			view->y, view->rotation, false, iterator, user_data);
		break;
#ifdef WLR_HAS_XWAYLAND
	case ROOTS_XWAYLAND_VIEW:
		if (view->wlr_surface != NULL) {
			surface_for_each_surface(view->wlr_surface, view->x, view->y,
				view->rotation, iterator, user_data);
		}
		break;
#endif
	}
}

#ifdef WLR_HAS_XWAYLAND
static void xwayland_children_for_each_surface(
		struct wlr_xwayland_surface *surface,
		surface_iterator_func_t iterator, void *user_data) {
	struct wlr_xwayland_surface *child;
	wl_list_for_each(child, &surface->children, parent_link) {
		if (child->surface != NULL && child->added) {
			surface_for_each_surface(child->surface, child->x, child->y, 0,
				iterator, user_data);
		}
		xwayland_children_for_each_surface(child, iterator, user_data);
	}
}
#endif

static void drag_icons_for_each_surface(struct roots_input *input,
		surface_iterator_func_t iterator, void *user_data) {
	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		struct roots_drag_icon *drag_icon;
		wl_list_for_each(drag_icon, &seat->drag_icons, link) {
			if (!drag_icon->wlr_drag_icon->mapped) {
				continue;
			}
			surface_for_each_surface(drag_icon->wlr_drag_icon->surface,
				drag_icon->x, drag_icon->y, 0, iterator, user_data);
		}
	}
}


struct render_data {
	struct roots_output *output;
	struct timespec *when;
	pixman_region32_t *damage;
	float alpha;
};

/**
 * Checks whether a surface at (lx, ly) intersects an output. If `box` is not
 * NULL, it populates it with the surface box in the output, in output-local
 * coordinates.
 */
static bool surface_intersect_output(struct wlr_surface *surface,
		struct wlr_output_layout *output_layout, struct wlr_output *wlr_output,
		double lx, double ly, float rotation, struct wlr_box *box) {
	double ox = lx, oy = ly;
	wlr_output_layout_output_coords(output_layout, wlr_output, &ox, &oy);

	if (box != NULL) {
		box->x = ox * wlr_output->scale;
		box->y = oy * wlr_output->scale;
		box->width = surface->current->width * wlr_output->scale;
		box->height = surface->current->height * wlr_output->scale;
	}

	struct wlr_box layout_box = {
		.x = lx, .y = ly,
		.width = surface->current->width, .height = surface->current->height,
	};
	wlr_box_rotated_bounds(&layout_box, -rotation, &layout_box);
	return wlr_output_layout_intersects(output_layout, wlr_output, &layout_box);
}

static void scissor_output(struct roots_output *output, pixman_box32_t *rect) {
	struct wlr_output *wlr_output = output->wlr_output;
	struct wlr_renderer *renderer = wlr_backend_get_renderer(wlr_output->backend);
	assert(renderer);

	struct wlr_box box = {
		.x = rect->x1,
		.y = rect->y1,
		.width = rect->x2 - rect->x1,
		.height = rect->y2 - rect->y1,
	};

	int ow, oh;
	wlr_output_transformed_resolution(output->wlr_output, &ow, &oh);

	// Scissor is in renderer coordinates, ie. upside down
	enum wl_output_transform transform = wlr_output_transform_compose(
		wlr_output_transform_invert(wlr_output->transform),
		WL_OUTPUT_TRANSFORM_FLIPPED_180);
	wlr_box_transform(&box, transform, ow, oh, &box);

	wlr_renderer_scissor(renderer, &box);
}

static void render_surface(struct wlr_surface *surface, double lx, double ly,
		float rotation, void *_data) {
	struct render_data *data = _data;
	struct roots_output *output = data->output;
	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(output->wlr_output->backend);
	assert(renderer);

	if (!wlr_surface_has_buffer(surface)) {
		return;
	}

	struct wlr_box box;
	bool intersects = surface_intersect_output(surface, output->desktop->layout,
		output->wlr_output, lx, ly, rotation, &box);
	if (!intersects) {
		return;
	}

	struct wlr_box rotated;
	wlr_box_rotated_bounds(&box, -rotation, &rotated);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_union_rect(&damage, &damage, rotated.x, rotated.y,
		rotated.width, rotated.height);
	pixman_region32_intersect(&damage, &damage, data->damage);
	bool damaged = pixman_region32_not_empty(&damage);
	if (!damaged) {
		goto damage_finish;
	}

	float matrix[16];
	enum wl_output_transform transform =
		wlr_output_transform_invert(surface->current->transform);
	wlr_matrix_project_box(&matrix, &box, transform, rotation,
		&output->wlr_output->transform_matrix);

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output, &rects[i]);
		wlr_render_with_matrix(renderer, surface->texture, &matrix, data->alpha);
	}

damage_finish:
	pixman_region32_fini(&damage);
}

static void get_decoration_box(struct roots_view *view,
		struct roots_output *output, struct wlr_box *box) {
	struct wlr_output *wlr_output = output->wlr_output;

	struct wlr_box deco_box;
	view_get_deco_box(view, &deco_box);
	double sx = deco_box.x - view->x;
	double sy = deco_box.y - view->y;
	rotate_child_position(&sx, &sy, deco_box.width, deco_box.height,
		view->wlr_surface->current->width,
		view->wlr_surface->current->height, view->rotation);
	double x = sx + view->x;
	double y = sy + view->y;

	wlr_output_layout_output_coords(output->desktop->layout, wlr_output, &x, &y);

	box->x = x * wlr_output->scale;
	box->y = y * wlr_output->scale;
	box->width = deco_box.width * wlr_output->scale;
	box->height = deco_box.height * wlr_output->scale;
}

static void render_decorations(struct roots_view *view,
		struct render_data *data) {
	if (!view->decorated || view->wlr_surface == NULL) {
		return;
	}

	struct roots_output *output = data->output;
	struct wlr_renderer *renderer =
		wlr_backend_get_renderer(output->wlr_output->backend);
	assert(renderer);

	struct wlr_box box;
	get_decoration_box(view, output, &box);

	struct wlr_box rotated;
	wlr_box_rotated_bounds(&box, -view->rotation, &rotated);

	pixman_region32_t damage;
	pixman_region32_init(&damage);
	pixman_region32_union_rect(&damage, &damage, rotated.x, rotated.y,
		rotated.width, rotated.height);
	pixman_region32_intersect(&damage, &damage, data->damage);
	bool damaged = pixman_region32_not_empty(&damage);
	if (!damaged) {
		goto damage_finish;
	}

	float matrix[16];
	wlr_matrix_project_box(&matrix, &box, WL_OUTPUT_TRANSFORM_NORMAL,
		view->rotation, &output->wlr_output->transform_matrix);
	float color[] = { 0.2, 0.2, 0.2, view->alpha };

	int nrects;
	pixman_box32_t *rects =
		pixman_region32_rectangles(&damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output, &rects[i]);
		wlr_render_colored_quad(renderer, &color, &matrix);
	}

damage_finish:
	pixman_region32_fini(&damage);
}

static void render_view(struct roots_view *view, struct render_data *data) {
	// Do not render views fullscreened on other outputs
	if (view->fullscreen_output != NULL &&
			view->fullscreen_output != data->output) {
		return;
	}

	data->alpha = view->alpha;
	render_decorations(view, data);
	view_for_each_surface(view, render_surface, data);
}

static bool has_standalone_surface(struct roots_view *view) {
	if (!wl_list_empty(&view->wlr_surface->subsurface_list)) {
		return false;
	}

	switch (view->type) {
	case ROOTS_XDG_SHELL_V6_VIEW:
		return wl_list_empty(&view->xdg_surface_v6->popups);
	case ROOTS_XDG_SHELL_VIEW:
		return wl_list_empty(&view->xdg_surface->popups);
	case ROOTS_WL_SHELL_VIEW:
		return wl_list_empty(&view->wl_shell_surface->popups);
#ifdef WLR_HAS_XWAYLAND
	case ROOTS_XWAYLAND_VIEW:
		return wl_list_empty(&view->xwayland_surface->children);
#endif
	}
	return true;
}

static void surface_send_frame_done(struct wlr_surface *surface, double lx,
		double ly, float rotation, void *_data) {
	struct render_data *data = _data;
	struct roots_output *output = data->output;
	struct timespec *when = data->when;

	if (!surface_intersect_output(surface, output->desktop->layout,
			output->wlr_output, lx, ly, rotation, NULL)) {
		return;
	}

	wlr_surface_send_frame_done(surface, when);
}

static void render_output(struct roots_output *output) {
	struct wlr_output *wlr_output = output->wlr_output;
	struct roots_desktop *desktop = output->desktop;
	struct roots_server *server = desktop->server;
	struct wlr_renderer *renderer = wlr_backend_get_renderer(wlr_output->backend);
	assert(renderer);

	if (!wlr_output->enabled) {
		return;
	}

	struct timespec now;
	clock_gettime(CLOCK_MONOTONIC, &now);

	float clear_color[] = {0.25f, 0.25f, 0.25f, 1.0f};

	// Check if we can delegate the fullscreen surface to the output
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
		}

		// Fullscreen views are rendered on a black background
		clear_color[0] = clear_color[1] = clear_color[2] = 0;
	} else {
		wlr_output_set_fullscreen_surface(wlr_output, NULL);
	}

	bool needs_swap;
	pixman_region32_t damage;
	pixman_region32_init(&damage);
	if (!wlr_output_damage_make_current(output->damage, &needs_swap, &damage)) {
		return;
	}

	struct render_data data = {
		.output = output,
		.when = &now,
		.damage = &damage,
		.alpha = 1.0,
	};

	if (!needs_swap) {
		// Output doesn't need swap and isn't damaged, skip rendering completely
		goto damage_finish;
	}

	wlr_renderer_begin(renderer, wlr_output);

	if (!pixman_region32_not_empty(&damage)) {
		// Output isn't damaged but needs buffer swap
		goto renderer_end;
	}

	int nrects;
	pixman_box32_t *rects = pixman_region32_rectangles(&damage, &nrects);
	for (int i = 0; i < nrects; ++i) {
		scissor_output(output, &rects[i]);
		wlr_renderer_clear(renderer, &clear_color);
	}

	// If a view is fullscreen on this output, render it
	if (output->fullscreen_view != NULL) {
		struct roots_view *view = output->fullscreen_view;

		if (wlr_output->fullscreen_surface == view->wlr_surface) {
			// The output will render the fullscreen view
			goto renderer_end;
		}

		if (view->wlr_surface != NULL) {
			view_for_each_surface(view, render_surface, &data);
		}

		// During normal rendering the xwayland window tree isn't traversed
		// because all windows are rendered. Here we only want to render
		// the fullscreen window's children so we have to traverse the tree.
#ifdef WLR_HAS_XWAYLAND
		if (view->type == ROOTS_XWAYLAND_VIEW) {
			xwayland_children_for_each_surface(view->xwayland_surface,
				render_surface, &data);
		}
#endif

		goto renderer_end;
	}

	// Render all views
	struct roots_view *view;
	wl_list_for_each_reverse(view, &desktop->views, link) {
		render_view(view, &data);
	}

	// Render drag icons
	data.alpha = 1.0;
	drag_icons_for_each_surface(server->input, render_surface, &data);

renderer_end:
	wlr_renderer_scissor(renderer, NULL);
	wlr_renderer_end(renderer);
	if (!wlr_output_damage_swap_buffers(output->damage, &now, &damage)) {
		goto damage_finish;
	}
	output->last_frame = desktop->last_frame = now;

damage_finish:
	pixman_region32_fini(&damage);

	// Send frame done events to all surfaces
	if (output->fullscreen_view != NULL) {
		struct roots_view *view = output->fullscreen_view;
		if (wlr_output->fullscreen_surface == view->wlr_surface) {
			// The surface is managed by the wlr_output
			return;
		}

		view_for_each_surface(view, surface_send_frame_done, &data);

#ifdef WLR_HAS_XWAYLAND
		if (view->type == ROOTS_XWAYLAND_VIEW) {
			xwayland_children_for_each_surface(view->xwayland_surface,
				surface_send_frame_done, &data);
		}
#endif
	} else {
		struct roots_view *view;
		wl_list_for_each_reverse(view, &desktop->views, link) {
			view_for_each_surface(view, surface_send_frame_done, &data);
		}

		drag_icons_for_each_surface(server->input, surface_send_frame_done,
			&data);
	}
}

void output_damage_whole(struct roots_output *output) {
	wlr_output_damage_add_whole(output->damage);
}

static bool view_accept_damage(struct roots_output *output,
		struct roots_view *view) {
	if (view->wlr_surface == NULL) {
		return false;
	}
	if (output->fullscreen_view == NULL) {
		return true;
	}
	if (output->fullscreen_view == view) {
		return true;
	}
#ifdef WLR_HAS_XWAYLAND
	if (output->fullscreen_view->type == ROOTS_XWAYLAND_VIEW &&
			view->type == ROOTS_XWAYLAND_VIEW) {
		// Special case: accept damage from children
		struct wlr_xwayland_surface *xsurface = view->xwayland_surface;
		while (xsurface != NULL) {
			if (output->fullscreen_view->xwayland_surface == xsurface) {
				return true;
			}
			xsurface = xsurface->parent;
		}
	}
#endif
	return false;
}

static void damage_whole_surface(struct wlr_surface *surface,
		double lx, double ly, float rotation, void *data) {
	struct roots_output *output = data;

	if (!wlr_surface_has_buffer(surface)) {
		return;
	}

	int ow, oh;
	wlr_output_transformed_resolution(output->wlr_output, &ow, &oh);

	struct wlr_box box;
	bool intersects = surface_intersect_output(surface, output->desktop->layout,
		output->wlr_output, lx, ly, rotation, &box);
	if (!intersects) {
		return;
	}

	wlr_box_rotated_bounds(&box, -rotation, &box);

	wlr_output_damage_add_box(output->damage, &box);
}

static void damage_whole_decoration(struct roots_view *view,
		struct roots_output *output) {
	if (!view->decorated || view->wlr_surface == NULL) {
		return;
	}

	struct wlr_box box;
	get_decoration_box(view, output, &box);

	wlr_box_rotated_bounds(&box, -view->rotation, &box);

	wlr_output_damage_add_box(output->damage, &box);
}

void output_damage_whole_view(struct roots_output *output,
		struct roots_view *view) {
	if (!view_accept_damage(output, view)) {
		return;
	}

	damage_whole_decoration(view, output);
	view_for_each_surface(view, damage_whole_surface, output);
}

void output_damage_whole_drag_icon(struct roots_output *output,
		struct roots_drag_icon *icon) {
	surface_for_each_surface(icon->wlr_drag_icon->surface, icon->x, icon->y, 0,
		damage_whole_surface, output);
}

static void damage_from_surface(struct wlr_surface *surface,
		double lx, double ly, float rotation, void *data) {
	struct roots_output *output = data;
	struct wlr_output *wlr_output = output->wlr_output;

	if (!wlr_surface_has_buffer(surface)) {
		return;
	}

	int ow, oh;
	wlr_output_transformed_resolution(wlr_output, &ow, &oh);

	struct wlr_box box;
	surface_intersect_output(surface, output->desktop->layout,
		wlr_output, lx, ly, rotation, &box);

	if (rotation == 0) {
		pixman_region32_t damage;
		pixman_region32_init(&damage);
		pixman_region32_copy(&damage, &surface->current->surface_damage);
		wlr_region_scale(&damage, &damage, wlr_output->scale);
		if (ceil(wlr_output->scale) > surface->current->scale) {
			// When scaling up a surface, it'll become blurry so we need to
			// expand the damage region
			wlr_region_expand(&damage, &damage,
				ceil(wlr_output->scale) - surface->current->scale);
		}
		pixman_region32_translate(&damage, box.x, box.y);
		wlr_output_damage_add(output->damage, &damage);
	} else {
		pixman_box32_t *extents =
			pixman_region32_extents(&surface->current->surface_damage);
		struct wlr_box damage_box = {
			.x = box.x + extents->x1 * wlr_output->scale,
			.y = box.y + extents->y1 * wlr_output->scale,
			.width = (extents->x2 - extents->x1) * wlr_output->scale,
			.height = (extents->y2 - extents->y1) * wlr_output->scale,
		};
		wlr_box_rotated_bounds(&damage_box, -rotation, &damage_box);
		wlr_output_damage_add_box(output->damage, &damage_box);
	}
}

void output_damage_from_view(struct roots_output *output,
		struct roots_view *view) {
	if (!view_accept_damage(output, view)) {
		return;
	}

	view_for_each_surface(view, damage_from_surface, output);
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

static void output_destroy(struct roots_output *output) {
	// TODO: cursor
	//example_config_configure_cursor(sample->config, sample->cursor,
	//	sample->compositor);

	wl_list_remove(&output->link);
	wl_list_remove(&output->destroy.link);
	wl_list_remove(&output->damage_frame.link);
	wl_list_remove(&output->damage_destroy.link);
	free(output);
}

static void output_handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_output *output = wl_container_of(listener, output, destroy);
	output_destroy(output);
}

static void output_damage_handle_frame(struct wl_listener *listener,
		void *data) {
	struct roots_output *output =
		wl_container_of(listener, output, damage_frame);
	render_output(output);
}

static void output_damage_handle_destroy(struct wl_listener *listener,
		void *data) {
	struct roots_output *output =
		wl_container_of(listener, output, damage_destroy);
	output_destroy(output);
}

void handle_new_output(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop = wl_container_of(listener, desktop,
		new_output);
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
	wl_list_insert(&desktop->outputs, &output->link);

	output->damage = wlr_output_damage_create(wlr_output);

	output->destroy.notify = output_handle_destroy;
	wl_signal_add(&wlr_output->events.destroy, &output->destroy);
	output->damage_frame.notify = output_damage_handle_frame;
	wl_signal_add(&output->damage->events.frame, &output->damage_frame);
	output->damage_destroy.notify = output_damage_handle_destroy;
	wl_signal_add(&output->damage->events.destroy, &output->damage_destroy);

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

	output_damage_whole(output);
}
