#ifndef WLR_TYPES_WLR_IDLE_H
#define WLR_TYPES_WLR_IDLE_H

#include <wayland-server.h>
#include <wlr/types/wlr_seat.h>

/**
 * Idle protocol is used to create timers which will notify the client when the
 * compositor does not receive any input for a given time(in milliseconds). Also
 * the client will be notify when the timer receve an activity notify and already
 * was in idle state. Besides this, the client is able to simulate user activity
 * which will reset the timers and at any time can destroy the timer.
 */


struct wlr_idle {
	struct wl_global *wl_global;
	struct wl_list idle_timers; // wlr_idle_timeout::link
	struct wl_event_loop *event_loop;

	struct wl_listener display_destroy;
	struct {
		struct wl_signal activity_notify;
	} events;

	void *data;
};

struct wlr_idle_timeout {
	struct wl_resource *resource;
	struct wl_list link;
	struct wlr_seat *seat;

	struct wl_event_source *idle_source;
	bool idle_state;
	uint32_t timeout; // milliseconds

	struct wl_listener input_listener;
	struct wl_listener seat_destroy;

	void *data;
};

struct wlr_idle *wlr_idle_create(struct wl_display *display);

void wlr_idle_destroy(struct wlr_idle *idle);

/**
 * Send notification to restart all timers for the given seat. Called by
 * compositor when there is an user activity event on that seat.
 */
void wlr_idle_notify_activity(struct wlr_idle *idle, struct wlr_seat *seat);
#endif
