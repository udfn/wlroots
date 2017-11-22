#define _XOPEN_SOURCE 700
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <xcb/xfixes.h>
#include <fcntl.h>
#include "wlr/util/log.h"
#include "wlr/types/wlr_data_device.h"
#include "xwm.h"

static const size_t incr_chunk_size = 64 * 1024;

static void xwm_send_selection_notify(struct wlr_xwm *xwm,
		xcb_atom_t property) {
	xcb_selection_notify_event_t selection_notify;

	memset(&selection_notify, 0, sizeof selection_notify);
	selection_notify.response_type = XCB_SELECTION_NOTIFY;
	selection_notify.sequence = 0;
	selection_notify.time = xwm->selection_request.time;
	selection_notify.requestor = xwm->selection_request.requestor;
	selection_notify.selection = xwm->selection_request.selection;
	selection_notify.target = xwm->selection_request.target;
	selection_notify.property = property;

	xcb_send_event(xwm->xcb_conn, 0, // propagate
		xwm->selection_request.requestor,
		XCB_EVENT_MASK_NO_EVENT, (char *)&selection_notify);
}

static int xwm_flush_source_data(struct wlr_xwm *xwm)
{
	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xwm->selection_request.requestor,
		xwm->selection_request.property,
		xwm->selection_target,
		8, // format
		xwm->source_data.size,
		xwm->source_data.data);
	xwm->selection_property_set = 1;
	int length = xwm->source_data.size;
	xwm->source_data.size = 0;

	return length;
}

static int xwm_read_data_source(int fd, uint32_t mask, void *data) {
	struct wlr_xwm *xwm = data;
	void *p;

	int current = xwm->source_data.size;
	if (xwm->source_data.size < incr_chunk_size) {
		p = wl_array_add(&xwm->source_data, incr_chunk_size);
	} else {
		p = (char *) xwm->source_data.data + xwm->source_data.size;
	}

	int available = xwm->source_data.alloc - current;

	int len = read(fd, p, available);
	if (len == -1) {
		wlr_log(L_ERROR, "read error from data source: %m\n");
		xwm_send_selection_notify(xwm, XCB_ATOM_NONE);
		wl_event_source_remove(xwm->property_source);
		xwm->property_source = NULL;
		close(fd);
		wl_array_release(&xwm->source_data);
	}

	wlr_log(L_DEBUG, "read %d (available %d, mask 0x%x) bytes: \"%.*s\"\n",
			len, available, mask, len, (char *) p);

	xwm->source_data.size = current + len;
	if (xwm->source_data.size >= incr_chunk_size) {
		if (!xwm->incr) {
			wlr_log(L_DEBUG, "got %zu bytes, starting incr\n",
					xwm->source_data.size);
			xwm->incr = 1;
			xcb_change_property(xwm->xcb_conn,
					XCB_PROP_MODE_REPLACE,
					xwm->selection_request.requestor,
					xwm->selection_request.property,
					xwm->atoms[INCR],
					32, /* format */
					1, &incr_chunk_size);
			xwm->selection_property_set = 1;
			xwm->flush_property_on_delete = 1;
			wl_event_source_remove(xwm->property_source);
			xwm->property_source = NULL;
			xwm_send_selection_notify(xwm, xwm->selection_request.property);
		} else if (xwm->selection_property_set) {
			wlr_log(L_DEBUG, "got %zu bytes, waiting for "
				"property delete\n", xwm->source_data.size);

			xwm->flush_property_on_delete = 1;
			wl_event_source_remove(xwm->property_source);
			xwm->property_source = NULL;
		} else {
			wlr_log(L_DEBUG, "got %zu bytes, "
				"property deleted, setting new property\n",
				xwm->source_data.size);
			xwm_flush_source_data(xwm);
		}
	} else if (len == 0 && !xwm->incr) {
		wlr_log(L_DEBUG, "non-incr transfer complete\n");
		/* Non-incr transfer all done. */
		xwm_flush_source_data(xwm);
		xwm_send_selection_notify(xwm, xwm->selection_request.property);
		xcb_flush(xwm->xcb_conn);
		wl_event_source_remove(xwm->property_source);
		xwm->property_source = NULL;
		close(fd);
		wl_array_release(&xwm->source_data);
		xwm->selection_request.requestor = XCB_NONE;
	} else if (len == 0 && xwm->incr) {
		wlr_log(L_DEBUG, "incr transfer complete\n");

		xwm->flush_property_on_delete = 1;
		if (xwm->selection_property_set) {
			wlr_log(L_DEBUG, "got %zu bytes, waiting for "
					"property delete\n", xwm->source_data.size);
		} else {
			wlr_log(L_DEBUG, "got %zu bytes, "
					"property deleted, setting new property\n",
					xwm->source_data.size);
			xwm_flush_source_data(xwm);
		}
		xcb_flush(xwm->xcb_conn);
		wl_event_source_remove(xwm->property_source);
		xwm->property_source = NULL;
		close(xwm->data_source_fd);
		xwm->data_source_fd = -1;
		close(fd);
	} else {
		wlr_log(L_DEBUG, "nothing happened, buffered the bytes\n");
	}

	return 1;
}

static void xwm_send_data(struct wlr_xwm *xwm, xcb_atom_t target,
		const char *mime_type) {
	struct wlr_data_source *source;
	int p[2];

	if (pipe(p) == -1) {
		wlr_log(L_ERROR, "pipe failed: %m\n");
		xwm_send_selection_notify(xwm, XCB_ATOM_NONE);
		return;
	}

	fcntl(p[0], F_SETFD, FD_CLOEXEC);
	fcntl(p[0], F_SETFL, O_NONBLOCK);
	fcntl(p[1], F_SETFD, FD_CLOEXEC);
	fcntl(p[1], F_SETFL, O_NONBLOCK);

	wl_array_init(&xwm->source_data);
	xwm->selection_target = target;
	xwm->data_source_fd = p[0];
	struct wl_event_loop *loop =
		wl_display_get_event_loop(xwm->xwayland->wl_display);
	xwm->property_source = wl_event_loop_add_fd(loop,
		xwm->data_source_fd,
		WL_EVENT_READABLE,
		xwm_read_data_source,
		xwm);

	source = xwm->seat->selection_source;
	source->send(source, mime_type, p[1]);
	close(p[1]);
}

static void xwm_send_timestamp(struct wlr_xwm *xwm) {
	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xwm->selection_request.requestor,
		xwm->selection_request.property,
		XCB_ATOM_INTEGER,
		32, // format
		1, &xwm->selection_timestamp);

	xwm_send_selection_notify(xwm, xwm->selection_request.property);
}

static void xwm_send_targets(struct wlr_xwm *xwm) {
	xcb_atom_t targets[] = {
		xwm->atoms[TIMESTAMP],
		xwm->atoms[TARGETS],
		xwm->atoms[UTF8_STRING],
		xwm->atoms[TEXT],
	};

	xcb_change_property(xwm->xcb_conn,
		XCB_PROP_MODE_REPLACE,
		xwm->selection_request.requestor,
		xwm->selection_request.property,
		XCB_ATOM_ATOM,
		32, // format
		sizeof(targets) / sizeof(targets[0]), targets);

	xwm_send_selection_notify(xwm, xwm->selection_request.property);
}

static void xwm_handle_selection_request(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	xcb_selection_request_event_t *selection_request =
		(xcb_selection_request_event_t *) event;

	xwm->selection_request = *selection_request;
	xwm->incr = 0;
	xwm->flush_property_on_delete = 0;

	if (selection_request->selection == xwm->atoms[CLIPBOARD_MANAGER]) {
		// The wlroots clipboard should already have grabbed
		// the first target, so just send selection notify
		// now.  This isn't synchronized with the clipboard
		// finishing getting the data, so there's a race here.
		xwm_send_selection_notify(xwm, xwm->selection_request.property);
		return;
	}

	if (selection_request->target == xwm->atoms[TARGETS]) {
		xwm_send_targets(xwm);
	} else if (selection_request->target == xwm->atoms[TIMESTAMP]) {
		xwm_send_timestamp(xwm);
	} else if (selection_request->target == xwm->atoms[UTF8_STRING] ||
			selection_request->target == xwm->atoms[TEXT]) {
		xwm_send_data(xwm, xwm->atoms[UTF8_STRING], "text/plain;charset=utf-8");
	} else {
		wlr_log(L_DEBUG, "can only handle UTF8_STRING targets\n");
		xwm_send_selection_notify(xwm, XCB_ATOM_NONE);
	}
}

static int writable_callback(int fd, uint32_t mask, void *data) {
	struct wlr_xwm *xwm = data;

	unsigned char *property = xcb_get_property_value(xwm->property_reply);
	int remainder = xcb_get_property_value_length(xwm->property_reply) -
		xwm->property_start;

	int len = write(fd, property + xwm->property_start, remainder);
	if (len == -1) {
		free(xwm->property_reply);
		xwm->property_reply = NULL;
		if (xwm->property_source) {
			wl_event_source_remove(xwm->property_source);
		}
		xwm->property_source = NULL;
		close(fd);
		wlr_log(L_ERROR, "write error to target fd: %m\n");
		return 1;
	}

	wlr_log(L_DEBUG, "wrote %d (chunk size %d) of %d bytes\n",
		xwm->property_start + len,
		len, xcb_get_property_value_length(xwm->property_reply));

	xwm->property_start += len;
	if (len == remainder) {
		free(xwm->property_reply);
		xwm->property_reply = NULL;
		if (xwm->property_source) {
			wl_event_source_remove(xwm->property_source);
		}
		xwm->property_source = NULL;

		if (xwm->incr) {
			xcb_delete_property(xwm->xcb_conn,
				xwm->selection_window,
				xwm->atoms[WL_SELECTION]);
		} else {
			wlr_log(L_DEBUG, "transfer complete\n");
			close(fd);
		}
	}

	return 1;
}

static void xwm_write_property(struct wlr_xwm *xwm,
		xcb_get_property_reply_t *reply) {
	xwm->property_start = 0;
	xwm->property_reply = reply;
	writable_callback(xwm->data_source_fd, WL_EVENT_WRITABLE, xwm);

	if (xwm->property_reply) {
		struct wl_event_loop *loop =
			wl_display_get_event_loop(xwm->xwayland->wl_display);
		xwm->property_source =
			wl_event_loop_add_fd(loop,
				xwm->data_source_fd,
				WL_EVENT_WRITABLE,
				writable_callback, xwm);
	}
}

static void xwm_get_selection_data(struct wlr_xwm *xwm) {
	xcb_get_property_cookie_t cookie =
		xcb_get_property(xwm->xcb_conn,
			1, // delete
			xwm->selection_window,
			xwm->atoms[WL_SELECTION],
			XCB_GET_PROPERTY_TYPE_ANY,
			0, // offset
			0x1fffffff // length
			);

	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);

	if (reply == NULL) {
		return;
	} else if (reply->type == xwm->atoms[INCR]) {
		xwm->incr = 1;
		free(reply);
	} else {
		xwm->incr = 0;
		// reply's ownership is transferred to wm, which is responsible
		// for freeing it
		xwm_write_property(xwm, reply);
	}

}

struct x11_data_source {
	struct wlr_data_source base;
	struct wlr_xwm *xwm;
};

static void data_source_accept(struct wlr_data_source *source, uint32_t time,
		const char *mime_type) {
}

static void data_source_send(struct wlr_data_source *base,
		const char *mime_type, int32_t fd) {
	struct x11_data_source *source = (struct x11_data_source *)base;
	struct wlr_xwm *xwm = source->xwm;

	if (strcmp(mime_type, "text/plain;charset=utf-8") == 0) {
		// Get data for the utf8_string target
		xcb_convert_selection(xwm->xcb_conn,
			xwm->selection_window,
			xwm->atoms[CLIPBOARD],
			xwm->atoms[UTF8_STRING],
			xwm->atoms[WL_SELECTION],
			XCB_TIME_CURRENT_TIME);

		xcb_flush(xwm->xcb_conn);

		fcntl(fd, F_SETFL, O_WRONLY | O_NONBLOCK);
		xwm->data_source_fd = fd;
	}
}

static void data_source_cancel(struct wlr_data_source *source) {
}

static void xwm_get_selection_targets(struct wlr_xwm *xwm) {
	// set the wayland clipboard selection to the copied selection

	xcb_get_property_cookie_t cookie = xcb_get_property(xwm->xcb_conn,
		1, // delete
		xwm->selection_window,
		xwm->atoms[WL_SELECTION],
		XCB_GET_PROPERTY_TYPE_ANY,
		0, // offset
		4096 //length
		);

	xcb_get_property_reply_t *reply =
		xcb_get_property_reply(xwm->xcb_conn, cookie, NULL);
	if (reply == NULL)
		return;

	if (reply->type != XCB_ATOM_ATOM) {
		free(reply);
		return;
	}

	struct x11_data_source *source = calloc(1, sizeof(struct x11_data_source));
	if (source == NULL) {
		free(reply);
		return;
	}

	wl_signal_init(&source->base.events.destroy);
	source->base.accept = data_source_accept;
	source->base.send = data_source_send;
	source->base.cancel = data_source_cancel;
	source->xwm = xwm;

	wl_array_init(&source->base.mime_types);
	xcb_atom_t *value = xcb_get_property_value(reply);
	for (uint32_t i = 0; i < reply->value_len; i++) {
		if (value[i] == xwm->atoms[UTF8_STRING]) {
			char **p = wl_array_add(&source->base.mime_types, sizeof *p);
			if (p) {
				*p = strdup("text/plain;charset=utf-8");
			}
		}
	}

	wlr_seat_set_selection(xwm->seat, &source->base,
		wl_display_next_serial(xwm->xwayland->wl_display));

	free(reply);

}

static void xwm_handle_selection_notify(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	xcb_selection_notify_event_t *selection_notify =
		(xcb_selection_notify_event_t *) event;

	if (selection_notify->property == XCB_ATOM_NONE) {
		wlr_log(L_ERROR, "convert selection failed");
	} else if (selection_notify->target == xwm->atoms[TARGETS]) {
		xwm_get_selection_targets(xwm);
	} else {
		xwm_get_selection_data(xwm);
	}
}

static int xwm_handle_xfixes_selection_notify(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	xcb_xfixes_selection_notify_event_t *xfixes_selection_notify =
		(xcb_xfixes_selection_notify_event_t *) event;


	if (xfixes_selection_notify->owner == XCB_WINDOW_NONE) {
		if (xwm->selection_owner != xwm->selection_window) {
			// A real X client selection went away, not our
			// proxy selection
			// TODO: Clear the wayland selection (or not)?
		}

		xwm->selection_owner = XCB_WINDOW_NONE;

		return 1;
	}

	// We have to use XCB_TIME_CURRENT_TIME when we claim the
	// selection, so grab the actual timestamp here so we can
	// answer TIMESTAMP conversion requests correctly.
	if (xfixes_selection_notify->owner == xwm->selection_window) {
		xwm->selection_timestamp = xfixes_selection_notify->timestamp;
		wlr_log(L_DEBUG, "TODO: our window");
		return 1;
	}

	xwm->selection_owner = xfixes_selection_notify->owner;

	xwm->incr = 0;
	// doing this will give a selection notify where we actually handle the sync
	xcb_convert_selection(xwm->xcb_conn, xwm->selection_window,
		xwm->atoms[CLIPBOARD],
		xwm->atoms[TARGETS],
		xwm->atoms[WL_SELECTION],
		xfixes_selection_notify->timestamp);

	xcb_flush(xwm->xcb_conn);

	return 1;
}

int xwm_handle_selection_event(struct wlr_xwm *xwm,
		xcb_generic_event_t *event) {
	if (!xwm->seat) {
		wlr_log(L_DEBUG, "not handling selection events:"
			"no seat assigned to xwayland");
		return 0;
	}

	switch (event->response_type & ~0x80) {
	case XCB_SELECTION_NOTIFY:
		xwm_handle_selection_notify(xwm, event);
		return 1;
	case XCB_SELECTION_REQUEST:
		xwm_handle_selection_request(xwm, event);
		return 1;
	}

	switch (event->response_type - xwm->xfixes->first_event) {
	case XCB_XFIXES_SELECTION_NOTIFY:
		// an X11 window has copied something to the clipboard
		return xwm_handle_xfixes_selection_notify(xwm, event);
	}

	return 0;
}

void xwm_selection_init(struct wlr_xwm *xwm) {
	uint32_t values[1], mask;

	xwm->selection_request.requestor = XCB_NONE;

	values[0] = XCB_EVENT_MASK_PROPERTY_CHANGE;
	xwm->selection_window = xcb_generate_id(xwm->xcb_conn);
	xcb_create_window(xwm->xcb_conn,
		XCB_COPY_FROM_PARENT,
		xwm->selection_window,
		xwm->screen->root,
		0, 0,
		10, 10,
		0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT,
		xwm->screen->root_visual,
		XCB_CW_EVENT_MASK, values);

	xcb_set_selection_owner(xwm->xcb_conn,
		xwm->selection_window,
		xwm->atoms[CLIPBOARD_MANAGER],
		XCB_TIME_CURRENT_TIME);

	mask =
		XCB_XFIXES_SELECTION_EVENT_MASK_SET_SELECTION_OWNER |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_WINDOW_DESTROY |
		XCB_XFIXES_SELECTION_EVENT_MASK_SELECTION_CLIENT_CLOSE;
	xcb_xfixes_select_selection_input(xwm->xcb_conn, xwm->selection_window,
		xwm->atoms[CLIPBOARD], mask);
}

static void handle_seat_set_selection(struct wl_listener *listener,
		void *data) {
	struct wlr_seat *seat = data;
	struct wlr_xwm *xwm =
		wl_container_of(listener, xwm, seat_selection_change);
	struct wlr_data_source *source = seat->selection_source;

	if (source == NULL) {
		if (xwm->selection_owner == xwm->selection_window) {
			xcb_set_selection_owner(xwm->xcb_conn,
				XCB_ATOM_NONE,
				xwm->atoms[CLIPBOARD],
				xwm->selection_timestamp);
		}

		return;
	}

	if (source->send == data_source_send) {
		return;
	}

	xwm_set_selection_owner(xwm);
}

void xwm_set_selection_owner(struct wlr_xwm *xwm) {
	if (xwm->focus_surface && xwm->seat->selection_source) {
		xcb_set_selection_owner(xwm->xcb_conn,
			xwm->selection_window,
			xwm->atoms[CLIPBOARD],
			XCB_TIME_CURRENT_TIME);
	} else {
		xcb_set_selection_owner(xwm->xcb_conn,
			XCB_ATOM_NONE,
			xwm->atoms[CLIPBOARD],
			xwm->selection_timestamp);
	}
}

void xwm_set_seat(struct wlr_xwm *xwm, struct wlr_seat *seat) {
	assert(xwm);
	assert(seat);
	if (xwm->seat) {
		wl_list_remove(&xwm->seat_selection_change.link);
		xwm->seat = NULL;
	}

	wl_signal_add(&seat->events.selection, &xwm->seat_selection_change);
	xwm->seat_selection_change.notify = handle_seat_set_selection;
	xwm->seat = seat;
	handle_seat_set_selection(&xwm->seat_selection_change, seat);
}
