#include "portal.h"
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusReply>
#include <QProcess>
#include <iostream>
struct WireSize { int width = 0, height = 0; };
struct WireStream { uint node = 0; QVariantMap properties; };
Q_DECLARE_METATYPE(WireSize)
Q_DECLARE_METATYPE(WireStream)
Q_DECLARE_METATYPE(QList<WireStream>)
QDBusArgument &operator<<(QDBusArgument &a, const WireSize &s) { a.beginStructure(); a << s.width << s.height; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, WireSize &s) { a.beginStructure(); a >> s.width >> s.height; a.endStructure(); return a; }
QDBusArgument &operator<<(QDBusArgument &a, const WireStream &s) { a.beginStructure(); a << s.node << s.properties; a.endStructure(); return a; }
const QDBusArgument &operator>>(const QDBusArgument &a, WireStream &s) { a.beginStructure(); a >> s.node >> s.properties; a.endStructure(); return a; }
class Fixture : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.lazarus.TestPortal")
public slots:
    QVariantMap StartReply() {
        QVariantMap properties{{"size", QVariant::fromValue(WireSize{2560,1440})}, {"pipewire-serial", qulonglong(98765)}};
        return {{"streams", QVariant::fromValue(QList<WireStream>{{42, properties}})}};
    }
    QVariantMap EmptyReply() { return {{"streams", QVariant::fromValue(QList<WireStream>{})}}; }
    QVariantMap InvalidReply() { return {{"streams", QString("invalid")}}; }
    QVariantMap WrongSizeReply() {
        return {{"streams", QVariant::fromValue(QList<WireStream>{{42, {{"size", QString("invalid")}}}})}};
    }
};
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    qDBusRegisterMetaType<WireSize>(); qDBusRegisterMetaType<WireStream>(); qDBusRegisterMetaType<QList<WireStream>>();
    if (app.arguments().contains("--fixture")) {
        Fixture fixture;
        if (!QDBusConnection::sessionBus().registerService("org.lazarus.TestPortal") ||
            !QDBusConnection::sessionBus().registerObject("/portal", &fixture, QDBusConnection::ExportAllSlots)) return 1;
        std::cout << "Ready\n" << std::flush;
        return app.exec();
    }
    QProcess fixture; fixture.start(app.applicationFilePath(), {"--fixture"});
    if (!fixture.waitForStarted() || !fixture.waitForReadyRead() || !fixture.readAllStandardOutput().contains("Ready")) return 1;
    QDBusInterface portal("org.lazarus.TestPortal", "/portal", "org.lazarus.TestPortal");
    QDBusReply<QVariantMap> reply = portal.call("StartReply");
    bool ok = reply.isValid();
    if (ok) {
        auto selected = readPortalSelection(reply.value());
        ok = selected.node == 42 && selected.serial == 98765 && selected.size == QSize(2560,1440);
    }
    if (ok) {
        QDBusReply<QVariantMap> empty = portal.call("EmptyReply");
        ok = empty.isValid() && !readPortalSelection(empty.value()).node;
    }
    if (ok) {
        QDBusReply<QVariantMap> invalid = portal.call("InvalidReply");
        QDBusReply<QVariantMap> wrongSize = portal.call("WrongSizeReply");
        auto selected = readPortalSelection(wrongSize.value());
        ok = invalid.isValid() && !readPortalSelection(invalid.value()).node &&
             wrongSize.isValid() && selected.node == 42 && !selected.size.isValid();
    }
    fixture.terminate(); fixture.waitForFinished();
    std::cout << (ok ? "Portal wire response parsed without abort\n" : "Portal wire response failed\n");
    return ok ? 0 : 1;
}
#include "portal_test.moc"
