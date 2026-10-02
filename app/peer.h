#pragma once
#include <QObject>
#include <QImage>
#include <QJsonObject>
#include <QJsonArray>
#include <QMutex>
#include <QTimer>
#include <QElapsedTimer>
#include <atomic>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <gst/webrtc/webrtc.h>

struct Quality { int width = 1920, height = 1080, fps = 60, kbps = 8000; };
class Peer : public QObject {
    Q_OBJECT
public:
    explicit Peer(bool host, QObject *parent = nullptr);
    ~Peer() override;
    bool start(Quality quality, const QString &stun, const QStringList &turn = {});
    void receive(const QJsonObject &message);
    void video(GstSample *sample);
    void audio(GstSample *sample);
    void quality(Quality value);
    QImage takeFrame();
    QString encoderName() const { return encoderName_; }
    bool connected() const { return connected_; }
signals:
    void outgoing(QJsonObject message);
    void status(QString state);
    void metrics(QJsonObject values);
    void error(QString message);
private:
    static void offerNeeded(GstElement *, gpointer);
    static void descriptionCreated(GstPromise *, gpointer);
    static void iceCandidate(GstElement *, guint, gchar *, gpointer);
    static void padAdded(GstElement *, GstPad *, gpointer);
    static GstFlowReturn newFrame(GstAppSink *, gpointer);
    void createDescription(bool offer);
    void poll();
    void stats();
    void applyStats(const QJsonObject &values);
    void push(GstElement *source, GstSample *sample);
    bool host_, offered_ = false, remoteSet_ = false, connected_ = false;
    GstElement *pipeline_ = nullptr, *rtc_ = nullptr, *video_ = nullptr, *audio_ = nullptr, *encoder_ = nullptr;
    Quality quality_;
    int targetKbps_ = 8000, currentKbps_ = 8000;
    QByteArray bitrateProperty_ = "target-bitrate";
    int bitrateMultiplier_ = 1000;
    QJsonArray pendingIce_;
    QMutex frameMutex_;
    QImage frame_;
    QTimer timer_;
    QElapsedTimer statsTime_;
    std::atomic<unsigned> encodedFrames_{0}, decodedFrames_{0};
    quint64 lastBytes_ = 0;
    qint64 lastLost_ = 0, lastPackets_ = 0;
    int pollCount_ = 0;
    int localCandidates_ = 0, remoteCandidates_ = 0;
    QString encoderName_;
    QString stage_ = "new";
};
