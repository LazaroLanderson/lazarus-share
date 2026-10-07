#pragma once
#include <QColor>
#include <QFont>
#include <QString>
#include <QIcon>
#include <QPixmap>

class QApplication;
class QDialog;

class Theme {
public:
    // Color Tokens (Kinetic Minimalist Utility)
    static inline const QColor Primary{0x00, 0x00, 0x00};             // #000000
    static inline const QColor Secondary{0x0F, 0xFC, 0xBE};           // #0FFCBE (Mint cyan)
    static inline const QColor SecondaryDark{0x00, 0x6C, 0x4F};       // #006C4F
    static inline const QColor SecondaryContainer{0x14, 0xFD, 0xBF};  // #14FDBF
    static inline const QColor Tertiary{0xEC, 0xE8, 0xD4};            // #ECE8D4 (Sand beige)
    static inline const QColor TertiaryContainer{0x1D, 0x1C, 0x10};   // #1D1C10
    static inline const QColor Background{0xFC, 0xFA, 0xEF};          // #FCFAEF
    static inline const QColor SurfaceContainerLowest{0xFF, 0xFF, 0xFF}; // #FFFFFF
    static inline const QColor SurfaceContainerLow{0xF6, 0xF4, 0xE9}; // #F6F4E9
    static inline const QColor SurfaceContainer{0xF0, 0xEE, 0xE3};    // #F0EEE3
    static inline const QColor SurfaceContainerHigh{0xEA, 0xE8, 0xDE}; // #EAE8DE
    static inline const QColor SurfaceContainerHighest{0xE4, 0xE3, 0xD8}; // #E4E3D8
    static inline const QColor OnSurface{0x1B, 0x1C, 0x16};           // #1B1C16
    static inline const QColor OnSurfaceVariant{0x4C, 0x45, 0x46};    // #4C4546
    static inline const QColor Outline{0x7E, 0x75, 0x76};             // #7E7576
    static inline const QColor OutlineVariant{0xCF, 0xC4, 0xC5};      // #CFC4C5
    static inline const QColor Error{0xBA, 0x1A, 0x1A};               // #BA1A1A
    static inline const QColor ErrorContainer{0xFF, 0xDA, 0xD6};      // #FFDAD6

    // Typography
    static void initFonts();
    static QFont headlineFont(int pointSize = 13, int weight = QFont::Bold);
    static QFont bodyFont(int pointSize = 10, int weight = QFont::Normal);
    static QFont codeFont(int pointSize = 9, int weight = QFont::DemiBold);

    // Styling
    static void apply(QApplication &app);
    static QString globalStyleSheet();
    static void setupDialog(QDialog *dialog);

    // Icon helpers
    static QIcon icon(const QString &name, const QColor &color = OnSurface, int size = 20);
    static QPixmap logoPixmap(int height = 32);
    static QIcon appIcon();

    // Cute Animal Avatars
    static QString avatarName(int index);
    static QPixmap avatarPixmap(int index, int size = 24);
    static QIcon avatarIcon(int index, int size = 24);
};
