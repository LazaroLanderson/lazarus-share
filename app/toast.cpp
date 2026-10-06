#include "toast.h"
#include "theme.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>

Toast::Toast(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedHeight(76);
    setMinimumWidth(380);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(18, 12, 18, 12);
    layout->setSpacing(12);

    auto *badge = new QLabel(this);
    badge->setFixedSize(36, 36);
    badge->setStyleSheet("background-color: #14FDBF; border-radius: 18px;");
    badge->setAlignment(Qt::AlignCenter);
    badge->setPixmap(Theme::icon("bolt", QColor(0, 33, 22), 18).pixmap(18, 18));
    layout->addWidget(badge);

    auto *textLayout = new QVBoxLayout;
    textLayout->setSpacing(2);
    textLayout->setContentsMargins(0, 0, 0, 0);

    auto *titleLabel = new QLabel("Em breve", this);
    titleLabel->setFont(Theme::headlineFont(11, QFont::Bold));
    titleLabel->setStyleSheet("color: #1B1C16; font-weight: 700;");
    textLayout->addWidget(titleLabel);

    auto *descLabel = new QLabel("Estamos trabalhando para te entregar essa feature.", this);
    descLabel->setFont(Theme::bodyFont(9));
    descLabel->setStyleSheet("color: #4C4546; padding: 1px 0px;");
    descLabel->setWordWrap(true);
    textLayout->addWidget(descLabel);

    layout->addLayout(textLayout, 1);

    auto *closeBtn = new QPushButton(this);
    closeBtn->setFixedSize(24, 24);
    closeBtn->setIcon(Theme::icon("close", QColor(76, 69, 70), 12));
    closeBtn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 12px; } QPushButton:hover { background: #F0EEE3; }");
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    layout->addWidget(closeBtn);

    // Auto close timer
    QTimer::singleShot(2800, this, [this] {
        auto *effect = new QGraphicsOpacityEffect(this);
        setGraphicsEffect(effect);
        auto *anim = new QPropertyAnimation(effect, "opacity", this);
        anim->setDuration(350);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        connect(anim, &QPropertyAnimation::finished, this, &QWidget::close);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

void Toast::showMessage(const QString &title, const QString &message) {
    Q_UNUSED(title);
    Q_UNUSED(message);
}

void Toast::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QPainterPath path;
    path.addRoundedRect(rect().adjusted(1, 1, -1, -1), 12, 12);

    p.fillPath(path, QColor(0xFF, 0xFF, 0xFF, 250));
    p.setPen(QPen(QColor(0x1B, 0x1C, 0x16), 1.5));
    p.drawPath(path);
}

void Toast::showComingSoon(QWidget *parent, const QString &feature) {
    if (!parent) return;
    auto *toast = new Toast(parent);

    // Position at top center or bottom right
    const int x = (parent->width() - toast->width()) / 2;
    const int y = 24;
    toast->move(qMax(16, x), y);
    toast->show();
    toast->raise();

    Q_UNUSED(feature);
}
