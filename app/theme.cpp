#include "theme.h"
#include <QApplication>
#include <QFontDatabase>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QBrush>
#include <QFileInfo>
#include <QPalette>
#include <QCoreApplication>

static QString s_headlineFamily = "Space Grotesk";
static QString s_bodyFamily = "Geist";
static QString s_codeFamily = "JetBrains Mono";

void Theme::initFonts() {
    QStringList fontFiles = {
        "assets/fonts/SpaceGrotesk-Regular.ttf",
        "assets/fonts/Geist-Regular.ttf",
        "assets/fonts/JetBrainsMono-Regular.ttf"
    };

    for (const auto &file : fontFiles) {
        if (QFile::exists(file)) {
            int id = QFontDatabase::addApplicationFont(file);
            if (id >= 0) {
                QStringList families = QFontDatabase::applicationFontFamilies(id);
                if (!families.isEmpty()) {
                    if (file.contains("SpaceGrotesk", Qt::CaseInsensitive)) s_headlineFamily = families.first();
                    else if (file.contains("Geist", Qt::CaseInsensitive)) s_bodyFamily = families.first();
                    else if (file.contains("JetBrainsMono", Qt::CaseInsensitive)) s_codeFamily = families.first();
                }
            }
        }
    }
}

QFont Theme::headlineFont(int pointSize, int weight) {
    QFont font(s_headlineFamily, pointSize, weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QFont Theme::bodyFont(int pointSize, int weight) {
    QFont font(s_bodyFamily, pointSize, weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QFont Theme::codeFont(int pointSize, int weight) {
    QFont font(s_codeFamily, pointSize, weight);
    font.setStyleStrategy(QFont::PreferAntialias);
    return font;
}

QPixmap Theme::logoPixmap(int height) {
    static QPixmap s_cached;
    static int s_cachedHeight = 0;
    if (!s_cached.isNull() && s_cachedHeight == height) {
        return s_cached;
    }
    QPixmap original;
    QStringList candidates = {
        "assets/share_lazarus_icon.svg",
        "assets/share_Lazarus_icon.png",
        "assets/share_lazarus_logo.svg",
        "assets/logo_lazarus_share.png",
        "assets/app_icon.png",
        QCoreApplication::applicationDirPath() + "/assets/share_lazarus_icon.svg",
        QCoreApplication::applicationDirPath() + "/assets/share_Lazarus_icon.png",
        QCoreApplication::applicationDirPath() + "/assets/share_lazarus_logo.svg",
        QCoreApplication::applicationDirPath() + "/assets/logo_lazarus_share.png",
        QCoreApplication::applicationDirPath() + "/../assets/share_lazarus_icon.svg",
        QCoreApplication::applicationDirPath() + "/../assets/share_Lazarus_icon.png",
        QCoreApplication::applicationDirPath() + "/../assets/share_lazarus_logo.svg",
        QCoreApplication::applicationDirPath() + "/../assets/logo_lazarus_share.png"
    };
    for (const auto &path : candidates) {
        if (QFile::exists(path)) {
            original.load(path);
            if (!original.isNull()) break;
        }
    }
    if (!original.isNull()) {
        s_cached = original.scaledToHeight(height, Qt::SmoothTransformation);
        s_cachedHeight = height;
        return s_cached;
    }
    return QPixmap();
}

QIcon Theme::appIcon() {
    static QIcon s_appIcon;
    if (!s_appIcon.isNull()) return s_appIcon;

    QStringList candidates = {
        "assets/share_lazarus_icon.svg",
        "assets/share_Lazarus_icon.png",
        "assets/app_icon.png",
        "assets/app_icon.ico",
        "assets/logo_lazarus_share.png",
        "assets/share_lazarus_logo.svg",
        QCoreApplication::applicationDirPath() + "/assets/share_lazarus_icon.svg",
        QCoreApplication::applicationDirPath() + "/assets/share_Lazarus_icon.png",
        QCoreApplication::applicationDirPath() + "/assets/app_icon.png",
        QCoreApplication::applicationDirPath() + "/assets/app_icon.ico",
        QCoreApplication::applicationDirPath() + "/assets/logo_lazarus_share.png",
        QCoreApplication::applicationDirPath() + "/../assets/share_lazarus_icon.svg",
        QCoreApplication::applicationDirPath() + "/../assets/share_Lazarus_icon.png",
        QCoreApplication::applicationDirPath() + "/../assets/app_icon.png",
        QCoreApplication::applicationDirPath() + "/../assets/logo_lazarus_share.png"
    };

    for (const auto &path : candidates) {
        if (QFile::exists(path)) {
            s_appIcon = QIcon(path);
            if (!s_appIcon.isNull()) break;
        }
    }
    return s_appIcon;
}

#include <cmath>

QString Theme::avatarName(int index) {
    static const QString names[] = {
        "Pato", "Castor", "Vaca", "Bode", "Rato",
        "Leão", "Raposa", "Urso", "Coruja", "Sapo"
    };
    return names[qBound(0, index, 9)];
}

QPixmap Theme::avatarPixmap(int index, int size) {
    int idx = qBound(0, index, 9);
    QString path = QString("assets/avatars/avatar_%1.png").arg(idx);
    QString altPath = QCoreApplication::applicationDirPath() + "/" + path;
    QString upPath = QCoreApplication::applicationDirPath() + "/../" + path;

    QPixmap px;
    if (QFile::exists(path)) px.load(path);
    else if (QFile::exists(altPath)) px.load(altPath);
    else if (QFile::exists(upPath)) px.load(upPath);

    if (!px.isNull()) {
        return px.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // Fallback: colored circle with first letter
    QPixmap circle(size, size);
    circle.fill(Qt::transparent);
    QPainter p(&circle);
    p.setRenderHint(QPainter::Antialiasing);
    static const char *colors[] = {"#EF5350", "#EC407A", "#AB47BC", "#5C6BC0", "#42A5F5", "#26C6DA", "#26A69A", "#66BB6A", "#FFA726", "#8D6E63"};
    p.setBrush(QColor(colors[idx]));
    p.setPen(Qt::NoPen);
    p.drawEllipse(1, 1, size - 2, size - 2);
    p.setPen(QColor(255, 255, 255));
    p.setFont(QFont("Space Grotesk", qMax(8, size / 2), QFont::Bold));
    p.drawText(QRect(0, 0, size, size), Qt::AlignCenter, avatarName(idx).left(1));
    p.end();
    return circle;
}

QIcon Theme::avatarIcon(int index, int size) {
    return QIcon(avatarPixmap(index, size));
}

QIcon Theme::icon(const QString &name, const QColor &color, int size) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);

    const qreal s = size;
    const qreal pad = s * 0.14;
    const qreal w = s - 2 * pad;
    const qreal h = s - 2 * pad;

    if (name == "settings") {
        // True 6-tooth mechanical cogwheel/gear with center hole (no crosshairs/mira!)
        const qreal cx = s / 2.0, cy = s / 2.0;
        const qreal rInner = w * 0.20;
        const qreal rGearInner = w * 0.38;
        const qreal rGearOuter = w * 0.50;
        QPainterPath gearPath;
        const int teeth = 6;
        for (int i = 0; i < teeth; ++i) {
            qreal a1 = (i * 2.0 * M_PI) / teeth;
            qreal a2 = a1 + (0.24 * 2.0 * M_PI) / teeth;
            qreal a3 = a1 + (0.50 * 2.0 * M_PI) / teeth;
            qreal a4 = a1 + (0.74 * 2.0 * M_PI) / teeth;
            qreal a5 = ((i + 1) * 2.0 * M_PI) / teeth;

            QPointF p1(cx + rGearInner * std::cos(a1), cy + rGearInner * std::sin(a1));
            QPointF p2(cx + rGearOuter * std::cos(a2), cy + rGearOuter * std::sin(a2));
            QPointF p3(cx + rGearOuter * std::cos(a3), cy + rGearOuter * std::sin(a3));
            QPointF p4(cx + rGearInner * std::cos(a4), cy + rGearInner * std::sin(a4));
            QPointF p5(cx + rGearInner * std::cos(a5), cy + rGearInner * std::sin(a5));

            if (i == 0) gearPath.moveTo(p1);
            else gearPath.lineTo(p1);
            gearPath.lineTo(p2);
            gearPath.lineTo(p3);
            gearPath.lineTo(p4);
            gearPath.lineTo(p5);
        }
        gearPath.closeSubpath();
        p.drawPath(gearPath);
        p.drawEllipse(QPointF(cx, cy), rInner, rInner);
    } else if (name == "tune") {
        // Clean sliders equalizer (horizontal tracks with knobs)
        qreal y1 = pad + h * 0.32;
        qreal y2 = pad + h * 0.68;
        p.drawLine(QPointF(pad, y1), QPointF(s - pad, y1));
        p.drawLine(QPointF(pad, y2), QPointF(s - pad, y2));
        p.setBrush(color);
        p.drawEllipse(QPointF(pad + w * 0.35, y1), 2.5, 2.5);
        p.drawEllipse(QPointF(pad + w * 0.65, y2), 2.5, 2.5);
        p.setBrush(Qt::NoBrush);
    } else if (name == "person") {
        p.drawEllipse(QPointF(s / 2, pad + h * 0.28), w * 0.24, w * 0.24);
        QPainterPath body;
        body.arcMoveTo(pad, pad + h * 0.45, w, h * 0.55, 0);
        body.arcTo(pad, pad + h * 0.45, w, h * 0.55, 0, 180);
        p.drawPath(body);
    } else if (name == "arrow_forward") {
        p.drawLine(QPointF(pad, s / 2), QPointF(s - pad, s / 2));
        p.drawLine(QPointF(s - pad - 4, s / 2 - 4), QPointF(s - pad, s / 2));
        p.drawLine(QPointF(s - pad - 4, s / 2 + 4), QPointF(s - pad, s / 2));
    } else if (name == "arrow_back") {
        p.drawLine(QPointF(pad, s / 2), QPointF(s - pad, s / 2));
        p.drawLine(QPointF(pad + 4, s / 2 - 4), QPointF(pad, s / 2));
        p.drawLine(QPointF(pad + 4, s / 2 + 4), QPointF(pad, s / 2));
    } else if (name == "cast" || name == "screen_share") {
        p.drawRoundedRect(QRectF(pad, pad + 1, w, h * 0.70), 3, 3);
        p.drawLine(QPointF(s / 2, pad + 1 + h * 0.70), QPointF(s / 2, s - pad));
        p.drawLine(QPointF(pad + w * 0.25, s - pad), QPointF(s - pad - w * 0.25, s - pad));
        // Upward broadcast arrow
        p.drawLine(QPointF(s / 2, pad + h * 0.52), QPointF(s / 2, pad + h * 0.22));
        p.drawLine(QPointF(s / 2 - 3, pad + h * 0.34), QPointF(s / 2, pad + h * 0.22));
        p.drawLine(QPointF(s / 2 + 3, pad + h * 0.34), QPointF(s / 2, pad + h * 0.22));
    } else if (name == "tv" || name == "monitor") {
        p.drawRoundedRect(QRectF(pad, pad + 1, w, h * 0.70), 3, 3);
        p.drawLine(QPointF(s / 2, pad + 1 + h * 0.70), QPointF(s / 2, s - pad));
        p.drawLine(QPointF(pad + w * 0.25, s - pad), QPointF(s - pad - w * 0.25, s - pad));
    } else if (name == "login") {
        // Door bracket
        QPainterPath door;
        door.moveTo(pad + w * 0.45, pad);
        door.lineTo(pad, pad);
        door.lineTo(pad, s - pad);
        door.lineTo(pad + w * 0.45, s - pad);
        p.drawPath(door);
        // Arrow pointing right
        p.drawLine(QPointF(pad + w * 0.25, s / 2), QPointF(s - pad, s / 2));
        p.drawLine(QPointF(s - pad - 4, s / 2 - 4), QPointF(s - pad, s / 2));
        p.drawLine(QPointF(s - pad - 4, s / 2 + 4), QPointF(s - pad, s / 2));
    } else if (name == "logout") {
        // Door bracket
        QPainterPath door;
        door.moveTo(pad + w * 0.45, pad);
        door.lineTo(pad, pad);
        door.lineTo(pad, s - pad);
        door.lineTo(pad + w * 0.45, s - pad);
        p.drawPath(door);
        // Arrow pointing left/out
        p.drawLine(QPointF(s - pad, s / 2), QPointF(pad + w * 0.30, s / 2));
        p.drawLine(QPointF(pad + w * 0.30 + 4, s / 2 - 4), QPointF(pad + w * 0.30, s / 2));
        p.drawLine(QPointF(pad + w * 0.30 + 4, s / 2 + 4), QPointF(pad + w * 0.30, s / 2));
    } else if (name == "bolt") {
        QPainterPath bolt;
        bolt.moveTo(s / 2 + 2, pad);
        bolt.lineTo(pad + 3, s / 2 + 1);
        bolt.lineTo(s / 2, s / 2 + 1);
        bolt.lineTo(s / 2 - 2, s - pad);
        bolt.lineTo(s - pad - 1, s / 2 - 1);
        bolt.lineTo(s / 2 + 2, s / 2 - 1);
        bolt.closeSubpath();
        p.setBrush(color);
        p.drawPath(bolt);
        p.setBrush(Qt::NoBrush);
    } else if (name == "lock") {
        p.drawRoundedRect(QRectF(pad + 1, s / 2 - 1, w - 2, h * 0.55), 2, 2);
        p.drawArc(QRectF(pad + w * 0.22, pad, w * 0.56, h * 0.55), 0, 180 * 16);
    } else if (name == "security") {
        QPainterPath shield;
        shield.moveTo(pad + 1, pad + 2);
        shield.lineTo(s - pad - 1, pad + 2);
        shield.lineTo(s - pad - 1, pad + h * 0.48);
        shield.quadTo(QPointF(s - pad - 1, s - pad), QPointF(s / 2, s - pad));
        shield.quadTo(QPointF(pad + 1, s - pad), QPointF(pad + 1, pad + h * 0.48));
        shield.closeSubpath();
        p.drawPath(shield);
    } else if (name == "check") {
        p.drawLine(QPointF(pad + 1, s / 2), QPointF(pad + w * 0.38, s - pad - 2));
        p.drawLine(QPointF(pad + w * 0.38, s - pad - 2), QPointF(s - pad, pad + 2));
    } else if (name == "close") {
        p.drawLine(QPointF(pad + 2, pad + 2), QPointF(s - pad - 2, s - pad - 2));
        p.drawLine(QPointF(s - pad - 2, pad + 2), QPointF(pad + 2, s - pad - 2));
    } else if (name == "fullscreen") {
        p.drawLine(QPointF(pad, pad + 4), QPointF(pad, pad));
        p.drawLine(QPointF(pad, pad), QPointF(pad + 4, pad));
        p.drawLine(QPointF(s - pad - 4, pad), QPointF(s - pad, pad));
        p.drawLine(QPointF(s - pad, pad), QPointF(s - pad, pad + 4));
        p.drawLine(QPointF(pad, s - pad - 4), QPointF(pad, s - pad));
        p.drawLine(QPointF(pad, s - pad), QPointF(pad + 4, s - pad));
        p.drawLine(QPointF(s - pad - 4, s - pad), QPointF(s - pad, s - pad));
        p.drawLine(QPointF(s - pad, s - pad), QPointF(s - pad, s - pad - 4));
    } else if (name == "volume_up") {
        QPainterPath spk;
        spk.moveTo(pad, s / 2 - 3);
        spk.lineTo(pad + 4, s / 2 - 3);
        spk.lineTo(pad + 9, pad + 2);
        spk.lineTo(pad + 9, s - pad - 2);
        spk.lineTo(pad + 4, s / 2 + 3);
        spk.lineTo(pad, s / 2 + 3);
        spk.closeSubpath();
        p.drawPath(spk);
        p.drawArc(QRectF(s / 2, s / 2 - 5, 8, 10), -60 * 16, 120 * 16);
    } else if (name == "volume_off") {
        QPainterPath spk;
        spk.moveTo(pad, s / 2 - 3);
        spk.lineTo(pad + 4, s / 2 - 3);
        spk.lineTo(pad + 9, pad + 2);
        spk.lineTo(pad + 9, s - pad - 2);
        spk.lineTo(pad + 4, s / 2 + 3);
        spk.lineTo(pad, s / 2 + 3);
        spk.closeSubpath();
        p.drawPath(spk);
        p.drawLine(QPointF(s / 2 + 1, s / 2 - 4), QPointF(s - pad, s / 2 + 4));
        p.drawLine(QPointF(s - pad, s / 2 - 4), QPointF(s / 2 + 1, s / 2 + 4));
    } else if (name == "content_paste" || name == "paste") {
        p.drawRoundedRect(QRectF(pad + 2, pad + 3, w - 4, h - 3), 2, 2);
        p.drawRoundedRect(QRectF(s / 2 - 3, pad, 6, 4), 1, 1);
    } else if (name == "copy") {
        p.drawRoundedRect(QRectF(pad + 4, pad + 4, w - 4, h - 4), 2, 2);
        p.drawPolyline({QPointF(pad, s - pad - 4), QPointF(pad, pad), QPointF(s - pad - 4, pad)});
    } else {
        p.drawEllipse(QPointF(s / 2, s / 2), w * 0.4, w * 0.4);
    }

    p.end();
    return QIcon(pixmap);
}

QString Theme::globalStyleSheet() {
    return QString(R"(
        QWidget {
            color: #1B1C16;
            font-family: 'Geist', 'Space Grotesk', -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            font-size: 13px;
        }

        QLabel {
            background: transparent;
            border: none;
        }

        QMainWindow, QStackedWidget {
            background-color: #FCFAEF;
        }

        QScrollArea {
            background-color: #FCFAEF;
            border: none;
        }

        /* Generic PushButton */
        QPushButton {
            background-color: #F0EEE3;
            color: #1B1C16;
            border: 1px solid #E4E3D8;
            border-radius: 8px;
            padding: 7px 14px;
            font-weight: 600;
        }
        QPushButton:hover {
            background-color: #EAE8DE;
            border-color: #CFD0C5;
        }
        QPushButton:pressed {
            background-color: #E4E3D8;
        }
        QPushButton:disabled {
            background-color: #F6F4E9;
            color: #A09E94;
            border-color: #EAE8DE;
        }

        /* Primary Action Buttons */
        QPushButton.primary-btn, QPushButton#create-room-btn, QPushButton#btn-start-share {
            background-color: #000000;
            color: #FFFFFF;
            border: 1px solid #000000;
            border-radius: 8px;
            padding: 8px 16px;
            font-weight: 600;
        }
        QPushButton.primary-btn:hover, QPushButton#create-room-btn:hover, QPushButton#btn-start-share:hover {
            background-color: #1B1B1B;
            border-color: #1B1B1B;
        }
        QPushButton.primary-btn:pressed, QPushButton#create-room-btn:pressed, QPushButton#btn-start-share:pressed {
            background-color: #333333;
        }

        /* Secondary Mint Buttons */
        QPushButton.mint-btn {
            background-color: #14FDBF;
            color: #002116;
            border: 1px solid #0FFCBE;
            border-radius: 8px;
            padding: 8px 16px;
            font-weight: 700;
        }
        QPushButton.mint-btn:hover {
            background-color: #38FFC3;
        }

        /* Danger / Exit Button */
        QPushButton.danger-btn, QPushButton#btn-exit-room {
            background-color: #BA1A1A;
            color: #FFFFFF;
            border: 1px solid #BA1A1A;
            border-radius: 8px;
            padding: 6px 14px;
            font-weight: 600;
        }
        QPushButton.danger-btn:hover, QPushButton#btn-exit-room:hover {
            background-color: #D32F2F;
        }

        /* Input Fields */
        QLineEdit {
            background-color: #F6F4E9;
            color: #000000;
            border: 1px solid #E4E3D8;
            border-radius: 8px;
            padding: 8px 12px;
            font-family: 'JetBrains Mono', monospace;
            font-size: 13px;
            selection-background-color: #14FDBF;
            selection-color: #000000;
        }
        QLineEdit:focus {
            background-color: #FFFFFF;
            border: 1.5px solid #000000;
        }
        QLineEdit:read-only {
            background-color: #EAE8DE;
            color: #4C4546;
        }

        /* Combo Boxes */
        QComboBox {
            background-color: #F6F4E9;
            color: #1B1C16;
            border: 1px solid #E4E3D8;
            border-radius: 8px;
            padding: 6px 12px;
            font-weight: 500;
        }
        QComboBox:hover {
            background-color: #EAE8DE;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 24px;
            border-left: none;
        }
        QComboBox QAbstractItemView {
            background-color: #FFFFFF;
            color: #1B1C16;
            border: 1px solid #E4E3D8;
            border-radius: 8px;
            selection-background-color: #F0EEE3;
            selection-color: #000000;
            padding: 4px;
        }

        /* Checkboxes */
        QCheckBox {
            spacing: 8px;
            color: #1B1C16;
            font-weight: 500;
        }
        QCheckBox::indicator {
            width: 18px;
            height: 18px;
            border-radius: 5px;
            border: 1.5px solid #7E7576;
            background-color: #FFFFFF;
        }
        QCheckBox::indicator:hover {
            border-color: #000000;
        }
        QCheckBox::indicator:checked {
            background-color: #000000;
            border-color: #000000;
            image: none;
        }

        /* Sliders */
        QSlider::groove:horizontal {
            height: 6px;
            background: #E4E3D8;
            border-radius: 3px;
        }
        QSlider::sub-page:horizontal {
            background: #0FFCBE;
            border-radius: 3px;
        }
        QSlider::handle:horizontal {
            background: #000000;
            border: 2px solid #FFFFFF;
            width: 16px;
            margin-top: -5px;
            margin-bottom: -5px;
            border-radius: 8px;
        }
        QSlider::handle:horizontal:hover {
            background: #14FDBF;
        }

        /* List Widgets (Participants & Apps) */
        QListWidget {
            background-color: #FFFFFF;
            color: #1B1C16;
            border: 1px solid #E4E3D8;
            border-radius: 10px;
            padding: 6px;
            outline: none;
        }
        QListWidget::item {
            padding: 8px 10px;
            border-radius: 6px;
            margin-bottom: 2px;
        }
        QListWidget::item:hover {
            background-color: #F6F4E9;
        }
        QListWidget::item:selected {
            background-color: #F0EEE3;
            color: #000000;
            font-weight: 600;
        }

        /* Scrollbars */
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 0px;
        }
        QScrollBar::handle:vertical {
            background: #CFD0C5;
            min-height: 24px;
            border-radius: 4px;
        }
        QScrollBar::handle:vertical:hover {
            background: #A09E94;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0px;
        }

        /* Dialogs */
        QDialog {
            background-color: #FCFAEF;
        }
    )");
}

void Theme::apply(QApplication &app) {
    initFonts();
    app.setFont(bodyFont(10));

    QPalette pal;
    pal.setColor(QPalette::Window, QColor(0xFC, 0xFA, 0xEF));          // #FCFAEF
    pal.setColor(QPalette::WindowText, QColor(0x1B, 0x1C, 0x16));      // #1B1C16
    pal.setColor(QPalette::Base, QColor(0xFF, 0xFF, 0xFF));            // #FFFFFF
    pal.setColor(QPalette::AlternateBase, QColor(0xF6, 0xF4, 0xE9));   // #F6F4E9
    pal.setColor(QPalette::ToolTipBase, QColor(0xFF, 0xFF, 0xFF));
    pal.setColor(QPalette::ToolTipText, QColor(0x1B, 0x1C, 0x16));
    pal.setColor(QPalette::Text, QColor(0x1B, 0x1C, 0x16));
    pal.setColor(QPalette::Button, QColor(0xF0, 0xEE, 0xE3));          // #F0EEE3
    pal.setColor(QPalette::ButtonText, QColor(0x1B, 0x1C, 0x16));      // #1B1C16
    pal.setColor(QPalette::BrightText, QColor(0xBA, 0x1A, 0x1A));      // #BA1A1A
    pal.setColor(QPalette::Link, QColor(0x00, 0x6C, 0x4F));            // #006C4F
    pal.setColor(QPalette::Highlight, QColor(0x14, 0xFD, 0xBF));       // #14FDBF
    pal.setColor(QPalette::HighlightedText, QColor(0x00, 0x21, 0x16)); // #002116
    app.setPalette(pal);
    app.setWindowIcon(appIcon());

    app.setStyleSheet(globalStyleSheet());
}
