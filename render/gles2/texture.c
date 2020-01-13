#include <assert.h>
#include <drm_fourcc.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/render/egl.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "render/gles2.h"
#include "util/signal.h"

static const struct wlr_texture_impl texture_impl;

bool wlr_texture_is_gles2(struct wlr_texture *wlr_texture) {
	return wlr_texture->impl == &texture_impl;
}

struct wlr_gles2_texture *gles2_get_texture(
		struct wlr_texture *wlr_texture) {
	assert(wlr_texture_is_gles2(wlr_texture));
	return (struct wlr_gles2_texture *)wlr_texture;
}

static struct wlr_gles2_texture *get_gles2_texture_in_context(
		struct wlr_texture *wlr_texture) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);
	if (!wlr_egl_is_current(texture->egl)) {
		wlr_egl_make_current(texture->egl, EGL_NO_SURFACE, NULL);
	}
	return texture;
}

static void gles2_texture_get_size(struct wlr_texture *wlr_texture, int *width,
		int *height) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);
	*width = texture->width;
	*height = texture->height;
}

static bool gles2_texture_is_opaque(struct wlr_texture *wlr_texture) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);
	return !texture->has_alpha;
}

static bool gles2_texture_write_pixels(struct wlr_texture *wlr_texture,
		uint32_t stride, uint32_t width, uint32_t height,
		uint32_t src_x, uint32_t src_y, uint32_t dst_x, uint32_t dst_y,
		const void *data) {
	struct wlr_gles2_texture *texture =
		get_gles2_texture_in_context(wlr_texture);

	if (texture->target != GL_TEXTURE_2D) {
		wlr_log(WLR_ERROR, "Cannot write pixels to immutable texture");
		return false;
	}

	const struct wlr_gles2_pixel_format *fmt =
		get_gles2_format_from_wl(texture->wl_format);
	assert(fmt);

	// TODO: what if the unpack subimage extension isn't supported?
	PUSH_GLES2_DEBUG;

	glBindTexture(GL_TEXTURE_2D, texture->tex);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride / (fmt->bpp / 8));
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, src_x);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, src_y);

	glTexSubImage2D(GL_TEXTURE_2D, 0, dst_x, dst_y, width, height,
		fmt->gl_format, fmt->gl_type, data);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);

	glBindTexture(GL_TEXTURE_2D, 0);

	POP_GLES2_DEBUG;
	return true;
}

static bool gles2_texture_to_dmabuf(struct wlr_texture *wlr_texture,
		struct wlr_dmabuf_attributes *attribs) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);

	if (!texture->image) {
		assert(texture->target == GL_TEXTURE_2D);

		if (!texture->egl->exts.image_base_khr) {
			return false;
		}

		texture->image = texture->egl->procs.eglCreateImageKHR(
			texture->egl->display, texture->egl->context, EGL_GL_TEXTURE_2D_KHR,
			(EGLClientBuffer)(uintptr_t)texture->tex, NULL);
		if (texture->image == EGL_NO_IMAGE_KHR) {
			return false;
		}
	}

	uint32_t flags = 0;
	if (texture->inverted_y) {
		flags |= WLR_DMABUF_ATTRIBUTES_FLAGS_Y_INVERT;
	}

	return wlr_egl_export_image_to_dmabuf(texture->egl, texture->image,
		texture->width, texture->height, flags, attribs);
}

static void gles2_texture_destroy(struct wlr_texture *wlr_texture) {
	if (wlr_texture == NULL) {
		return;
	}

	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);

	if (!wlr_egl_is_current(texture->egl)) {
		wlr_egl_make_current(texture->egl, EGL_NO_SURFACE, NULL);
	}

	PUSH_GLES2_DEBUG;

	glDeleteTextures(1, &texture->tex);
	wlr_egl_destroy_image(texture->egl, texture->image);

	POP_GLES2_DEBUG;

	free(texture);
}

static const struct wlr_texture_impl texture_impl = {
	.get_size = gles2_texture_get_size,
	.is_opaque = gles2_texture_is_opaque,
	.write_pixels = gles2_texture_write_pixels,
	.to_dmabuf = gles2_texture_to_dmabuf,
	.destroy = gles2_texture_destroy,
};

struct wlr_texture *wlr_gles2_texture_from_pixels(struct wlr_egl *egl,
		enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width,
		uint32_t height, const void *data) {
	if (!wlr_egl_is_current(egl)) {
		wlr_egl_make_current(egl, EGL_NO_SURFACE, NULL);
	}

	const struct wlr_gles2_pixel_format *fmt = get_gles2_format_from_wl(wl_fmt);
	if (fmt == NULL) {
		wlr_log(WLR_ERROR, "Unsupported pixel format %"PRIu32, wl_fmt);
		return NULL;
	}

	struct wlr_gles2_texture *texture =
		calloc(1, sizeof(struct wlr_gles2_texture));
	if (texture == NULL) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	wlr_texture_init(&texture->wlr_texture, &texture_impl);
	texture->egl = egl;
	texture->width = width;
	texture->height = height;
	texture->target = GL_TEXTURE_2D;
	texture->has_alpha = fmt->has_alpha;
	texture->wl_format = fmt->wl_format;

	PUSH_GLES2_DEBUG;

	glGenTextures(1, &texture->tex);
	glBindTexture(GL_TEXTURE_2D, texture->tex);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride / (fmt->bpp / 8));
	glTexImage2D(GL_TEXTURE_2D, 0, fmt->gl_format, width, height, 0,
		fmt->gl_format, fmt->gl_type, data);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);

	glBindTexture(GL_TEXTURE_2D, 0);

	POP_GLES2_DEBUG;
	return &texture->wlr_texture;
}

struct wlr_texture *wlr_gles2_texture_from_wl_drm(struct wlr_egl *egl,
		struct wl_resource *data) {
	if (!wlr_egl_is_current(egl)) {
		wlr_egl_make_current(egl, EGL_NO_SURFACE, NULL);
	}

	if (!gles2_procs.glEGLImageTargetTexture2DOES) {
		return NULL;
	}

	struct wlr_gles2_texture *texture =
		calloc(1, sizeof(struct wlr_gles2_texture));
	if (texture == NULL) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	wlr_texture_init(&texture->wlr_texture, &texture_impl);
	texture->egl = egl;

	EGLint fmt;
	texture->wl_format = 0xFFFFFFFF; // texture can't be written anyways
	texture->image = wlr_egl_create_image_from_wl_drm(egl, data, &fmt,
		&texture->width, &texture->height, &texture->inverted_y);
	if (texture->image == NULL) {
		free(texture);
		return NULL;
	}

	switch (fmt) {
	case EGL_TEXTURE_RGB:
		texture->has_alpha = false;
		break;
	case EGL_TEXTURE_RGBA:
	case EGL_TEXTURE_EXTERNAL_WL:
		texture->has_alpha = true;
		break;
	default:
		wlr_log(WLR_ERROR, "Invalid or unsupported EGL buffer format");
		free(texture);
		return NULL;
	}

	texture->target = GL_TEXTURE_EXTERNAL_OES;

	PUSH_GLES2_DEBUG;

	glGenTextures(1, &texture->tex);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture->tex);
	gles2_procs.glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES,
		texture->image);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

	POP_GLES2_DEBUG;
	return &texture->wlr_texture;
}

struct wlr_texture *wlr_gles2_texture_from_dmabuf(struct wlr_egl *egl,
		struct wlr_dmabuf_attributes *attribs) {
	if (!wlr_egl_is_current(egl)) {
		wlr_egl_make_current(egl, EGL_NO_SURFACE, NULL);
	}

	if (!gles2_procs.glEGLImageTargetTexture2DOES) {
		return NULL;
	}

	if (!egl->exts.image_dmabuf_import_ext) {
		wlr_log(WLR_ERROR, "Cannot create DMA-BUF texture: EGL extension "
			"unavailable");
		return NULL;
	}

	switch (attribs->format & ~DRM_FORMAT_BIG_ENDIAN) {
	case WL_SHM_FORMAT_YUYV:
	case WL_SHM_FORMAT_YVYU:
	case WL_SHM_FORMAT_UYVY:
	case WL_SHM_FORMAT_VYUY:
	case WL_SHM_FORMAT_AYUV:
		// TODO: YUV based formats not yet supported, require multiple images
		return false;
	default:
		break;
	}

	struct wlr_gles2_texture *texture =
		calloc(1, sizeof(struct wlr_gles2_texture));
	if (texture == NULL) {
		wlr_log(WLR_ERROR, "Allocation failed");
		return NULL;
	}
	wlr_texture_init(&texture->wlr_texture, &texture_impl);
	texture->egl = egl;
	texture->width = attribs->width;
	texture->height = attribs->height;
	texture->target = GL_TEXTURE_EXTERNAL_OES;
	texture->has_alpha = true;
	texture->wl_format = 0xFFFFFFFF; // texture can't be written anyways
	texture->inverted_y =
		(attribs->flags & WLR_DMABUF_ATTRIBUTES_FLAGS_Y_INVERT) != 0;

	texture->image = wlr_egl_create_image_from_dmabuf(egl, attribs);
	if (texture->image == NULL) {
		free(texture);
		return NULL;
	}

	PUSH_GLES2_DEBUG;

	glGenTextures(1, &texture->tex);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture->tex);
	gles2_procs.glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES,
		texture->image);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);

	POP_GLES2_DEBUG;
	return &texture->wlr_texture;
}

void wlr_gles2_texture_get_attribs(struct wlr_texture *wlr_texture,
		struct wlr_gles2_texture_attribs *attribs) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);
	memset(attribs, 0, sizeof(*attribs));
	attribs->target = texture->target;
	attribs->tex = texture->tex;
	attribs->inverted_y = texture->inverted_y;
	attribs->has_alpha = texture->has_alpha;
}
