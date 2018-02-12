#ifndef _ROOTSTON_SEAT_H
#define _ROOTSTON_SEAT_H
#include <wayland-server.h>
#include "rootston/input.h"
#include "rootston/keyboard.h"

struct roots_seat {
	struct roots_input *input;
	struct wlr_seat *seat;
	struct roots_cursor *cursor;
	struct wl_list link;

	// coordinates of the first touch point if it exists
	int32_t touch_id;
	double touch_x, touch_y;

	struct wl_list views; // roots_seat_view::link
	bool has_focus;

	struct wl_list drag_icons; // roots_drag_icon::link

	struct wl_list keyboards;
	struct wl_list pointers;
	struct wl_list touch;
	struct wl_list tablet_tools;

	struct wl_listener new_drag_icon;
	struct wl_listener destroy;
};

struct roots_seat_view {
	struct roots_seat *seat;
	struct roots_view *view;

	bool has_button_grab;
	double grab_sx;
	double grab_sy;

	struct wl_list link; // roots_seat::views

	struct wl_listener view_destroy;
};

struct roots_drag_icon {
	struct roots_seat *seat;
	struct wlr_drag_icon *wlr_drag_icon;
	struct wl_list link;

	double x, y;

	struct wl_listener surface_commit;
	struct wl_listener map;
	struct wl_listener destroy;
};

struct roots_pointer {
	struct roots_seat *seat;
	struct wlr_input_device *device;
	struct wl_listener device_destroy;
	struct wl_list link;
};

struct roots_touch {
	struct roots_seat *seat;
	struct wlr_input_device *device;
	struct wl_listener device_destroy;
	struct wl_list link;
};

struct roots_tablet_tool {
	struct roots_seat *seat;
	struct wlr_input_device *device;
	struct wl_listener device_destroy;
	struct wl_listener axis;
	struct wl_listener proximity;
	struct wl_listener tip;
	struct wl_listener button;
	struct wl_list link;
};

struct roots_seat *roots_seat_create(struct roots_input *input, char *name);

void roots_seat_destroy(struct roots_seat *seat);

void roots_seat_add_device(struct roots_seat *seat,
		struct wlr_input_device *device);

void roots_seat_remove_device(struct roots_seat *seat,
		struct wlr_input_device *device);

void roots_seat_configure_cursor(struct roots_seat *seat);

void roots_seat_configure_xcursor(struct roots_seat *seat);

bool roots_seat_has_meta_pressed(struct roots_seat *seat);

struct roots_view *roots_seat_get_focus(struct roots_seat *seat);

void roots_seat_set_focus(struct roots_seat *seat, struct roots_view *view);

void roots_seat_cycle_focus(struct roots_seat *seat);

void roots_seat_begin_move(struct roots_seat *seat, struct roots_view *view);

void roots_seat_begin_resize(struct roots_seat *seat, struct roots_view *view,
		uint32_t edges);

void roots_seat_begin_rotate(struct roots_seat *seat, struct roots_view *view);

void roots_seat_end_compositor_grab(struct roots_seat *seat);

struct roots_seat_view *roots_seat_view_from_view( struct roots_seat *seat,
	struct roots_view *view);

void roots_drag_icon_update_position(struct roots_drag_icon *icon);

void roots_drag_icon_damage_whole(struct roots_drag_icon *icon);

#endif
