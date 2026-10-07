#include "activation.h"
#include "protocol.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QThread>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

Activation::Activation(QObject *parent, const QString &scope) : QObject(parent) {
    const auto user = QStandardPaths::writableLocation(QStandardPaths::HomeLocation).toUtf8() + scope.toUtf8();
    name_ = "lazarus-share-" + QString::fromLatin1(QCryptographicHash::hash(user, QCryptographicHash::Sha256).toHex().left(24));
    lock_ = std::make_unique<QLockFile>(QDir::tempPath() + "/" + name_ + ".lock");
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server_, &QLocalServer::newConnection, this, [this] {
        while (auto *socket = server_.nextPendingConnection()) {
            socket->setParent(this);
            QTimer::singleShot(3000, socket, &QLocalSocket::abort);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, this, [this, socket] {
                if (socket->bytesAvailable() > 2048) { socket->abort(); return; }
                if (!socket->canReadLine()) return;
                const auto doc = QJsonDocument::fromJson(socket->readLine());
                const auto link = doc.object()["invite"].toString();
                const auto secret = Protocol::inviteSecret(link).isEmpty() ? Protocol::secret(link) : Protocol::inviteSecret(link);
                if (!doc.isObject() || (!link.isEmpty() && secret.isEmpty())) { socket->abort(); return; }
                socket->write("ok\n"); socket->flush(); socket->disconnectFromServer();
                emit invitation(link);
            });
        }
    });
}
Activation::~Activation() {
    server_.close();
    QFile::remove(QDir::tempPath() + "/" + name_ + ".endpoint");
}
Activation::Result Activation::start(const QString &invite) {
    if (lock_->tryLock(0)) {
        // Only the process owning the election lock may remove a stale endpoint.
        QLocalServer::removeServer(name_);
        if (server_.listen(name_)) {
            QFile endpoint(QDir::tempPath() + "/" + name_ + ".endpoint");
            if (endpoint.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                endpoint.write(name_.toUtf8() + '\n');
                endpoint.close();
            }
            return Result::Receiver;
        }
        return Result::Failed;
    }
    QLocalSocket socket;
    for (int attempt = 0; attempt < 20; ++attempt) {
        socket.connectToServer(name_);
        if (socket.waitForConnected(100)) break;
        socket.abort(); QThread::msleep(100);
    }
    if (socket.state() != QLocalSocket::ConnectedState) return Result::Failed;
    socket.write(QJsonDocument(QJsonObject{{"invite", invite}}).toJson(QJsonDocument::Compact) + '\n');
    if (!socket.waitForBytesWritten(2000)) return Result::Failed;
    if (!socket.canReadLine()) socket.waitForReadyRead(2000);
    if (socket.readLine() == "ok\n") {
#ifdef Q_OS_WIN
        AllowSetForegroundWindow(ASFW_ANY);
#endif
        return Result::Forwarded;
    }
    return Result::Failed;
}
bool Activation::registerProtocol() {
    QString executable = qEnvironmentVariable("LAZARUS_LAUNCHER_PATH");
    if (executable.isEmpty()) executable = qEnvironmentVariable("APPIMAGE");
    if (executable.isEmpty()) executable = QCoreApplication::applicationFilePath();
    if (!QFile::exists(executable) || executable.contains('\n') || executable.contains('\r')) return false;
#ifdef Q_OS_WIN
    QSettings registry("HKEY_CURRENT_USER\\Software\\Classes\\lazarus-share", QSettings::NativeFormat);
    registry.setValue(".", "URL:Lazarus Share");
    registry.setValue("URL Protocol", "");
    registry.setValue("shell/open/command/.", "\"" + QDir::toNativeSeparators(executable) + "\" \"%1\"");
    registry.sync(); return registry.status() == QSettings::NoError;
#else
    // Desktop Exec quoting also escapes field codes in the executable path.
    QString quoted = executable;
    quoted.replace("\\", "\\\\\\\\").replace("\"", "\\\"").replace("`", "\\`").replace("$", "\\$").replace("%", "%%");
    const auto dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/applications";
    if (!QDir().mkpath(dir)) return false;
    QSaveFile file(dir + "/lazarus-share.desktop");
    if (!file.open(QIODevice::WriteOnly)) return false;
    const auto text = "[Desktop Entry]\nType=Application\nName=Lazarus Share\nExec=\"" + quoted + "\" %u\nTerminal=false\nCategories=Network;AudioVideo;\nMimeType=x-scheme-handler/lazarus-share;\n";
    if (file.write(text.toUtf8()) < 0 || !file.commit()) return false;
    QProcess mime;
    mime.start("xdg-mime", {"default", "lazarus-share.desktop", "x-scheme-handler/lazarus-share"});
    return mime.waitForFinished(3000) && mime.exitStatus() == QProcess::NormalExit && mime.exitCode() == 0;
#endif
}
