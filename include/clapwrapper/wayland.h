/* wayland.h — Wayland embedding handoff for CLAP plugins hosted by
 * clap-wrapper (VST3 3.8 WaylandSurfaceID) and by hosts that emulate it.
 * Not a CLAP standard (clap#474 is open): this is the wrapper's own
 * documented convention, versioned in the extension id. Canonical copy:
 * clap-wrapper include/clapwrapper/wayland.h; other copies are mirrors.
 *
 * A host that exposes CLAP_HOST_WAYLAND_EMBED promises that clap_window.ptr
 * for api CLAP_WINDOW_API_WAYLAND with floating == false points at a
 * clap_wayland_embed_t. The host owns the connection and the parent surface
 * until gui destroy completes. The plugin attaches a wl_subsurface, reads the
 * socket only through its own event queue, never disconnects the display and
 * never dispatches host-owned queues. The host must not enter plugin callbacks
 * with an outstanding prepare_read on this connection, and must service the
 * compositor independently of plugin callbacks (EGL may wait synchronously
 * on its private queue during initialization and swap). */
#pragma once
#include <stdbool.h>
#include <stdint.h>

struct wl_display;
struct wl_surface;

#define CLAP_HOST_WAYLAND_EMBED "clap.host-wayland-embed/0"

typedef struct clap_host_wayland_embed {
    uint32_t size; /* sizeof(clap_host_wayland_embed_t) */
} clap_host_wayland_embed_t;

typedef struct clap_wayland_embed {
    uint32_t size;              /* sizeof(clap_wayland_embed_t); reject smaller */
    struct wl_display *display; /* host-owned, non-NULL */
    struct wl_surface *parent;  /* host-owned, non-NULL */
} clap_wayland_embed_t;

static inline bool clap_wayland_embed_valid(const clap_wayland_embed_t *embed) {
    return embed && embed->size >= sizeof(*embed) && embed->display && embed->parent;
}
