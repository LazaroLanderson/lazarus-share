#pragma once
#include "encoder.h"
#include "bitrate.h"
#include "frametime.h"
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
    bool start(Quality quality, const QString &stun, const QStringList &turn = {}, const VideoEncoder &backend = {});
    void receive(const QJsonObject &message);
    void video(GstSample *sample);
    void audio(GstSample *sample);
    void quality(Quality value);
    QImage takeFrame();
    QString inputFormat() const { return inputFormat_; }
    QString encoderFactory() const { return encoderFactory_; }
    QString encoderName() const { return encoderName_; }
    bool connected() const { return connected_; }
signals:
    void outgoing(QJsonObject message);
    void status(QString state);
    void metrics(QJsonObject values);
    void error(QString message);
    void transportError();
    void mediaFailure(QString code);
private:
#ifdef LAZARUS_TESTING
    friend struct PeerTestAccess;
#endif
    static void offerNeeded(GstElement *, gpointer);
    static void descriptionCreated(GstPromise *, gpointer);
    static void iceCandidate(GstElement *, guint, gchar *, gpointer);
    static void padAdded(GstElement *, GstPad *, gpointer);
    static GstFlowReturn newFrame(GstAppSink *, gpointer);
    void createDescription(bool offer);
    void poll();
    void stats();
    void applyStats(const QJsonObject &values);
    void push(GstElement *source, GstSample *sample,GstClockTime timestamp=GST_CLOCK_TIME_NONE);
    bool host_, offered_ = false, remoteSet_ = false, connected_ = false;
    GstElement *pipeline_ = nullptr, *rtc_ = nullptr, *video_ = nullptr, *audio_ = nullptr, *encoder_ = nullptr,*pay_=nullptr;
    Quality quality_;
    BitrateController control_;
    QString inputFormat_="I420",encoderFactory_;
    std::atomic<unsigned> inputFrames_{0},queueDrops_{0},displayDrops_{0};
    std::atomic<qint64> lastEncodedMs_{0},lastInputMs_{0};
    qint64 connectedAt_=0;
    bool stalled_=false;
    int targetKbps_ = 8000, currentKbps_ = 8000;
    QByteArray bitrateProperty_ = "target-bitrate";
    int bitrateMultiplier_ = 1000;
    QJsonArray pendingIce_;
    QMutex frameMutex_;
    QImage frame_;
    QTimer timer_;
    QElapsedTimer statsTime_;
    std::atomic<unsigned> videoSsrc_{0};
    std::atomic<unsigned> encodedFrames_{0}, decodedFrames_{0};
    quint64 lastBytes_ = 0;
    qint64 lastLost_ = 0, lastPackets_ = 0;
    int pollCount_ = 0;
    int localCandidates_ = 0, remoteCandidates_ = 0;
    VideoTimeline videoTimeline_;
    std::atomic<unsigned> decoderFrames_{0};
    QString decoderName_;
    QString encoderName_;
    QString stage_ = "new", iceState_ = "new", gatheringState_ = "new", dtlsState_ = "unknown";
    QJsonObject localCounts_, remoteCounts_;
};
