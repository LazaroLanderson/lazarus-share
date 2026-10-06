#include "protocol.h"
#include <QCryptographicHash>
#include <QMessageAuthenticationCode>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <utility>
#include <QUrl>

namespace Protocol {
QByteArray randomBytes(int count) {
    QByteArray out(count, Qt::Uninitialized);
    for (int i = 0; i < count; ++i) out[i] = char(QRandomGenerator::system()->generate() & 255);
    return out;
}
QString randomHex(int count) { return QString::fromLatin1(randomBytes(count).toHex()); }
QString token(const QByteArray &secret) {
    const char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    QString out;
    quint32 bits = 0; int available = 0;
    for (unsigned char c : secret) {
        bits = (bits << 8) | c; available += 8;
        while (available >= 5) { available -= 5; out += QLatin1Char(alphabet[(bits >> available) & 31]); }
    }
    if (available) out += QLatin1Char(alphabet[(bits << (5 - available)) & 31]);
    return out;
}
QByteArray secret(const QString &input) {
    QString normalized = input.trimmed().toUpper();
    if (normalized.size() != 26) return {};
    const QString alphabet = QStringLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZ234567");
    QByteArray out; quint32 bits = 0; int available = 0;
    for (QChar c : normalized) {
        int v = alphabet.indexOf(c); if (v < 0) return {};
        bits = (bits << 5) | quint32(v); available += 5;
        if (available >= 8) { available -= 8; out += char((bits >> available) & 255); }
    }
    return out.size() == 16 && token(out) == normalized ? out : QByteArray();
}
QString inviteLink(const QByteArray &secret, const QString &nickname, int avatar) {
    QString link = "https://share.app.lazaruslabs.com.br/join#" + token(secret);
    if (!nickname.trimmed().isEmpty()) {
        link += "&nick=" + QString::fromUtf8(QUrl::toPercentEncoding(nickname.trimmed()));
    }
    if (avatar >= 0 && avatar <= 9) {
        link += "&avatar=" + QString::number(avatar);
    }
    return link;
}
QByteArray inviteSecret(const QString &input) {
    const QUrl url(input.trimmed(), QUrl::StrictMode);
    if (!url.isValid() || !url.userInfo().isEmpty() || url.port() != -1 || url.hasQuery()) return {};
    const bool https = url.scheme() == "https" && url.host() == "share.app.lazaruslabs.com.br" && url.path() == "/join";
    const bool native = url.scheme() == "lazarus-share" && url.host() == "join" && url.path().isEmpty();
    if (!https && !native) return {};
    QString frag = url.fragment();
    int sep = frag.indexOf('&');
    if (sep >= 0) frag = frag.left(sep);
    return secret(frag);
}
QString room(const QByteArray &secret) {
    return QString::fromLatin1(QCryptographicHash::hash("lazarus-share/room/v1:" + secret, QCryptographicHash::Sha256).toHex());
}
bool equal(const QByteArray &a, const QByteArray &b) {
    if (a.size() != b.size()) return false;
    unsigned int difference = 0;
    for (qsizetype i = 0; i < a.size(); ++i) difference |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return difference == 0;
}
Channel::Channel(QByteArray secret, QString session, QString localId, QString remoteId,
                 QString localChallenge, QString remoteChallenge, qint64 revision)
    : key_(QMessageAuthenticationCode::hash("lazarus-share/signaling/v2", secret, QCryptographicHash::Sha256)),
      session_(std::move(session)), local_(std::move(localChallenge)), remote_(std::move(remoteChallenge)),
      localId_(std::move(localId)), remoteId_(std::move(remoteId)), revision_(revision) {}
QJsonObject Channel::seal(const QJsonObject &body) {
    QJsonObject envelope{{"session", session_}, {"challenge", remote_},
                         {"sender", localId_}, {"recipient", remoteId_}, {"revision", revision_},
                         {"seq", ++sent_}, {"body", body}};
    QByteArray payload = QJsonDocument(envelope).toJson(QJsonDocument::Compact).toBase64();
    QByteArray mac = QMessageAuthenticationCode::hash(payload, key_, QCryptographicHash::Sha256).toHex();
    return {{"type", "signal"}, {"revision", revision_}, {"peer", remoteId_}, {"payload", QString::fromLatin1(payload)}, {"mac", QString::fromLatin1(mac)}};
}
bool Channel::open(const QJsonObject &wire, QJsonObject &body) {
    QByteArray payload = wire["payload"].toString().toLatin1();
    QByteArray expected = QMessageAuthenticationCode::hash(payload, key_, QCryptographicHash::Sha256).toHex();
    if (payload.size() > 120000 || !equal(expected, wire["mac"].toString().toLatin1())) return false;
    auto doc = QJsonDocument::fromJson(QByteArray::fromBase64(payload));
    if (!doc.isObject()) return false;
    auto e = doc.object(); qint64 seq = e["seq"].toInteger();
    if (e["session"] != session_ || e["challenge"] != local_ || seq <= received_ || !e["body"].isObject()) return false;
    if (e["sender"] != remoteId_ || e["recipient"] != localId_ || e["revision"].toInteger() != revision_) return false;
    received_ = seq; body = e["body"].toObject(); return true;
}
}
