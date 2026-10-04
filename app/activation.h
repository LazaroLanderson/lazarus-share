#pragma once
#include <QObject>
#include <QLocalServer>
#include <QLockFile>
#include <memory>

// One invitation receiver per user; diagnostic executables do not use this.
class Activation : public QObject {
    Q_OBJECT
public:
    explicit Activation(QObject *parent = nullptr, const QString &scope = {});
    ~Activation() override;
    enum class Result { Receiver, Forwarded, Failed };
    Result start(const QString &invite);
    bool registerProtocol();
signals:
    void invitation(const QString &link);
private:
    QString name_;
    QLocalServer server_;
    std::unique_ptr<QLockFile> lock_;
};
