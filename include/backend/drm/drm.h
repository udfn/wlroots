#ifndef BACKEND_DRM_DRM_H
#define BACKEND_DRM_DRM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <wayland-server.h>
#include <xf86drmMode.h>
#include <EGL/egl.h>
#include <gbm.h>
#include <wayland-util.h>

#include <wlr/backend/session.h>
#include <wlr/backend/drm.h>
#include <wlr/types/wlr_output.h>
#include <wlr/render/egl.h>
#include <wlr/types/wlr_list.h>

#include "iface.h"
#include "properties.h"
#include "renderer.h"

struct wlr_drm_plane {
	uint32_t type;
	uint32_t id;

	uint32_t possible_crtcs;

	struct wlr_drm_surface surf;
	struct wlr_drm_surface mgpu_surf;

	// Only used by cursor
	float matrix[16];
	struct wlr_texture *wlr_tex;
	struct gbm_bo *cursor_bo;
	bool cursor_enabled;

	union wlr_drm_plane_props props;
};

struct wlr_drm_crtc {
	uint32_t id;
	uint32_t mode_id; // atomic modesetting only
	drmModeAtomicReq *atomic;

	union {
		struct {
			struct wlr_drm_plane *overlay;
			struct wlr_drm_plane *primary;
			struct wlr_drm_plane *cursor;
		};
		struct wlr_drm_plane *planes[3];
	};

	union wlr_drm_crtc_props props;

	struct wl_list connectors;
};

struct wlr_drm_backend {
	struct wlr_backend backend;

	struct wlr_drm_backend *parent;
	const struct wlr_drm_interface *iface;

	int fd;

	size_t num_crtcs;
	struct wlr_drm_crtc *crtcs;
	size_t num_planes;
	struct wlr_drm_plane *planes;

	union {
		struct {
			size_t num_overlay_planes;
			size_t num_primary_planes;
			size_t num_cursor_planes;
		};
		size_t num_type_planes[3];
	};

	union {
		struct {
			struct wlr_drm_plane *overlay_planes;
			struct wlr_drm_plane *primary_planes;
			struct wlr_drm_plane *cursor_planes;
		};
		struct wlr_drm_plane *type_planes[3];
	};

	struct wl_display *display;
	struct wl_event_source *drm_event;

	struct wl_listener session_signal;
	struct wl_listener drm_invalidated;

	struct wl_list outputs;

	struct wlr_drm_renderer renderer;
	struct wlr_session *session;
};

enum wlr_drm_connector_state {
	WLR_DRM_CONN_DISCONNECTED,
	WLR_DRM_CONN_NEEDS_MODESET,
	WLR_DRM_CONN_CLEANUP,
	WLR_DRM_CONN_CONNECTED,
};

struct wlr_drm_mode {
	struct wlr_output_mode wlr_mode;
	drmModeModeInfo drm_mode;
};

struct wlr_drm_connector {
	struct wlr_output output;

	enum wlr_drm_connector_state state;
	uint32_t id;

	struct wlr_drm_crtc *crtc;
	uint32_t possible_crtc;

	union wlr_drm_connector_props props;

	uint32_t width;
	uint32_t height;

	drmModeCrtc *old_crtc;

	bool pageflip_pending;
	struct wl_event_source *retry_pageflip;
	struct wl_list link;
};

bool wlr_drm_check_features(struct wlr_drm_backend *drm);
bool wlr_drm_resources_init(struct wlr_drm_backend *drm);
void wlr_drm_resources_free(struct wlr_drm_backend *drm);
void wlr_drm_restore_outputs(struct wlr_drm_backend *drm);
void wlr_drm_connector_cleanup(struct wlr_drm_connector *conn);
void wlr_drm_scan_connectors(struct wlr_drm_backend *state);
int wlr_drm_event(int fd, uint32_t mask, void *data);

void wlr_drm_connector_start_renderer(struct wlr_drm_connector *conn);

#endif
