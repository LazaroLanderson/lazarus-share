#pragma once
#include <QObject>
#include <QList>
#include <QMutex>
#include <QSet>
#include <QTimer>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <memory>
struct AudioApplication { QString id, name; quint64 target = 0; };
class Audio : public QObject {
    Q_OBJECT
public:
    explicit Audio(QObject *parent = nullptr);
    ~Audio() override;
    QList<AudioApplication> applications() const;
    QSet<QString> selected() const { return selected_; }
    bool supported() const;
    QString limitation() const;
    void select(const QSet<QString> &ids);
    void stop();
    QList<GstSample *> takeSamples();
signals:
    void changed();
    void error(QString message);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    void refresh();
    void clearPipeline();
    static GstFlowReturn sample(GstAppSink *, gpointer);
    GstElement *pipeline_ = nullptr;
    QSet<QString> selected_;
    QList<GstSample *> samples_;
    QMutex mutex_;
    QTimer timer_;
};
