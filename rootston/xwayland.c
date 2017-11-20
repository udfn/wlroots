#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_surface.h>
#include <wlr/xwayland.h>
#include <wlr/util/log.h>
#include "rootston/desktop.h"
#include "rootston/server.h"

static void activate(struct roots_view *view, bool active) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	struct wlr_xwayland *xwayland = view->desktop->xwayland;
	wlr_xwayland_surface_activate(xwayland, view->xwayland_surface, active);
}

static void move(struct roots_view *view, double x, double y) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	struct wlr_xwayland_surface *xwayland_surface = view->xwayland_surface;
	view->x = x;
	view->y = y;
	wlr_xwayland_surface_configure(view->desktop->xwayland, xwayland_surface,
		x, y, xwayland_surface->width, xwayland_surface->height);
}

static void apply_size_constraints(
		struct wlr_xwayland_surface *xwayland_surface, uint32_t width,
		uint32_t height, uint32_t *dest_width, uint32_t *dest_height) {
	*dest_width = width;
	*dest_height = height;

	struct wlr_xwayland_surface_size_hints *size_hints =
		xwayland_surface->size_hints;
	if (size_hints != NULL) {
		if (width < (uint32_t)size_hints->min_width) {
			*dest_width = size_hints->min_width;
		} else if (size_hints->max_width > 0 &&
				width > (uint32_t)size_hints->max_width) {
			*dest_width = size_hints->max_width;
		}
		if (height < (uint32_t)size_hints->min_height) {
			*dest_height = size_hints->min_height;
		} else if (size_hints->max_height > 0 &&
				height > (uint32_t)size_hints->max_height) {
			*dest_height = size_hints->max_height;
		}
	}
}

static void resize(struct roots_view *view, uint32_t width, uint32_t height) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	struct wlr_xwayland_surface *xwayland_surface = view->xwayland_surface;

	uint32_t contrained_width, contrained_height;
	apply_size_constraints(xwayland_surface, width, height, &contrained_width,
		&contrained_height);

	wlr_xwayland_surface_configure(view->desktop->xwayland, xwayland_surface,
		xwayland_surface->x, xwayland_surface->y, contrained_width,
		contrained_height);
}

static void move_resize(struct roots_view *view, double x, double y,
		uint32_t width, uint32_t height) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	struct wlr_xwayland_surface *xwayland_surface = view->xwayland_surface;

	uint32_t contrained_width, contrained_height;
	apply_size_constraints(xwayland_surface, width, height, &contrained_width,
		&contrained_height);

	x = x + width - contrained_width;
	y = y + height - contrained_height;

	view->x = x;
	view->y = y;

	wlr_xwayland_surface_configure(view->desktop->xwayland, xwayland_surface,
		x, y, contrained_width, contrained_height);
}

static void close(struct roots_view *view) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);
	wlr_xwayland_surface_close(view->desktop->xwayland, view->xwayland_surface);
}

static void maximize(struct roots_view *view, bool maximized) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);

	wlr_xwayland_surface_set_maximized(view->desktop->xwayland,
		view->xwayland_surface, maximized);
}

static void set_fullscreen(struct roots_view *view, bool fullscreen) {
	assert(view->type == ROOTS_XWAYLAND_VIEW);

	wlr_xwayland_surface_set_fullscreen(view->desktop->xwayland,
		view->xwayland_surface, fullscreen);
}

static void handle_destroy(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, destroy);
	struct wlr_xwayland_surface *xwayland_surface =
		roots_surface->view->xwayland_surface;
	wl_list_remove(&roots_surface->destroy.link);
	wl_list_remove(&roots_surface->request_configure.link);
	wl_list_remove(&roots_surface->request_move.link);
	wl_list_remove(&roots_surface->request_resize.link);
	wl_list_remove(&roots_surface->request_maximize.link);
	wl_list_remove(&roots_surface->map_notify.link);
	wl_list_remove(&roots_surface->unmap_notify.link);
	if (xwayland_surface->mapped) {
		wl_list_remove(&roots_surface->view->link);
	}
	view_destroy(roots_surface->view);
	free(roots_surface);
}

static void handle_request_configure(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_configure);
	struct wlr_xwayland_surface *xwayland_surface =
		roots_surface->view->xwayland_surface;
	struct wlr_xwayland_surface_configure_event *event = data;

	roots_surface->view->x = (double)event->x;
	roots_surface->view->y = (double)event->y;

	wlr_xwayland_surface_configure(roots_surface->view->desktop->xwayland,
		xwayland_surface, event->x, event->y, event->width, event->height);
}

static struct roots_seat *guess_seat_for_view(struct roots_view *view) {
	// the best we can do is to pick the first seat that has the surface focused
	// for the pointer
	struct roots_input *input = view->desktop->server->input;
	struct roots_seat *seat;
	wl_list_for_each(seat, &input->seats, link) {
		if (seat->seat->pointer_state.focused_surface == view->wlr_surface) {
			return seat;
		}
	}
	return NULL;
}

static void handle_request_move(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_move);
	struct roots_view *view = roots_surface->view;
	struct roots_seat *seat = guess_seat_for_view(view);

	if (!seat || seat->cursor->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}

	roots_seat_begin_move(seat, view);
}

static void handle_request_resize(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_resize);
	struct roots_view *view = roots_surface->view;
	struct roots_seat *seat = guess_seat_for_view(view);
	struct wlr_xwayland_resize_event *e = data;

	if (!seat || seat->cursor->mode != ROOTS_CURSOR_PASSTHROUGH) {
		return;
	}
	roots_seat_begin_resize(seat, view, e->edges);
}

static void handle_request_maximize(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_maximize);
	struct roots_view *view = roots_surface->view;
	struct wlr_xwayland_surface *xwayland_surface = view->xwayland_surface;

	bool maximized = xwayland_surface->maximized_vert &&
		xwayland_surface->maximized_horz;
	view_maximize(view, maximized);
}

static void handle_request_fullscreen(struct wl_listener *listener,
		void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, request_fullscreen);
	struct roots_view *view = roots_surface->view;
	struct wlr_xwayland_surface *xwayland_surface = view->xwayland_surface;

	view_set_fullscreen(view, xwayland_surface->fullscreen, NULL);
}

static void handle_map_notify(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, map_notify);
	struct wlr_xwayland_surface *xsurface = data;
	struct roots_view *view = roots_surface->view;
	struct roots_desktop *desktop = view->desktop;

	view->wlr_surface = xsurface->surface;
	view->x = (double)xsurface->x;
	view->y = (double)xsurface->y;

	wl_list_insert(&desktop->views, &view->link);
}

static void handle_unmap_notify(struct wl_listener *listener, void *data) {
	struct roots_xwayland_surface *roots_surface =
		wl_container_of(listener, roots_surface, unmap_notify);
	roots_surface->view->wlr_surface = NULL;

	wl_list_remove(&roots_surface->view->link);
}

void handle_xwayland_surface(struct wl_listener *listener, void *data) {
	struct roots_desktop *desktop =
		wl_container_of(listener, desktop, xwayland_surface);

	struct wlr_xwayland_surface *surface = data;
	wlr_log(L_DEBUG, "new xwayland surface: title=%s, class=%s, instance=%s",
		surface->title, surface->class, surface->instance);

	struct roots_xwayland_surface *roots_surface =
		calloc(1, sizeof(struct roots_xwayland_surface));
	if (roots_surface == NULL) {
		return;
	}

	roots_surface->destroy.notify = handle_destroy;
	wl_signal_add(&surface->events.destroy, &roots_surface->destroy);
	roots_surface->request_configure.notify = handle_request_configure;
	wl_signal_add(&surface->events.request_configure,
		&roots_surface->request_configure);
	roots_surface->map_notify.notify = handle_map_notify;
	wl_signal_add(&surface->events.map_notify, &roots_surface->map_notify);
	roots_surface->unmap_notify.notify = handle_unmap_notify;
	wl_signal_add(&surface->events.unmap_notify, &roots_surface->unmap_notify);
	roots_surface->request_move.notify = handle_request_move;
	wl_signal_add(&surface->events.request_move, &roots_surface->request_move);
	roots_surface->request_resize.notify = handle_request_resize;
	wl_signal_add(&surface->events.request_resize,
		&roots_surface->request_resize);
	roots_surface->request_maximize.notify = handle_request_maximize;
	wl_signal_add(&surface->events.request_maximize,
		&roots_surface->request_maximize);
	roots_surface->request_fullscreen.notify = handle_request_fullscreen;
	wl_signal_add(&surface->events.request_fullscreen,
		&roots_surface->request_fullscreen);

	struct roots_view *view = calloc(1, sizeof(struct roots_view));
	if (view == NULL) {
		free(roots_surface);
		return;
	}
	view->type = ROOTS_XWAYLAND_VIEW;
	view->x = (double)surface->x;
	view->y = (double)surface->y;
	view->xwayland_surface = surface;
	view->roots_xwayland_surface = roots_surface;
	view->wlr_surface = surface->surface;
	view->activate = activate;
	view->resize = resize;
	view->move = move;
	view->move_resize = move_resize;
	view->maximize = maximize;
	view->set_fullscreen = set_fullscreen;
	view->close = close;
	roots_surface->view = view;
	view_init(view, desktop);
	wl_list_insert(&desktop->views, &view->link);

	if (!surface->override_redirect) {
		view_setup(view);
	}
}
