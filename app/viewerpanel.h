#pragma once
#include "videoview.h"
#include <QWidget>
#include <QElapsedTimer>
class QScrollArea;
class QComboBox;
class QSlider;
class QCheckBox;
class QBoxLayout;
class QPushButton;
class QFrame;
class QTimer;

class ViewerPanel : public QWidget {
    Q_OBJECT
public:
    explicit ViewerPanel(QWidget *parent = nullptr);
    VideoView *video() const { return video_; }
    void setFrame(QImage image);
    void clearFrame(const QString &message);
    void presentation(const QString &state, const QString &name, int avatar);
    void updateFreeze(qint64 now);
    void frameAt(qint64 now) { lastFrame_ = now; }
    void toggleFullscreen();
    bool fullscreen() const { return fullscreen_; }
    double volume() const;
    bool muted() const;
signals:
    void playbackChanged();
protected:
    bool eventFilter(QObject *, QEvent *) override;
    void closeEvent(QCloseEvent *) override;
    void resizeEvent(QResizeEvent *) override;
private:
    void updateSize();
    void updateFloatingGeometry();
    void updateMuteUi(bool isMuted);
    VideoView *video_;
    QScrollArea *scroll_;
    QComboBox *size_;
    QSlider *volume_;
    QCheckBox *mute_;
    QLabel *state_, *transmitter_, *avatar_, *frozen_ = nullptr;
    QString baseState_ = "Aguardando compartilhamento";
    QWidget *originalParent_ = nullptr;
    QBoxLayout *originalLayout_ = nullptr;
    int originalIndex_ = 0;
    bool fullscreen_ = false;
    QSize frameSize_;
    qint64 lastFrame_ = -1;

    QWidget *topBar_ = nullptr;
    QFrame *floatingControls_ = nullptr;
    QWidget *volumeContainer_ = nullptr;
    QPushButton *muteBtn_ = nullptr;
    QLabel *volumeLabel_ = nullptr;
    QPushButton *fullscreenBtn_ = nullptr;
    QTimer *autoHideTimer_ = nullptr;
};
