#include "dmabufmanager.hpp"
#include <atomic>
#include <cstring>
#include <errno.h>

#include <QProcess>
#include <fcntl.h>
#include <qdebug.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qprocess.h>
#include <qregularexpression.h>
#include <qthread.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static DMABufManager* g_instance = nullptr;

DMABufManager* DMABufManager::instance() {
	if (!g_instance) g_instance = new DMABufManager();
	return g_instance;
}

DMABufManager::DMABufManager(QObject* parent): QObject(parent) {
	connect(
	    this,
	    &DMABufManager::executeCommandSignal,
	    this,
	    &DMABufManager::executeCommand,
	    Qt::QueuedConnection
	);
}
QString DMABufManager::socketPath() { return m_socketPath; }

QString DMABufManager::expandTemplate(const QString& input, int w, int h, int v) {

	QString output = input;

	engine.globalObject().setProperty("w", w);
	engine.globalObject().setProperty("h", h);
	engine.globalObject().setProperty("v", v);

	QRegularExpression re(R"(\{\{(.*?)\}\})");

	auto it = re.globalMatch(input);

	while (it.hasNext()) {
		auto match = it.next();

		QString fullMatch = match.captured(0);
		QString expression = match.captured(1).trimmed();

		QJSValue result = engine.evaluate(expression);

		QString replacement;

		if (result.isError()) {
			replacement = "<js_error>";
		} else {
			replacement = result.toString();
		}

		output.replace(fullMatch, replacement);
	}

	return output;
}

void DMABufManager::executeCommand(QString command, int w, int h, int v) {
	if (command == nullptr || command.isEmpty()) return;

	if (w == 0 && h == 0) {

		if (visibilityChangeCommand.isEmpty()) visibilityChangeCommand = command;

		if (v) {
			const int previous = activeSurfaces.fetch_add(1, std::memory_order_relaxed);

			if (previous != 0) return;

		} else {
			int current = activeSurfaces.load(std::memory_order_relaxed);

			while (current > 0) {
				if (activeSurfaces.compare_exchange_weak(
				        current,
				        current - 1,
				        std::memory_order_relaxed,
				        std::memory_order_relaxed
				    ))
				{

					if (current > 1) return;

					break;
				}
			}

			if (current <= 0) return;
		}

		v = activeSurfaces.load(std::memory_order_relaxed) > 0;
	}

	QString cmd = expandTemplate(w == 0 && h == 0 ? visibilityChangeCommand : command, w, h, v);

	QProcess process;
	process.setStandardOutputFile(QProcess::nullDevice());
	process.setStandardErrorFile(QProcess::nullDevice());

	process.startDetached("/bin/sh", {"-c", cmd});
}

DMABufManager::~DMABufManager() {
	if (notifier) notifier->setEnabled(false);

	{
		QMutexLocker locker(&m_bufferMutex);
		if (dmabufFd >= 0) close(dmabufFd);
		dmabufFd = -1;
	}

	if (socketFd >= 0) close(socketFd);
}

void DMABufManager::setSocketPath(const QString& socketPathString) {
	m_socketPath = socketPathString;

	socketFd = createSocket();

	notifier = new QSocketNotifier(socketFd, QSocketNotifier::Read, this);
	connect(notifier, &QSocketNotifier::activated, this, &DMABufManager::onSocketReadable);
	connect(notifier, &QSocketNotifier::destroyed, this, &DMABufManager::onSocketDestroyed);
}

int DMABufManager::createSocket() {
	int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
	if (fd < 0) {
		qWarning() << "Failed to create receiver socket";
		return -1;
	}

	sockaddr_un addr {};
	addr.sun_family = AF_UNIX;

	std::string pathStr = socketPath().toStdString();
	strncpy(addr.sun_path, pathStr.c_str(), sizeof(addr.sun_path) - 1);

	if (::connect(fd, (sockaddr*) &addr, sizeof(addr)) < 0) {
		qWarning() << "Receiver failed to connect to sender at path:" << socketPath();
		::close(fd);
		emit socketClosed();
		return -1;
	}

	fcntl(fd, F_SETFL, O_NONBLOCK);

	return fd;
}

void DMABufManager::onSocketReadable() {
	if (recvMessage()) {
		if (isFirstBuffer) emit socketOpened();

		emit bufferUpdated();

		if (isFirstBuffer) {
			isFirstBuffer = false;
		}
	}
}

void DMABufManager::onSocketDestroyed() { emit socketClosed(); }

bool DMABufManager::recvMessage() {

	if (socketFd < 0) {
		return false;
	}

	DMABufferMessage header {};
	char control[CMSG_SPACE(sizeof(int))] {};
	iovec iov {&header, sizeof(header)};
	msghdr msg {};

	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);

	ssize_t bytesRead = recvmsg(socketFd, &msg, 0);

	if (bytesRead == 0) {
		emit socketClosed();

		isFirstBuffer = true;

		{
			QMutexLocker locker(&m_bufferMutex);
			if (dmabufFd >= 0) {
				close(dmabufFd);
				dmabufFd = -1;
			}
		}

		close(socketFd);
		socketFd = -1;
		return false;
	}

	if (bytesRead < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			return false;
		}

		{
			QMutexLocker locker(&m_bufferMutex);
			if (dmabufFd >= 0) {
				close(dmabufFd);
				dmabufFd = -1;
			}
		}

		close(socketFd);
		socketFd = -1;
		return false;
	}

	if (bytesRead < static_cast<ssize_t>(sizeof(header))) {
		return false;
	}

	int receivedFd = -1;
	int fdsInMessage = 0;

	for (cmsghdr* cmsg = CMSG_FIRSTHDR(&msg); cmsg != nullptr; cmsg = CMSG_NXTHDR(&msg, cmsg)) {

		if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
			int* fds = (int*) CMSG_DATA(cmsg);
			fdsInMessage += (cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int);
			if (receivedFd >= 0) close(receivedFd);

			receivedFd = fds[0];
			fcntl(receivedFd, F_SETFD, FD_CLOEXEC);
		}
	}

	if (receivedFd < 0) {
		return false;
	}

	if (lseek(receivedFd, 0, SEEK_END) < 0) {
		char target[256] {};
		readlink(
		    QStringLiteral("/proc/self/fd/%1").arg(receivedFd).toLocal8Bit().constData(),
		    target,
		    sizeof(target) - 1
		);
		qWarning().nospace() << "DMABufManager: received an fd that is not a dma-buf on "
		                     << socketPath() << ": " << target << " (fds in message: " << fdsInMessage
		                     << ", control truncated: " << bool(msg.msg_flags & MSG_CTRUNC) << ")";
		close(receivedFd);
		return false;
	}

	QMutexLocker locker(&m_bufferMutex);
	if (dmabufFd >= 0) close(dmabufFd);

	dmabufFd = receivedFd;
	currentInfo = header;

	return true;
}

bool DMABufManager::snapshotBuffer(DMABufferMessage& info, int& fd) {
	QMutexLocker locker(&m_bufferMutex);
	if (dmabufFd < 0) return false;

	fd = fcntl(dmabufFd, F_DUPFD_CLOEXEC, 0);
	if (fd < 0) return false;

	info = currentInfo;
	return true;
}

bool DMABufManager::hasBuffer() {
	QMutexLocker locker(&m_bufferMutex);
	return dmabufFd >= 0;
}
