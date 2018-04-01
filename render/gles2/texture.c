#include <assert.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdint.h>
#include <stdlib.h>
#include <wayland-server-protocol.h>
#include <wayland-util.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/render/egl.h>
#include <wlr/render/interface.h>
#include <wlr/types/wlr_matrix.h>
#include <wlr/util/log.h>
#include "glapi.h"
#include "render/gles2.h"
#include "util/signal.h"

static const struct wlr_texture_impl texture_impl;

static struct wlr_gles2_texture *gles2_get_texture(
		struct wlr_texture *wlr_texture) {
	assert(wlr_texture->impl == &texture_impl);
	return (struct wlr_gles2_texture *)wlr_texture;
}

struct wlr_gles2_texture *gles2_get_texture_in_context(
		struct wlr_texture *wlr_texture) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);
	assert(eglGetCurrentContext() == texture->renderer->egl->context);
	return texture;
}

static void gles2_texture_get_size(struct wlr_texture *wlr_texture, int *width,
		int *height) {
	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);
	*width = texture->width;
	*height = texture->height;
}

static bool gles2_texture_write_pixels(struct wlr_texture *wlr_texture,
		enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width,
		uint32_t height, uint32_t src_x, uint32_t src_y, uint32_t dst_x,
		uint32_t dst_y, const void *data) {
	struct wlr_gles2_texture *texture =
		gles2_get_texture_in_context(wlr_texture);

	if (texture->type != WLR_GLES2_TEXTURE_GLTEX) {
		wlr_log(L_ERROR, "Cannot write pixels to immutable texture");
		return false;
	}

	const struct gles2_pixel_format *fmt = gles2_format_from_wl(wl_fmt);
	if (fmt == NULL) {
		wlr_log(L_ERROR, "Unsupported pixel format %"PRIu32, wl_fmt);
		return false;
	}

	// TODO: what if the unpack subimage extension isn't supported?
	GLES2_DEBUG_PUSH;

	glBindTexture(GL_TEXTURE_2D, texture->gl_tex);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride / (fmt->bpp / 8));
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, src_x);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, src_y);

	glTexSubImage2D(GL_TEXTURE_2D, 0, dst_x, dst_y, width, height,
		fmt->gl_format, fmt->gl_type, data);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_PIXELS_EXT, 0);
	glPixelStorei(GL_UNPACK_SKIP_ROWS_EXT, 0);

	GLES2_DEBUG_POP;
	return true;
}

static void gles2_texture_destroy(struct wlr_texture *wlr_texture) {
	if (wlr_texture == NULL) {
		return;
	}

	struct wlr_gles2_texture *texture = gles2_get_texture(wlr_texture);

	wlr_egl_make_current(texture->renderer->egl, EGL_NO_SURFACE, NULL);

	GLES2_DEBUG_PUSH;

	if (texture->image_tex) {
		glDeleteTextures(1, &texture->image_tex);
	}
	if (texture->image) {
		assert(eglDestroyImageKHR);
		wlr_egl_destroy_image(texture->renderer->egl, texture->image);
	}

	if (texture->type == WLR_GLES2_TEXTURE_GLTEX) {
		glDeleteTextures(1, &texture->gl_tex);
	}

	GLES2_DEBUG_POP;

	free(texture);
}

static const struct wlr_texture_impl texture_impl = {
	.get_size = gles2_texture_get_size,
	.write_pixels = gles2_texture_write_pixels,
	.destroy = gles2_texture_destroy,
};

struct wlr_texture *gles2_texture_from_pixels(struct wlr_renderer *wlr_renderer,
		enum wl_shm_format wl_fmt, uint32_t stride, uint32_t width,
		uint32_t height, const void *data) {
	struct wlr_gles2_renderer *renderer =
		gles2_get_renderer_in_context(wlr_renderer);

	const struct gles2_pixel_format *fmt = gles2_format_from_wl(wl_fmt);
	if (fmt == NULL) {
		wlr_log(L_ERROR, "Unsupported pixel format %"PRIu32, wl_fmt);
		return NULL;
	}

	struct wlr_gles2_texture *texture =
		calloc(1, sizeof(struct wlr_gles2_texture));
	if (texture == NULL) {
		wlr_log(L_ERROR, "Allocation failed");
		return NULL;
	}
	wlr_texture_init(&texture->wlr_texture, &texture_impl);
	texture->renderer = renderer;
	texture->width = width;
	texture->height = height;
	texture->type = WLR_GLES2_TEXTURE_GLTEX;
	texture->has_alpha = fmt->has_alpha;

	GLES2_DEBUG_PUSH;

	glGenTextures(1, &texture->gl_tex);
	glBindTexture(GL_TEXTURE_2D, texture->gl_tex);

	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, stride / (fmt->bpp / 8));
	glTexImage2D(GL_TEXTURE_2D, 0, fmt->gl_format, width, height, 0,
		fmt->gl_format, fmt->gl_type, data);
	glPixelStorei(GL_UNPACK_ROW_LENGTH_EXT, 0);

	GLES2_DEBUG_POP;
	return &texture->wlr_texture;
}

struct wlr_texture *gles2_texture_from_wl_drm(struct wlr_renderer *wlr_renderer,
		struct wl_resource *data) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);

	if (!glEGLImageTargetTexture2DOES) {
		return NULL;
	}

	struct wlr_gles2_texture *texture =
		calloc(1, sizeof(struct wlr_gles2_texture));
	if (texture == NULL) {
		wlr_log(L_ERROR, "Allocation failed");
		return NULL;
	}
	wlr_texture_init(&texture->wlr_texture, &texture_impl);
	texture->renderer = renderer;
	texture->wl_drm = data;

	EGLint fmt;
	texture->image = wlr_egl_create_image_from_wl_drm(renderer->egl, data, &fmt,
		&texture->width, &texture->height, &texture->inverted_y);
	if (texture->image == NULL) {
		free(texture);
		return NULL;
	}

	GLenum target;
	switch (fmt) {
	case EGL_TEXTURE_RGB:
	case EGL_TEXTURE_RGBA:
		target = GL_TEXTURE_2D;
		texture->type = WLR_GLES2_TEXTURE_WL_DRM_GL;
		texture->has_alpha = fmt == EGL_TEXTURE_RGBA;
		break;
	case EGL_TEXTURE_EXTERNAL_WL:
		target = GL_TEXTURE_EXTERNAL_OES;
		texture->type = WLR_GLES2_TEXTURE_WL_DRM_EXT;
		texture->has_alpha = true;
		break;
	default:
		wlr_log(L_ERROR, "Invalid or unsupported EGL buffer format");
		free(texture);
		return NULL;
	}

	GLES2_DEBUG_PUSH;

	glGenTextures(1, &texture->image_tex);
	glBindTexture(target, texture->image_tex);
	glEGLImageTargetTexture2DOES(target, texture->image);

	GLES2_DEBUG_POP;
	return &texture->wlr_texture;
}

struct wlr_texture *gles2_texture_from_dmabuf(struct wlr_renderer *wlr_renderer,
		struct wlr_dmabuf_buffer_attribs *attribs) {
	struct wlr_gles2_renderer *renderer = gles2_get_renderer(wlr_renderer);

	if (!glEGLImageTargetTexture2DOES) {
		return NULL;
	}

	if (!renderer->egl->egl_exts.dmabuf_import) {
		wlr_log(L_ERROR, "Cannot create DMA-BUF texture: EGL extension "
			"unavailable");
		return NULL;
	}

	struct wlr_gles2_texture *texture =
		calloc(1, sizeof(struct wlr_gles2_texture));
	if (texture == NULL) {
		wlr_log(L_ERROR, "Allocation failed");
		return NULL;
	}
	wlr_texture_init(&texture->wlr_texture, &texture_impl);
	texture->renderer = renderer;
	texture->width = attribs->width;
	texture->height = attribs->height;
	texture->type = WLR_GLES2_TEXTURE_DMABUF;
	texture->has_alpha = true;
	texture->inverted_y =
		(attribs->flags & WLR_DMABUF_BUFFER_ATTRIBS_FLAGS_Y_INVERT) != 0;

	texture->image = wlr_egl_create_image_from_dmabuf(renderer->egl, attribs);
	if (texture->image == NULL) {
		free(texture);
		return NULL;
	}

	GLES2_DEBUG_PUSH;

	glGenTextures(1, &texture->image_tex);
	glBindTexture(GL_TEXTURE_EXTERNAL_OES, texture->image_tex);
	glEGLImageTargetTexture2DOES(GL_TEXTURE_EXTERNAL_OES, texture->image);

	GLES2_DEBUG_POP;
	return &texture->wlr_texture;
}
