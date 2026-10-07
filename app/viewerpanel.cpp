#include "viewerpanel.h"
#include "profile.h"
#include "theme.h"
#include <QScrollArea>
#include <QScrollBar>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include <QBoxLayout>
#include <QShortcut>
#include <QCloseEvent>
#include <QPainter>
#include <QFrame>
#include <QTimer>
#include <QMouseEvent>
#include <QApplication>

ViewerPanel::ViewerPanel(QWidget *parent) : QWidget(parent) {
    setObjectName("viewerPanel");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    // Top Identity Bar (hidden during true fullscreen)
    topBar_ = new QWidget(this);
    topBar_->setObjectName("viewerTopBar");
    auto *topLayout = new QVBoxLayout(topBar_);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(2);

    transmitter_ = new QLabel("Ninguém está compartilhando", topBar_);
    transmitter_->setObjectName("transmitter");
    state_ = new QLabel("Aguardando compartilhamento", topBar_);
    state_->setObjectName("viewerState");
    avatar_ = new QLabel(topBar_);
    avatar_->setFixedSize(22, 22);

    auto *identity = new QHBoxLayout;
    identity->setContentsMargins(0, 0, 0, 0);
    identity->setSpacing(8);
    identity->addWidget(avatar_);
    identity->addWidget(transmitter_, 1);
    topLayout->addLayout(identity);
    topLayout->addWidget(state_);
    layout->addWidget(topBar_);

    // Scroll Area & Video
    scroll_ = new QScrollArea(this);
    scroll_->setAlignment(Qt::AlignCenter);
    scroll_->setWidgetResizable(true);
    scroll_->setMinimumSize(400, 260);
    scroll_->viewport()->installEventFilter(this);
    scroll_->viewport()->setMouseTracking(true);
    scroll_->setStyleSheet("QScrollArea { background-color: #F0EEE3; border: 1px solid #EAE8DE; border-radius: 12px; }");

    frozen_ = new QLabel(scroll_->viewport());
    frozen_->setObjectName("frozenWarning");
    frozen_->setWordWrap(true);
    frozen_->setAlignment(Qt::AlignCenter);
    frozen_->setStyleSheet("background: rgba(255, 235, 235, 230); color: #BA1A1A; border: 1.5px solid #BA1A1A; border-radius: 8px; padding: 12px; font-weight: 600;");
    frozen_->hide();

    video_ = new VideoView;
    video_->setObjectName("video");
    video_->setAlignment(Qt::AlignCenter);
    video_->setMouseTracking(true);
    video_->installEventFilter(this);
    video_->setStyleSheet("background: #FFFFFF; color: #1B1C16; border: 1px solid #EAE8DE; border-radius: 12px; font-size: 14px; font-weight: 600;");
    video_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    video_->clearFrame("Aguardando compartilhamento");
    scroll_->setWidget(video_);
    layout->addWidget(scroll_, 1);

    // Floating Controls Pill overlay inside viewport
    floatingControls_ = new QFrame(scroll_->viewport());
    floatingControls_->setObjectName("floatingViewerControls");
    floatingControls_->setStyleSheet(
        "QFrame#floatingViewerControls { "
        "  background-color: rgba(27, 28, 22, 0.88); "
        "  border-radius: 20px; "
        "  border: 1px solid rgba(255, 255, 255, 0.18); "
        "}"
    );
    auto *fcLayout = new QHBoxLayout(floatingControls_);
    fcLayout->setContentsMargins(10, 5, 10, 5);
    fcLayout->setSpacing(8);

    size_ = new QComboBox(floatingControls_);
    size_->setObjectName("viewerSize");
    size_->addItems({"Ajustar à janela", "Tamanho real (100%)"});
    size_->setStyleSheet(
        "QComboBox { "
        "  background-color: rgba(255, 255, 255, 0.15); "
        "  color: #FFFFFF; "
        "  border: none; "
        "  border-radius: 10px; "
        "  padding: 4px 10px; "
        "  font-size: 11px; "
        "  font-weight: 600; "
        "} "
        "QComboBox::drop-down { border: none; width: 0px; } "
        "QComboBox QAbstractItemView { "
        "  background-color: #1B1C16; "
        "  color: #FFFFFF; "
        "  selection-background-color: #0FFCBE; "
        "  selection-color: #000000; "
        "  border: 1px solid #EAE8DE; "
        "}"
    );
    fcLayout->addWidget(size_);

    // Collapsible Volume Control on Hover
    volumeContainer_ = new QWidget(floatingControls_);
    volumeContainer_->setObjectName("volumeContainer");
    auto *vcLayout = new QHBoxLayout(volumeContainer_);
    vcLayout->setContentsMargins(2, 0, 2, 0);
    vcLayout->setSpacing(6);

    muteBtn_ = new QPushButton(volumeContainer_);
    muteBtn_->setObjectName("muteBtn");
    muteBtn_->setFixedSize(28, 28);
    muteBtn_->setIcon(Theme::icon("volume_up", QColor(255, 255, 255), 18));
    muteBtn_->setToolTip("Volume (clique para mutar/desmutar)");
    muteBtn_->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 14px; } "
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.20); }"
    );
    vcLayout->addWidget(muteBtn_);

    volume_ = new QSlider(Qt::Horizontal, volumeContainer_);
    volume_->setObjectName("receivedVolume");
    volume_->setAccessibleName("Volume recebido");
    volume_->setRange(0, 100);
    volume_->setValue(100);
    volume_->setFixedWidth(80);
    volume_->setStyleSheet(
        "QSlider::groove:horizontal { height: 4px; background: rgba(255, 255, 255, 0.3); border-radius: 2px; } "
        "QSlider::sub-page:horizontal { background: #0FFCBE; border-radius: 2px; } "
        "QSlider::handle:horizontal { background: #FFFFFF; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }"
    );
    volume_->hide();
    vcLayout->addWidget(volume_);

    volumeLabel_ = new QLabel("100%", volumeContainer_);
    volumeLabel_->setObjectName("volumeLabel");
    volumeLabel_->setStyleSheet("color: #FFFFFF; font-size: 10px; font-weight: 700; font-family: 'JetBrains Mono'; min-width: 34px;");
    volumeLabel_->hide();
    vcLayout->addWidget(volumeLabel_);
    fcLayout->addWidget(volumeContainer_);

    fullscreenBtn_ = new QPushButton(floatingControls_);
    fullscreenBtn_->setObjectName("fullscreen");
    fullscreenBtn_->setFixedSize(28, 28);
    fullscreenBtn_->setIcon(Theme::icon("monitor", QColor(255, 255, 255), 18));
    fullscreenBtn_->setToolTip("Tela cheia (F11)");
    fullscreenBtn_->setStyleSheet(
        "QPushButton { background: transparent; border: none; border-radius: 14px; } "
        "QPushButton:hover { background-color: rgba(255, 255, 255, 0.20); }"
    );
    fcLayout->addWidget(fullscreenBtn_);

    // Hidden checkbox to maintain contract with existing tests
    mute_ = new QCheckBox("Silenciar", this);
    mute_->setObjectName("receivedMute");
    mute_->hide();

    // Event Filters & Connections
    volumeContainer_->installEventFilter(this);
    floatingControls_->installEventFilter(this);

    autoHideTimer_ = new QTimer(this);
    autoHideTimer_->setInterval(2500);
    autoHideTimer_->setSingleShot(true);
    connect(autoHideTimer_, &QTimer::timeout, this, [this] {
        if (fullscreen_ && !floatingControls_->underMouse()) {
            floatingControls_->hide();
        }
    });

    connect(size_, &QComboBox::currentIndexChanged, this, [this] { updateSize(); updateFloatingGeometry(); });
    connect(fullscreenBtn_, &QPushButton::clicked, this, &ViewerPanel::toggleFullscreen);
    connect(muteBtn_, &QPushButton::clicked, this, [this] {
        mute_->setChecked(!mute_->isChecked());
    });
    connect(mute_, &QCheckBox::toggled, this, [this](bool m) {
        updateMuteUi(m);
        emit playbackChanged();
    });
    connect(volume_, &QSlider::valueChanged, this, [this](int v) {
        volumeLabel_->setText(QString("%1%").arg(v));
        if (v == 0 && !mute_->isChecked()) mute_->setChecked(true);
        else if (v > 0 && mute_->isChecked()) mute_->setChecked(false);
        emit playbackChanged();
    });

    auto *f11 = new QShortcut(QKeySequence(Qt::Key_F11), this);
    connect(f11, &QShortcut::activated, this, &ViewerPanel::toggleFullscreen);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(escape, &QShortcut::activated, this, [this] { if (fullscreen_) toggleFullscreen(); });

    updateFloatingGeometry();
}

double ViewerPanel::volume() const { return volume_->value() / 100.0; }
bool ViewerPanel::muted() const { return mute_->isChecked(); }

void ViewerPanel::updateMuteUi(bool isMuted) {
    if (isMuted) {
        muteBtn_->setIcon(Theme::icon("volume_off", QColor(255, 120, 120), 18));
        volumeLabel_->setText("0%");
    } else {
        muteBtn_->setIcon(Theme::icon("volume_up", QColor(255, 255, 255), 18));
        volumeLabel_->setText(QString("%1%").arg(volume_->value()));
    }
}

void ViewerPanel::updateFloatingGeometry() {
    if (!floatingControls_ || !scroll_ || !scroll_->viewport()) return;
    floatingControls_->adjustSize();
    int pw = scroll_->viewport()->width();
    int ph = scroll_->viewport()->height();
    int fw = floatingControls_->width();
    int fh = floatingControls_->height();
    floatingControls_->setGeometry((pw - fw) / 2, qMax(10, ph - fh - 16), fw, fh);
    floatingControls_->raise();
}

void ViewerPanel::setFrame(QImage image) {
    const bool changed = frameSize_ != image.size();
    frameSize_ = image.size();
    video_->setFrame(std::move(image));
    if (changed) updateSize();
    frozen_->hide();
    state_->setText(baseState_);
    updateFloatingGeometry();
}

void ViewerPanel::clearFrame(const QString &message) {
    lastFrame_ = -1;
    frameSize_ = {};
    video_->clearFrame(message);
    frozen_->hide();
    updateSize();
    updateFloatingGeometry();
}

void ViewerPanel::presentation(const QString &state, const QString &name, int avatar) {
    baseState_ = state;
    const auto label = name.isEmpty() ? QString("Ninguém está compartilhando") : name;
    if (state_->text() == state && transmitter_->text() == label && transmitter_->property("avatar").toInt() == avatar) return;
    state_->setTextFormat(Qt::PlainText);
    transmitter_->setTextFormat(Qt::PlainText);
    state_->setText(state);
    transmitter_->setText(label);
    avatar_->setPixmap(Theme::avatarPixmap(avatar, 20));
    transmitter_->setProperty("avatar", avatar);
}

void ViewerPanel::updateFreeze(qint64 now) {
    if (lastFrame_ >= 0 && video_->hasFrame() && now - lastFrame_ >= 5000) {
        frozen_->setText(QString("Imagem congelada — sem novos quadros há %1 segundos").arg((now - lastFrame_) / 1000));
        frozen_->setGeometry(0, 0, scroll_->viewport()->width(), qMin(80, scroll_->viewport()->height()));
        frozen_->show();
        frozen_->raise();
        if (state_->text() == "Ao vivo") state_->setText("Imagem congelada");
    }
}

void ViewerPanel::updateSize() {
    bool native = size_->currentIndex() == 1 && frameSize_.isValid();
    scroll_->setWidgetResizable(!native);
    video_->setMinimumSize(native ? frameSize_ : QSize(0, 0));
    if (native) video_->resize(frameSize_);
}

bool ViewerPanel::eventFilter(QObject *o, QEvent *e) {
    if (o == scroll_->viewport() || o == video_) {
        if (e->type() == QEvent::Resize) {
            updateSize();
            updateFloatingGeometry();
            if (frozen_) frozen_->setGeometry(0, 0, scroll_->viewport()->width(), qMin(80, scroll_->viewport()->height()));
        } else if (e->type() == QEvent::MouseMove) {
            floatingControls_->show();
            updateFloatingGeometry();
            if (fullscreen_) autoHideTimer_->start(2500);
        }
    } else if (o == volumeContainer_) {
        if (e->type() == QEvent::Enter) {
            volume_->show();
            volumeLabel_->show();
            updateFloatingGeometry();
        } else if (e->type() == QEvent::Leave) {
            volume_->hide();
            volumeLabel_->hide();
            updateFloatingGeometry();
        }
    } else if (o == floatingControls_) {
        if (e->type() == QEvent::Enter && fullscreen_) {
            autoHideTimer_->stop();
        } else if (e->type() == QEvent::Leave && fullscreen_) {
            autoHideTimer_->start(2500);
        }
    }
    return QWidget::eventFilter(o, e);
}

void ViewerPanel::resizeEvent(QResizeEvent *e) {
    QWidget::resizeEvent(e);
    updateFloatingGeometry();
}

void ViewerPanel::toggleFullscreen() {
    if (!fullscreen_) {
        originalParent_ = parentWidget();
        if (!originalParent_) return;
        originalLayout_ = qobject_cast<QBoxLayout *>(originalParent_->layout());
        if (!originalLayout_) return;
        originalIndex_ = originalLayout_->indexOf(this);
        originalLayout_->removeWidget(this);
        setParent(nullptr, Qt::Window);
        fullscreen_ = true;
        topBar_->hide();
        scroll_->setStyleSheet("QScrollArea { background-color: #000000; border: none; }");
        video_->setStyleSheet("background-color: #000000; color: #FFFFFF; border: none; font-size: 14px; font-weight: 600;");
        showFullScreen();
        floatingControls_->show();
        updateFloatingGeometry();
        autoHideTimer_->start(2500);
    } else {
        autoHideTimer_->stop();
        hide();
        setParent(originalParent_);
        originalLayout_->insertWidget(originalIndex_, this, 2);
        fullscreen_ = false;
        topBar_->show();
        scroll_->setStyleSheet("QScrollArea { background-color: #F0EEE3; border: 1px solid #EAE8DE; border-radius: 12px; }");
        video_->setStyleSheet("background: #FFFFFF; color: #1B1C16; border: 1px solid #EAE8DE; border-radius: 12px; font-size: 14px; font-weight: 600;");
        floatingControls_->show();
        show();
        updateFloatingGeometry();
    }
    setFocus();
}

void ViewerPanel::closeEvent(QCloseEvent *e) {
    if (fullscreen_) {
        e->ignore();
        toggleFullscreen();
    } else {
        QWidget::closeEvent(e);
    }
}
