#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <wayland-server.h>
#include "config.h"
#include <wlr/backend/session/interface.h>
#include <wlr/util/log.h>

#ifdef WLR_HAS_SYSTEMD
	#include <systemd/sd-bus.h>
	#include <systemd/sd-login.h>
#elif WLR_HAS_ELOGIND
	#include <elogind/sd-bus.h>
	#include <elogind/sd-login.h> 
#endif

enum { DRM_MAJOR = 226 };

const struct session_impl session_logind;

struct logind_session {
	struct wlr_session base;

	sd_bus *bus;
	struct wl_event_source *event;

	char *id;
	char *path;
};

static int logind_take_device(struct wlr_session *base, const char *path) {
	struct logind_session *session = wl_container_of(base, session, base);

	int ret;
	int fd = -1;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	struct stat st;
	if (stat(path, &st) < 0) {
		wlr_log(L_ERROR, "Failed to stat '%s'", path);
		return -1;
	}

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
		session->path, "org.freedesktop.login1.Session", "TakeDevice",
		&error, &msg, "uu", major(st.st_rdev), minor(st.st_rdev));
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to take device '%s': %s", path, error.message);
		goto error;
	}

	int paused = 0;
	ret = sd_bus_message_read(msg, "hb", &fd, &paused);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to parse D-Bus response for '%s': %s",
			path, strerror(-ret));
		goto error;
	}

	// The original fd seems to be closed when the message is freed
	// so we just clone it.
	fd = fcntl(fd, F_DUPFD_CLOEXEC, 0);
	if (fd == -1) {
		wlr_log(L_ERROR, "Failed to clone file descriptor for '%s': %s",
			path, strerror(errno));
		goto error;
	}

error:
	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return fd;
}

static void logind_release_device(struct wlr_session *base, int fd) {
	struct logind_session *session = wl_container_of(base, session, base);

	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	struct stat st;
	if (fstat(fd, &st) < 0) {
		wlr_log(L_ERROR, "Failed to stat device '%d'", fd);
		return;
	}

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
		session->path, "org.freedesktop.login1.Session", "ReleaseDevice",
		&error, &msg, "uu", major(st.st_rdev), minor(st.st_rdev));
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to release device '%d'", fd);
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
}

static bool logind_change_vt(struct wlr_session *base, unsigned vt) {
	struct logind_session *session = wl_container_of(base, session, base);

	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
		"/org/freedesktop/login1/seat/self", "org.freedesktop.login1.Seat", "SwitchTo",
		&error, &msg, "u", (uint32_t)vt);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to change to vt '%d'", vt);
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return ret >= 0;
}

static bool find_session_path(struct logind_session *session) {
	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
			"/org/freedesktop/login1", "org.freedesktop.login1.Manager",
			"GetSession", &error, &msg, "s", session->id);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to get session path: %s", strerror(-ret));
		goto out;
	}

	const char *path;

	ret = sd_bus_message_read(msg, "o", &path);
	if (ret < 0) {
		wlr_log(L_ERROR, "Could not parse session path: %s", strerror(-ret));
		goto out;
	}

	session->path = strdup(path);

out:
	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);

	return ret >= 0;
}

static bool session_activate(struct logind_session *session) {
	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
		session->path, "org.freedesktop.login1.Session", "Activate",
		&error, &msg, "");
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to activate session");
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return ret >= 0;
}

static bool take_control(struct logind_session *session) {
	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
		session->path, "org.freedesktop.login1.Session", "TakeControl",
		&error, &msg, "b", false);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to take control of session");
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
	return ret >= 0;
}

static void release_control(struct logind_session *session) {
	int ret;
	sd_bus_message *msg = NULL;
	sd_bus_error error = SD_BUS_ERROR_NULL;

	ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
		session->path, "org.freedesktop.login1.Session", "ReleaseControl",
		&error, &msg, "");
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to release control of session");
	}

	sd_bus_error_free(&error);
	sd_bus_message_unref(msg);
}

static void logind_session_destroy(struct wlr_session *base) {
	struct logind_session *session = wl_container_of(base, session, base);

	release_control(session);

	wl_event_source_remove(session->event);
	sd_bus_unref(session->bus);
	free(session->id);
	free(session->path);
	free(session);
}

static int session_removed(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error) {
	wlr_log(L_INFO, "SessionRemoved signal received");
	return 0;
}

static struct wlr_device *find_device(struct wlr_session *session, dev_t devnum) {
	struct wlr_device *dev;

	wl_list_for_each(dev, &session->devices, link) {
		if (dev->dev == devnum) {
			return dev;
		}
	}

	wlr_log(L_ERROR, "Tried to use dev_t %lu not opened by session",
		(unsigned long)devnum);
	assert(0);
}

static int pause_device(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error) {
	struct logind_session *session = userdata;
	int ret;

	uint32_t major, minor;
	const char *type;
	ret = sd_bus_message_read(msg, "uus", &major, &minor, &type);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to parse D-Bus response for PauseDevice: %s",
			strerror(-ret));
		goto error;
	}

	if (major == DRM_MAJOR) {
		session->base.active = false;
		wl_signal_emit(&session->base.session_signal, session);
	}

	if (strcmp(type, "pause") == 0) {
		ret = sd_bus_call_method(session->bus, "org.freedesktop.login1",
			session->path, "org.freedesktop.login1.Session", "PauseDeviceComplete",
			ret_error, &msg, "uu", major, minor);
		if (ret < 0) {
			wlr_log(L_ERROR, "Failed to send PauseDeviceComplete signal: %s",
				strerror(-ret));
		}
	}

error:
	return 0;
}

static int resume_device(sd_bus_message *msg, void *userdata, sd_bus_error *ret_error) {
	struct logind_session *session = userdata;
	int ret;

	int fd;
	uint32_t major, minor;
	ret = sd_bus_message_read(msg, "uuh", &major, &minor, &fd);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to parse D-Bus response for ResumeDevice: %s",
			strerror(-ret));
		goto error;
	}

	if (major == DRM_MAJOR) {
		struct wlr_device *dev = find_device(&session->base, makedev(major, minor));
		dup2(fd, dev->fd);

		if (!session->base.active) {
			session->base.active = true;
			wl_signal_emit(&session->base.session_signal, session);
		}
	}

error:
	return 0;
}

static bool add_signal_matches(struct logind_session *session) {
	int ret;

	char str[256];
	const char *fmt = "type='signal',"
		"sender='org.freedesktop.login1',"
		"interface='org.freedesktop.login1.%s',"
		"member='%s',"
		"path='%s'";

	snprintf(str, sizeof(str), fmt, "Manager", "SessionRemoved", "/org/freedesktop/login1");
	ret = sd_bus_add_match(session->bus, NULL, str, session_removed, session);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to add D-Bus match: %s", strerror(-ret));
		return false;
	}

	snprintf(str, sizeof(str), fmt, "Session", "PauseDevice", session->path);
	ret = sd_bus_add_match(session->bus, NULL, str, pause_device, session);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to add D-Bus match: %s", strerror(-ret));
		return false;
	}

	snprintf(str, sizeof(str), fmt, "Session", "ResumeDevice", session->path);
	ret = sd_bus_add_match(session->bus, NULL, str, resume_device, session);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to add D-Bus match: %s", strerror(-ret));
		return false;
	}

	return true;
}

static int dbus_event(int fd, uint32_t mask, void *data) {
	sd_bus *bus = data;
	while (sd_bus_process(bus, NULL) > 0);
	return 1;
}

static struct wlr_session *logind_session_create(struct wl_display *disp) {
	int ret;
	struct logind_session *session = calloc(1, sizeof(*session));
	if (!session) {
		wlr_log(L_ERROR, "Allocation failed: %s", strerror(errno));
		return NULL;
	}

	ret = sd_pid_get_session(getpid(), &session->id);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to get session id: %s", strerror(-ret));
		goto error;
	}

	char *seat;
	ret = sd_session_get_seat(session->id, &seat);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to get seat id: %s", strerror(-ret));
		goto error;
	}
	snprintf(session->base.seat, sizeof(session->base.seat), "%s", seat);

	if (strcmp(seat, "seat0") == 0) {
		ret = sd_session_get_vt(session->id, &session->base.vtnr);
		if (ret < 0) {
			wlr_log(L_ERROR, "Session not running in virtual terminal");
			goto error;
		}
	}
	free(seat);

	ret = sd_bus_default_system(&session->bus);
	if (ret < 0) {
		wlr_log(L_ERROR, "Failed to open D-Bus connection: %s", strerror(-ret));
		goto error;
	}

	if (!find_session_path(session)) {
		sd_bus_unref(session->bus);
		goto error;
	}

	if (!add_signal_matches(session)) {
		goto error_bus;
	}

	struct wl_event_loop *event_loop = wl_display_get_event_loop(disp);
	session->event = wl_event_loop_add_fd(event_loop, sd_bus_get_fd(session->bus),
		WL_EVENT_READABLE, dbus_event, session->bus);

	if (!session_activate(session)) {
		goto error_bus;
	}

	if (!take_control(session)) {
		goto error_bus;
	}

	wlr_log(L_INFO, "Successfully loaded logind session");

	session->base.impl = &session_logind;
	return &session->base;

error_bus:
	sd_bus_unref(session->bus);
	free(session->path);

error:
	free(session->id);
	return NULL;
}

const struct session_impl session_logind = {
	.create = logind_session_create,
	.destroy = logind_session_destroy,
	.open = logind_take_device,
	.close = logind_release_device,
	.change_vt = logind_change_vt,
};
