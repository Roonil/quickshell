#include "fpsloop.hpp"

#include <QEventLoop>
#include <QTimer>

FpsLoop::FpsLoop(int fps) { this->fps = fps; }

FpsLoop::~FpsLoop() { stop(); }

void FpsLoop::start() {
	if (m_running.loadAcquire()) return;
	m_running.storeRelease(1);

	auto* safeEmitter = this;
	int fps = this->fps;

	m_thread = QThread::create([safeEmitter, fps] {
		QEventLoop loop;
		QTimer timer;
		timer.setTimerType(Qt::PreciseTimer);
		const qint64 targetFrameTimeMs = 1000 / fps;

		QObject::connect(&timer, &QTimer::timeout, safeEmitter, [safeEmitter]() {
			if (!safeEmitter->m_running.loadAcquire()) return;
			QMetaObject::invokeMethod(safeEmitter, &FpsLoop::tick, Qt::QueuedConnection);
		});

		timer.start(targetFrameTimeMs);

		loop.exec();
	});

	connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	m_thread->start();
}

void FpsLoop::stop() {
	if (!m_running.loadAcquire()) return;
	m_running.storeRelease(0);

	if (m_thread) {
		m_thread->quit();
		m_thread = nullptr;
	}
}
