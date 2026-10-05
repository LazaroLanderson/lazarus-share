#pragma once
#include <QObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPointer>
#include <QTemporaryFile>
#include <QCryptographicHash>
#include <QVersionNumber>
#include <QSet>
#include <memory>

struct UpdateRelease {
    QString version, notes;
    QUrl url;
    QByteArray sha256;
    qint64 size = 0;
    bool valid() const { return !version.isEmpty(); }
};
UpdateRelease selectUpdate(const QJsonArray &releases, const QString &current, const QString &assetName);
class UpdateClient : public QObject {
    Q_OBJECT
public:
    explicit UpdateClient(QObject *parent = nullptr, QNetworkAccessManager *transport = nullptr);
    void check();
    void download();
    void cancel();
    const UpdateRelease &release() const { return best_; }
    QString downloadedFile() const;
    bool ready() const { return ready_; }
    bool downloading() const { return downloading_; }
signals:
    void available();
    void progress(qint64 received, qint64 total);
    void downloaded();
    void error(const QString &message);
    void cancelled();
private:
#ifdef LAZARUS_TESTING
    friend struct UpdateTestAccess;
#endif
    void page(const QUrl &url);
    void fail(const QString &message);
    QNetworkAccessManager network_;
    QNetworkAccessManager *transport_;
    QPointer<QNetworkReply> reply_;
    UpdateRelease best_;
    std::unique_ptr<QTemporaryFile> file_;
    std::unique_ptr<QCryptographicHash> hash_;
    bool checked_ = false, downloading_ = false, ready_ = false;
    qint64 received_ = 0;
    QSet<QUrl> pages_;
};
