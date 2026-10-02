#include "tls.h"
#include <QCoreApplication>
#include <QFile>
#include <QTimer>
#include <QWebSocket>
#include <iostream>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (argc == 3 && QString::fromLocal8Bit(argv[1]) == "--policy") {
        QFile file(QString::fromLocal8Bit(argv[2])); if (!file.open(QIODevice::ReadOnly)) return 2;
        QSslCertificate cert(file.readAll()); auto pin = QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());
        for (auto error : {QSslError::SelfSignedCertificate, QSslError::CertificateUntrusted}) {
            if (!acceptsPinnedTls(cert, {QSslError(error, cert)}, pin)) { std::cerr << "Pinned self-signed certificate rejected by trust policy\n"; return 1; }
        }
        for (auto error : {QSslError::HostNameMismatch, QSslError::CertificateExpired, QSslError::CertificateRevoked})
            if (acceptsPinnedTls(cert, {QSslError(error, cert)}, pin)) return 1;
        if (acceptsPinnedTls(cert, {QSslError(QSslError::SelfSignedCertificate, cert)}, QString(64, '0'))) return 1;
        std::cout << "Pinned trust policy passed\n"; return 0;
    }
    if (argc < 3) return 2;
    QString pin = QString::fromLocal8Bit(argv[2]); bool refusal = argc > 3;
    QWebSocket socket; int result = 1; bool rejectedCertificate = false;
    QObject::connect(&socket, &QWebSocket::sslErrors, &app, [&](const QList<QSslError> &errors) {
        if (acceptsPinnedTls(socket.sslConfiguration().peerCertificate(), errors, pin)) socket.ignoreSslErrors(errors);
        else rejectedCertificate = true;
    });
    QObject::connect(&socket, &QWebSocket::connected, &app, [&] {
        bool matched = socket.sslConfiguration().peerCertificate().digest(QCryptographicHash::Sha256).toHex() == pin.toLatin1();
        result = matched && !refusal ? 0 : 1; socket.close(); app.quit();
    });
    QObject::connect(&socket, qOverload<QAbstractSocket::SocketError>(&QWebSocket::error), &app, [&](QAbstractSocket::SocketError) {
        result = refusal && rejectedCertificate ? 0 : 1; if (result) std::cerr << "TLS probe: " << socket.errorString().toStdString() << "\n"; app.quit();
    });
    QTimer::singleShot(5000, &app, &QCoreApplication::quit);
    socket.open(QUrl(QString::fromLocal8Bit(argv[1]))); app.exec();
    std::cout << (result == 0 ? "TLS pin test passed\n" : "TLS pin test failed\n"); return result;
}
