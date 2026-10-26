#ifndef DMA_BUF_MANAGER_H
#define DMA_BUF_MANAGER_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <QMutex>
#include <QObject>
#include <QSocketNotifier>
#include <QString>
#include <sys/un.h>

struct DMABufferMessage {
	int width, height, pixelFormat;
	EGLint stride, offset;
	EGLuint64KHR modifier;
};

class DMABufManager: public QObject {
	Q_OBJECT

public:
	Q_PROPERTY(QString socketPath READ socketPath WRITE setSocketPath)

	static DMABufManager* instance();

	explicit DMABufManager(QObject* parent = nullptr);

	QString socketPath();
	~DMABufManager();

	void setSocketPath(const QString&);
	void executeCommand(QString command, int w, int h, int v);

	bool snapshotBuffer(DMABufferMessage& info, int& fd);
	bool hasBuffer();
	int activeSurfaceCount() const { return activeSurfaces.load(std::memory_order_relaxed); }

signals:
	void socketOpened();
	void bufferUpdated();
	void socketClosed();
	void executeCommandSignal(QString command, int w, int h, int v);

private slots:
	void onSocketReadable();
	void onSocketDestroyed();

private:
	int createSocket();
	bool recvMessage();
	QString expandTemplate(const QString& input, int w, int h, int v);

	QJSEngine engine;

	int socketFd = -1;
	int dmabufFd = -1;

	bool isFirstBuffer = true;

	QString m_socketPath;
	QSocketNotifier* notifier = nullptr;

	DMABufferMessage currentInfo {};
	QMutex m_bufferMutex;

private:
	std::atomic<int> activeSurfaces {0};
	QString visibilityChangeCommand = "", geometryChangeCommand = "";
};

#endif
