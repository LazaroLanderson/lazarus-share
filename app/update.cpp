#include "update.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QDir>
#include <cmath>

static QVersionNumber version(const QString &value) {
    static const QRegularExpression pattern("^v?([0-9]+\\.[0-9]+\\.[0-9]+)$");
    const auto match = pattern.match(value);
    const auto parsed = match.hasMatch() ? QVersionNumber::fromString(match.captured(1)) : QVersionNumber{};
    return parsed.segmentCount() == 3 ? parsed : QVersionNumber{};
}
static bool safeDownload(const QUrl &url) {
    return url.scheme() == "https" && url.host() == "github.com" && url.userInfo().isEmpty()
        && url.path().startsWith("/LazaroLanderson/lazarus-share/releases/download/") && (url.port() == -1 || url.port() == 443);
}
UpdateRelease selectUpdate(const QJsonArray &releases, const QString &current, const QString &assetName) {
    UpdateRelease best;
    for (const auto &entry : releases) {
        const auto release = entry.toObject(); const auto tag = release["tag_name"].toString();
        const auto candidate = version(tag);
        if (release["draft"].toBool() || candidate.isNull() || candidate <= version(current)
            || (best.valid() && candidate <= version(best.version))) continue;
        for (const auto &entryAsset : release["assets"].toArray()) {
            const auto asset = entryAsset.toObject();
            const auto digest = asset["digest"].toString();
            static const QRegularExpression sha("^sha256:([a-fA-F0-9]{64})$");
            const auto match = sha.match(digest);
            const double size = asset["size"].toDouble(); const QUrl url(asset["browser_download_url"].toString());
            if (asset["name"].toString() != assetName || !match.hasMatch() || !safeDownload(url)
                || size <= 0 || size > double(8LL * 1024 * 1024 * 1024) || std::floor(size) != size) continue;
            best = {candidate.toString(), release["body"].toString(), url, match.captured(1).toLatin1().toLower(), qint64(size)};
            break;
        }
    }
    return best;
}
static QNetworkRequest request(const QUrl &url) {
    QNetworkRequest r(url);
    r.setRawHeader("User-Agent", "LazarusShare-Updater");
    r.setRawHeader("Accept", "application/vnd.github+json");
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(30000);
    return r;
}
UpdateClient::UpdateClient(QObject *parent, QNetworkAccessManager *transport)
    : QObject(parent), network_(this), transport_(transport ? transport : &network_) {}
void UpdateClient::check() {
    if (checked_) return;
    checked_ = true;
    page(QUrl("https://api.github.com/repos/LazaroLanderson/lazarus-share/releases?per_page=100"));
}
void UpdateClient::page(const QUrl &url) {
    if (url.scheme() != "https" || url.host() != "api.github.com" || !url.userInfo().isEmpty()
        || url.path() != "/repos/LazaroLanderson/lazarus-share/releases" || pages_.contains(url)) return;
    pages_.insert(url);
    auto *reply = transport_->get(request(url)); reply_ = reply;
    connect(reply, &QNetworkReply::readyRead, this, [reply] { if (reply->bytesAvailable() > 16 * 1024 * 1024) reply->abort(); });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater(); reply_ = nullptr;
        if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) return;
        const auto data = reply->readAll(); if (data.size() > 16 * 1024 * 1024) return;
        const auto doc = QJsonDocument::fromJson(data); if (!doc.isArray()) return;
#ifdef Q_OS_WIN
        const QString asset = "LazarusShare.exe";
#else
        const QString asset = "LazarusShare-x86_64.AppImage";
#endif
        auto found = selectUpdate(doc.array(), QCoreApplication::applicationVersion(), asset);
        if (found.valid() && (!best_.valid() || version(found.version) > version(best_.version))) best_ = found;
        const QRegularExpression next("<([^>]+)>;\\s*rel=\"next\"");
        const auto match = next.match(QString::fromLatin1(reply->rawHeader("Link")));
        if (match.hasMatch()) { page(QUrl(match.captured(1))); return; }
        if (best_.valid()) emit available();
    });
}
QString UpdateClient::downloadedFile() const { return ready_ && file_ ? file_->fileName() : QString{}; }
void UpdateClient::download() {
    if (!best_.valid() || downloading_ || ready_) return;
    file_ = std::make_unique<QTemporaryFile>(QDir::tempPath() + "/lazarus-download-XXXXXX");
    if (!file_->open()) { fail("Não foi possível criar o arquivo temporário do download."); return; }
    hash_ = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    received_ = 0; downloading_ = true;
    auto *reply = transport_->get(request(best_.url)); reply_ = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        while (reply->bytesAvailable() > 0 && downloading_) {
            auto bytes = reply->read(256 * 1024);
            if (received_ + bytes.size() > best_.size || file_->write(bytes) != bytes.size()) {
                fail("O pacote excedeu o tamanho esperado ou não há espaço para o download."); return;
            }
            hash_->addData(bytes); received_ += bytes.size(); emit progress(received_, best_.size);
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater(); if (reply_ == reply) reply_ = nullptr;
        if (!downloading_) return;
        if (reply->error() != QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
            fail("Não foi possível concluir o download. Verifique sua conexão e tente novamente."); return;
        }
        if (received_ != best_.size || hash_->result().toHex() != best_.sha256 || !file_->flush()) {
            fail("A integridade do pacote não confere. Tente baixar novamente."); return;
        }
        file_->close(); downloading_ = false; ready_ = true; emit downloaded();
    });
}
void UpdateClient::fail(const QString &message) {
    downloading_ = false; ready_ = false;
    if (reply_) { auto *r = reply_.data(); reply_ = nullptr; r->abort(); }
    file_.reset(); hash_.reset(); emit error(message);
}
void UpdateClient::cancel() {
    if (!downloading_) return;
    downloading_ = false;
    if (reply_) { auto *r = reply_.data(); reply_ = nullptr; r->abort(); }
    file_.reset(); hash_.reset(); emit cancelled();
}
