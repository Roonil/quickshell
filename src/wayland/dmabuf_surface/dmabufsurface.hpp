#include <QQuickRhiItem>
#include <QQuickRhiItemRenderer>
#include <QString>
#include <qopengl.h>
#include <qsize.h>
#include <qtmetamacros.h>
#include <rhi/qrhi.h>

#include "dmabufmanager.hpp"

#pragma once
#include <QAtomicInt>

#include "fpsallocator.hpp"
#include "fpsloop.hpp"

class DMABufSurface: public QQuickRhiItem {
	Q_OBJECT
	QML_ELEMENT
private:
	friend class DMABufRenderer;
	FPSAllocator* fpsAllocator = FPSAllocator::instance();

	void dispatchUpdateToRenderer();

	void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

	void triggerSizeUpdate(int width, int height);

	QQuickRhiItemRenderer* m_rendererItem = nullptr;
	FpsLoop* m_loop = nullptr;

signals:
	void requestNextFrame();
	void emitToRenderer();

public:
	explicit DMABufSurface(QQuickItem* parent = nullptr);
	bool m_active = true;
	bool m_countedActive = false;

	~DMABufSurface() override {
		if (debug())
			qDebug() << "DESTROYING SURFACE" << manager() << ":"
			         << (manager() == nullptr ? "NULL" : manager()->socketPath().toStdString())
			         << m_active << "\n";
		if (manager() != nullptr && m_countedActive)
			emit manager() -> executeCommandSignal(visibilityChangeCommand(), 0, 0, 0);
	}

	QSizeF size = QSizeF(-1, -1), oldSize = QSizeF(-1, -1);

	bool active() { return m_active; }
	void setActive(bool active) {
		if (active && !m_active) markStaleIfSenderInactive();
		m_active = active;
		emit activeChanged();
	}

	std::atomic<bool> m_clearStaleBuffer {false};
	void markStaleIfSenderInactive() {
		if (m_manager != nullptr && m_manager->activeSurfaceCount() == 0)
			m_clearStaleBuffer.store(true);
	}

	bool m_ready = false;
	bool ready() { return m_ready; }
	void setReady(bool ready) {
		if (ready == m_ready) return;
		m_ready = ready;
		emit readyChanged();
	}

signals:
	void activeChanged();
	void readyChanged();

protected:
	QQuickRhiItemRenderer* createRenderer() override;
	void componentComplete() override;

	Q_PROPERTY(bool debug READ debug WRITE setDebug NOTIFY debugChanged);

	Q_PROPERTY(QString socketPath READ socketPath WRITE setSocketPath NOTIFY socketPathChanged);

	Q_PROPERTY(DMABufManager* manager READ manager NOTIFY managerChanged);

	Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged);

	Q_PROPERTY(bool ready READ ready NOTIFY readyChanged);

	Q_PROPERTY(
	    QString geometryChangeCommand READ geometryChangeCommand WRITE setGeometryChangeCommand NOTIFY
	        geometryChangeCommandChanged
	);

	Q_PROPERTY(
	    QString visibilityChangeCommand READ visibilityChangeCommand WRITE setVisibilityChangeCommand
	        NOTIFY visibilityChangeCommandChanged
	);

	Q_PROPERTY(int fps READ fps WRITE setFps NOTIFY fpsChanged);

public:
	int fps();
	void setFps(int fps);

private:
	int m_fps = 60;
	bool m_debug = false;
signals:
	void fpsChanged();
	void debugChanged();

public:
	QString socketPath();
	void setSocketPath(QString socketPath);

	DMABufManager* manager();
	void setManager(DMABufManager* manager);

	bool debug();
	void setDebug(bool debug);

private:
	QString m_socketPath = "";
	DMABufManager* m_manager = nullptr;

signals:
	void socketPathChanged();
	void managerChanged();
	void bufferUpdated();

private slots:
	void onBufferUpdated();
	void onSocketOpened();
	void onSocketClosed();

public:
	QString geometryChangeCommand();
	void setGeometryChangeCommand(QString geometryChangeCommand);

signals:
	void geometryChangeCommandChanged();

private:
	QString m_geometryChangeCommand = "";

public:
	QString visibilityChangeCommand();
	void setVisibilityChangeCommand(QString visibilityChangeCommand);

signals:
	void visibilityChangeCommandChanged();

private:
	QString m_visibilityChangeCommand = "";
	void triggerVisibilityChange();
};

class DMABufRenderer
    : public QObject
    , public QQuickRhiItemRenderer {
	Q_OBJECT
public:
	void updateFunction();
	void requestReimport();
	void deleteTexturesNow();

signals:
	void sizeSettled(int width, int height);

public:
	~DMABufRenderer() override { deleteTexturesNow(); };

protected:
	void initialize(QRhiCommandBuffer* cb) override;
	void synchronize(QQuickRhiItem* item) override;
	void render(QRhiCommandBuffer* cb) override;

private:
	bool m_debug = false;
	GLint qtFboId = -1;
	QSize frameBufferSize;
	QRhi* m_initializedRhi = nullptr;
	QSize size = QSize(-1, -1), cachedSize = QSize(-1, -1);

	DMABufManager* m_manager = nullptr;

private:
	int importEGLImage();
	EGLImageKHR eglImage = EGL_NO_IMAGE_KHR;
	QRhiTexture* rhiTexture = nullptr;
	QRhiTextureRenderTarget* rhiRenderTarget = nullptr;

	bool m_clearStaleBuffer = false;

public:
	bool texturesInitialised = false;
};
