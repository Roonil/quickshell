#include "dmabufregistry.hpp"
#include <iostream>

#include "dmabufmanager.hpp"

DMABufRegistry* DMABufRegistry::s_instance = nullptr;

DMABufRegistry* DMABufRegistry::instance() {
	if (!s_instance) s_instance = new DMABufRegistry();
	return s_instance;
}

DMABufRegistry::DMABufRegistry(QObject* parent): QObject(parent) {}

bool DMABufRegistry::hasManager(const QString& key) const { return m_managers.contains(key); }

DMABufRegistry::~DMABufRegistry() {
	if (s_instance == this) s_instance = nullptr;

	qDeleteAll(m_managers);

	m_managers.clear();
}

DMABufManager* DMABufRegistry::ensureManager(const QString& key) {
	if (m_managers.contains(key)) {
		;

		return m_managers[key];
	}

	auto* mgr = new DMABufManager();
	QQmlEngine::setObjectOwnership(mgr, QQmlEngine::CppOwnership);
	mgr->setSocketPath(key);

	m_managers.insert(key, mgr);
	return mgr;
}
