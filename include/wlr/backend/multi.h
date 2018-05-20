#ifndef WLR_BACKEND_MULTI_H
#define WLR_BACKEND_MULTI_H

#include <wlr/backend.h>
#include <wlr/backend/session.h>

/**
 * Creates a multi-backend. Multi-backends wrap an arbitrary number of backends
 * and aggregate their new_output/new_input signals.
 */
struct wlr_backend *wlr_multi_backend_create(struct wl_display *display);
/**
 * Adds the given backend to the multi backend. This should be done before the
 * new backend is started.
 */
bool wlr_multi_backend_add(struct wlr_backend *multi,
	struct wlr_backend *backend);

void wlr_multi_backend_remove(struct wlr_backend *multi,
	struct wlr_backend *backend);

bool wlr_backend_is_multi(struct wlr_backend *backend);
struct wlr_session *wlr_multi_get_session(struct wlr_backend *base);
bool wlr_multi_is_empty(struct wlr_backend *backend);

#endif
