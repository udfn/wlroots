#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <time.h>
#include <gbm.h>
#include <stdlib.h>
#include <wlr/util/log.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include "backend/drm/drm.h"
#include "backend/drm/iface.h"
#include "backend/drm/util.h"

bool drm_legacy_crtc_commit(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, uint32_t flags) {
	struct wlr_output *output = &conn->output;
	struct wlr_drm_crtc *crtc = conn->crtc;

	uint32_t fb_id = 0;
	if (crtc->pending.active) {
		struct wlr_drm_fb *fb = plane_get_next_fb(crtc->primary);
		struct gbm_bo *bo = drm_fb_acquire(fb, drm, &crtc->primary->mgpu_surf);
		if (!bo) {
			return false;
		}

		fb_id = get_fb_for_bo(bo, drm->addfb2_modifiers);
		if (!fb_id) {
			return false;
		}
	}
	switch (conn->output.present_mode) {
		case WLR_OUTPUT_PRESENT_MODE_IMMEDIATE:
			flags |= DRM_MODE_PAGE_FLIP_ASYNC;
			break;
		case WLR_OUTPUT_PRESENT_MODE_ADAPTIVE: {
			struct timespec cur;
			clock_gettime(CLOCK_MONOTONIC, &cur);
			// I'm sure this messy logic can be simplified..
			if (cur.tv_sec == conn->next_present.tv_sec) {
				if (cur.tv_nsec > conn->next_present.tv_nsec) {
					// rats, we missed vblank, flip immediately!
					flags |= DRM_MODE_PAGE_FLIP_ASYNC;
				}
			} else if (cur.tv_sec > conn->next_present.tv_sec) {
				flags |= DRM_MODE_PAGE_FLIP_ASYNC;
			}
		}
	}

	if (crtc->pending_modeset) {
		uint32_t *conns = NULL;
		size_t conns_len = 0;
		drmModeModeInfo *mode = NULL;
		if (crtc->pending.active) {
			conns = &conn->id;
			conns_len = 1;
			mode = &crtc->pending.mode->drm_mode;
		}

		uint32_t dpms = crtc->pending.active ?
			DRM_MODE_DPMS_ON : DRM_MODE_DPMS_OFF;
		if (drmModeConnectorSetProperty(drm->fd, conn->id, conn->props.dpms,
				dpms) != 0) {
			wlr_drm_conn_log_errno(conn, WLR_ERROR,
				"Failed to set DPMS property");
			return false;
		}

		if (drmModeSetCrtc(drm->fd, crtc->id, fb_id, 0, 0,
				conns, conns_len, mode)) {
			wlr_drm_conn_log_errno(conn, WLR_ERROR, "Failed to set CRTC");
			return false;
		}
	}

	if (output->pending.committed & WLR_OUTPUT_STATE_GAMMA_LUT) {
		if (!drm_legacy_crtc_set_gamma(drm, crtc,
				output->pending.gamma_lut_size, output->pending.gamma_lut)) {
			return false;
		}
	}
	if ((output->pending.committed & WLR_OUTPUT_STATE_ADAPTIVE_SYNC_ENABLED) &&
			drm_connector_supports_vrr(conn)) {
		if (drmModeObjectSetProperty(drm->fd, crtc->id, DRM_MODE_OBJECT_CRTC,
				crtc->props.vrr_enabled,
				output->pending.adaptive_sync_enabled) != 0) {
			wlr_drm_conn_log_errno(conn, WLR_ERROR,
				"drmModeObjectSetProperty(VRR_ENABLED) failed");
			return false;
		}
		output->adaptive_sync_status = output->pending.adaptive_sync_enabled ?
			WLR_OUTPUT_ADAPTIVE_SYNC_ENABLED :
			WLR_OUTPUT_ADAPTIVE_SYNC_DISABLED;
		wlr_drm_conn_log(conn, WLR_DEBUG, "VRR %s",
			output->pending.adaptive_sync_enabled ? "enabled" : "disabled");
	}

	if (flags & DRM_MODE_PAGE_FLIP_EVENT) {
		if (drmModePageFlip(drm->fd, crtc->id, fb_id,
				flags, drm)) {
			wlr_drm_conn_log_errno(conn, WLR_ERROR, "drmModePageFlip failed");
			return false;
		}
	}

	return true;
}

bool drm_legacy_crtc_move_cursor(struct wlr_drm_backend *drm,
	struct wlr_drm_crtc *crtc, int x, int y) {
	return !drmModeMoveCursor(drm->fd,crtc->id,x,y);
}

bool drm_legacy_crtc_set_cursor(struct wlr_drm_backend *drm,
	struct wlr_drm_crtc *crtc, struct gbm_bo *bo) {
	if (!bo) {
		return !drmModeSetCursor(drm->fd,crtc->id,0,0,0);
	}
	struct wlr_drm_plane *cursor = crtc->cursor;
	struct wlr_drm_fb *cursor_fb = plane_get_next_fb(cursor);
	struct gbm_bo *cursor_bo = drm_fb_acquire(cursor_fb, drm, &cursor->mgpu_surf);
	if (!cursor_bo) {
		return false;
	}
	return !drmModeSetCursor(drm->fd,crtc->id,gbm_bo_get_handle(bo).u32,cursor->surf.width,cursor->surf.height);
}

static void fill_empty_gamma_table(size_t size,
		uint16_t *r, uint16_t *g, uint16_t *b) {
	assert(0xFFFF < UINT64_MAX / (size - 1));
	for (uint32_t i = 0; i < size; ++i) {
		uint16_t val = (uint64_t)0xFFFF * i / (size - 1);
		r[i] = g[i] = b[i] = val;
	}
}

bool drm_legacy_crtc_set_gamma(struct wlr_drm_backend *drm,
		struct wlr_drm_crtc *crtc, size_t size, uint16_t *lut) {
	uint16_t *linear_lut = NULL;
	if (size == 0) {
		// The legacy interface doesn't offer a way to reset the gamma LUT
		size = drm_crtc_get_gamma_lut_size(drm, crtc);
		if (size == 0) {
			return false;
		}

		linear_lut = malloc(3 * size * sizeof(uint16_t));
		if (linear_lut == NULL) {
			wlr_log_errno(WLR_ERROR, "Allocation failed");
			return false;
		}
		fill_empty_gamma_table(size, linear_lut, linear_lut + size,
			linear_lut + 2 * size);

		lut = linear_lut;
	}

	uint16_t *r = lut, *g = lut + size, *b = lut + 2 * size;
	if (drmModeCrtcSetGamma(drm->fd, crtc->id, size, r, g, b) != 0) {
		wlr_log_errno(WLR_ERROR, "Failed to set gamma LUT on CRTC %"PRIu32,
			crtc->id);
		return false;
	}

	free(linear_lut);
	return true;
}

const struct wlr_drm_interface legacy_iface = {
	.crtc_commit = drm_legacy_crtc_commit,
};
