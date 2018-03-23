#ifndef WLR_BACKEND_DRM_H
#define WLR_BACKEND_DRM_H

#include <wayland-server.h>
#include <wlr/backend.h>
#include <wlr/backend/session.h>
#include <wlr/types/wlr_output.h>

/**
 * Creates a DRM backend using the specified GPU file descriptor (typically from
 * a device node in /dev/dri).
 *
 * To slave this to another DRM backend, pass it as the parent (which _must_ be
 * a DRM backend, other kinds of backends raise SIGABRT).
 */
struct wlr_backend *wlr_drm_backend_create(struct wl_display *display,
	struct wlr_session *session, int gpu_fd, struct wlr_backend *parent);

bool wlr_backend_is_drm(struct wlr_backend *backend);
bool wlr_output_is_drm(struct wlr_output *output);

#endif
