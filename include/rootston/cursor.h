#ifndef _ROOTSTON_CURSOR_H
#define _ROOTSTON_CURSOR_H

#include "rootston/seat.h"

enum roots_cursor_mode {
	ROOTS_CURSOR_PASSTHROUGH = 0,
	ROOTS_CURSOR_MOVE = 1,
	ROOTS_CURSOR_RESIZE = 2,
	ROOTS_CURSOR_ROTATE = 3,
};

enum roots_cursor_resize_edge {
	ROOTS_CURSOR_RESIZE_EDGE_TOP = 1,
	ROOTS_CURSOR_RESIZE_EDGE_BOTTOM = 2,
	ROOTS_CURSOR_RESIZE_EDGE_LEFT = 4,
	ROOTS_CURSOR_RESIZE_EDGE_RIGHT = 8,
};

struct roots_input_event {
	uint32_t serial;
	struct wlr_cursor *cursor;
	struct wlr_input_device *device;
};

struct roots_cursor {
	struct roots_seat *seat;
	struct wlr_cursor *cursor;

	enum roots_cursor_mode mode;

	// state from input (review if this is necessary)
	struct roots_xcursor_theme *xcursor_theme;
	struct wlr_seat *wl_seat;
	struct wl_client *cursor_client;
	int offs_x, offs_y;
	int view_x, view_y, view_width, view_height;
	float view_rotation;
	uint32_t resize_edges;
	// Ring buffer of input events that could trigger move/resize/rotate
	int input_events_idx;
	struct wl_list touch_points;
	struct roots_input_event input_events[16];

	struct wl_listener motion;
	struct wl_listener motion_absolute;
	struct wl_listener button;
	struct wl_listener axis;

	struct wl_listener touch_down;
	struct wl_listener touch_up;
	struct wl_listener touch_motion;

	struct wl_listener tool_axis;
	struct wl_listener tool_tip;

	struct wl_listener pointer_grab_begin;
	struct wl_listener pointer_grab_end;

	struct wl_listener request_set_cursor;
};

struct roots_cursor *roots_cursor_create(struct roots_seat *seat);

void roots_cursor_destroy(struct roots_cursor *cursor);

void roots_cursor_handle_motion(struct roots_cursor *cursor,
		struct wlr_event_pointer_motion *event);

void roots_cursor_handle_motion_absolute(struct roots_cursor *cursor,
		struct wlr_event_pointer_motion_absolute *event);

void roots_cursor_handle_button(struct roots_cursor *cursor,
		struct wlr_event_pointer_button *event);

void roots_cursor_handle_axis(struct roots_cursor *cursor,
		struct wlr_event_pointer_axis *event);

void roots_cursor_handle_touch_down(struct roots_cursor *cursor,
		struct wlr_event_touch_down *event);

void roots_cursor_handle_touch_up(struct roots_cursor *cursor,
		struct wlr_event_touch_up *event);

void roots_cursor_handle_touch_motion(struct roots_cursor *cursor,
		struct wlr_event_touch_motion *event);

void roots_cursor_handle_tool_axis(struct roots_cursor *cursor,
		struct wlr_event_tablet_tool_axis *event);

void roots_cursor_handle_tool_tip(struct roots_cursor *cursor,
		struct wlr_event_tablet_tool_tip *event);

void roots_cursor_handle_request_set_cursor(struct roots_cursor *cursor,
		struct wlr_seat_pointer_request_set_cursor_event *event);

void roots_cursor_handle_pointer_grab_begin(struct roots_cursor *cursor,
		struct wlr_seat_pointer_grab *grab);

void roots_cursor_handle_pointer_grab_end(struct roots_cursor *cursor,
		struct wlr_seat_pointer_grab *grab);

#endif
