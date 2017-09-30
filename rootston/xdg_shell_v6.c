#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/types/wlr_xdg_shell_v6.h>
#include <wlr/util/log.h>
#include "rootston/desktop.h"
#include "rootston/server.h"
#include "rootston/input.h"

static void get_input_bounds(struct roots_view *view, struct wlr_box *box) {
	assert(view->type == ROOTS_XDG_SHELL_V6_VIEW);
	struct wlr_xdg_surface_v6 *surf = view->xdg_surface_v6;
	memcpy(box, surf->geometry, sizeof(struct wlr_box));
	// TODO: real input bounds
	box->x -= 10;
	box->y -= 10;
	box->width += 20;
	box->height += 20;
}

static void activate(struct roots_view *view, bool active) {
	assert(view->type == ROOTS_XDG_SHELL_V6_VIEW);
	struct wlr_xdg_surface_v6 *surf = view->xdg_surface_v6;
	if (surf->role == WLR_XDG_SURFACE_V6_ROLE_TOPLEVEL) {
		wlr_xdg_toplevel_v6_set_activated(surf, active);
	}
}

static void handle_request_move(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, request_move);
	struct roots_view *view = roots_xdg_surface->view;
	struct roots_input *input = view->desktop->server->input;
	struct wlr_xdg_toplevel_v6_move_event *e = data;
	const struct roots_input_event *event = get_input_event(input, e->serial);
	if (!event || input->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	view_begin_move(input, event->cursor, view);
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, request_resize);
	struct roots_view *view = roots_xdg_surface->view;
	struct roots_input *input = view->desktop->server->input;
	struct wlr_xdg_toplevel_v6_resize_event *e = data;
	const struct roots_input_event *event = get_input_event(input, e->serial);
	if (!event || input->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	view_begin_resize(input, event->cursor, view);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_xdg_surface_v6 *roots_xdg_surface =
		wl_container_of(listener, roots_xdg_surface, destroy);
	wl_list_remove(&roots_xdg_surface->destroy.link);
	wl_list_remove(&roots_xdg_surface->ping_timeout.link);
	wl_list_remove(&roots_xdg_surface->request_move.link);
	wl_list_remove(&roots_xdg_surface->request_resize.link);
	wl_list_remove(&roots_xdg_surface->request_show_window_menu.link);
	wl_list_remove(&roots_xdg_surface->request_minimize.link);
	view_destroy(roots_xdg_surface->view);
	free(roots_xdg_surface);
}

void handle_xdg_shell_v6_surface(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, xdg_shell_v6_surface);

	struct wlr_xdg_surface_v6 *surface = data;
	wlr_log(L_DEBUG, "new xdg surface: title=%s, app_id=%s",
		surface->title, surface->app_id);
	wlr_xdg_surface_v6_ping(surface);

	struct roots_xdg_surface_v6 *roots_surface =
		calloc(1, sizeof(struct roots_xdg_surface_v6));
	// TODO: all of the trimmings
	wl_list_init(&roots_surface->destroy.link);
	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&surface->events.destroy, &roots_surface->destroy);
	wl_list_init(&roots_surface->ping_timeout.link);
	wl_list_init(&roots_surface->request_minimize.link);
	wl_list_init(&roots_surface->request_move.link);
	roots_surface->request_move.notify = handle_request_move;
	wl_signal_add(&surface->events.request_move, &roots_surface->request_move);
	wl_list_init(&roots_surface->request_resize.link);
	roots_surface->request_resize.notify = handle_request_resize;
	wl_signal_add(&surface->events.request_resize,
		&roots_surface->request_resize);
	wl_list_init(&roots_surface->request_show_window_menu.link);

	struct roots_view *view = calloc(1, sizeof(struct roots_view));
	view->type = ROOTS_XDG_SHELL_V6_VIEW;
	view->x = view->y = 200;
	view->xdg_surface_v6 = surface;
	view->roots_xdg_surface_v6 = roots_surface;
	view->wlr_surface = surface->surface;
	view->get_input_bounds = get_input_bounds;
	view->activate = activate;
	view->desktop = desktop;
	roots_surface->view = view;
	list_add(desktop->views, view);
}
