#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace Protocol {
QByteArray randomBytes(int count);
QString token(const QByteArray &secret);
QByteArray secret(const QString &token);
QString inviteLink(const QByteArray &secret, const QString &nickname = QString(), int avatar = -1);
QByteArray inviteSecret(const QString &link);
QString room(const QByteArray &secret);
QString randomHex(int count);
bool equal(const QByteArray &a, const QByteArray &b);
class Channel {
public:
    Channel() = default;
    Channel(QByteArray secret, QString session, QString localId, QString remoteId,
            QString localChallenge, QString remoteChallenge, qint64 revision);
    QJsonObject seal(const QJsonObject &body);
    bool open(const QJsonObject &wire, QJsonObject &body);
private:
    QByteArray key_;
    QString session_, local_, remote_;
    QString localId_, remoteId_;
    qint64 revision_ = 0;
    qint64 sent_ = 0, received_ = 0;
};
}
