#include "update_transaction.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLockFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace Updates {
QString root() { return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/updates"; }
QString portablePath() {
#ifdef Q_OS_WIN
    const auto path = qEnvironmentVariable("LAZARUS_LAUNCHER_PATH");
#else
    const auto path = qEnvironmentVariable("APPIMAGE");
#endif
    return path.isEmpty() ? QString{} : QFileInfo(path).absoluteFilePath();
}
QString helperSource() {
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + "/update-runtime";
#else
    return QCoreApplication::applicationDirPath() + "/../libexec/update-runtime";
#endif
}
bool writeJson(const QString &path, const QJsonObject &object) {
    QSaveFile file(path);
    auto bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
QJsonObject readJson(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536) return {};
    return QJsonDocument::fromJson(file.readAll()).object();
}
bool validTransaction(const QString &journal, const QJsonObject &o) {
    const auto id = o["id"].toString();
    if (QUuid(id).isNull() || QUuid(id).toString(QUuid::WithoutBraces) != id) return false;
    const auto dir = QFileInfo(journal).absolutePath();
    if (QFileInfo(dir).isSymLink() || QFileInfo(journal).isSymLink()) return false;
    if (QDir::cleanPath(dir) != QDir::cleanPath(root() + "/" + id) || QFileInfo(journal).fileName() != "transaction.json") return false;
    const auto target = o["target"].toString();
    if (!QDir::isAbsolutePath(target) || QFileInfo(target).isSymLink()) return false;
    const auto prefix = QFileInfo(target).absolutePath() + "/.lazarus-update-" + id;
    return o["staged"].toString() == prefix + ".new" && o["backup"].toString() == prefix + ".old"
        && o["size"].toDouble() > 0 && o["sha256"].toString().size() == 64;
}
bool verifyFile(const QString &path, qint64 size, const QByteArray &sha256) {
    QFile file(path);
    if (QFileInfo(path).isSymLink() || !file.open(QIODevice::ReadOnly) || file.size() != size) return false;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) && hash.result().toHex() == sha256;
}
static bool copyRuntime(const QString &from, const QString &to) {
    if (!QDir(from).exists() || !QDir().mkpath(to)) return false;
    QDirIterator files(from, QDir::Files, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const auto source = files.next();
        const auto dest = to + "/" + QDir(from).relativeFilePath(source);
        if (!QDir().mkpath(QFileInfo(dest).absolutePath()) || !QFile::copy(source, dest)) return false;
    }
    return true;
}
QString prepare(const QString &download, qint64 size, const QByteArray &sha256, const QString &version, QString *error) {
    const auto target = portablePath();
    auto fail = [&](const QString &message) { *error = message; return QString{}; };
    if (target.isEmpty()) return fail("Este build permite consultar versões, mas não atualizar o executável de desenvolvimento.");
    if (!QFileInfo(target).isFile() || QFileInfo(target).isSymLink()) return fail("O arquivo original não está disponível para atualização.");
    if (!verifyFile(download, size, sha256)) return fail("O download não passou na verificação de integridade.");
    QStorageInfo disk(QFileInfo(target).absolutePath());
    if (!disk.isValid() || disk.isReadOnly() || disk.bytesAvailable() < size + QFileInfo(target).size() + 1024 * 1024)
        return fail("Não há espaço ou permissão para preparar a atualização nesta pasta.");
    QDir existing(root());
    for (const auto &entry : existing.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto journal = existing.filePath(entry + "/transaction.json");
        const auto o = readJson(journal);
        if (validTransaction(journal, o) && o["target"] == target && o["state"] != "completed")
            return fail("Existe uma atualização pendente. Aguarde o atualizador ou reabra o aplicativo para recuperá-la.");
    }
    cleanupCompleted();
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto dir = root() + "/" + id;
    if (!QDir().mkpath(dir)) return fail("Não foi possível preparar o atualizador.");
    QFile::setPermissions(dir, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const auto staged = QFileInfo(target).absolutePath() + "/.lazarus-update-" + id + ".new";
    if (!QFile::copy(download, staged) || !QFile::setPermissions(staged, QFile::permissions(target))) {
        QFile::remove(staged); QDir(dir).removeRecursively();
        return fail("Não foi possível gravar a versão nova na pasta do aplicativo. A versão atual continua intacta.");
    }
    const auto runtime = dir + "/runtime";
    if (!copyRuntime(helperSource(), runtime)) {
        QFile::remove(staged); QDir(dir).removeRecursively();
        return fail("O pacote não contém um atualizador completo.");
    }
    const bool extract = !qEnvironmentVariable("APPIMAGE_EXTRACT_AND_RUN").isEmpty()
        || qEnvironmentVariable("APPDIR").contains("appimage_extracted_");
    QJsonObject transaction{{"id", id}, {"target", target}, {"staged", staged},
        {"backup", QFileInfo(target).absolutePath() + "/.lazarus-update-" + id + ".old"},
        {"size", double(size)}, {"sha256", QString::fromLatin1(sha256)}, {"version", version},
        {"appPid", double(QCoreApplication::applicationPid())},
        {"launcherPid", qEnvironmentVariable("LAZARUS_LAUNCHER_PID").toDouble()},
        {"extract", extract}, {"state", "prepared"}};
    const auto journal = dir + "/transaction.json";
    if (!writeJson(journal, transaction)) {
        QFile::remove(staged); QDir(dir).removeRecursively();
        return fail("Não foi possível registrar a atualização com segurança.");
    }
    return journal;
}
void abandon(const QString &journal) {
    const auto o = readJson(journal);
    if (!validTransaction(journal, o)) return;
    const auto dir = QFileInfo(journal).absolutePath();
    writeJson(dir + "/abort.json", {{"id", o["id"]}});
    QLockFile lock(dir + "/helper.lock");
    if (lock.tryLock() && o["state"] == "prepared") {
        auto done = o; done["state"] = "completed";
        if (writeJson(journal, done)) QFile::remove(o["staged"].toString());
    }
}
bool startHelper(const QString &journal) {
    const auto dir = QFileInfo(journal).absolutePath();
    if (!validTransaction(journal, readJson(journal))) return false;
    QProcess process;
    auto env = QProcessEnvironment::systemEnvironment();
#ifndef Q_OS_WIN
    env.insert("LD_LIBRARY_PATH", dir + "/runtime");
#endif
    process.setProcessEnvironment(env);
    process.setWorkingDirectory(dir);
#ifdef Q_OS_WIN
    process.setProgram(dir + "/runtime/lazarus-updater.exe");
#else
    process.setProgram(dir + "/runtime/lazarus-updater");
#endif
    process.setArguments({journal});
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
        args->flags &= ~CREATE_NEW_CONSOLE; args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.setStandardOutputFile(QProcess::nullDevice()); process.setStandardErrorFile(QProcess::nullDevice());
    return process.startDetached();
}
void cleanupCompleted() {
    const QDir updates(root());
    for (const auto &id : updates.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto dir = updates.filePath(id);
        const auto journal = dir + "/transaction.json";
        const auto o = readJson(journal);
        if (!validTransaction(journal, o) || o["state"] != "completed") continue;
        QLockFile lock(dir + "/helper.lock"); if (!lock.tryLock()) continue;
        // Completed transactions no longer need their backups; retry transient cleanup failures.
        QFile::remove(o["backup"].toString()); QFile::remove(o["staged"].toString());
        if (!QFile::exists(o["backup"].toString()) && !QFile::exists(o["staged"].toString())) {
            lock.unlock(); QDir(dir).removeRecursively();
        }
    }
}
bool recover() {
    if (!qEnvironmentVariable("LAZARUS_UPDATE_TRANSACTION").isEmpty()) return true;
    cleanupCompleted();
    const auto target = portablePath();
    if (target.isEmpty()) return true;
    QDir updates(root());
    for (const auto &id : updates.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const auto journal = updates.filePath(id + "/transaction.json");
        auto o = readJson(journal);
        if (!validTransaction(journal, o) || o["target"] != target || o["state"] == "completed") continue;
        const auto dir = QFileInfo(journal).absolutePath();
        QLockFile lock(dir + "/helper.lock");
        if (!lock.tryLock()) return false; // Another helper is finishing this transaction.
        // Do not trust stale PIDs after a reboot; this process and launcher will exit for recovery.
        o["appPid"] = double(QCoreApplication::applicationPid());
        o["launcherPid"] = qEnvironmentVariable("LAZARUS_LAUNCHER_PID").toDouble();
        o["recovering"] = true;
        if (!writeJson(journal, o)) return false;
        lock.unlock();
        if (startHelper(journal)) return false;
        return !QFile::exists(o["backup"].toString());
    }
    return true;
}
void acknowledge() {
    const auto journal = qEnvironmentVariable("LAZARUS_UPDATE_TRANSACTION");
    const auto o = readJson(journal);
    if (!validTransaction(journal, o) || o["target"].toString() != portablePath()
        || o["version"].toString() != QCoreApplication::applicationVersion()) return;
    writeJson(QFileInfo(journal).absolutePath() + "/ack.json", {{"id", o["id"]}, {"version", o["version"]}});
}
}
