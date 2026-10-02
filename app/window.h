#pragma once
#include "audio.h"
#include "capture.h"
#include "protocol.h"
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
    Window();
    ~Window() override;
private:
    struct Connection {
        Protocol::Channel channel;
        std::unique_ptr<Peer> media;
        bool localConsent = false, remoteConsent = false, failed = false;
        QStringList turns;
        int generation = 1, retries = 0;
        int pendingRestart = 0;
        QJsonArray pendingSignals;
        qint64 started = 0;
        double kbps = 0;
        QString route = "Negociando P2P";
        QJsonObject metrics;
    };
    void create();
    void join();
    void stop();
    void openSocket();
    void send(QJsonObject message);
    void message(const QJsonObject &message);
    void startPeer(const QString &id);
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
    QLineEdit *endpoint_, *stun_, *token_, *tlsPin_;
    QComboBox *monitor_;
    QSpinBox *width_, *height_, *fps_, *bitrate_;
    QListWidget *viewers_, *apps_;
    QPushButton *create_, *join_, *stop_, *approve_, *remove_, *relay_;
    QLabel *status_, *video_, *metrics_, *audioStatus_;
    QWebSocket socket_;
    Capture capture_;
    Audio audio_;
    QTimer frameTimer_, maintenance_;
    QElapsedTimer time_;
    bool active_ = false, host_ = false, created_ = false, refreshingAudio_ = false, testPattern_ = false;
    QByteArray secret_;
    QString room_, admin_, challenge_, viewerId_;
    int reconnects_ = 0, frames_ = 0;
    qint64 socketLost_ = -1, lastFrameTime_ = 0;
    std::map<QString, std::unique_ptr<Connection>> peers_;
    QSet<QString> approved_, seenSessions_;
};
