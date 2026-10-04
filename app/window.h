#pragma once
#include "audio.h"
#include "videoview.h"
#include "capture.h"
#include "protocol.h"
#include "profile.h"
#include "diagnostic.h"
#include "turnrenewal.h"
#include <QMainWindow>
#include <QWebSocket>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QElapsedTimer>
#include <map>
#include <memory>
class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window(bool onboarding = true);
    ~Window() override;
private:
#ifdef LAZARUS_TESTING
    friend struct WindowTestAccess;
    int blockedTransport_ = -2;
#endif
    struct Connection {
        Protocol::Channel channel;
        std::unique_ptr<Peer> media;
        bool localConsent = false, remoteConsent = false, failed = false;
        QStringList turns;
        QString nickname, session, lastRoute;
        int avatar = 0, transport = -1, reconnectAttempts = 0;
        qint64 turnExpiry = 0, turnEpoch = 0;
        TurnRenewal renewal;
        bool fatalMedia = false, exhausted = false, relayRequested = false, everConnected = false, modern = false;
        int generation = 1, retries = 0;
        bool software=false, selecting=false;
        int selection=0;
        int encoderWidth=0,encoderHeight=0,encoderFps=0;
        int pendingRestart = 0;
        QJsonArray pendingSignals;
        qint64 started = 0;
        double kbps = 0;
        QString route = "Negociando P2P";
        QJsonObject metrics;
        QSize receivedSize;
    };
    void editIdentity();
    void updateIdentity();
    void share();
    void stopSharing();
    void restart(const QString &id, int transport);
    void requestRelay(const QString &id);
    void renewTurn(const QString &id);
    void advance(const QString &id);
    void log(const QString &event, const QString &id = {}, QJsonObject fields = {});
    void create();
    void join();
    void stop();
    void openSocket();
    void send(QJsonObject message);
    void message(const QJsonObject &message);
    void startPeer(const QString &id);
    void attachPeer(const QString &id, VideoEncoder backend, int generation);
    void mediaFailure(const QString &id,int generation,const QString &code);
    void signal(const QString &id, QJsonObject body);
    void relay();
    void tick();
    void refreshAudio();
    void selectAudio();
    void diagnostics();
    void applyQuality();
    void row(const QString &id, const QString &text);
    QString selectedPeer() const;
    Quality quality() const;
    void notice(const QString &message);
    QLineEdit *token_;
    QString endpoint_, stun_, tlsPin_;
    QComboBox *monitor_, *preset_;
    QPushButton *identity_, *share_, *pause_, *change_;
    Profile profile_;
    DiagnosticLog log_;
    QSpinBox *width_, *height_, *fps_, *bitrate_;
    QListWidget *viewers_, *apps_;
    QPushButton *create_, *join_, *stop_, *approve_, *remove_, *relay_;
    QLabel *status_, *metrics_, *audioStatus_;
    VideoView *video_;
    QWebSocket socket_;
    Capture capture_;
    Audio audio_;
    QTimer frameTimer_, maintenance_;
    QElapsedTimer time_;
    bool sharing_ = false, capturePending_ = false;
    bool active_ = false, host_ = false, created_ = false, refreshingAudio_ = false, testPattern_ = false;
    QByteArray secret_;
    QString room_, admin_, challenge_, viewerId_;
    int reconnects_ = 0, frames_ = 0;
    qint64 socketLost_ = -1, lastFrameTime_ = 0;
    std::map<QString, std::unique_ptr<Connection>> peers_;
    QSet<QString> approved_, seenSessions_;
};
