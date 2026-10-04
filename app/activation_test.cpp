#include "activation.h"
#include "protocol.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QProcess>
#include <QTemporaryDir>
#include <QFile>
#include <QTimer>
#include <QUuid>
#include <iostream>
static void check(bool ok) { if (!ok) { std::cerr << "Invitation activation check failed\n"; std::exit(1); } }
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 4 && args[1] == "--forward") {
        Activation sender(nullptr, args[2]);
        return int(sender.start(args[3]));
    }
    const auto scope = QUuid::createUuid().toString();
    Activation receiver(nullptr, scope);
    check(receiver.start({}) == Activation::Result::Receiver);
    QStringList delivered;
    QObject::connect(&receiver, &Activation::invitation, &app, [&](QString link) { delivered.append(link); });
    const auto link = Protocol::inviteLink(Protocol::randomBytes(16));
    for (const auto &input : QStringList{link, link, QString{}}) {
        QProcess child; QEventLoop loop;
        QObject::connect(&child, &QProcess::finished, &loop, &QEventLoop::quit);
        QTimer timeout; timeout.setSingleShot(true);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        child.start(app.applicationFilePath(), {"--forward", scope, input}); timeout.start(8000); loop.exec();
        check(child.state() == QProcess::NotRunning && child.exitStatus() == QProcess::NormalExit && child.exitCode() == int(Activation::Result::Forwarded));
    }
    check(delivered == QStringList({link, link, QString{}}));
#ifndef Q_OS_WIN
    // Registration points at the stable portable path, including spaces.
    QTemporaryDir state; check(state.isValid());
    qputenv("XDG_DATA_HOME", state.path().toUtf8());
    QFile executable(state.path() + "/Portable AppImage"); check(executable.open(QIODevice::WriteOnly)); executable.close();
    QFile mime(state.path() + "/xdg-mime"); check(mime.open(QIODevice::WriteOnly)); mime.write("#!/bin/sh\nexit 0\n"); mime.close();
    mime.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("PATH", state.path().toUtf8()); qputenv("APPIMAGE", executable.fileName().toUtf8());
    check(receiver.registerProtocol());
    QFile desktop(state.path() + "/applications/lazarus-share.desktop"); check(desktop.open(QIODevice::ReadOnly));
    const auto content = desktop.readAll();
    check(content.contains(("Exec=\"" + executable.fileName() + "\" %u").toUtf8()));
    check(content.contains("MimeType=x-scheme-handler/lazarus-share;"));
#endif
    std::cout << "Invitations delivered to existing process; portable protocol registration passed\n";
}
