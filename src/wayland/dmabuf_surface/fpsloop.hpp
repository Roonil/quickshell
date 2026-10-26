#ifndef FPSLOOP_HPP
#define FPSLOOP_HPP

#include <QAtomicInt>
#include <QObject>
#include <QThread>

class FpsLoop: public QObject {
	Q_OBJECT
signals:
	void tick();

public:
	int fps = 60;

	FpsLoop(int fps);
	~FpsLoop();

public slots:
	void start();
	void stop();

private:
	QThread* m_thread = nullptr;
	QAtomicInt m_running {0};
};

#endif
