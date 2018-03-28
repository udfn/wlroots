#ifndef WLR_RENDER_EGL_H
#define WLR_RENDER_EGL_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <pixman.h>
#include <stdbool.h>
#include <wayland-server.h>
#include <wlr/types/wlr_linux_dmabuf.h>

struct wlr_egl {
	EGLDisplay display;
	EGLConfig config;
	EGLContext context;

	const char *exts_str;

	struct {
		bool buffer_age;
		bool swap_buffers_with_damage;
		bool dmabuf_import;
		bool dmabuf_import_modifiers;
		bool bind_wayland_display;
	} egl_exts;

	struct wl_display *wl_display;
};

// TODO: Allocate and return a wlr_egl
/**
 *  Initializes an egl context for the given platform and remote display.
 * Will attempt to load all possibly required api functions.
 */
bool wlr_egl_init(struct wlr_egl *egl, EGLenum platform, void *remote_display,
	EGLint *config_attribs, EGLint visual_id);

/**
 * Frees all related egl resources, makes the context not-current and
 * unbinds a bound wayland display.
 */
void wlr_egl_finish(struct wlr_egl *egl);

/**
 * Binds the given display to the egl instance.
 * This will allow clients to create egl surfaces from wayland ones and render to it.
 */
bool wlr_egl_bind_display(struct wlr_egl *egl, struct wl_display *local_display);

/**
 * Refer to the eglQueryWaylandBufferWL extension function.
 */
bool wlr_egl_query_buffer(struct wlr_egl *egl, struct wl_resource *buf,
	EGLint attrib, EGLint *value);

/**
 * Returns a surface for the given native window
 * The window must match the remote display the wlr_egl was created with.
 */
EGLSurface wlr_egl_create_surface(struct wlr_egl *egl, void *window);

/**
 * Creates an egl image from the given client buffer and attributes.
 */
EGLImageKHR wlr_egl_create_image(struct wlr_egl *egl,
		EGLenum target, EGLClientBuffer buffer, const EGLint *attribs);

/**
 * Creates an egl image from the given dmabuf attributes. Check usability
 * of the dmabuf with wlr_egl_check_import_dmabuf once first.
 */
EGLImageKHR wlr_egl_create_image_from_dmabuf(struct wlr_egl *egl,
		struct wlr_dmabuf_buffer_attribs *attributes);

/**
 * Try to import the given dmabuf. On success return true false otherwise.
 * If this succeeds the dmabuf can be used for rendering on a texture
 */
bool wlr_egl_check_import_dmabuf(struct wlr_egl *egl,
		struct wlr_dmabuf_buffer *dmabuf);

/**
 * Get the available dmabuf formats
 */
int wlr_egl_get_dmabuf_formats(struct wlr_egl *egl, int **formats);

/**
 * Get the available dmabuf modifiers for a given format
 */
int wlr_egl_get_dmabuf_modifiers(struct wlr_egl *egl, int format,
		uint64_t **modifiers);

/**
 * Destroys an egl image created with the given wlr_egl.
 */
bool wlr_egl_destroy_image(struct wlr_egl *egl, EGLImageKHR image);

bool wlr_egl_make_current(struct wlr_egl *egl, EGLSurface surface,
	int *buffer_age);

bool wlr_egl_swap_buffers(struct wlr_egl *egl, EGLSurface surface,
	pixman_region32_t *damage);

#endif
