#include "fpsallocator.hpp"

#include "fpsloop.hpp"

static FPSAllocator* s_instance = nullptr;

FPSAllocator* FPSAllocator::instance() {
	if (!s_instance) s_instance = new FPSAllocator(nullptr);
	return s_instance;
}

FPSAllocator::FPSAllocator(QObject* parent): QObject(parent) {}
FPSAllocator::~FPSAllocator() {}

FpsLoop* FPSAllocator::requestTickerForFps(int fps) { return ensureFpsLoop(fps); }

bool FPSAllocator::hasFpsLoop(const int fps) const { return m_fpsLoops.contains(fps); }

FpsLoop* FPSAllocator::ensureFpsLoop(int fps) {

	QMutexLocker locker(&m_mutex);

	if (m_fpsLoops.contains(fps)) {

		m_counts[fps]++;
		return m_fpsLoops[fps];
	}

	auto* fpsLoop = new FpsLoop(fps);
	QQmlEngine::setObjectOwnership(fpsLoop, QQmlEngine::CppOwnership);

	m_fpsLoops.insert(fps, fpsLoop);
	m_counts.insert(fps, 1);
	return fpsLoop;
}

void FPSAllocator::requestTickerStart(FpsLoop* ticker) { ticker->start(); }
void FPSAllocator::requestTickerRemoval(FpsLoop* ticker) {

	QMutexLocker locker(&m_mutex);
	int fps = ticker->fps;

	m_counts[fps]--;
	if (m_counts[fps] <= 0) {
		ticker->stop();
		m_fpsLoops[fps]->deleteLater();
		m_fpsLoops.remove(fps);
		m_counts.remove(fps);
	}
}