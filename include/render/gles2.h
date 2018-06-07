#ifndef RENDER_GLES2_H
#define RENDER_GLES2_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <wlr/backend.h>
#include <wlr/render/egl.h>
#include <wlr/render/gles2.h>
#include <wlr/render/interface.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/util/log.h>

extern PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES;

struct wlr_gles2_pixel_format {
	uint32_t wl_format;
	GLint gl_format, gl_type;
	int depth, bpp;
	bool has_alpha;
};

struct wlr_gles2_renderer {
	struct wlr_renderer wlr_renderer;

	struct wlr_egl *egl;
	const char *exts_str;

	struct {
		struct {
			GLuint program;
			GLint proj;
			GLint color;
		} quad;
		struct {
			GLuint program;
			GLint proj;
			GLint color;
		} ellipse;
		struct {
			GLuint program;
			GLint proj;
			GLint invert_y;
			GLint tex;
			GLint alpha;
		} tex_rgba;
		struct  {
			GLuint program;
			GLint proj;
			GLint invert_y;
			GLint tex;
			GLint alpha;
		} tex_rgbx;
		struct  {
			GLuint program;
			GLint proj;
			GLint invert_y;
			GLint tex;
			GLint alpha;
		} tex_ext;
	} shaders;

	uint32_t viewport_width, viewport_height;
};

enum wlr_gles2_texture_type {
	WLR_GLES2_TEXTURE_GLTEX,
	WLR_GLES2_TEXTURE_WL_DRM_GL,
	WLR_GLES2_TEXTURE_WL_DRM_EXT,
	WLR_GLES2_TEXTURE_DMABUF,
};

struct wlr_gles2_texture {
	struct wlr_texture wlr_texture;

	struct wlr_egl *egl;
	enum wlr_gles2_texture_type type;
	int width, height;
	bool has_alpha;
	bool inverted_y;

	// Not set if WLR_GLES2_TEXTURE_GLTEX
	EGLImageKHR image;
	GLuint image_tex;

	union {
		GLuint gl_tex;
		struct wl_resource *wl_drm;
	};
};

const struct wlr_gles2_pixel_format *get_gles2_format_from_wl(
	enum wl_shm_format fmt);
const enum wl_shm_format *get_gles2_formats(size_t *len);

struct wlr_gles2_texture *get_gles2_texture_in_context(
	struct wlr_texture *wlr_texture);

void push_gles2_marker(const char *file, const char *func);
void pop_gles2_marker(void);
#define PUSH_GLES2_DEBUG push_gles2_marker(wlr_strip_path(__FILE__), __func__)
#define POP_GLES2_DEBUG pop_gles2_marker()

#endif
