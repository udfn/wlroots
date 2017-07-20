#ifndef DRM_PROPERTIES_H
#define DRM_PROPERTIES_H

#include <stdbool.h>
#include <stdint.h>

/*
 * These types contain the property ids for several DRM objects.
 * See https://01.org/linuxgraphics/gfx-docs/drm/drm-kms-properties.html
 * for more details.
 */

union wlr_drm_connector_props {
	struct {
		uint32_t edid;
		uint32_t dpms;

		// atomic-modesetting only

		uint32_t crtc_id;
	};
	uint32_t props[3];
};

union wlr_drm_crtc_props {
	struct {
		// Neither of these are guranteed to exist
		uint32_t rotation;
		uint32_t scaling_mode;
	};
	uint32_t props[2];
};

union wlr_drm_plane_props {
	struct {
		uint32_t type;
		uint32_t rotation; // Not guranteed to exist

		// atomic-modesetting only

		uint32_t src_x;
		uint32_t src_y;
		uint32_t src_w;
		uint32_t src_h;
		uint32_t crtc_x;
		uint32_t crtc_y;
		uint32_t crtc_w;
		uint32_t crtc_h;
		uint32_t fb_id;
		uint32_t crtc_id;
	};
	uint32_t props[12];
};

bool wlr_drm_get_connector_props(int fd, uint32_t id, union wlr_drm_connector_props *out);
bool wlr_drm_get_crtc_props(int fd, uint32_t id, union wlr_drm_crtc_props *out);
bool wlr_drm_get_plane_props(int fd, uint32_t id, union wlr_drm_plane_props *out);

#endif
