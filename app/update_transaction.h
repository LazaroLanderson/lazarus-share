#pragma once
#include <QJsonObject>
#include <QString>

namespace Updates {
QString root();
QString portablePath();
QString helperSource();
bool writeJson(const QString &path, const QJsonObject &object);
QJsonObject readJson(const QString &path);
bool validTransaction(const QString &journal, const QJsonObject &object);
bool verifyFile(const QString &path, qint64 size, const QByteArray &sha256);
QString prepare(const QString &download, qint64 size, const QByteArray &sha256,
                const QString &version, QString *error);
void abandon(const QString &journal);
bool startHelper(const QString &journal);
// Return false when recovery takes over startup. Must precede instance election.
bool recover();
void acknowledge();
void cleanupCompleted();
}
