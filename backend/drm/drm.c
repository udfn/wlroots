#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <errno.h>
#include <time.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_mode.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <gbm.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <wlr/backend/interface.h>
#include <wlr/interfaces/wlr_output.h>
#include <wlr/util/log.h>
#include <wlr/util/signal.h>
#include <wlr/render/matrix.h>
#include <wlr/render/gles2.h>
#include <wlr/render.h>
#include "backend/drm/drm.h"
#include "backend/drm/iface.h"
#include "backend/drm/util.h"

bool wlr_drm_check_features(struct wlr_drm_backend *drm) {
	if (drmSetClientCap(drm->fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1)) {
		wlr_log(L_ERROR, "DRM universal planes unsupported");
		return false;
	}

	if (getenv("WLR_DRM_NO_ATOMIC")) {
		wlr_log(L_DEBUG, "WLR_DRM_NO_ATOMIC set, forcing legacy DRM interface");
		drm->iface = &legacy_iface;
	} else if (drmSetClientCap(drm->fd, DRM_CLIENT_CAP_ATOMIC, 1)) {
		wlr_log(L_DEBUG, "Atomic modesetting unsupported, using legacy DRM interface");
		drm->iface = &legacy_iface;
	} else {
		wlr_log(L_DEBUG, "Using atomic DRM interface");
		drm->iface = &atomic_iface;
	}

	return true;
}

static int cmp_plane(const void *arg1, const void *arg2) {
	const struct wlr_drm_plane *a = arg1;
	const struct wlr_drm_plane *b = arg2;

	return (int)a->type - (int)b->type;
}

static bool init_planes(struct wlr_drm_backend *drm) {
	drmModePlaneRes *plane_res = drmModeGetPlaneResources(drm->fd);
	if (!plane_res) {
		wlr_log_errno(L_ERROR, "Failed to get DRM plane resources");
		return false;
	}

	wlr_log(L_INFO, "Found %"PRIu32" DRM planes", plane_res->count_planes);

	if (plane_res->count_planes == 0) {
		drmModeFreePlaneResources(plane_res);
		return true;
	}

	drm->num_planes = plane_res->count_planes;
	drm->planes = calloc(drm->num_planes, sizeof(*drm->planes));
	if (!drm->planes) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		goto error_res;
	}

	for (size_t i = 0; i < drm->num_planes; ++i) {
		struct wlr_drm_plane *p = &drm->planes[i];

		drmModePlane *plane = drmModeGetPlane(drm->fd, plane_res->planes[i]);
		if (!plane) {
			wlr_log_errno(L_ERROR, "Failed to get DRM plane");
			goto error_planes;
		}

		p->id = plane->plane_id;
		p->possible_crtcs = plane->possible_crtcs;
		uint64_t type;

		if (!wlr_drm_get_plane_props(drm->fd, p->id, &p->props) ||
				!wlr_drm_get_prop(drm->fd, p->id, p->props.type, &type)) {
			drmModeFreePlane(plane);
			goto error_planes;
		}

		p->type = type;
		drm->num_type_planes[type]++;

		drmModeFreePlane(plane);
	}

	wlr_log(L_INFO, "(%zu overlay, %zu primary, %zu cursor)",
		drm->num_overlay_planes,
		drm->num_primary_planes,
		drm->num_cursor_planes);

	qsort(drm->planes, drm->num_planes, sizeof(*drm->planes), cmp_plane);

	drm->overlay_planes = drm->planes;
	drm->primary_planes = drm->overlay_planes
		+ drm->num_overlay_planes;
	drm->cursor_planes = drm->primary_planes
		+ drm->num_primary_planes;

	drmModeFreePlaneResources(plane_res);
	return true;

error_planes:
	free(drm->planes);
error_res:
	drmModeFreePlaneResources(plane_res);
	return false;
}

bool wlr_drm_resources_init(struct wlr_drm_backend *drm) {
	drmModeRes *res = drmModeGetResources(drm->fd);
	if (!res) {
		wlr_log_errno(L_ERROR, "Failed to get DRM resources");
		return false;
	}

	wlr_log(L_INFO, "Found %d DRM CRTCs", res->count_crtcs);

	drm->num_crtcs = res->count_crtcs;
	drm->crtcs = calloc(drm->num_crtcs, sizeof(drm->crtcs[0]));
	if (!drm->crtcs) {
		wlr_log_errno(L_ERROR, "Allocation failed");
		goto error_res;
	}

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		struct wlr_drm_crtc *crtc = &drm->crtcs[i];
		crtc->id = res->crtcs[i];
		crtc->legacy_crtc = drmModeGetCrtc(drm->fd, crtc->id);
		wlr_drm_get_crtc_props(drm->fd, crtc->id, &crtc->props);
	}

	if (!init_planes(drm)) {
		goto error_crtcs;
	}

	drmModeFreeResources(res);

	return true;

error_crtcs:
	free(drm->crtcs);
error_res:
	drmModeFreeResources(res);
	return false;
}

void wlr_drm_resources_free(struct wlr_drm_backend *drm) {
	if (!drm) {
		return;
	}

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		struct wlr_drm_crtc *crtc = &drm->crtcs[i];
		drmModeAtomicFree(crtc->atomic);
		drmModeFreeCrtc(crtc->legacy_crtc);
		if (crtc->mode_id) {
			drmModeDestroyPropertyBlob(drm->fd, crtc->mode_id);
		}
		if (crtc->gamma_lut) {
			drmModeDestroyPropertyBlob(drm->fd, crtc->gamma_lut);
		}
	}
	for (size_t i = 0; i < drm->num_planes; ++i) {
		struct wlr_drm_plane *plane = &drm->planes[i];
		if (plane->cursor_bo) {
			gbm_bo_destroy(plane->cursor_bo);
		}
		if (plane->wlr_tex) {
			wlr_texture_destroy(plane->wlr_tex);
		}
	}

	free(drm->crtcs);
	free(drm->planes);
}

static bool wlr_drm_connector_make_current(struct wlr_output *output,
		int *buffer_age) {
	struct wlr_drm_connector *conn = (struct wlr_drm_connector *)output;
	return wlr_drm_surface_make_current(&conn->crtc->primary->surf, buffer_age);
}

static bool wlr_drm_connector_swap_buffers(struct wlr_output *output,
		pixman_region32_t *damage) {
	struct wlr_drm_connector *conn = (struct wlr_drm_connector *)output;
	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)output->backend;
	if (!drm->session->active) {
		return false;
	}

	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		return false;
	}
	struct wlr_drm_plane *plane = crtc->primary;

	struct gbm_bo *bo = wlr_drm_surface_swap_buffers(&plane->surf, damage);
	if (drm->parent) {
		bo = wlr_drm_surface_mgpu_copy(&plane->mgpu_surf, bo);
	}
	uint32_t fb_id = get_fb_for_bo(bo);

	if (conn->pageflip_pending) {
		wlr_log(L_ERROR, "Skipping pageflip");
		return false;
	}

	if (!drm->iface->crtc_pageflip(drm, conn, crtc, fb_id, NULL)) {
		return false;
	}

	conn->pageflip_pending = true;
	wlr_output_update_enabled(output, true);
	return true;
}

static void wlr_drm_connector_set_gamma(struct wlr_output *output,
		uint32_t size, uint16_t *r, uint16_t *g, uint16_t *b) {
	struct wlr_drm_connector *conn = (struct wlr_drm_connector *)output;
	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)output->backend;
	bool ok;

	if (conn->crtc) {
		ok = drm->iface->crtc_set_gamma(drm, conn->crtc, r, g, b, size);
		if (ok) {
			wlr_output_update_needs_swap(output);
		}
	}

}

static uint32_t wlr_drm_connector_get_gamma_size(struct wlr_output *output) {
	struct wlr_drm_connector *conn = (struct wlr_drm_connector *)output;
	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)output->backend;

	if (conn->crtc) {
		return drm->iface->crtc_get_gamma_size(drm, conn->crtc);
	}

	return 0;
}

void wlr_drm_connector_start_renderer(struct wlr_drm_connector *conn) {
	if (conn->state != WLR_DRM_CONN_CONNECTED) {
		return;
	}

	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)conn->output.backend;
	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		return;
	}
	struct wlr_drm_plane *plane = crtc->primary;

	struct gbm_bo *bo = wlr_drm_surface_get_front(
		drm->parent ? &plane->mgpu_surf : &plane->surf);
	uint32_t fb_id = get_fb_for_bo(bo);

	struct wlr_drm_mode *mode = (struct wlr_drm_mode *)conn->output.current_mode;
	if (drm->iface->crtc_pageflip(drm, conn, crtc, fb_id, &mode->drm_mode)) {
		conn->pageflip_pending = true;
		wlr_output_update_enabled(&conn->output, true);
	} else {
		wl_event_source_timer_update(conn->retry_pageflip,
			1000000.0f / conn->output.current_mode->refresh);
	}
}

static void wlr_drm_connector_enable(struct wlr_output *output, bool enable) {
	struct wlr_drm_connector *conn = (struct wlr_drm_connector *)output;
	if (conn->state != WLR_DRM_CONN_CONNECTED) {
		return;
	}

	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)output->backend;
	bool ok = drm->iface->conn_enable(drm, conn, enable);
	if (!ok) {
		return;
	}

	if (enable) {
		wlr_drm_connector_start_renderer(conn);
	}

	wlr_output_update_enabled(&conn->output, enable);
}

static void realloc_planes(struct wlr_drm_backend *drm, const uint32_t *crtc_in,
		bool *changed_outputs) {
	// overlay, primary, cursor
	for (int type = 0; type < 3; ++type) {
		if (drm->num_type_planes[type] == 0) {
			continue;
		}

		uint32_t possible[drm->num_type_planes[type]];
		uint32_t crtc[drm->num_crtcs];
		uint32_t crtc_res[drm->num_crtcs];

		for (size_t i = 0; i < drm->num_type_planes[type]; ++i) {
			possible[i] = drm->type_planes[type][i].possible_crtcs;
		}

		for (size_t i = 0; i < drm->num_crtcs; ++i) {
			if (crtc_in[i] == UNMATCHED) {
				crtc[i] = SKIP;
			} else if (drm->crtcs[i].planes[type]) {
				crtc[i] = drm->crtcs[i].planes[type]
					- drm->type_planes[type];
			} else {
				crtc[i] = UNMATCHED;
			}
		}

		match_obj(drm->num_type_planes[type], possible,
			drm->num_crtcs, crtc, crtc_res);

		for (size_t i = 0; i < drm->num_crtcs; ++i) {
			if (crtc_res[i] == UNMATCHED || crtc_res[i] == SKIP) {
				continue;
			}

			struct wlr_drm_crtc *c = &drm->crtcs[i];
			struct wlr_drm_plane **old = &c->planes[type];
			struct wlr_drm_plane *new = &drm->type_planes[type][crtc_res[i]];

			if (*old != new) {
				changed_outputs[crtc_res[i]] = true;
				if (*old) {
					wlr_drm_surface_finish(&(*old)->surf);
				}
				wlr_drm_surface_finish(&new->surf);
				*old = new;
			}
		}
	}
}

static void realloc_crtcs(struct wlr_drm_backend *drm,
		struct wlr_drm_connector *conn, bool *changed_outputs) {
	uint32_t crtc[drm->num_crtcs];
	uint32_t crtc_res[drm->num_crtcs];
	ssize_t num_outputs = wl_list_length(&drm->outputs);
	uint32_t possible_crtc[num_outputs];

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		crtc[i] = UNMATCHED;
	}

	memset(possible_crtc, 0, sizeof(possible_crtc));

	ssize_t index = -1, i = -1;
	struct wlr_drm_connector *c;
	wl_list_for_each(c, &drm->outputs, link) {
		i++;
		if (c == conn) {
			index = i;
		}

		if (c->crtc) {
			crtc[c->crtc - drm->crtcs] = i;
		}

		if (c->state == WLR_DRM_CONN_CONNECTED) {
			possible_crtc[i] = c->possible_crtc;
		}

	}
	assert(index != -1);

	possible_crtc[index] = conn->possible_crtc;
	match_obj(wl_list_length(&drm->outputs), possible_crtc,
		drm->num_crtcs, crtc, crtc_res);

	bool matched[num_outputs];
	memset(matched, false, sizeof(matched));
	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		if (crtc_res[i] != UNMATCHED) {
			matched[crtc_res[i]] = true;
		}
	}

	// There is no point doing anything if this monitor doesn't get activated
	if (!matched[index]) {
		return;
	}

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		// We don't want any of the current monitors to be deactivated.
		if (crtc[i] != UNMATCHED && !matched[crtc[i]]) {
			return;
		}
	}

	changed_outputs[index] = true;

	for (size_t i = 0; i < drm->num_crtcs; ++i) {
		if (crtc_res[i] == UNMATCHED) {
			continue;
		}

		if (crtc_res[i] != crtc[i]) {
			changed_outputs[crtc_res[i]] = true;
			struct wlr_drm_connector *c;
			size_t pos = 0;
			wl_list_for_each(c, &drm->outputs, link) {
				if (pos == crtc_res[i]) {
					break;
				}
				pos++;
			}
			c->crtc = &drm->crtcs[i];
		}
	}

	realloc_planes(drm, crtc_res, changed_outputs);
}

static uint32_t get_possible_crtcs(int fd, uint32_t conn_id) {
	drmModeConnector *conn = drmModeGetConnector(fd, conn_id);
	if (!conn) {
		wlr_log_errno(L_ERROR, "Failed to get DRM connector");
		return 0;
	}

	if (conn->connection != DRM_MODE_CONNECTED || conn->count_modes == 0) {
		wlr_log(L_ERROR, "Output is not connected");
		goto error_conn;
	}

	drmModeEncoder *enc = NULL;
	for (int i = 0; !enc && i < conn->count_encoders; ++i) {
		enc = drmModeGetEncoder(fd, conn->encoders[i]);
	}

	if (!enc) {
		wlr_log(L_ERROR, "Failed to get DRM encoder");
		goto error_conn;
	}

	uint32_t ret = enc->possible_crtcs;
	drmModeFreeEncoder(enc);
	drmModeFreeConnector(conn);
	return ret;

error_conn:
	drmModeFreeConnector(conn);
	return 0;
}

static bool wlr_drm_connector_set_mode(struct wlr_output *output,
		struct wlr_output_mode *mode) {
	struct wlr_drm_connector *conn = (struct wlr_drm_connector *)output;
	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)output->backend;
	bool changed_outputs[wl_list_length(&drm->outputs)];

	wlr_log(L_INFO, "Modesetting '%s' with '%ux%u@%u mHz'", conn->output.name,
			mode->width, mode->height, mode->refresh);

	conn->possible_crtc = get_possible_crtcs(drm->fd, conn->id);
	if (conn->possible_crtc == 0) {
		goto error_conn;
	}

	memset(changed_outputs, false, sizeof(changed_outputs));
	realloc_crtcs(drm, conn, changed_outputs);

	if (!conn->crtc) {
		wlr_log(L_ERROR, "Unable to match %s with a CRTC", conn->output.name);
		goto error_conn;
	}

	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		return false;
	}
	wlr_log(L_DEBUG, "%s: crtc=%td ovr=%td pri=%td cur=%td", conn->output.name,
		crtc - drm->crtcs,
		crtc->overlay ? crtc->overlay - drm->overlay_planes : -1,
		crtc->primary ? crtc->primary - drm->primary_planes : -1,
		crtc->cursor ? crtc->cursor - drm->cursor_planes : -1);

	conn->state = WLR_DRM_CONN_CONNECTED;
	wlr_output_update_mode(&conn->output, mode);

	// Since realloc_crtcs can deallocate planes on OTHER outputs,
	// we actually need to reinitialize any than has changed
	ssize_t output_index = -1;
	wl_list_for_each(conn, &drm->outputs, link) {
		output_index += 1;
		struct wlr_output_mode *mode = conn->output.current_mode;
		struct wlr_drm_crtc *crtc = conn->crtc;

		if (conn->state != WLR_DRM_CONN_CONNECTED ||
				!changed_outputs[output_index]) {
			continue;
		}

		if (!wlr_drm_plane_surfaces_init(crtc->primary, drm,
				mode->width, mode->height, GBM_FORMAT_XRGB8888)) {
			wlr_log(L_ERROR, "Failed to initialize renderer for plane");
			goto error_conn;
		}

		wlr_drm_connector_start_renderer(conn);
	}

	return true;

error_conn:
	wlr_drm_connector_cleanup(conn);
	return false;
}

static void wlr_drm_connector_transform(struct wlr_output *output,
		enum wl_output_transform transform) {
	output->transform = transform;
}

static bool wlr_drm_connector_set_cursor(struct wlr_output *output,
		const uint8_t *buf, int32_t stride, uint32_t width, uint32_t height,
		int32_t hotspot_x, int32_t hotspot_y, bool update_pixels) {
	struct wlr_drm_connector *conn = (struct wlr_drm_connector *)output;
	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)output->backend;
	struct wlr_drm_renderer *renderer = &drm->renderer;

	struct wlr_drm_crtc *crtc = conn->crtc;
	if (!crtc) {
		return false;
	}
	struct wlr_drm_plane *plane = crtc->cursor;

	// We don't have a real cursor plane, so we make a fake one
	if (!plane) {
		plane = calloc(1, sizeof(*plane));
		if (!plane) {
			wlr_log_errno(L_ERROR, "Allocation failed");
			return false;
		}
		crtc->cursor = plane;
	}

	if (!buf && update_pixels) {
		// Hide the cursor
		plane->cursor_enabled = false;
		if (!drm->session->active) {
			return true;
		}
		return drm->iface->crtc_set_cursor(drm, crtc, NULL);
	}
	plane->cursor_enabled = true;

	if (!plane->surf.gbm) {
		int ret;
		uint64_t w, h;
		ret = drmGetCap(drm->fd, DRM_CAP_CURSOR_WIDTH, &w);
		w = ret ? 64 : w;
		ret = drmGetCap(drm->fd, DRM_CAP_CURSOR_HEIGHT, &h);
		h = ret ? 64 : h;

		if (width > w || height > h) {
			wlr_log(L_INFO, "Cursor too large (max %dx%d)", (int)w, (int)h);
			return false;
		}

		if (!wlr_drm_surface_init(&plane->surf, renderer, w, h,
				GBM_FORMAT_ARGB8888, 0)) {
			wlr_log(L_ERROR, "Cannot allocate cursor resources");
			return false;
		}

		plane->cursor_bo = gbm_bo_create(renderer->gbm, w, h,
			GBM_FORMAT_ARGB8888, GBM_BO_USE_CURSOR | GBM_BO_USE_WRITE);
		if (!plane->cursor_bo) {
			wlr_log_errno(L_ERROR, "Failed to create cursor bo");
			return false;
		}

		// OpenGL will read the pixels out upside down,
		// so we need to flip the image vertically
		enum wl_output_transform transform = wlr_output_transform_compose(
			wlr_output_transform_invert(output->transform),
			WL_OUTPUT_TRANSFORM_FLIPPED_180);
		wlr_matrix_texture(plane->matrix, plane->surf.width, plane->surf.height,
			transform);

		plane->wlr_tex =
			wlr_render_texture_create(plane->surf.renderer->wlr_rend);
		if (!plane->wlr_tex) {
			return false;
		}
	}

	struct wlr_box hotspot = {
		.x = hotspot_x,
		.y = hotspot_y,
	};
	enum wl_output_transform transform =
		wlr_output_transform_invert(output->transform);
	wlr_box_transform(&hotspot, transform,
		plane->surf.width, plane->surf.height, &hotspot);
	plane->cursor_hotspot_x = hotspot.x;
	plane->cursor_hotspot_y = hotspot.y;

	if (!update_pixels) {
		// Only update the cursor hotspot
		return true;
	}

	struct gbm_bo *bo = plane->cursor_bo;
	uint32_t bo_width = gbm_bo_get_width(bo);
	uint32_t bo_height = gbm_bo_get_height(bo);
	uint32_t bo_stride;
	void *bo_data;

	if (!gbm_bo_map(bo, 0, 0, bo_width, bo_height,
			GBM_BO_TRANSFER_WRITE, &bo_stride, &bo_data)) {
		wlr_log_errno(L_ERROR, "Unable to map buffer");
		return false;
	}

	wlr_drm_surface_make_current(&plane->surf, NULL);

	wlr_texture_upload_pixels(plane->wlr_tex, WL_SHM_FORMAT_ARGB8888,
		stride, width, height, buf);

	glViewport(0, 0, plane->surf.width, plane->surf.height);
	glClearColor(0.0, 0.0, 0.0, 0.0);
	glClear(GL_COLOR_BUFFER_BIT);

	float matrix[16];
	wlr_texture_get_matrix(plane->wlr_tex, &matrix, &plane->matrix, 0, 0);
	wlr_render_with_matrix(plane->surf.renderer->wlr_rend, plane->wlr_tex,
		&matrix);

	glFinish();
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, bo_stride);
	glReadPixels(0, 0, plane->surf.width, plane->surf.height, GL_BGRA_EXT,
		GL_UNSIGNED_BYTE, bo_data);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);

	wlr_drm_surface_swap_buffers(&plane->surf, NULL);

	gbm_bo_unmap(bo, bo_data);

	if (!drm->session->active) {
		return true;
	}

	bool ok = drm->iface->crtc_set_cursor(drm, crtc, bo);
	if (ok) {
		wlr_output_update_needs_swap(output);
	}
	return ok;
}

static bool wlr_drm_connector_move_cursor(struct wlr_output *output,
		int x, int y) {
	struct wlr_drm_connector *conn = (struct wlr_drm_connector *)output;
	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)output->backend;
	if (!conn->crtc) {
		return false;
	}
	struct wlr_drm_plane *plane = conn->crtc->cursor;

	struct wlr_box box = { .x = x, .y = y };

	int width, height;
	wlr_output_transformed_resolution(output, &width, &height);

	enum wl_output_transform transform =
		wlr_output_transform_invert(output->transform);
	wlr_box_transform(&box, transform, width, height, &box);

	if (plane != NULL) {
		box.x -= plane->cursor_hotspot_x;
		box.y -= plane->cursor_hotspot_y;
	}

	conn->cursor_x = box.x;
	conn->cursor_y = box.y;

	if (!drm->session->active) {
		return true;
	}

	bool ok = drm->iface->crtc_move_cursor(drm, conn->crtc, box.x, box.y);
	if (ok) {
		wlr_output_update_needs_swap(output);
	}
	return ok;
}

static void wlr_drm_connector_destroy(struct wlr_output *output) {
	struct wlr_drm_connector *conn = (struct wlr_drm_connector *)output;
	wlr_drm_connector_cleanup(conn);
	wl_event_source_remove(conn->retry_pageflip);
	wl_list_remove(&conn->link);
	free(conn);
}

static struct wlr_output_impl output_impl = {
	.enable = wlr_drm_connector_enable,
	.set_mode = wlr_drm_connector_set_mode,
	.transform = wlr_drm_connector_transform,
	.set_cursor = wlr_drm_connector_set_cursor,
	.move_cursor = wlr_drm_connector_move_cursor,
	.destroy = wlr_drm_connector_destroy,
	.make_current = wlr_drm_connector_make_current,
	.swap_buffers = wlr_drm_connector_swap_buffers,
	.set_gamma = wlr_drm_connector_set_gamma,
	.get_gamma_size = wlr_drm_connector_get_gamma_size,
};

bool wlr_output_is_drm(struct wlr_output *output) {
	return output->impl == &output_impl;
}

static int retry_pageflip(void *data) {
	struct wlr_drm_connector *conn = data;
	wlr_log(L_INFO, "%s: Retrying pageflip", conn->output.name);
	wlr_drm_connector_start_renderer(conn);
	return 0;
}

static const int32_t subpixel_map[] = {
	[DRM_MODE_SUBPIXEL_UNKNOWN] = WL_OUTPUT_SUBPIXEL_UNKNOWN,
	[DRM_MODE_SUBPIXEL_HORIZONTAL_RGB] = WL_OUTPUT_SUBPIXEL_HORIZONTAL_RGB,
	[DRM_MODE_SUBPIXEL_HORIZONTAL_BGR] = WL_OUTPUT_SUBPIXEL_HORIZONTAL_BGR,
	[DRM_MODE_SUBPIXEL_VERTICAL_RGB] = WL_OUTPUT_SUBPIXEL_VERTICAL_RGB,
	[DRM_MODE_SUBPIXEL_VERTICAL_BGR] = WL_OUTPUT_SUBPIXEL_VERTICAL_BGR,
	[DRM_MODE_SUBPIXEL_NONE] = WL_OUTPUT_SUBPIXEL_NONE,
};

void wlr_drm_scan_connectors(struct wlr_drm_backend *drm) {
	wlr_log(L_INFO, "Scanning DRM connectors");

	drmModeRes *res = drmModeGetResources(drm->fd);
	if (!res) {
		wlr_log_errno(L_ERROR, "Failed to get DRM resources");
		return;
	}

	size_t seen_len = wl_list_length(&drm->outputs);
	// +1 so length can never be 0, which is undefined behaviour.
	// Last element isn't used.
	bool seen[seen_len + 1];
	memset(seen, 0, sizeof(seen));

	for (int i = 0; i < res->count_connectors; ++i) {
		drmModeConnector *drm_conn = drmModeGetConnector(drm->fd,
			res->connectors[i]);
		if (!drm_conn) {
			wlr_log_errno(L_ERROR, "Failed to get DRM connector");
			continue;
		}
		drmModeEncoder *curr_enc = drmModeGetEncoder(drm->fd,
			drm_conn->encoder_id);

		int index = -1;
		struct wlr_drm_connector *c, *wlr_conn = NULL;
		wl_list_for_each(c, &drm->outputs, link) {
			index++;
			if (c->id == drm_conn->connector_id) {
				wlr_conn = c;
				break;
			}
		}

		if (!wlr_conn) {
			wlr_conn = calloc(1, sizeof(*wlr_conn));
			if (!wlr_conn) {
				wlr_log_errno(L_ERROR, "Allocation failed");
				drmModeFreeEncoder(curr_enc);
				drmModeFreeConnector(drm_conn);
				continue;
			}
			wlr_output_init(&wlr_conn->output, &drm->backend, &output_impl,
				drm->display);

			struct wl_event_loop *ev = wl_display_get_event_loop(drm->display);
			wlr_conn->retry_pageflip = wl_event_loop_add_timer(ev, retry_pageflip,
				wlr_conn);


			wlr_conn->state = WLR_DRM_CONN_DISCONNECTED;
			wlr_conn->id = drm_conn->connector_id;

			if (curr_enc) {
				wlr_conn->old_crtc = drmModeGetCrtc(drm->fd, curr_enc->crtc_id);
			}

			snprintf(wlr_conn->output.name, sizeof(wlr_conn->output.name),
				"%s-%"PRIu32,
				 conn_get_name(drm_conn->connector_type),
				 drm_conn->connector_type_id);

			wl_list_insert(&drm->outputs, &wlr_conn->link);
			wlr_log(L_INFO, "Found display '%s'", wlr_conn->output.name);
		} else {
			seen[index] = true;
		}

		if (curr_enc) {
			for (size_t i = 0; i < drm->num_crtcs; ++i) {
				if (drm->crtcs[i].id == curr_enc->crtc_id) {
					wlr_conn->crtc = &drm->crtcs[i];
					break;
				}
			}
		} else {
			wlr_conn->crtc = NULL;
		}

		if (wlr_conn->state == WLR_DRM_CONN_DISCONNECTED &&
				drm_conn->connection == DRM_MODE_CONNECTED) {
			wlr_log(L_INFO, "'%s' connected", wlr_conn->output.name);

			wlr_conn->output.phys_width = drm_conn->mmWidth;
			wlr_conn->output.phys_height = drm_conn->mmHeight;
			wlr_log(L_INFO, "Physical size: %"PRId32"x%"PRId32,
				wlr_conn->output.phys_width, wlr_conn->output.phys_height);
			wlr_conn->output.subpixel = subpixel_map[drm_conn->subpixel];

			wlr_drm_get_connector_props(drm->fd, wlr_conn->id, &wlr_conn->props);

			size_t edid_len = 0;
			uint8_t *edid = wlr_drm_get_prop_blob(drm->fd,
				wlr_conn->id, wlr_conn->props.edid, &edid_len);
			parse_edid(&wlr_conn->output, edid_len, edid);
			free(edid);

			wlr_log(L_INFO, "Detected modes:");

			for (int i = 0; i < drm_conn->count_modes; ++i) {
				struct wlr_drm_mode *mode = calloc(1, sizeof(*mode));
				if (!mode) {
					wlr_log_errno(L_ERROR, "Allocation failed");
					continue;
				}
				mode->drm_mode = drm_conn->modes[i];
				mode->wlr_mode.width = mode->drm_mode.hdisplay;
				mode->wlr_mode.height = mode->drm_mode.vdisplay;
				mode->wlr_mode.refresh = calculate_refresh_rate(&mode->drm_mode);

				wlr_log(L_INFO, "  %"PRId32"@%"PRId32"@%"PRId32,
					mode->wlr_mode.width, mode->wlr_mode.height,
					mode->wlr_mode.refresh);

				wl_list_insert(&wlr_conn->output.modes, &mode->wlr_mode.link);
			}

			wlr_output_update_enabled(&wlr_conn->output, true);

			wlr_conn->state = WLR_DRM_CONN_NEEDS_MODESET;
			wlr_log(L_INFO, "Sending modesetting signal for '%s'",
				wlr_conn->output.name);
			wlr_signal_emit_safe(&drm->backend.events.output_add, &wlr_conn->output);
		} else if (wlr_conn->state == WLR_DRM_CONN_CONNECTED &&
				drm_conn->connection != DRM_MODE_CONNECTED) {
			wlr_log(L_INFO, "'%s' disconnected", wlr_conn->output.name);

			wlr_output_update_enabled(&wlr_conn->output, false);
			wlr_drm_connector_cleanup(wlr_conn);
		}

		drmModeFreeEncoder(curr_enc);
		drmModeFreeConnector(drm_conn);
	}

	drmModeFreeResources(res);

	struct wlr_drm_connector *conn, *tmp_conn;
	size_t index = wl_list_length(&drm->outputs);
	wl_list_for_each_safe(conn, tmp_conn, &drm->outputs, link) {
		index--;
		if (index >= seen_len || seen[index]) {
			continue;
		}

		wlr_log(L_INFO, "'%s' disappeared", conn->output.name);
		wlr_drm_connector_cleanup(conn);

		drmModeFreeCrtc(conn->old_crtc);
		wl_event_source_remove(conn->retry_pageflip);
		wl_list_remove(&conn->link);
		free(conn);
	}
}

static void page_flip_handler(int fd, unsigned seq,
		unsigned tv_sec, unsigned tv_usec, void *user) {
	struct wlr_drm_connector *conn = user;
	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)conn->output.backend;

	conn->pageflip_pending = false;
	if (conn->state != WLR_DRM_CONN_CONNECTED) {
		return;
	}

	wlr_drm_surface_post(&conn->crtc->primary->surf);
	if (drm->parent) {
		wlr_drm_surface_post(&conn->crtc->primary->mgpu_surf);
	}

	if (drm->session->active) {
		wlr_output_send_frame(&conn->output);
	}
}

int wlr_drm_event(int fd, uint32_t mask, void *data) {
	drmEventContext event = {
		.version = DRM_EVENT_CONTEXT_VERSION,
		.page_flip_handler = page_flip_handler,
	};

	drmHandleEvent(fd, &event);
	return 1;
}

void wlr_drm_restore_outputs(struct wlr_drm_backend *drm) {
	uint64_t to_close = (1 << wl_list_length(&drm->outputs)) - 1;

	struct wlr_drm_connector *conn;
	wl_list_for_each(conn, &drm->outputs, link) {
		if (conn->state == WLR_DRM_CONN_CONNECTED) {
			conn->state = WLR_DRM_CONN_CLEANUP;
		}
	}

	time_t timeout = time(NULL) + 5;

	while (to_close && time(NULL) < timeout) {
		wlr_drm_event(drm->fd, 0, NULL);
		size_t i = 0;
		struct wlr_drm_connector *conn;
		wl_list_for_each(conn, &drm->outputs, link) {
			if (conn->state != WLR_DRM_CONN_CLEANUP || !conn->pageflip_pending) {
				to_close &= ~(1 << i);
			}
			i++;
		}
	}

	if (to_close) {
		wlr_log(L_ERROR, "Timed out stopping output renderers");
	}

	wl_list_for_each(conn, &drm->outputs, link) {
		drmModeCrtc *crtc = conn->old_crtc;
		if (!crtc) {
			continue;
		}

		drmModeSetCrtc(drm->fd, crtc->crtc_id, crtc->buffer_id, crtc->x, crtc->y,
			&conn->id, 1, &crtc->mode);
		drmModeFreeCrtc(crtc);
	}
}

void wlr_drm_connector_cleanup(struct wlr_drm_connector *conn) {
	if (!conn) {
		return;
	}

	struct wlr_drm_backend *drm = (struct wlr_drm_backend *)conn->output.backend;

	switch (conn->state) {
	case WLR_DRM_CONN_CONNECTED:
	case WLR_DRM_CONN_CLEANUP:;
		struct wlr_drm_crtc *crtc = conn->crtc;
		for (int i = 0; i < 3; ++i) {
			if (!crtc->planes[i]) {
				continue;
			}

			wlr_drm_surface_finish(&crtc->planes[i]->surf);
			wlr_drm_surface_finish(&crtc->planes[i]->mgpu_surf);
			if (crtc->planes[i]->id == 0) {
				free(crtc->planes[i]);
				crtc->planes[i] = NULL;
			}
		}

		struct wlr_drm_mode *mode;
		struct wlr_drm_mode *tmp;
		wl_list_for_each_safe(mode, tmp, &conn->output.modes, wlr_mode.link) {
			wl_list_remove(&mode->wlr_mode.link);
			free(mode);
		}

		memset(&conn->output.make, 0, sizeof(conn->output.make));
		memset(&conn->output.model, 0, sizeof(conn->output.model));
		memset(&conn->output.serial, 0, sizeof(conn->output.serial));

		conn->crtc = NULL;
		conn->possible_crtc = 0;
		/* Fallthrough */
	case WLR_DRM_CONN_NEEDS_MODESET:
		wlr_log(L_INFO, "Emitting destruction signal for '%s'",
				conn->output.name);
		wlr_signal_emit_safe(&drm->backend.events.output_remove, &conn->output);
		break;
	case WLR_DRM_CONN_DISCONNECTED:
		break;
	}

	conn->state = WLR_DRM_CONN_DISCONNECTED;
}
