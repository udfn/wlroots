#include <stdbool.h>
#include <stdlib.h>
#include <wlr/render/interface.h>
#include "util/defs.h"

WLR_API
void wlr_renderer_init(struct wlr_renderer *renderer,
		struct wlr_renderer_impl *impl) {
	renderer->impl = impl;
}

WLR_API
void wlr_renderer_destroy(struct wlr_renderer *r) {
	if (r && r->impl && r->impl->destroy) {
		r->impl->destroy(r);
	} else {
		free(r);
	}
}

WLR_API
void wlr_renderer_begin(struct wlr_renderer *r, struct wlr_output *o) {
	r->impl->begin(r, o);
}

WLR_API
void wlr_renderer_end(struct wlr_renderer *r) {
	r->impl->end(r);
}

WLR_API
void wlr_renderer_clear(struct wlr_renderer *r, const float (*color)[4]) {
	r->impl->clear(r, color);
}

WLR_API
void wlr_renderer_scissor(struct wlr_renderer *r, struct wlr_box *box) {
	r->impl->scissor(r, box);
}

WLR_API
struct wlr_texture *wlr_render_texture_create(struct wlr_renderer *r) {
	return r->impl->texture_create(r);
}

WLR_API
bool wlr_render_with_matrix(struct wlr_renderer *r,
		struct wlr_texture *texture, const float (*matrix)[16]) {
	return r->impl->render_with_matrix(r, texture, matrix);
}

WLR_API
void wlr_render_colored_quad(struct wlr_renderer *r,
		const float (*color)[4], const float (*matrix)[16]) {
	r->impl->render_quad(r, color, matrix);
}

WLR_API
void wlr_render_colored_ellipse(struct wlr_renderer *r,
		const float (*color)[4], const float (*matrix)[16]) {
	r->impl->render_ellipse(r, color, matrix);
}

WLR_API
const enum wl_shm_format *wlr_renderer_get_formats(
		struct wlr_renderer *r, size_t *len) {
	return r->impl->formats(r, len);
}

WLR_API
bool wlr_renderer_buffer_is_drm(struct wlr_renderer *r,
		struct wl_resource *buffer) {
	return r->impl->buffer_is_drm(r, buffer);
}

WLR_API
bool wlr_renderer_read_pixels(struct wlr_renderer *r, enum wl_shm_format fmt,
		uint32_t stride, uint32_t width, uint32_t height,
		uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
		void *data) {
	return r->impl->read_pixels(r, fmt, stride, width, height, src_x, src_y,
		dst_x, dst_y, data);
}

WLR_API
bool wlr_renderer_format_supported(struct wlr_renderer *r,
		enum wl_shm_format fmt) {
	return r->impl->format_supported(r, fmt);
}
