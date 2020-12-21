#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend/interface.h>
#include <wlr/backend/session.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/render/egl.h>
#include <wlr/types/wlr_list.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include "backend/drm/drm.h"
#include "util/signal.h"

struct wlr_drm_backend *get_drm_backend_from_backend(
		struct wlr_backend *wlr_backend) {
	assert(wlr_backend_is_drm(wlr_backend));
	return (struct wlr_drm_backend *)wlr_backend;
}

static bool backend_start(struct wlr_backend *backend) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(backend);
	scan_drm_connectors(drm);
	return true;
}

static void backend_destroy(struct wlr_backend *backend) {
	if (!backend) {
		return;
	}

	struct wlr_drm_backend *drm = get_drm_backend_from_backend(backend);

	restore_drm_outputs(drm);

	struct wlr_drm_connector *conn, *next;
	wl_list_for_each_safe(conn, next, &drm->outputs, link) {
		destroy_drm_connector(conn);
	}

	wlr_signal_emit_safe(&backend->events.destroy, backend);

	wl_list_remove(&drm->display_destroy.link);
	wl_list_remove(&drm->session_destroy.link);
	wl_list_remove(&drm->session_signal.link);
	wl_list_remove(&drm->drm_invalidated.link);

	finish_drm_resources(drm);
	finish_drm_renderer(&drm->renderer);

	free(drm->name);
	wlr_session_close_file(drm->session, drm->dev);
	wl_event_source_remove(drm->drm_event);
	free(drm);
}

static struct wlr_renderer *backend_get_renderer(
		struct wlr_backend *backend) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(backend);

	if (drm->parent) {
		return drm->parent->renderer.wlr_rend;
	} else {
		return drm->renderer.wlr_rend;
	}
}

static clockid_t backend_get_presentation_clock(struct wlr_backend *backend) {
	struct wlr_drm_backend *drm = get_drm_backend_from_backend(backend);
	return drm->clock;
}

static struct wlr_backend_impl backend_impl = {
	.start = backend_start,
	.destroy = backend_destroy,
	.get_renderer = backend_get_renderer,
	.get_presentation_clock = backend_get_presentation_clock,
};

bool wlr_backend_is_drm(struct wlr_backend *b) {
	return b->impl == &backend_impl;
}

static void session_signal(struct wl_listener *listener, void *data) {
	struct wlr_drm_backend *drm =
		wl_container_of(listener, drm, session_signal);
	struct wlr_session *session = drm->session;

	if (session->active) {
		wlr_log(WLR_INFO, "DRM fd resumed");
		scan_drm_connectors(drm);

		struct wlr_drm_connector *conn;
		wl_list_for_each(conn, &drm->outputs, link){
			if (conn->output.enabled && conn->output.current_mode != NULL) {
				drm_connector_set_mode(conn, conn->output.current_mode);
			} else {
				drm_connector_set_mode(conn, NULL);
			}
		}
	} else {
		wlr_log(WLR_INFO, "DRM fd paused");
	}
}

static void drm_invalidated(struct wl_listener *listener, void *data) {
	struct wlr_drm_backend *drm =
		wl_container_of(listener, drm, drm_invalidated);

	wlr_log(WLR_DEBUG, "%s invalidated", drm->name);

	scan_drm_connectors(drm);
}

static void handle_session_destroy(struct wl_listener *listener, void *data) {
	struct wlr_drm_backend *drm =
		wl_container_of(listener, drm, session_destroy);
	backend_destroy(&drm->backend);
}

static void handle_display_destroy(struct wl_listener *listener, void *data) {
	struct wlr_drm_backend *drm =
		wl_container_of(listener, drm, display_destroy);
	backend_destroy(&drm->backend);
}

struct wlr_backend *wlr_drm_backend_create(struct wl_display *display,
		struct wlr_session *session, struct wlr_device *dev,
		struct wlr_backend *parent,
		wlr_renderer_create_func_t create_renderer_func) {
	assert(display && session && dev);
	assert(!parent || wlr_backend_is_drm(parent));

	char *name = drmGetDeviceNameFromFd2(dev->fd);
	drmVersion *version = drmGetVersion(dev->fd);
	wlr_log(WLR_INFO, "Initializing DRM backend for %s (%s)", name, version->name);
	drmFreeVersion(version);

	struct wlr_drm_backend *drm = calloc(1, sizeof(struct wlr_drm_backend));
	if (!drm) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	wlr_backend_init(&drm->backend, &backend_impl);

	drm->session = session;
	wl_list_init(&drm->outputs);

	drm->dev = dev;
	drm->fd = dev->fd;
	drm->name = name;
	if (parent != NULL) {
		drm->parent = get_drm_backend_from_backend(parent);
	}

	drm->drm_invalidated.notify = drm_invalidated;
	wl_signal_add(&dev->events.change, &drm->drm_invalidated);

	drm->display = display;
	struct wl_event_loop *event_loop = wl_display_get_event_loop(display);

	drm->drm_event = wl_event_loop_add_fd(event_loop, drm->fd,
		WL_EVENT_READABLE, handle_drm_event, NULL);
	if (!drm->drm_event) {
		wlr_log(WLR_ERROR, "Failed to create DRM event source");
		goto error_fd;
	}

	drm->session_signal.notify = session_signal;
	wl_signal_add(&session->events.active, &drm->session_signal);

	if (!check_drm_features(drm)) {
		goto error_event;
	}

	if (!init_drm_resources(drm)) {
		goto error_event;
	}

	if (!init_drm_renderer(drm, &drm->renderer, create_renderer_func)) {
		wlr_log(WLR_ERROR, "Failed to initialize renderer");
		goto error_event;
	}

	drm->session_destroy.notify = handle_session_destroy;
	wl_signal_add(&session->events.destroy, &drm->session_destroy);

	drm->display_destroy.notify = handle_display_destroy;
	wl_display_add_destroy_listener(display, &drm->display_destroy);

	return &drm->backend;

error_event:
	wl_list_remove(&drm->session_signal.link);
	wl_event_source_remove(drm->drm_event);
error_fd:
	wlr_session_close_file(drm->session, dev);
	free(drm);
	return NULL;
}
