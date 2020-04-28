#include <gbm.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "backend/drm/drm.h"
#include "backend/drm/iface.h"
#include "backend/drm/util.h"

bool legacy_crtc_pageflip(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, drmModeModeInfo *mode) {
	struct wlr_drm_crtc *crtc = conn->crtc;
	struct wlr_drm_fb *fb = plane_get_next_fb(crtc->primary);
	struct gbm_bo *bo = drm_fb_acquire(fb, drm, &crtc->primary->mgpu_surf);
	if (!bo) {
		return false;
	}

	uint32_t fb_id = get_fb_for_bo(bo, drm->addfb2_modifiers);
	if (!fb_id) {
		return false;
	}

	if (mode) {
		if (drmModeSetCrtc(drm->fd, crtc->id, fb_id, 0, 0,
				&conn->id, 1, mode)) {
			wlr_log_errno(WLR_ERROR, "%s: Failed to set CRTC", conn->output.name);
			return false;
		}
	}
	uint32_t flags = DRM_MODE_PAGE_FLIP_EVENT;
	if (conn->output.present_mode == WLR_OUTPUT_PRESENT_MODE_IMMEDIATE)
		flags |= DRM_MODE_PAGE_FLIP_ASYNC;
	if (drmModePageFlip(drm->fd, crtc->id, fb_id, flags, drm)) {
		wlr_log_errno(WLR_ERROR, "%s: Failed to page flip", conn->output.name);
		return false;
	}

	return true;
}

static bool legacy_conn_enable(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, bool enable) {
	int ret = drmModeConnectorSetProperty(drm->fd, conn->id, conn->props.dpms,
		enable ? DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF);

	if (!enable) {
		drmModeSetCrtc(drm->fd, conn->crtc->id, 0, 0, 0, NULL, 0,
					   NULL);
	}

	return ret >= 0;
}

bool legacy_crtc_set_cursor(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, struct gbm_bo *bo) {
	if (!crtc || !crtc->cursor) {
		return true;
	}

	if (!bo) {
		if (drmModeSetCursor(drm->fd, crtc->id, 0, 0, 0)) {
			wlr_log_errno(WLR_DEBUG, "Failed to clear hardware cursor");
			return false;
		}
		return true;
	}

	struct wlr_drm_plane *plane = crtc->cursor;

	if (drmModeSetCursor(drm->fd, crtc->id, gbm_bo_get_handle(bo).u32,
			plane->surf.width, plane->surf.height)) {
		wlr_log_errno(WLR_DEBUG, "Failed to set hardware cursor");
		return false;
	}

	drm_fb_move(&crtc->cursor->queued_fb, &crtc->cursor->pending_fb);
	return true;
}

bool legacy_crtc_move_cursor(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, int x, int y) {
	return !drmModeMoveCursor(drm->fd, crtc->id, x, y);
}

static bool legacy_crtc_set_gamma(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, size_t size,
		uint16_t *r, uint16_t *g, uint16_t *b) {
	return !drmModeCrtcSetGamma(drm->fd, crtc->id, (uint32_t)size, r, g, b);
}

static size_t legacy_crtc_get_gamma_size(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc) {
	return (size_t)crtc->legacy_crtc->gamma_size;
}

const struct wlr_drm_interface legacy_iface = {
	.conn_enable = legacy_conn_enable,
	.crtc_pageflip = legacy_crtc_pageflip,
	.crtc_set_cursor = legacy_crtc_set_cursor,
	.crtc_move_cursor = legacy_crtc_move_cursor,
	.crtc_set_gamma = legacy_crtc_set_gamma,
	.crtc_get_gamma_size = legacy_crtc_get_gamma_size,
};
