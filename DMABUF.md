# DMABufSurface

`DMABufSurface` shows a GPU buffer that another process renders, the way an `Image` shows a file. The other process exports its render target as a DMA-BUF and passes the file descriptor over a Unix socket. Quickshell imports the buffer as an EGL image and samples it directly, so frames are never copied through the CPU.

It lives in the `Quickshell.Wayland` module:

```qml
import Quickshell.Wayland

DMABufSurface {
    width: 400
    height: 400
    socketPath: "/tmp/WayVes/ncs.sock"
    fps: 60
    active: visible
    visibilityChangeCommand: "WayVes -c ncs -v {{v}}"
    geometryChangeCommand: "WayVes -c ncs -w {{w}} -h {{h}}"
}
```

## Properties

| Property | Type | Default | Description |
| --- | --- | --- | --- |
| `socketPath` | `string` | `""` | The sender's socket. Surfaces with the same path share one connection and one buffer. An empty string detaches the surface. |
| `fps` | `int` | `60` | How often the surface redraws. Independent of the display's refresh rate and of how fast the sender renders. |
| `active` | `bool` | `true` | Whether the surface is in use. An inactive surface stops redrawing and counts as hidden for `visibilityChangeCommand`. |
| `ready` | `bool` | `false` | Read-only. `true` once the connection is up and a buffer has arrived. Use it to show a fallback until then. |
| `visibilityChangeCommand` | `string` | `""` | Shell command run when the socket's visibility changes. `{{v}}` becomes `1` or `0`. |
| `geometryChangeCommand` | `string` | `""` | Shell command run when the surface's size settles. `{{w}}` and `{{h}}` become the size in device pixels. |
| `manager` | `DMABufManager` | | Read-only. The shared connection behind `socketPath`. |
| `debug` | `bool` | `false` | Logs the surface's lifecycle. |

### Commands

Commands run through `/bin/sh -c`, detached, with their output discarded.

Each `{{ }}` placeholder holds a JavaScript expression evaluated with `w`, `h` and `v` in scope, so `{{w}}`, `{{w / 2}}` and `{{v ? "show" : "hide"}}` all work.

**Visibility is counted per socket, not per surface.** With three surfaces on one socket, the command runs with `v = 1` when the first one becomes active and with `v = 0` when the last one goes inactive. Toggling the others in between runs nothing, so the sender only stops rendering when nobody is showing it. The first `visibilityChangeCommand` a socket sees is the one it keeps using.

**Geometry is reported in device pixels**, after the window's scale factor is applied. A 400×400 surface at scale 1.5 asks the sender for 600×600, so the buffer matches the screen pixel for pixel.

### Frame pacing

The sender writes into the same buffer every frame, and the surface reads from it on its own timer, at `fps`. There's no fence or swap between them; in practice this hasn't produced visible tearing.

Surfaces with the same `fps` share one ticker thread. Each tick only redraws surfaces that are `active` and have a buffer, so a hidden or disconnected surface costs nothing. Keep `fps` as low as the content allows: a surface redraws its whole area on every tick even when the sender has nothing new.

## Protocol

For anyone writing a sender other than WayVes:

1. The sender creates a `SOCK_SEQPACKET` Unix socket at the path and listens on it. One receiver at a time: a new connection replaces the old one.
2. The receiver connects. It does this once, when the first surface sets that `socketPath`.
3. The sender replies with one message per buffer: the struct below as the payload, and the DMA-BUF fd as `SCM_RIGHTS` ancillary data. It sends one straight away if it already has a buffer, and again whenever the buffer is re-created (after a resize, say).
4. Closing the connection tells the receiver the buffer is gone; its surfaces become not `ready`.

```c
struct DMABufferMessage {
    int width, height;
    int pixelFormat;       /* DRM fourcc */
    EGLint stride, offset; /* plane 0 */
    EGLuint64KHR modifier;
};
```

The struct is sent as raw bytes, so both sides need the same layout (same architecture, natural alignment). Only single-plane formats are supported.

With EGL, `eglExportDMABUFImageQueryMESA` and `eglExportDMABUFImageMESA` give everything the message needs. On the receiving side, the fields map to `EGL_LINUX_DRM_FOURCC_EXT`, `EGL_DMA_BUF_PLANE0_*` and the two modifier halves for `eglCreateImage(..., EGL_LINUX_DMA_BUF_EXT, ...)`.

## Limitations

- **No reconnecting.** A socket path connects once, when it's first used. Start the sender before Quickshell. If the sender restarts, reload Quickshell.
- **Same GPU only.** The buffer is imported on the GPU that renders Quickshell, so the sender has to render on that GPU too.
- **No explicit sync.** See [Frame pacing](#frame-pacing).
- **Single plane only.** Multi-plane formats aren't imported.

Tested with WayVes on NVIDIA (proprietary driver) and Intel, on Hyprland.
