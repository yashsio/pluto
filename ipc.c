// ipc.c -- status/control IPC for Pluto.
//
// See ipc.h for the wire format.  This deliberately stays tiny: the bar is
// stateless, the WM sends a complete snapshot on every change, and the only
// command a bar needs is "switch this monitor to these tags".
//
// Copyright 2026 Yash Sharma.  See LICENSE for details.

#define _GNU_SOURCE

#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include "pluto.h"
#include "ipc.h"

#define MAX_CLIENTS 8
#define LINE_MAX_LEN 512
#define SNAPSHOT_MAX 32768

struct IpcClient {
	int fd;
	char line[LINE_MAX_LEN];
	size_t line_len;
};

static struct IpcClient clients[MAX_CLIENTS];
static int listen_fd = -1;
static char socket_path[256];

static void close_client(struct IpcClient *c) {
	if (c->fd < 0)
		return;
	close(c->fd);
	c->fd = -1;
	c->line_len = 0;
}

// The bar and the WM share the environment, so both derive the same path.
static int make_socket_path(char *buf, size_t size) {
	const char *runtime = getenv("XDG_RUNTIME_DIR");
	const char *display = getenv("WAYLAND_DISPLAY");
	char safe[128];
	size_t i, j = 0;

	if (runtime == NULL || runtime[0] == '\0')
		runtime = "/tmp";
	if (display == NULL || display[0] == '\0')
		display = "wayland-0";

	for (i = 0; display[i] != '\0' && j < sizeof(safe) - 1; i++) {
		unsigned char ch = display[i];
		if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z')
				|| (ch >= '0' && ch <= '9')
				|| ch == '-' || ch == '_' || ch == '.')
			safe[j++] = ch;
		else
			safe[j++] = '_';
	}
	safe[j] = '\0';

	int n = snprintf(buf, size, "%s/pluto-%s.sock", runtime, safe);
	return n < 0 || (size_t)n >= size ? -1 : 0;
}

static size_t append(char *buf, size_t cap, size_t len, const char *fmt, ...) {
	va_list ap;
	int n;

	if (len >= cap)
		return cap;

	va_start(ap, fmt);
	n = vsnprintf(buf + len, cap - len, fmt, ap);
	va_end(ap);

	if (n < 0)
		return len;
	if ((size_t)n >= cap - len)
		return cap;
	return len + n;
}

// Titles come from clients and can contain anything except NUL.  Keep the
// snapshot one-line and bounded.
static void copy_title(char *dst, size_t size, const char *src) {
	size_t i;

	if (src == NULL)
		src = "";
	for (i = 0; src[i] != '\0' && i + 1 < size; i++)
		dst[i] = (src[i] == '\n' || src[i] == '\r') ? ' ' : src[i];
	dst[i] = '\0';
}

static void build_snapshot(char *buf, size_t cap) {
	struct Monitor *m;
	struct Client *c;
	size_t len = 0;

	len = append(buf, cap, len, "begin\n");

	wl_list_for_each(m, &wm.monitors, link) {
		unsigned int occupied = 0;
		unsigned int focused_tags = m->tagset[m->seltags] & tagmask;
		const char *layout = (m->lt != NULL && m->lt->symbol != NULL)
			? m->lt->symbol : "-";
		char title[256];
		int focused = m == selmon;
		int floating = m->sel != NULL && m->sel->isfloating;

		wl_list_for_each(c, &wm.clients, link)
			if (c->mon == m)
				occupied |= c->tags & tagmask;

		copy_title(title, sizeof(title),
				m->sel != NULL ? m->sel->title : NULL);

		len = append(buf, cap, len,
				"monitor %s %x %x %s %d %d %s\n",
				m->name[0] != '\0' ? m->name : "?",
				focused_tags, occupied, layout,
				focused, floating, title);
	}

	append(buf, cap, len, "end\n");
}

static void send_to(struct IpcClient *cl, const char *buf, size_t len) {
	size_t off = 0;

	if (cl->fd < 0)
		return;
	while (off < len) {
		ssize_t n = send(cl->fd, buf + off, len - off, MSG_NOSIGNAL);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			close_client(cl);
			return;
		}
		off += (size_t)n;
	}
}

static void broadcast_snapshot(int force) {
	static char last[SNAPSHOT_MAX];
	static size_t last_len;
	char snapshot[SNAPSHOT_MAX];
	size_t len;

	build_snapshot(snapshot, sizeof(snapshot));
	len = strlen(snapshot);

	// Do not wake the bar for no reason.  This is also called from
	// manage/render sequences, which can otherwise run every frame.
	if (!force && len == last_len && len <= sizeof(last)
			&& (len == 0 || memcmp(snapshot, last, len) == 0))
		return;
	if (len <= sizeof(last)) {
		memcpy(last, snapshot, len);
		last_len = len;
	} else {
		last_len = 0;
	}

	for (size_t i = 0; i < LENGTH(clients); i++)
		send_to(&clients[i], snapshot, len);
}

void ipc_update(void) {
	if (listen_fd < 0)
		return;
	broadcast_snapshot(0);
}

static struct Monitor *monitor_by_name(const char *name) {
	struct Monitor *m;

	wl_list_for_each(m, &wm.monitors, link)
		if (strcmp(m->name, name) == 0)
			return m;

	return NULL;
}

static void command_view(const char *name, unsigned int tags) {
	struct Monitor *m = monitor_by_name(name);

	if (m == NULL)
		return;

	tags &= tagmask;
	if (tags == 0)
		return;

	// Clicking a tag selects that monitor too, as in dwm.
	selmon = m;
	if (m->tagset[m->seltags] == tags) {
		manage_dirty();
		ipc_update();
		return;
	}

	m->tagset[m->seltags] = tags;
	m->sel = first_visible(m);
	focus(m->sel);
	manage_dirty();
	ipc_update();
}

static void handle_command(char *line) {
	char name[64];
	unsigned int tags;
	char extra;

	if (sscanf(line, "view %63s %x %c", name, &tags, &extra) == 2)
		command_view(name, tags);
	else if (strcmp(line, "refresh") == 0)
		ipc_update();
}

static void read_client(struct IpcClient *c) {
	char buf[1024];

	for (;;) {
		ssize_t n = read(c->fd, buf, sizeof(buf));
		if (n < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return;
			close_client(c);
			return;
		}
		if (n == 0) {
			close_client(c);
			return;
		}

		for (ssize_t i = 0; i < n; i++) {
			if (buf[i] == '\n') {
				c->line[c->line_len] = '\0';
				if (c->line_len > 0
						&& c->line[c->line_len - 1] == '\r')
					c->line[--c->line_len] = '\0';
				if (c->line_len > 0)
					handle_command(c->line);
				c->line_len = 0;
			} else if (c->line_len < sizeof(c->line) - 1) {
				c->line[c->line_len++] = buf[i];
			} else {
				// Overlong command: discard it.
				c->line_len = 0;
			}
		}
	}
}

int ipc_add_pollfds(struct pollfd *fds, int nfds, int max) {
	if (listen_fd < 0)
		return nfds;

	if (nfds < max) {
		fds[nfds].fd = listen_fd;
		fds[nfds].events = POLLIN;
		nfds++;
	}

	for (size_t i = 0; i < LENGTH(clients) && nfds < max; i++) {
		if (clients[i].fd < 0)
			continue;
		fds[nfds].fd = clients[i].fd;
		fds[nfds].events = POLLIN;
		nfds++;
	}

	return nfds;
}

void ipc_handle_pollfds(const struct pollfd *fds, int nfds) {
	for (int i = 0; i < nfds; i++) {
		if (fds[i].revents == 0)
			continue;

		if (fds[i].fd == listen_fd) {
			for (;;) {
				int fd = accept4(listen_fd, NULL, NULL,
						SOCK_CLOEXEC | SOCK_NONBLOCK);
				if (fd < 0) {
					if (errno == EINTR)
						continue;
					break;
				}

				struct IpcClient *slot = NULL;
				for (size_t j = 0; j < LENGTH(clients); j++) {
					if (clients[j].fd < 0) {
						slot = &clients[j];
						break;
					}
				}
				if (slot == NULL) {
					close(fd);
					continue;
				}

				slot->fd = fd;
				slot->line_len = 0;
				broadcast_snapshot(1);
			}
			continue;
		}

		for (size_t j = 0; j < LENGTH(clients); j++)
			if (clients[j].fd == fds[i].fd)
				read_client(&clients[j]);
	}
}

void ipc_init(void) {
	struct sigaction sa;

	// Writing to a bar that just died must not kill the WM.
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);

	for (size_t i = 0; i < LENGTH(clients); i++)
		clients[i].fd = -1;

	if (make_socket_path(socket_path, sizeof(socket_path)) < 0) {
		fprintf(stderr, "pluto: IPC socket path is too long\n");
		return;
	}

	listen_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (listen_fd < 0) {
		fprintf(stderr, "pluto: cannot create IPC socket: %s\n",
				strerror(errno));
		return;
	}

	struct sockaddr_un addr;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	if (strlen(socket_path) >= sizeof(addr.sun_path)) {
		fprintf(stderr, "pluto: IPC socket path is too long\n");
		close(listen_fd);
		listen_fd = -1;
		return;
	}
	strcpy(addr.sun_path, socket_path);

	unlink(socket_path);
	if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0
			|| listen(listen_fd, 4) < 0) {
		fprintf(stderr, "pluto: cannot listen on %s: %s\n",
				socket_path, strerror(errno));
		close(listen_fd);
		listen_fd = -1;
		return;
	}
	chmod(socket_path, 0600);

	atexit(ipc_finish);
}

void ipc_finish(void) {
	for (size_t i = 0; i < LENGTH(clients); i++)
		close_client(&clients[i]);

	if (listen_fd >= 0) {
		close(listen_fd);
		listen_fd = -1;
	}

	if (socket_path[0] != '\0')
		unlink(socket_path);
}
