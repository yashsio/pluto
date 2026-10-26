// ipc.h -- small status/control IPC for Pluto.
//
// A Unix stream socket, one JSON-free line protocol.  It exists so a status
// bar can read per-monitor tag/layout/title state and ask the WM to switch
// tags without implementing river-window-management-v1 itself.
//
// The socket lives at $XDG_RUNTIME_DIR/pluto-<WAYLAND_DISPLAY>.sock.
//
// Server -> client, on connect and whenever state changes:
//
//     begin
//     monitor <name> <focused_tags_hex> <occupied_tags_hex> <layout>
//             <focused 0|1> <floating 0|1> <title>
//     ...
//     end
//
// Client -> server:
//
//     view <name> <tags_hex>
//
// Tags are bitmasks, bit 0 is tag 1.  Names are wl_output names.

#ifndef PLUTO_IPC_H
#define PLUTO_IPC_H

#include <poll.h>

void ipc_init(void);
void ipc_finish(void);

// Fill fds/nfds for poll(2), returning the new count.
int ipc_add_pollfds(struct pollfd *fds, int nfds, int max);

// Handle the revents filled in by poll(2).
void ipc_handle_pollfds(const struct pollfd *fds, int nfds);

// Push the current state to every connected client.  Cheap when nothing
// changed: identical snapshots are not sent twice.
void ipc_update(void);

#endif
