#include "dmabufsurface.hpp"

#include <QProcess>
#include <QQuickWindow>
#include <QRegularExpression>
#include <private/qrhigles2_p.h>
#include <qcolor.h>
#include <qnamespace.h>
#include <qopenglcontext.h>
#include <qopenglextrafunctions.h>
#include <qquickitem.h>
#include <qsize.h>
#include <rhi/qrhi.h>
#include <unistd.h>

#include "dmabufmanager.hpp"
#include "dmabufregistry.hpp"

#ifndef GL_TEXTURE_EXTERNAL_OES
#define GL_TEXTURE_EXTERNAL_OES 0x8D65
#endif

namespace {

using EGLImageTargetTexture2DOESFn = void (*)(GLenum, void*);

EGLImageTargetTexture2DOESFn eglImageTargetTexture2DOES() {
	static const auto fn = reinterpret_cast<EGLImageTargetTexture2DOESFn>(
	    eglGetProcAddress("glEGLImageTargetTexture2DOES")
	);
	return fn;
}

} // namespace

DMABufSurface::DMABufSurface(QQuickItem* parent): QQuickRhiItem(parent) {

	if (debug()) qDebug() << "Creating Surface\n";
	setAlphaBlending(true);
	setFlag(ItemHasContents, true);

	connect(
	    this,
	    &DMABufSurface::activeChanged,
	    this,
	    &DMABufSurface::triggerVisibilityChange,
	    Qt::DirectConnection
	);
	connect(this, &DMABufSurface::managerChanged, this, [this]() {
		if (manager() && manager()->hasBuffer()) {

			onBufferUpdated();
			update();
		}
	});
}

void DMABufSurface::triggerVisibilityChange() {
	if (debug())
		qDebug() << "Triggering change! " << manager() << ":"
		         << (manager() == nullptr ? "NULL" : manager()->socketPath().toStdString()) << m_active
		         << "\n";

	if (!isComponentComplete() || manager() == nullptr || m_active == m_countedActive) return;

	m_countedActive = m_active;
	emit manager() -> executeCommandSignal(visibilityChangeCommand(), 0, 0, m_countedActive);
}

void DMABufSurface::componentComplete() {
	QQuickRhiItem::componentComplete();

	if (m_manager && active()) {
		markStaleIfSenderInactive();
		triggerVisibilityChange();
	}
}

void DMABufSurface::dispatchUpdateToRenderer() {
	if (m_rendererItem == nullptr || manager() == nullptr) return;

	if (active()) emit emitToRenderer();
}

void DMABufSurface::triggerSizeUpdate(int width, int height) {
	if (!(geometryChangeCommand().isNull() || geometryChangeCommand().isEmpty())) {

		const qreal dpr = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
		width = std::max(1, qRound(width * dpr));
		height = std::max(1, qRound(height * dpr));

		if (manager() != nullptr)
			emit manager() -> executeCommandSignal(geometryChangeCommand(), width, height, 0);
	}
}

bool DMABufSurface::debug() { return m_debug; }

void DMABufSurface::setDebug(bool debug) {
	bool shouldEmit = debug != m_debug;

	m_debug = debug;

	if (shouldEmit) emit debugChanged();
}

void DMABufSurface::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
	size = newGeometry.size();

	QQuickItem::geometryChange(newGeometry, oldGeometry);

	if (newGeometry.isEmpty() || newGeometry.width() <= 0 || newGeometry.height() <= 0) {
		return;
	}
}

QQuickRhiItemRenderer* DMABufSurface::createRenderer() {

	m_rendererItem = new DMABufRenderer();

	if (QQuickWindow* w = window()) {
		((DMABufRenderer*) (m_rendererItem))->moveToThread(w->thread());
	}
	connect(
	    this,
	    &DMABufSurface::emitToRenderer,
	    (DMABufRenderer*) m_rendererItem,
	    &DMABufRenderer::updateFunction,
	    Qt::QueuedConnection
	);

	connect(
	    this,
	    &DMABufSurface::bufferUpdated,
	    (DMABufRenderer*) m_rendererItem,
	    &DMABufRenderer::requestReimport,
	    Qt::QueuedConnection
	);

	connect(
	    (DMABufRenderer*) m_rendererItem,
	    &DMABufRenderer::sizeSettled,
	    this,
	    &DMABufSurface::triggerSizeUpdate,
	    Qt::QueuedConnection
	);

	return m_rendererItem;
}

QString DMABufSurface::socketPath() { return m_socketPath; }

void DMABufSurface::setSocketPath(QString socketPath) {
	if (socketPath == m_socketPath) return;

	m_socketPath = socketPath;
	setManager(
	    socketPath.isEmpty() ? nullptr : DMABufRegistry::instance()->ensureManager(socketPath)
	);
	emit socketPathChanged();
}

DMABufManager* DMABufSurface::manager() { return m_manager; }

void DMABufSurface::setManager(DMABufManager* manager) {
	if (debug()) qDebug() << "TRYING to set Manager: Old, New: " << m_manager << ":" << manager;
	if (manager == m_manager) return;

	if (m_manager != nullptr) {
		if (m_countedActive) {
			emit m_manager->executeCommandSignal(visibilityChangeCommand(), 0, 0, 0);
			m_countedActive = false;
		}

		disconnect(m_manager, &DMABufManager::bufferUpdated, this, &DMABufSurface::onBufferUpdated);
		disconnect(m_manager, &DMABufManager::socketOpened, this, &DMABufSurface::onSocketOpened);
		disconnect(m_manager, &DMABufManager::socketClosed, this, &DMABufSurface::onSocketClosed);
	}

	setReady(false);

	if (manager == nullptr) {
		m_manager = nullptr;
		emit(managerChanged());
		return;
	}

	m_manager = manager;
	connect(m_manager, &DMABufManager::bufferUpdated, this, &DMABufSurface::onBufferUpdated);
	connect(m_manager, &DMABufManager::socketOpened, this, &DMABufSurface::onSocketOpened);
	connect(m_manager, &DMABufManager::socketClosed, this, &DMABufSurface::onSocketClosed);
	emit(managerChanged());

	if (m_manager && active()) {
		markStaleIfSenderInactive();
		triggerVisibilityChange();
	}
}

void DMABufSurface::onSocketOpened() {
	if (m_loop != nullptr) fpsAllocator->requestTickerStart(m_loop);
}

void DMABufSurface::onSocketClosed() {
	setReady(false);

	if (m_loop == nullptr) return;

	disconnect(m_loop, &FpsLoop::tick, this, &DMABufSurface::dispatchUpdateToRenderer);
	fpsAllocator->requestTickerRemoval(m_loop);
	m_loop = nullptr;
}

void DMABufSurface::onBufferUpdated() {

	setReady(true);
	emit bufferUpdated();

	if (m_loop == nullptr) {
		m_loop = fpsAllocator->requestTickerForFps(fps());

		fpsAllocator->requestTickerStart(m_loop);

		connect(
		    m_loop,
		    &FpsLoop::tick,
		    this,
		    &DMABufSurface::dispatchUpdateToRenderer,
		    Qt::QueuedConnection
		);
	}
}

QString DMABufSurface::geometryChangeCommand() { return m_geometryChangeCommand; }

void DMABufSurface::setGeometryChangeCommand(QString geometryChangeCommand) {
	m_geometryChangeCommand = geometryChangeCommand;
}

QString DMABufSurface::visibilityChangeCommand() { return m_visibilityChangeCommand; }

void DMABufSurface::setVisibilityChangeCommand(QString visibilityChangeCommand) {
	m_visibilityChangeCommand = visibilityChangeCommand;
}

void DMABufRenderer::updateFunction() { update(); }

int DMABufSurface::fps() { return m_fps; }

void DMABufSurface::setFps(int fps) {
	if (m_fps != fps) {
		m_fps = fps;

		if (m_loop != nullptr) {
			disconnect(m_loop, &FpsLoop::tick, this, &DMABufSurface::dispatchUpdateToRenderer);
			fpsAllocator->requestTickerRemoval(m_loop);

			m_loop = fpsAllocator->requestTickerForFps(m_fps);
			fpsAllocator->requestTickerStart(m_loop);
			connect(
			    m_loop,
			    &FpsLoop::tick,
			    this,
			    &DMABufSurface::dispatchUpdateToRenderer,
			    Qt::QueuedConnection
			);
		}

		emit fpsChanged();
	}
}

void DMABufRenderer::initialize(QRhiCommandBuffer* cb) {
	qtFboId = -1;

	if (rhi() != m_initializedRhi) {
		m_initializedRhi = rhi();
		deleteTexturesNow();
		frameBufferSize = QSize();
	}
}

void DMABufRenderer::requestReimport() { texturesInitialised = false; }

void DMABufRenderer::deleteTexturesNow() {

	if (rhiTexture != nullptr) {
		rhiTexture->destroy();
		rhiTexture = nullptr;
		texturesInitialised = false;
	}

	if (rhiRenderTarget != nullptr) {
		rhiRenderTarget->destroy();
		rhiRenderTarget = nullptr;
	}

	if (eglImage != EGL_NO_IMAGE_KHR) eglDestroyImage(eglGetCurrentDisplay(), eglImage);

	eglImage = EGL_NO_IMAGE_KHR;
}

void DMABufRenderer::synchronize(QQuickRhiItem* item) {
	auto* surface = static_cast<DMABufSurface*>(item);

	m_debug = surface->debug();

	if (this->m_manager != surface->manager()) {
		this->m_manager = surface->manager();
		requestReimport();
	}

	size = surface->size.toSize();

	if (surface->m_clearStaleBuffer.exchange(false)) m_clearStaleBuffer = true;
}

void DMABufRenderer::render(QRhiCommandBuffer* cb) {
	if (m_debug)
		qDebug() << "Rendering! " << m_manager << ":"
		         << (m_manager == nullptr ? "NULL" : m_manager->socketPath().toStdString()) << "\n";

	QRhi* rhiDevice = rhi();

	if (!rhiDevice) return;

	if (rhiDevice->backend() != QRhi::OpenGLES2) return;

	QRhiRenderTarget* rt = renderTarget();
	if (!rt) return;

	QSize targetWindowSize = rt->pixelSize();

	if (targetWindowSize.isEmpty()) return;

	if (!cb || !rt) return;

	if (!texturesInitialised) importEGLImage();

	if (rhiTexture == nullptr || rhiTexture->nativeTexture().object == 0 || frameBufferSize.isEmpty())
	{

		cb->beginPass(renderTarget(), Qt::transparent, QRhiDepthStencilClearValue());
		cb->endPass();

		return;
	}

	if (size != QSize(-1, -1)) {

		if (cachedSize != size) {
			cachedSize = size;
		} else {

			emit sizeSettled(size.width(), size.height());

			cachedSize = QSize(-1, -1);
			size = cachedSize;
		}
	}

	cb->beginExternal();

	QOpenGLExtraFunctions* gl = QOpenGLContext::currentContext()->extraFunctions();

	gl->glBindFramebuffer(
	    GL_READ_FRAMEBUFFER,
	    ((QGles2TextureRenderTarget*) (rhiRenderTarget))->framebuffer
	);

	gl->glFramebufferTexture2D(
	    GL_READ_FRAMEBUFFER,
	    GL_COLOR_ATTACHMENT0,
	    GL_TEXTURE_2D,
	    rhiTexture->nativeTexture().object,
	    0
	);

	gl->glBindFramebuffer(
	    GL_DRAW_FRAMEBUFFER,
	    ((QGles2TextureRenderTarget*) (renderTarget()))->framebuffer
	);

	gl->glDisable(GL_SCISSOR_TEST);
	gl->glDisable(GL_DEPTH_TEST);
	gl->glDisable(GL_STENCIL_TEST);

	if (m_clearStaleBuffer) {
		gl->glBindFramebuffer(
		    GL_DRAW_FRAMEBUFFER,
		    ((QGles2TextureRenderTarget*) (rhiRenderTarget))->framebuffer
		);
		gl->glClearColor(0, 0, 0, 0);
		gl->glClear(GL_COLOR_BUFFER_BIT);
		gl->glBindFramebuffer(
		    GL_DRAW_FRAMEBUFFER,
		    ((QGles2TextureRenderTarget*) (renderTarget()))->framebuffer
		);
		m_clearStaleBuffer = false;
	}

	gl->glBlitFramebuffer(
	    0,
	    0,
	    frameBufferSize.width(),
	    frameBufferSize.height(),
	    0,
	    0,
	    targetWindowSize.width(),
	    targetWindowSize.height(),
	    GL_COLOR_BUFFER_BIT,
	    GL_LINEAR
	);

	gl->glBindFramebuffer(GL_FRAMEBUFFER, 0);

	cb->endExternal();
}

int DMABufRenderer::importEGLImage() {

	if (!m_manager) return -1;

	const auto imageTargetTexture = eglImageTargetTexture2DOES();
	if (!imageTargetTexture) {
		qWarning() << "DMABufSurface: glEGLImageTargetTexture2DOES is not available";
		return -1;
	}

	DMABufferMessage info {};
	int fd = -1;
	if (!m_manager->snapshotBuffer(info, fd)) return -1;

	const EGLAttrib attrs[] = {
	    EGL_WIDTH,
	    (EGLAttrib) info.width,
	    EGL_HEIGHT,
	    (EGLAttrib) info.height,
	    EGL_LINUX_DRM_FOURCC_EXT,
	    info.pixelFormat,
	    EGL_DMA_BUF_PLANE0_FD_EXT,
	    fd,
	    EGL_DMA_BUF_PLANE0_OFFSET_EXT,
	    info.offset,
	    EGL_DMA_BUF_PLANE0_PITCH_EXT,
	    info.stride,
	    EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT,
	    (uint32_t) info.modifier,
	    EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT,
	    (uint32_t) (info.modifier >> 32),
	    EGL_NONE
	};

	EGLDisplay dpy = eglGetCurrentDisplay();
	eglImage = eglCreateImage(dpy, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attrs);

	close(fd);

	if (eglImage == EGL_NO_IMAGE_KHR) {
		const EGLint error = eglGetError();
		qWarning().nospace() << "DMABufSurface: eglCreateImage failed, error 0x" << Qt::hex << error
		                     << Qt::dec << " socket=" << m_manager->socketPath()
		                     << " size=" << info.width << "x" << info.height << " fourcc=0x" << Qt::hex
		                     << info.pixelFormat << Qt::dec << " stride=" << info.stride
		                     << " offset=" << info.offset << " modifier=0x" << Qt::hex << info.modifier;
		return -1;
	}

	frameBufferSize = QSize(info.width, info.height);

	if (rhiTexture != nullptr) {
		rhiTexture->destroy();
	}
	rhiTexture = rhi()->newTexture(QRhiTexture::RGBA16F, QSize(info.width, info.height), 1);
	rhiTexture->create();

	if (rhiRenderTarget != nullptr) {
		rhiRenderTarget->destroy();
	}
	rhiRenderTarget = rhi()->newTextureRenderTarget(QRhiTextureRenderTargetDescription(rhiTexture));
	rhiRenderTarget->create();

	QOpenGLExtraFunctions* gl = QOpenGLContext::currentContext()->extraFunctions();

	for (int i = 0; i < 8 && gl->glGetError() != GL_NO_ERROR; i++) {}

	gl->glBindTexture(GL_TEXTURE_2D, rhiTexture->nativeTexture().object);
	imageTargetTexture(GL_TEXTURE_2D, eglImage);
	const GLenum targetError = gl->glGetError();

	gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	gl->glBindTexture(GL_TEXTURE_EXTERNAL_OES, 0);
	gl->glBindTexture(GL_TEXTURE_2D, 0);
	gl->glActiveTexture(GL_TEXTURE0);

	if (eglImage != EGL_NO_IMAGE_KHR) {
		eglDestroyImage(eglGetCurrentDisplay(), eglImage);
		eglImage = EGL_NO_IMAGE_KHR;
	}

	texturesInitialised = true;

	if (targetError != GL_NO_ERROR) {
		qWarning().nospace() << "DMABufSurface: glEGLImageTargetTexture2DOES failed, error 0x"
		                     << Qt::hex << targetError << Qt::dec
		                     << " socket=" << m_manager->socketPath() << " size=" << info.width << "x"
		                     << info.height << " fourcc=0x" << Qt::hex << info.pixelFormat << Qt::dec
		                     << " modifier=0x" << Qt::hex << info.modifier;
		deleteTexturesNow();
		frameBufferSize = QSize();
		return -1;
	}

	return 0;
}
