#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace Protocol {
QByteArray randomBytes(int count);
QString token(const QByteArray &secret);
QByteArray secret(const QString &token);
QString room(const QByteArray &secret);
QString randomHex(int count);
bool equal(const QByteArray &a, const QByteArray &b);
class Channel {
public:
    Channel() = default;
    Channel(QByteArray secret, QString session, QString peer, QString localChallenge,
            QString remoteChallenge, bool host);
    QJsonObject seal(const QJsonObject &body);
    bool open(const QJsonObject &wire, QJsonObject &body);
private:
    QByteArray key_;
    QString session_, peer_, local_, remote_;
    bool host_ = false;
    qint64 sent_ = 0, received_ = 0;
};
}
