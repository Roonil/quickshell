# Quickshell, with DMA-BUF surfaces

A fork of [Quickshell](https://quickshell.outfoxxed.me) that adds **`DMABufSurface`**: a QML item that shows a buffer rendered by another process, imported straight from the GPU with no copies. It was built to show [WayVes](https://github.com/Roonil/WayVes) visualisers inside [wayves-shell](https://github.com/Roonil/wayves-shell), but it works with anything that sends DMA-BUFs over the same socket protocol.

```qml
import Quickshell.Wayland

DMABufSurface {
    anchors.fill: parent
    socketPath: "/tmp/WayVes/ncs.sock"
    fps: 60
    visibilityChangeCommand: "WayVes -c ncs -v {{v}}"
    geometryChangeCommand: "WayVes -c ncs -w {{w}} -h {{h}}"
}
```

**Documentation: [DMABUF.md](DMABUF.md)** covers the properties, the socket protocol, frame pacing and the known limitations.

## What this fork adds

- `DMABufSurface` (`src/wayland/dmabuf_surface/dmabufsurface.*`): imports the buffer into a `QQuickRhiItem` and redraws at its own `fps`, not at the display's refresh rate.
- `DMABufManager` and `DMABufRegistry` (`dmabufmanager.*`, `dmabufregistry.*`): one connection per socket path, shared by every surface that shows it.
- `FPSAllocator` and `FpsLoop` (`fpsallocator.*`, `fpsloop.*`): one ticker thread per frame rate, shared by every surface running at it.
- A layer-shell fix: with `QT_SCALE_FACTOR` below 1, `ExclusionMode.Ignore` surfaces no longer leave a gap under other panels' exclusive zones.

## Building

Same as upstream; see [BUILD.md](BUILD.md). The DMA-BUF code also needs **EGL** with `EGL_EXT_image_dma_buf_import`.

```sh
cmake -GNinja -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
ninja -C build
```

---

## Upstream README

See the [website](https://quickshell.outfoxxed.me) for more information
and installation instructions.

This repo is hosted at:
- https://git.outfoxxed.me/quickshell/quickshell
- https://github.com/quickshell-mirror/quickshell

### Contributing / Development
- [HACKING.md](HACKING.md) - Development instructions and policy.
- [CONTRIBUTING.md](CONTRIBUTING.md) - Contribution policy.
- [BUILD.md](BUILD.md) - Packaging and build instructions.

#### License

<sup>
Licensed under the GNU LGPL 3.
</sup>

<br>

<sub>
Unless you explicitly state otherwise, any contribution submitted
for inclusion shall be licensed as above, without any additional
terms or conditions.
</sub>
