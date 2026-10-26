#ifndef FPSALLOCATOR_HPP
#define FPSALLOCATOR_HPP

#include <qhash.h>

#include "fpsloop.hpp"
#pragma once
#include <QHash>
#include <QObject>
#include <QString>
#include <qmutex.h>

class FPSAllocator: public QObject {
	Q_OBJECT
public:
	explicit FPSAllocator(QObject* parent = nullptr);

	static FPSAllocator* instance();
	~FPSAllocator();

	bool hasFpsLoop(const int fps) const;

	FpsLoop* ensureFpsLoop(int fps);

	FpsLoop* requestTickerForFps(int fps);

	void requestTickerStart(FpsLoop* ticker);
	void requestTickerRemoval(FpsLoop* ticker);

private:
	QHash<int, FpsLoop*> m_fpsLoops;
	QHash<int, int> m_counts;
	QMutex m_mutex;
};

#endif
