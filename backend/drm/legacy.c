#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <wlr/util/log.h>
#include "backend/drm/drm.h"
#include "backend/drm/iface.h"
#include "backend/drm/util.h"

static bool legacy_crtc_pageflip(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, struct wlr_drm_crtc *crtc,
		uint32_t fb_id, drmModeModeInfo *mode) {
	if (mode) {
		if (drmModeSetCrtc(drm->fd, crtc->id, fb_id, 0, 0,
				&conn->id, 1, mode)) {
			wlr_log_errno(L_ERROR, "%s: Failed to set CRTC", conn->output.name);
			return false;
		}
	}

	if (drmModePageFlip(drm->fd, crtc->id, fb_id, DRM_MODE_PAGE_FLIP_EVENT, conn)) {
		wlr_log_errno(L_ERROR, "%s: Failed to page flip", conn->output.name);
		return false;
	}

	return true;
}

static void legacy_conn_enable(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, bool enable) {
	drmModeConnectorSetProperty(drm->fd, conn->id, conn->props.dpms,
		enable ? DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF);
}

bool legacy_crtc_set_cursor(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, struct gbm_bo *bo) {
	if (!crtc || !crtc->cursor) {
		return true;
	}

	if (!bo) {
		drmModeSetCursor(drm->fd, crtc->id, 0, 0, 0);
		return true;
	}

	struct wlr_drm_plane *plane = crtc->cursor;

	if (drmModeSetCursor(drm->fd, crtc->id, gbm_bo_get_handle(bo).u32,
			plane->surf.width, plane->surf.height)) {
		wlr_log_errno(L_ERROR, "Failed to set hardware cursor");
		return false;
	}

	return true;
}

bool legacy_crtc_move_cursor(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, int x, int y) {
	return !drmModeMoveCursor(drm->fd, crtc->id, x, y);
}

const struct wlr_drm_interface legacy_iface = {
	.conn_enable = legacy_conn_enable,
	.crtc_pageflip = legacy_crtc_pageflip,
	.crtc_set_cursor = legacy_crtc_set_cursor,
	.crtc_move_cursor = legacy_crtc_move_cursor,
};
