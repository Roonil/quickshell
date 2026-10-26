find_package(PkgConfig REQUIRED)
pkg_check_modules(dmabufsurface-deps REQUIRED IMPORTED_TARGET egl)

qt_add_library(quickshell-wayland-dmabufsurface STATIC
	dmabufsurface.cpp
	dmabufmanager.cpp
	dmabufregistry.cpp
	fpsallocator.cpp
	fpsloop.cpp
)

qt_add_qml_module(quickshell-wayland-dmabufsurface
	URI Quickshell.Wayland._DMABufSurface
	VERSION 0.1
	DEPENDENCIES QtQuick
)

install_qml_module(quickshell-wayland-dmabufsurface)

qs_add_module_deps_light(quickshell-wayland-dmabufsurface quickshell-core)

target_link_libraries(quickshell-wayland-dmabufsurface PRIVATE
	Qt::Quick Qt::QuickPrivate
	PkgConfig::dmabufsurface-deps
)

qs_module_pch(quickshell-wayland-dmabufsurface SET large)

target_link_libraries(quickshell PRIVATE quickshell-wayland-dmabufsurfaceplugin)
