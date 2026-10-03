#pragma once
#include "peer.h"
#include "frameprep.h"
#include <QMutex>
#include <QSize>
#include <functional>
#ifndef Q_OS_WIN
#include <QDBusMessage>
#endif

class Capture : public QObject {
    Q_OBJECT
public:
    explicit Capture(QObject *parent = nullptr);
    ~Capture() override;
    void start(int monitor, Quality quality, bool testPattern = false);
    void stop();
    void quality(Quality quality);
    GstSample *takeVideo(GstSample **nv12=nullptr);
    QJsonObject takeMetrics();
    void requireNv12(bool value) { nv12Required_=value; }
    QSize sourceSize() const { return sourceSize_; }
    QSize dimensions() const { return dimensions_; }
signals:
    void ready();
    void error(QString message);
private slots:
#ifndef Q_OS_WIN
    void response(uint code, QVariantMap result, const QDBusMessage &message);
#endif
private:
    void launch(const QString &source);
#ifndef Q_OS_WIN
    void request(const QString &method, const QVariantList &args, const QVariantMap &options,
                 std::function<void(QVariantMap)> callback);
    void selectPortal();
    QHash<QString, std::function<void(QVariantMap)>> requests_;
    QString session_;
    int fd_ = -1;
#endif
    static GstFlowReturn sample(GstAppSink *, gpointer);
    GstElement *pipeline_ = nullptr, *filter_ = nullptr;
    GstSample *latest_ = nullptr,*nv12_=nullptr;
    FramePreparer preparer_;
    unsigned discarded_=0,prepared_=0;
    quint64 prepareNs_=0;
    std::atomic<bool> nv12Required_{false};
    QString rawFormat_ = "I420";
    Quality quality_;
    QSize sourceSize_, dimensions_;
    QMutex mutex_;
    QTimer timer_;
    bool requested_ = false;
    quint64 operation_ = 0;
};
