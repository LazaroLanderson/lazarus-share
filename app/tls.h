#pragma once
#include <QCryptographicHash>
#include <QSslCertificate>
#include <QSslError>
#include <QString>

inline bool acceptsPinnedTls(const QSslCertificate &certificate, const QList<QSslError> &errors, QString pin) {
    pin = pin.trimmed().remove(':').remove(' ').toLower();
    if (pin.size() != 64 || certificate.isNull() || certificate.digest(QCryptographicHash::Sha256).toHex() != pin.toLatin1()) return false;
    for (const auto &error : errors) {
        // Schannel reports an untrusted self-signed root as CertificateUntrusted.
        bool trustOnly = error.error() == QSslError::SelfSignedCertificate
            || error.error() == QSslError::SelfSignedCertificateInChain
            || (error.error() == QSslError::CertificateUntrusted && certificate.isSelfSigned());
        if (!trustOnly) return false;
    }
    return true;
}
