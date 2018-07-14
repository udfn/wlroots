#ifndef WLR_TYPES_WLR_TABLET_V2_H
#define WLR_TYPES_WLR_TABLET_V2_H

#include <wayland-server.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_input_device.h>

#include "tablet-unstable-v2-protocol.h"

/* This can probably be even lower,the tools don't have a lot of buttons */
#define WLR_TABLEt_V2_TOOL_BUTTONS_CAP 16

struct wlr_tablet_client_v2;
struct wlr_tablet_tool_client_v2;
struct wlr_tablet_pad_client_v2;

struct wlr_tablet_manager_v2 {
	struct wl_global *wl_global;
	struct wl_list clients; // wlr_tablet_manager_client_v2::link
	struct wl_list seats; // wlr_tablet_seat_v2::link

	struct wl_listener display_destroy;

	void *data;
};

struct wlr_tablet_v2_tablet {
	struct wl_list link; // wlr_tablet_seat_v2::tablets
	struct wlr_tablet_tool *wlr_tool;
	struct wlr_input_device *wlr_device;
	struct wl_list clients; // wlr_tablet_client_v2::tablet_link

	struct wl_listener tool_destroy;

	struct wlr_tablet_client_v2 *current_client;
};

struct wlr_tablet_v2_tablet_tool {
	struct wl_list link; // wlr_tablet_seat_v2::tablets
	struct wlr_tablet_tool_tool *wlr_tool;
	struct wl_list clients; // wlr_tablet_tool_client_v2::tablet_link

	struct wl_listener tool_destroy;

	struct wlr_tablet_tool_client_v2 *current_client;
	struct wlr_surface *focused_surface;
	struct wl_listener surface_destroy;
	struct wl_listener client_destroy;

	uint32_t button_serial;
	size_t num_buttons;
	uint32_t pressed_buttons[WLR_TABLEt_V2_TOOL_BUTTONS_CAP];

	struct {
		struct wl_signal set_cursor; // struct wlr_tablet_v2_event_cursor
	} events;
};

struct wlr_tablet_v2_tablet_pad {
	struct wl_list link; // wlr_tablet_seat_v2::pads
	struct wlr_tablet_pad *wlr_pad;
	struct wlr_input_device *wlr_device;
	struct wl_list clients; // wlr_tablet_pad_client_v2::tablet_link

	size_t group_count;
	uint32_t *groups;

	struct wl_listener pad_destroy;

	struct wlr_tablet_pad_client_v2 *current_client;

	struct {
		struct wl_signal button_feedback; // struct wlr_tablet_v2_event_feedback
		struct wl_signal strip_feedback; // struct wlr_tablet_v2_event_feedback
		struct wl_signal ring_feedback; // struct wlr_tablet_v2_event_feedback
	} events;
};

struct wlr_tablet_v2_event_cursor {
	struct wlr_surface *surface;
	uint32_t serial;
	int32_t hotspot_x;
	int32_t hotspot_y;
};

struct wlr_tablet_v2_event_feedback {
	const char *description;
	size_t index;
	uint32_t serial;
};

struct wlr_tablet_v2_tablet *wlr_tablet_create(
		struct wlr_tablet_manager_v2 *manager,
		struct wlr_seat *wlr_seat,
		struct wlr_input_device *wlr_device);

struct wlr_tablet_v2_tablet_pad *wlr_tablet_pad_create(
		struct wlr_tablet_manager_v2 *manager,
		struct wlr_seat *wlr_seat,
		struct wlr_input_device *wlr_device);

struct wlr_tablet_v2_tablet_tool *wlr_tablet_tool_create(
		struct wlr_tablet_manager_v2 *manager,
		struct wlr_seat *wlr_seat,
		struct wlr_tablet_tool_tool *wlr_tool);

struct wlr_tablet_manager_v2 *wlr_tablet_v2_create(struct wl_display *display);
void wlr_tablet_v2_destroy(struct wlr_tablet_manager_v2 *manager);

uint32_t wlr_send_tablet_v2_tablet_tool_proximity_in(
	struct wlr_tablet_v2_tablet_tool *tool,
	struct wlr_tablet_v2_tablet *tablet,
	struct wlr_surface *surface);

void wlr_send_tablet_v2_tablet_tool_motion(
		struct wlr_tablet_v2_tablet_tool *tool, double x, double y);

void wlr_send_tablet_v2_tablet_tool_distance(
	struct wlr_tablet_v2_tablet_tool *tool, uint32_t distance);

void wlr_send_tablet_v2_tablet_tool_wheel(
	struct wlr_tablet_v2_tablet_tool *tool, double delta, int32_t clicks);

void wlr_send_tablet_v2_tablet_tool_proximity_out(
	struct wlr_tablet_v2_tablet_tool *tool);

uint32_t wlr_send_tablet_v2_tablet_tool_button(
		struct wlr_tablet_v2_tablet_tool *tool, uint32_t button,
		enum zwp_tablet_pad_v2_button_state state);

uint32_t wlr_send_tablet_v2_tablet_pad_enter(
		struct wlr_tablet_v2_tablet_pad *pad,
		struct wlr_tablet_v2_tablet *tablet,
		struct wlr_surface *surface);

void wlr_send_tablet_v2_tablet_pad_button(
		struct wlr_tablet_v2_tablet_pad *pad, size_t button,
		uint32_t time, enum zwp_tablet_pad_v2_button_state state);

void wlr_send_tablet_v2_tablet_pad_strip( struct wlr_tablet_v2_tablet_pad *pad,
		uint32_t strip, double position, bool finger, uint32_t time);
void wlr_send_tablet_v2_tablet_pad_ring(struct wlr_tablet_v2_tablet_pad *pad,
		uint32_t ring, double position, bool finger, uint32_t time);

uint32_t wlr_send_tablet_v2_tablet_pad_leave(struct wlr_tablet_v2_tablet_pad *pad,
		struct wlr_surface *surface);

uint32_t wlr_send_tablet_v2_tablet_pad_mode(struct wlr_tablet_v2_tablet_pad *pad,
		size_t group, uint32_t mode, uint32_t time);

bool wlr_surface_accepts_tablet_v2(struct wlr_tablet_v2_tablet *tablet,
		struct wlr_surface *surface);
#endif /* WLR_TYPES_WLR_TABLET_V2_H */
