#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/session.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/egl.h>
#include <wlr/types/wlr_list.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include "backend/drm/drm.h"
#include "util/signal.h"

static bool wlr_drm_backend_start(struct wlr_backend *backend) {
	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)backend;
	wlr_drm_scan_connectors(drm);
	return true;
}

static void wlr_drm_backend_destroy(struct wlr_backend *backend) {
	if (!backend) {
		return;
	}

	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)backend;

	wlr_drm_restore_outputs(drm);

	struct wlr_drm_connector *conn, *next;
	wl_list_for_each_safe(conn, next, &drm->outputs, link) {
		wlr_output_destroy(&conn->output);
	}

	wlr_signal_emit_safe(&backend->events.destroy, backend);

	wl_list_remove(&drm->display_destroy.link);
	wl_list_remove(&drm->session_signal.link);
	wl_list_remove(&drm->drm_invalidated.link);

	wlr_drm_resources_free(drm);
	wlr_drm_renderer_finish(&drm->renderer);
	wlr_session_close_file(drm->session, drm->fd);
	wl_event_source_remove(drm->drm_event);
	free(drm);
}

static struct wlr_egl *wlr_drm_backend_get_egl(struct wlr_backend *backend) {
	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)backend;
	return &drm->renderer.egl;
}

static struct wlr_renderer *wlr_drm_backend_get_renderer(
		struct wlr_backend *backend) {
	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)backend;
	return drm->renderer.wlr_rend;
}

static struct wlr_backend_impl backend_impl = {
	.start = wlr_drm_backend_start,
	.destroy = wlr_drm_backend_destroy,
	.get_egl = wlr_drm_backend_get_egl,
	.get_renderer = wlr_drm_backend_get_renderer,
};

bool wlr_backend_is_drm(struct wlr_backend *b) {
	return b->impl == &backend_impl;
}

static void session_signal(struct wl_listener *listener, void *data) {
	struct wlr_drm_backend *drm =
		wl_container_of(listener, drm, session_signal);
	struct wlr_session *session = data;

	if (session->active) {
		wlr_log(L_INFO, "DRM fd resumed");
		wlr_drm_scan_connectors(drm);

		struct wlr_drm_connector *conn;
		wl_list_for_each(conn, &drm->outputs, link){
			if (conn->output.current_mode) {
				wlr_output_set_mode(&conn->output, conn->output.current_mode);
			} else {
				wlr_drm_connector_enable(&conn->output, false);
			}

			if (!conn->crtc) {
				continue;
			}

			struct wlr_drm_plane *plane = conn->crtc->cursor;
			drm->iface->crtc_set_cursor(drm, conn->crtc,
				(plane && plane->cursor_enabled) ? plane->cursor_bo : NULL);
			drm->iface->crtc_move_cursor(drm, conn->crtc, conn->cursor_x,
				conn->cursor_y);
		}
	} else {
		wlr_log(L_INFO, "DRM fd paused");
	}
}

static void drm_invalidated(struct wl_listener *listener, void *data) {
	struct wlr_drm_backend *drm =
		wl_container_of(listener, drm, drm_invalidated);

	char *name = drmGetDeviceNameFromFd2(drm->fd);
	wlr_log(L_DEBUG, "%s invalidated", name);
	free(name);

	wlr_drm_scan_connectors(drm);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_drm_backend *drm =
		wl_container_of(listener, drm, display_destroy);
	wlr_drm_backend_destroy(&drm->backend);
}

struct wlr_backend *wlr_drm_backend_create(struct wl_display *display,
		struct wlr_session *session, int gpu_fd, struct wlr_backend *parent) {
	assert(display && session && gpu_fd >= 0);
	assert(!parent || wlr_backend_is_drm(parent));

	char *name = drmGetDeviceNameFromFd2(gpu_fd);
	drmVersion *version = drmGetVersion(gpu_fd);
	wlr_log(L_INFO, "Initializing DRM backend for %s (%s)", name, version->name);
	free(name);
	drmFreeVersion(version);

	struct wlr_drm_backend *drm = calloc(1, sizeof(struct wlr_drm_backend));
	if (!drm) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		return NULL;
	}
	wlr_backend_init(&drm->backend, &backend_impl);

	drm->session = session;
	wl_list_init(&drm->outputs);

	drm->fd = gpu_fd;
	drm->parent = (struct wlr_drm_backend *)parent;

	drm->drm_invalidated.notify = drm_invalidated;
	wlr_session_signal_add(session, gpu_fd, &drm->drm_invalidated);

	drm->display = display;
	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

	drm->drm_event = wl_event_loop_add_fd(event_loop, drm->fd,
		WL_EVENT_READABLE, wlr_drm_event, NULL);
	if (!drm->drm_event) {
		wlr_log(L_ERROR, "Failed to create DRM event source");
		goto error_fd;
	}

	drm->session_signal.notify = session_signal;
	wl_signal_add(&session->session_signal, &drm->session_signal);

	if (!wlr_drm_check_features(drm)) {
		goto error_event;
	}

	if (!wlr_drm_resources_init(drm)) {
		goto error_event;
	}

	if (!wlr_drm_renderer_init(drm, &drm->renderer)) {
		wlr_log(L_ERROR, "Failed to initialize renderer");
		goto error_event;
	}

	if (!wlr_egl_bind_display(&drm->renderer.egl, display)) {
		wlr_log(L_INFO, "Failed to bind egl/wl display: %s", egl_error());
	}

	drm->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &drm->display_destroy);

	return &drm->backend;

error_event:
	wl_list_remove(&drm->session_signal.link);
	wl_event_source_remove(drm->drm_event);
error_fd:
	wlr_session_close_file(drm->session, drm->fd);
	free(drm);
	return NULL;
}

struct wlr_session *wlr_drm_backend_get_session(struct wlr_backend *backend) {
	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)backend;
	return drm->session;
}
