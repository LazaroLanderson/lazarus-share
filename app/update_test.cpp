#include "update.h"
#include "update_transaction.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QProcessEnvironment>
#include <QThread>
#include <QJsonObject>
#include <QUuid>
#include <iostream>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QStorageInfo>
#include <cstring>
static void check(bool ok, const char *message) { if (!ok) { std::cerr << message << '\n'; std::exit(1); } }
static QJsonObject release(const QString &tag, bool draft = false) {
    return {{"tag_name", tag}, {"draft", draft}, {"prerelease", true}, {"body", "Novidades"},
        {"assets", QJsonArray{QJsonObject{{"name", "LazarusShare.exe"}, {"size", 100},
         {"digest", "sha256:" + QString(64, 'a')}, {"browser_download_url", "https://github.com/LazaroLanderson/lazarus-share/releases/download/" + tag + "/LazarusShare.exe"}}}}};
}
// Deterministic transport exercises production requests and streaming without internet access.
class FixtureReply : public QNetworkReply {
    QByteArray bytes_; qint64 position_ = 0; bool ended_ = false;
public:
    FixtureReply(const QNetworkRequest &request, QByteArray body, int status, QByteArray link, QObject *parent)
        : QNetworkReply(parent), bytes_(body) {
        setRequest(request); setUrl(request.url()); open(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, status);
        if (!link.isEmpty()) setRawHeader("Link", link);
        if (status != 200) setError(QNetworkReply::ContentAccessDenied, "Fixture HTTP failure");
        QTimer::singleShot(0, this, [this] {
            if (ended_) return;
            emit readyRead(); if (ended_) return; ended_ = true; setFinished(true); emit finished();
        });
    }
    void abort() override { if (ended_) return; ended_ = true; setError(OperationCanceledError, "cancel"); setFinished(true); emit finished(); }
    qint64 bytesAvailable() const override { return bytes_.size() - position_ + QNetworkReply::bytesAvailable(); }
protected:
    qint64 readData(char *data, qint64 size) override {
        const auto count = qMin(size, bytes_.size() - position_);
        if (!count) return -1;
        std::memcpy(data, bytes_.constData() + position_, size_t(count)); position_ += count; return count;
    }
};
class FixtureNetwork : public QNetworkAccessManager {
public:
    QByteArray payload = "hello"; int status = 200, calls = 0;
protected:
    QNetworkReply *createRequest(Operation, const QNetworkRequest &request, QIODevice *) override {
        ++calls;
        if (request.url().host() == "api.github.com") {
            QJsonObject item = release(request.url().query().contains("page=2") ? "v0.4.0" : "v0.3.0");
            auto assets = item["assets"].toArray(); auto asset = assets[0].toObject();
#ifdef Q_OS_WIN
            asset["name"] = "LazarusShare.exe";
#else
            asset["name"] = "LazarusShare-x86_64.AppImage";
#endif
            asset["size"] = 5;
            asset["digest"] = "sha256:" + QString::fromLatin1(QCryptographicHash::hash("hello", QCryptographicHash::Sha256).toHex());
            assets[0] = asset; item["assets"] = assets;
            const auto link = request.url().query().contains("page=2") ? QByteArray{} : QByteArray("<https://api.github.com/repos/LazaroLanderson/lazarus-share/releases?per_page=100&page=2>; rel=\"next\"");
            return new FixtureReply(request, QJsonDocument(QJsonArray{item}).toJson(), status, link, this);
        }
        return new FixtureReply(request, payload, status, {}, this);
    }
};
static void pump() { QEventLoop loop; QTimer::singleShot(30, &loop, &QEventLoop::quit); loop.exec(); }
static void networkTests() {
    QCoreApplication::setApplicationVersion("0.2.3");
    FixtureNetwork network; UpdateClient client(nullptr, &network);
    bool available = false; int failures = 0; bool cancelled = false;
    QObject::connect(&client, &UpdateClient::available, [&] { available = true; });
    QObject::connect(&client, &UpdateClient::error, [&](const QString &) { ++failures; });
    QObject::connect(&client, &UpdateClient::cancelled, [&] { cancelled = true; });
    client.check(); client.check(); pump();
    check(available && client.release().version == "0.4.0" && network.calls == 2, "Paginated startup check runs once");
    network.payload = "wrong"; client.download(); pump(); check(failures == 1 && !client.ready(), "Corrupt download rejected");
    network.payload = "hi"; client.download(); pump(); check(failures == 2 && !client.ready(), "Truncated download rejected");
    network.payload = "too large"; client.download(); pump(); check(failures == 3 && !client.ready(), "Oversized download rejected");
    network.status = 403; client.download(); pump(); check(failures == 4, "HTTP download failure");
    network.status = 200; network.payload = "hello"; client.download(); client.cancel(); pump(); check(cancelled && !client.ready(), "Download cancellation");
    client.download(); pump(); check(client.ready() && QFile::exists(client.downloadedFile()), "Retry and verified download");
    FixtureNetwork offline; offline.status = 403; UpdateClient noApi(nullptr, &offline);
    bool notified = false; QObject::connect(&noApi, &UpdateClient::available, [&] { notified = true; }); noApi.check(); pump();
    check(!notified, "Unavailable/rate limited API stays silent");
    QCoreApplication::setApplicationVersion("9.0.0");
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv); app.setOrganizationName("LazarusLabs"); app.setApplicationName("LazarusShare"); app.setApplicationVersion("9.0.0");
    if (app.arguments().contains("--hold")) { QThread::msleep(500); return 0; }
    if (app.arguments().contains("--hold-launcher")) { QThread::msleep(1000); return 0; }
    if (!qEnvironmentVariable("LAZARUS_UPDATE_TRANSACTION").isEmpty()) {
#ifdef Q_OS_WIN
        qputenv("LAZARUS_LAUNCHER_PATH", app.applicationFilePath().toUtf8());
#else
        qputenv("APPIMAGE", app.applicationFilePath().toUtf8());
#endif
        if (qEnvironmentVariableIsSet("LAZARUS_TEST_UPDATE_FAIL")) return 1;
        if (qEnvironmentVariableIsSet("LAZARUS_TEST_UPDATE_HANG")) { QThread::sleep(120); return 1; }
        Updates::acknowledge(); QThread::msleep(500); return 0;
    }
    if (!qEnvironmentVariable("LAZARUS_UPDATE_ROLLBACK").isEmpty()) return 0;
    check(selectUpdate({release("v0.2.9"), release("v0.10.0"), release("v1.0.0", true)}, "0.2.3", "LazarusShare.exe").version == "0.10.0", "Semantic versions and drafts");
    check(!selectUpdate({release("v0.2.3"), release("v0.2.2"), release("nightly")}, "0.2.3", "LazarusShare.exe").valid(), "No downgrade or invalid tags");
    check(!selectUpdate({release("v1.0.0")}, "0.2.3", "missing").valid(), "Missing platform asset");
    auto bad = release("v1.0.0"); auto assets = bad["assets"].toArray(); auto asset = assets[0].toObject(); asset["digest"] = ""; assets[0] = asset; bad["assets"] = assets;
    check(!selectUpdate({bad}, "0.2.3", "LazarusShare.exe").valid(), "Missing digest rejected");
    asset["digest"] = "sha256:" + QString(64, 'a'); asset["browser_download_url"] = "http://github.com/fake"; assets[0] = asset; bad["assets"] = assets;
    check(!selectUpdate({bad}, "0.2.3", "LazarusShare.exe").valid(), "Untrusted URL rejected");
    networkTests();
    QTemporaryDir state; check(state.isValid(), "Temporary state");
    qputenv("XDG_DATA_HOME", state.path().toUtf8());
#ifdef Q_OS_WIN
    qputenv("LOCALAPPDATA", state.path().toUtf8());
#endif
    QFile sample(state.filePath("sample")); check(sample.open(QIODevice::WriteOnly), "Sample create"); sample.write("hello"); sample.close();
    const auto hash = QCryptographicHash::hash("hello", QCryptographicHash::Sha256).toHex();
    check(Updates::verifyFile(sample.fileName(), 5, hash), "Hash match");
    check(!Updates::verifyFile(sample.fileName(), 4, hash) && !Updates::verifyFile(sample.fileName(), 5, QByteArray(64, '0')), "Size/hash mismatch");
    check(app.arguments().size() >= 2, "Updater path");
#ifndef Q_OS_WIN
    QString error;
    qunsetenv("APPIMAGE");
    check(Updates::prepare(sample.fileName(), 5, hash, "9.0.0", &error).isEmpty() && !error.isEmpty(), "Development build is not replaced");
    // A sparse original models the space needed for the backup without consuming that space.
    QFile sparse(state.filePath("large original.AppImage"));
    check(sparse.open(QIODevice::WriteOnly) && sparse.resize(QStorageInfo(state.path()).bytesAvailable()), "Sparse original"); sparse.close();
    qputenv("APPIMAGE", sparse.fileName().toUtf8());
    check(Updates::prepare(sample.fileName(), 5, hash, "9.0.0", &error).isEmpty() && error.contains("espaço"), "Preflight includes backup space");
    sparse.remove();
    const auto readonly = state.filePath("readonly"); check(QDir().mkpath(readonly), "Read-only folder");
    const auto readonlyTarget = readonly + "/app.AppImage"; check(QFile::copy(sample.fileName(), readonlyTarget), "Read-only original");
    check(QFile::setPermissions(readonly, QFileDevice::ReadOwner | QFileDevice::ExeOwner), "Read-only permissions");
    qputenv("APPIMAGE", readonlyTarget.toUtf8());
    check(Updates::prepare(sample.fileName(), 5, hash, "9.0.0", &error).isEmpty(), "Read-only destination refused before exit");
    QFile::setPermissions(readonly, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qunsetenv("APPIMAGE");
#endif
    for (const auto &scenario : {QString("success"), QString("failure"), QString("interrupted"), QString("cancelled"), QString("timeout"), QString("rollingBack"), QString("parents")}) {
        if (scenario == "timeout" && app.arguments().contains("--quick")) continue;
        const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto dir = Updates::root() + "/" + id; check(QDir().mkpath(dir), "Journal directory");
        const auto target = state.filePath("Portable app " + scenario
#ifdef Q_OS_WIN
            + ".exe"
#endif
        );
        check(QFile::copy(app.applicationFilePath(), target), "Old target copy");
        QFile old(target); check(old.open(QIODevice::Append), "Old target create"); old.write("previous version"); old.close();
        check(old.open(QIODevice::ReadOnly), "Old target read"); const auto original = old.readAll(); old.close();
        QFile unrelated(state.filePath("keep me.txt")); if (!unrelated.exists()) { check(unrelated.open(QIODevice::WriteOnly), "Unrelated file"); unrelated.write("persistent"); unrelated.close(); }
        const auto staged = QFileInfo(target).absolutePath() + "/.lazarus-update-" + id + ".new";
        const auto backup = QFileInfo(target).absolutePath() + "/.lazarus-update-" + id + ".old";
        check(QFile::copy(app.applicationFilePath(), staged), "Stage fixture");
        QFile payload(staged); check(payload.open(QIODevice::ReadOnly), "Read fixture"); auto bytes = payload.readAll(); payload.close();
        QJsonObject o{{"id", id}, {"target", target}, {"staged", staged}, {"backup", backup},
            {"size", double(bytes.size())}, {"sha256", QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex())},
            {"version", "9.0.0"}, {"state", "prepared"}, {"appPid", 0}, {"launcherPid", 0}};
        if (scenario == "interrupted") { check(QFile::rename(target, backup), "Interrupted backup"); check(QFile::copy(staged, target), "Interrupted target"); o["state"] = "installed"; o["recovering"] = true; }
        if (scenario == "rollingBack") { o["state"] = "rollingBack"; o["recovering"] = true; }
        QProcess parentApp, parentLauncher;
        if (scenario == "parents") {
            parentApp.start(app.applicationFilePath(), {"--hold"}); parentLauncher.start(app.applicationFilePath(), {"--hold-launcher"});
            check(parentApp.waitForStarted() && parentLauncher.waitForStarted(), "Original processes start");
            o["appPid"] = double(parentApp.processId()); o["launcherPid"] = double(parentLauncher.processId());
        }
        const auto journal = dir + "/transaction.json";
        check(Updates::writeJson(journal, o) && Updates::validTransaction(journal, o), "Transaction validation");
        auto malicious = o; malicious["staged"] = unrelated.fileName(); check(!Updates::validTransaction(journal, malicious), "Unrelated paths rejected");
        if (scenario == "cancelled") check(Updates::writeJson(dir + "/abort.json", {{"id", id}}), "Abort journal");
        QProcess helper; auto env = QProcessEnvironment::systemEnvironment();
        if (scenario == "failure") env.insert("LAZARUS_TEST_UPDATE_FAIL", "1");
        if (scenario == "timeout") env.insert("LAZARUS_TEST_UPDATE_HANG", "1");
        helper.setProcessEnvironment(env); helper.start(app.arguments()[1], {journal});
        check(helper.waitForStarted(), "Helper starts");
        if (scenario == "parents") {
            QThread::msleep(150);
            QFile unchanged(target); check(unchanged.open(QIODevice::ReadOnly) && unchanged.readAll() == original && !QFile::exists(backup), "Wait for app and launcher before replacement");
            check(parentApp.waitForFinished(2000) && parentLauncher.waitForFinished(2000), "Original processes exit and are reaped");
        }
        check(helper.waitForFinished(80000) && helper.exitCode() == 0, "Helper transaction");
        if (scenario == "parents") { parentApp.waitForFinished(1000); parentLauncher.waitForFinished(1000); }
        check(Updates::readJson(journal)["state"] == "completed", "Completed journal");
        QFile installed(target); check(installed.open(QIODevice::ReadOnly), "Installed target exists");
        check(installed.readAll() == ((scenario == "success" || scenario == "parents") ? bytes : original), "Correct replacement or rollback");
        check(!QFile::exists(backup) && !QFile::exists(staged) && unrelated.exists(), "Safe cleanup");
    }
    Updates::cleanupCompleted();
    std::cout << "Release selection, integrity, replacement, rollback, interruption and cleanup passed\n";
}
