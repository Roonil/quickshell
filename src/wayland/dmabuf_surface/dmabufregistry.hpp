#ifndef DMABUFREGISTRY_HPP
#define DMABUFREGISTRY_HPP

#pragma once
#include <QHash>
#include <QObject>
#include <QString>

#include "dmabufmanager.hpp"

class DMABufRegistry: public QObject {
public:
	static DMABufRegistry* instance();

	bool hasManager(const QString& key) const;

	DMABufManager* ensureManager(const QString& key);
	~DMABufRegistry();

private:
	explicit DMABufRegistry(QObject* parent = nullptr);
	static DMABufRegistry* s_instance;

	QHash<QString, DMABufManager*> m_managers;
};

#endif
