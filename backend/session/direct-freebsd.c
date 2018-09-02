#include <dev/evdev/input.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/consio.h>
#include <sys/ioctl.h>
#include <sys/kbio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>
#include <wayland-server.h>
#include <wlr/backend/session/interface.h>
#include <wlr/util/log.h>
#include "backend/session/direct-ipc.h"
#include "util/signal.h"

const struct session_impl session_direct;

struct direct_session {
	struct wlr_session base;
	int tty_fd;
	int old_tty;
	int old_kbmode;
	int sock;
	pid_t child;

	struct wl_event_source *vt_source;
};

static int direct_session_open(struct wlr_session *base, const char *path) {
	struct direct_session *session = wl_container_of(base, session, base);

	int fd = direct_ipc_open(session->sock, path);
	if (fd < 0) {
		wlr_log(WLR_ERROR, "Failed to open %s: %s%s", path, strerror(-fd),
			fd == -EINVAL ? "; is another display server running?" : "");
		return fd;
	}

	return fd;
}

static void direct_session_close(struct wlr_session *base, int fd) {
	struct direct_session *session = wl_container_of(base, session, base);

	struct stat st;
	if (fstat(fd, &st) < 0) {
		wlr_log_errno(WLR_ERROR, "Stat failed");
		close(fd);
		return;
	}

	char *name;
	name = devname(st.st_rdev, S_IFCHR);
	if (name == NULL) {
		wlr_log_errno(WLR_ERROR, "Failed to get device name");
		close(fd);
		return;
	}

	if (strncmp(name, "drm/", 4) == 0) {
		direct_ipc_dropmaster(session->sock, fd);
	} else if (strncmp(name, "input/event", 11)) {
		ioctl(fd, EVIOCREVOKE, 0);
	}

	close(fd);
}

static bool direct_change_vt(struct wlr_session *base, unsigned vt) {
	struct direct_session *session = wl_container_of(base, session, base);
	return ioctl(session->tty_fd, VT_ACTIVATE, (int)vt) == 0;
}

static void direct_session_destroy(struct wlr_session *base) {
	struct direct_session *session = wl_container_of(base, session, base);
	struct vt_mode mode = {
		.mode = VT_AUTO,
	};

	errno = 0;

	ioctl(session->tty_fd, KDSKBMODE, session->old_kbmode);
	ioctl(session->tty_fd, KDSETMODE, KD_TEXT);
	ioctl(session->tty_fd, VT_SETMODE, &mode);

	ioctl(session->tty_fd, VT_ACTIVATE, session->old_tty);

	if (errno) {
		wlr_log(WLR_ERROR, "Failed to restore tty");
	}

	direct_ipc_finish(session->sock, session->child);
	close(session->sock);

	wl_event_source_remove(session->vt_source);
	close(session->tty_fd);
	free(session);
}

static int vt_handler(int signo, void *data) {
	struct direct_session *session = data;

	if (session->base.active) {
		session->base.active = false;
		wlr_signal_emit_safe(&session->base.session_signal, session);

		char *name;
		struct wlr_device *dev;
		wl_list_for_each(dev, &session->base.devices, link) {
			name = devname(dev->dev, S_IFCHR);
			if (name != NULL && strncmp(name, "drm/", 4) == 0) {
				direct_ipc_dropmaster(session->sock, dev->fd);
			}
		}

		ioctl(session->tty_fd, VT_RELDISP, 1);
	} else {
		ioctl(session->tty_fd, VT_RELDISP, VT_ACKACQ);

		char *name;
		struct wlr_device *dev;
		wl_list_for_each(dev, &session->base.devices, link) {
			name = devname(dev->dev, S_IFCHR);
			if (name != NULL && strncmp(name, "drm/", 4) == 0) {
				direct_ipc_setmaster(session->sock, dev->fd);
			}
		}

		session->base.active = true;
		wlr_signal_emit_safe(&session->base.session_signal, session);
	}

	return 1;
}

static bool setup_tty(struct direct_session *session, struct wl_display *display) {
	int fd = -1, tty = -1, tty0_fd = -1, old_tty = 1;
	if ((tty0_fd = open("/dev/ttyv0", O_RDWR | O_CLOEXEC)) < 0) {
		wlr_log_errno(WLR_ERROR, "Could not open /dev/ttyv0 to find a free vt");
		goto error;
	}
	if (ioctl(tty0_fd, VT_GETACTIVE, &old_tty) != 0) {
		wlr_log_errno(WLR_ERROR, "Could not get active vt");
		goto error;
	}
	if (ioctl(tty0_fd, VT_OPENQRY, &tty) != 0) {
		wlr_log_errno(WLR_ERROR, "Could not find a free vt");
		goto error;
	}
	close(tty0_fd);
	char tty_path[64];
	snprintf(tty_path, sizeof(tty_path), "/dev/ttyv%d", tty - 1);
	wlr_log(WLR_INFO, "Using tty %s", tty_path);
	fd = open(tty_path, O_RDWR | O_NOCTTY | O_CLOEXEC);

	if (fd == -1) {
		wlr_log_errno(WLR_ERROR, "Cannot open tty");
		return false;
	}

	ioctl(fd, VT_ACTIVATE, tty);
	ioctl(fd, VT_WAITACTIVE, tty);

	int old_kbmode;
	if (ioctl(fd, KDGKBMODE, &old_kbmode)) {
		wlr_log_errno(WLR_ERROR, "Failed to read tty %d keyboard mode", tty);
		goto error;
	}

	if (ioctl(fd, KDSKBMODE, K_CODE)) {
		wlr_log_errno(WLR_ERROR, "Failed to set keyboard mode K_CODE on tty %d", tty);
		goto error;
	}

	if (ioctl(fd, KDSETMODE, KD_GRAPHICS)) {
		wlr_log_errno(WLR_ERROR, "Failed to set graphics mode on tty %d", tty);
		goto error;
	}

	struct vt_mode mode = {
		.mode = VT_PROCESS,
		.relsig = SIGUSR2,
		.acqsig = SIGUSR2,
		.frsig = SIGIO, // has to be set
	};

	if (ioctl(fd, VT_SETMODE, &mode) < 0) {
		wlr_log(WLR_ERROR, "Failed to take control of tty %d", tty);
		goto error;
	}

	struct wl_event_loop *loop = wl_display_get_event_loop(display);
	session->vt_source = wl_event_loop_add_signal(loop, SIGUSR2,
		vt_handler, session);
	if (!session->vt_source) {
		goto error;
	}

	session->base.vtnr = tty;
	session->tty_fd = fd;
	session->old_tty = old_tty;
	session->old_kbmode = old_kbmode;

	return true;

error:
	// In case we could not get the last active one, drop back to tty 1,
	// better than hanging in a useless blank console. Otherwise activate the
	// last active.
	ioctl(fd, VT_ACTIVATE, old_tty);
	close(fd);
	return false;
}

static struct wlr_session *direct_session_create(struct wl_display *disp) {
	struct direct_session *session = calloc(1, sizeof(*session));
	if (!session) {
		wlr_log_errno(WLR_ERROR, "Allocation failed");
		return NULL;
	}

	session->sock = direct_ipc_init(&session->child);
	if (session->sock == -1) {
		goto error_session;
	}

	if (!setup_tty(session, disp)) {
		goto error_ipc;
	}

	wlr_log(WLR_INFO, "Successfully loaded direct session");

	snprintf(session->base.seat, sizeof(session->base.seat), "seat0");
	session->base.impl = &session_direct;
	return &session->base;

error_ipc:
	direct_ipc_finish(session->sock, session->child);
	close(session->sock);
error_session:
	free(session);
	return NULL;
}

const struct session_impl session_direct = {
	.create = direct_session_create,
	.destroy = direct_session_destroy,
	.open = direct_session_open,
	.close = direct_session_close,
	.change_vt = direct_change_vt,
};
