#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
class DiagnosticLog {
public:
    explicit DiagnosticLog(QString directory = {});
    void append(QString event, QJsonObject fields = {});
    QJsonArray events() const;
    void clear();
private:
    QString directory_;
    qint64 lastPrune_ = 0;
    void prune();
};
