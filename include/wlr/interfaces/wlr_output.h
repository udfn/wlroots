/*
 * This an unstable interface of wlroots. No guarantees are made regarding the
 * future consistency of this API.
 */
#ifndef WLR_USE_UNSTABLE
#error "Add -DWLR_USE_UNSTABLE to enable unstable wlroots features"
#endif

#ifndef WLR_INTERFACES_WLR_OUTPUT_H
#define WLR_INTERFACES_WLR_OUTPUT_H

#include <stdbool.h>
#include <wlr/backend.h>
#include <wlr/types/wlr_box.h>
#include <wlr/types/wlr_output.h>

struct wlr_output_impl {
	bool (*set_cursor)(struct wlr_output *output, struct wlr_texture *texture,
		float scale, enum wl_output_transform transform,
		int32_t hotspot_x, int32_t hotspot_y, bool update_texture);
	bool (*move_cursor)(struct wlr_output *output, int x, int y, bool visible);
	void (*destroy)(struct wlr_output *output);
	bool (*attach_render)(struct wlr_output *output, int *buffer_age);
	bool (*test)(struct wlr_output *output);
	bool (*commit)(struct wlr_output *output);
	void (*rollback_render)(struct wlr_output *output);
	size_t (*get_gamma_size)(struct wlr_output *output);
	bool (*export_dmabuf)(struct wlr_output *output,
		struct wlr_dmabuf_attributes *attribs);
};

void wlr_output_init(struct wlr_output *output, struct wlr_backend *backend,
	const struct wlr_output_impl *impl, struct wl_display *display);
void wlr_output_update_mode(struct wlr_output *output,
	struct wlr_output_mode *mode);
void wlr_output_update_custom_mode(struct wlr_output *output, int32_t width,
	int32_t height, int32_t refresh);
void wlr_output_update_enabled(struct wlr_output *output, bool enabled);
void wlr_output_update_needs_frame(struct wlr_output *output);
void wlr_output_damage_whole(struct wlr_output *output);
void wlr_output_send_frame(struct wlr_output *output);
void wlr_output_send_present(struct wlr_output *output,
	struct wlr_output_event_present *event);

#endif
