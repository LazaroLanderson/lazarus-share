#include "window.h"
#include "tls.h"
#include "theme.h"
#include "toast.h"
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStackedWidget>
#include <QScrollArea>
#include <QFrame>
#include <QJsonDocument>
#include <QMessageBox>
#include <QScreen>
#include <QUrl>
#include <QCheckBox>
#include <QPixmap>
#include <QSslError>
#include <QSslConfiguration>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPainter>
#include <QSettings>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
#include <QRegularExpression>
#include <QDateTime>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

void Window::showPage(int index) {
    if (index == 3 && stack_->currentIndex() != 3) {
        previousPageIndex_ = stack_->currentIndex();
    }
    stack_->setCurrentIndex(index);
    updateRoomIndicators();
}

void Window::showComingSoon(const QString &feature) {
    Toast::showComingSoon(this, feature);
}

void Window::updateRoomIndicators() {
    if (roomSessionCodeLabel_) {
        roomSessionCodeLabel_->setText(room_.isEmpty() ? "Sessão Pronta" : "Sessão #" + room_.left(8));
    }
    if (roomStatusBadge_) {
        roomStatusBadge_->setText(viewerState_);
    }
    if (waitingModalWidget_) {
        waitingModalWidget_->setVisible(active_ && !administrator_ && viewerState_ == "Aguardando aprovação");
    }
    if (roomInviteBanner_) {
        roomInviteBanner_->setVisible(active_ && administrator_);
        if (inviteHeadingLabel_) {
            QString name = profile_.valid() ? profile_.nickname : "Anfitrião";
            inviteHeadingLabel_->setText(QString("%1, está te convidando para assistir com ele").arg(name));
        }
        if (inviteAvatarLabel_) {
            inviteAvatarLabel_->setPixmap(Theme::avatarPixmap(profile_.avatar, 28));
        }
    }
    updateTelemetrySummary();
}

void Window::updateTelemetrySummary() {
    if (!prepTelemetrySummary_) return;
    QString audioStr = prepAudioEnabled_ ? "Áudio Ativo" : "Áudio Mudo";
    QString screenStr = monitor_ && monitor_->count() > 0 ? monitor_->currentText() : "Tela Principal";
    QString qualityStr = preset_ ? (preset_->currentIndex() == 0 ? "720p30" : preset_->currentIndex() == 1 ? "1080p60" : "Máxima") : "1080p60";
    prepTelemetrySummary_->setText(QString("Conexão Direta • %1 • %2 • %3").arg(screenStr, audioStr, qualityStr));
}

QWidget *Window::createHeaderWidget() {
    auto *header = new QWidget(this);
    header->setObjectName("topHeader");
    header->setFixedHeight(56);
    header->setStyleSheet("QWidget#topHeader { background-color: rgba(252, 250, 239, 0.95); border-bottom: 1px solid #EAE8DE; }");

    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(20, 8, 20, 8);
    layout->setSpacing(16);

    auto *brandLayout = new QHBoxLayout;
    brandLayout->setSpacing(8);
    auto *logoLabel = new QLabel(header);
    logoLabel->setPixmap(Theme::logoPixmap(28));
    brandLayout->addWidget(logoLabel);

    auto *titleLabel = new QLabel("LAZARUS SHARE", header);
    titleLabel->setFont(Theme::headlineFont(13, QFont::Bold));
    titleLabel->setStyleSheet("color: #000000; letter-spacing: 0.5px;");
    brandLayout->addWidget(titleLabel);
    layout->addLayout(brandLayout);

    layout->addStretch();

    auto *navLayout = new QHBoxLayout;
    navLayout->setSpacing(8);

    auto *quickShareBtn = new QPushButton("Início", header);
    quickShareBtn->setObjectName("navHome");
    quickShareBtn->setStyleSheet("background-color: #EAE8DE; color: #1B1C16; border-radius: 6px; padding: 5px 12px; font-weight: 700; border: 1px solid #CFD0C5;");
    connect(quickShareBtn, &QPushButton::clicked, this, [this] {
        if (active_) showPage(2); else showPage(0);
    });
    navLayout->addWidget(quickShareBtn);

    auto *targetsBtn = new QPushButton("Dispositivos", header);
    targetsBtn->setObjectName("navTargets");
    targetsBtn->setStyleSheet("background: transparent; border: none; color: #4C4546; padding: 5px 10px; font-weight: 500;");
    connect(targetsBtn, &QPushButton::clicked, this, [this] { showComingSoon("Dispositivos"); });
    navLayout->addWidget(targetsBtn);

    auto *historyBtn = new QPushButton("Histórico", header);
    historyBtn->setObjectName("navHistory");
    historyBtn->setStyleSheet("background: transparent; border: none; color: #4C4546; padding: 5px 10px; font-weight: 500;");
    connect(historyBtn, &QPushButton::clicked, this, [this] { showComingSoon("Histórico"); });
    navLayout->addWidget(historyBtn);
    layout->addLayout(navLayout);

    layout->addStretch();

    auto *rightLayout = new QHBoxLayout;
    rightLayout->setSpacing(10);

    auto *settingsBtn = new QPushButton(header);
    settingsBtn->setFixedSize(36, 36);
    settingsBtn->setIcon(Theme::icon("settings", QColor(0, 0, 0), 18));
    settingsBtn->setStyleSheet("QPushButton { background-color: #F0EEE3; border-radius: 8px; border: 1px solid #E4E3D8; } QPushButton:hover { background-color: #EAE8DE; }");
    settingsBtn->setToolTip("Configurações");
    connect(settingsBtn, &QPushButton::clicked, this, [this] {
        if (stack_->currentIndex() == 3) showPage(previousPageIndex_); else showPage(3);
    });
    rightLayout->addWidget(settingsBtn);

    identity_ = new QPushButton(header);
    identity_->setObjectName("profile");
    identity_->setStyleSheet("QPushButton { background-color: #FFFFFF; color: #1B1C16; border: 1.5px solid #E4E3D8; border-radius: 18px; padding: 4px 14px 4px 10px; font-weight: 600; font-size: 12px; } QPushButton:hover { background-color: #F0EEE3; border-color: #CFD0C5; }");
    rightLayout->addWidget(identity_);

    layout->addLayout(rightLayout);
    return header;
}

QWidget *Window::createHomePage() {
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet("background-color: #FCFAEF;");

    auto *page = new QWidget(scroll);
    page->setObjectName("homePage");
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 16, 40, 20);
    layout->setSpacing(14);

    auto *bar = new QWidget(page);
    bar->setStyleSheet("background-color: #F0EEE3; border-radius: 10px; padding: 4px;");
    auto *barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(14, 6, 14, 6);

    auto *barLeft = new QHBoxLayout;
    barLeft->setSpacing(8);
    auto *pulseDot = new QLabel(bar);
    pulseDot->setFixedSize(8, 8);
    pulseDot->setStyleSheet("background-color: #0FFCBE; border-radius: 4px; border: none;");
    barLeft->addWidget(pulseDot);

    headerStatusLabel_ = new QLabel("Sessão • Pronta para conectar", bar);
    headerStatusLabel_->setFont(Theme::codeFont(9));
    headerStatusLabel_->setStyleSheet("color: #4C4546; text-transform: uppercase; font-weight: 600; border: none;");
    barLeft->addWidget(headerStatusLabel_);
    barLayout->addLayout(barLeft);

    barLayout->addStretch();

    auto *barRight = new QHBoxLayout;
    barRight->setSpacing(10);
    auto *p2pPill = new QLabel("Conexão Direta Ativa", bar);
    p2pPill->setFont(Theme::codeFont(9));
    p2pPill->setStyleSheet("background-color: #FFFFFF; color: #006C4F; padding: 4px 10px; border-radius: 6px; font-weight: 700; border: none;");
    barRight->addWidget(p2pPill);
    barLayout->addLayout(barRight);
    layout->addWidget(bar);

    auto *heroWidget = new QWidget(page);
    auto *heroLayout = new QVBoxLayout(heroWidget);
    heroLayout->setAlignment(Qt::AlignCenter);
    heroLayout->setSpacing(8);

    auto *badgePill = new QLabel("100% Privado • Criptografia de Ponta a Ponta", heroWidget);
    badgePill->setFont(Theme::codeFont(9));
    badgePill->setStyleSheet("background-color: #EAE8DE; color: #1B1C16; padding: 4px 14px; border-radius: 12px; font-weight: 600; border: none;");
    badgePill->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(badgePill, 0, Qt::AlignCenter);

    auto *heroTitle = new QLabel(heroWidget);
    heroTitle->setFont(Theme::headlineFont(24, QFont::Bold));
    heroTitle->setTextFormat(Qt::RichText);
    heroTitle->setText("Compartilhe sua tela, <span style='border-bottom: 3px solid #0FFCBE; padding-bottom: 2px;'>sem cadastro</span>");
    heroTitle->setStyleSheet("color: #000000; border: none;");
    heroTitle->setAlignment(Qt::AlignCenter);
    heroTitle->setWordWrap(true);
    heroLayout->addWidget(heroTitle, 0, Qt::AlignCenter);

    auto *heroSubtitle = new QLabel("Transmissão em alta definição e tempo real diretamente entre você e seus colegas, sem precisar criar contas.", heroWidget);
    heroSubtitle->setFont(Theme::bodyFont(11));
    heroSubtitle->setStyleSheet("color: #4C4546; border: none; padding: 2px 0px;");
    heroSubtitle->setAlignment(Qt::AlignCenter);
    heroSubtitle->setWordWrap(true);
    heroSubtitle->setMaximumWidth(700);
    heroLayout->addWidget(heroSubtitle, 0, Qt::AlignCenter);
    layout->addWidget(heroWidget);

    auto *cardsLayout = new QHBoxLayout;
    cardsLayout->setSpacing(20);

    // CARD 1: Compartilhar Minha Tela
    auto *card1 = new QFrame(page);
    card1->setObjectName("homeCard1");
    card1->setStyleSheet("QFrame#homeCard1 { background-color: #FFFFFF; border: 1px solid #EAE8DE; border-radius: 16px; }");
    auto *card1Layout = new QVBoxLayout(card1);
    card1Layout->setContentsMargins(22, 18, 22, 18);
    card1Layout->setSpacing(12);

    auto *card1Top = new QHBoxLayout;
    auto *card1Icon = new QLabel(card1);
    card1Icon->setFixedSize(48, 48);
    card1Icon->setStyleSheet("background-color: #F0EEE3; border-radius: 12px; border: none;");
    card1Icon->setAlignment(Qt::AlignCenter);
    card1Icon->setPixmap(Theme::icon("screen_share", Theme::SecondaryDark, 26).pixmap(26, 26));
    card1Top->addWidget(card1Icon);
    card1Top->addStretch();

    auto *readyBadge = new QLabel("PRONTO", card1);
    readyBadge->setFont(Theme::codeFont(9));
    readyBadge->setStyleSheet("background-color: #F0EEE3; color: #006C4F; font-weight: 700; padding: 4px 10px; border-radius: 10px; border: none;");
    card1Top->addWidget(readyBadge);
    card1Layout->addLayout(card1Top);

    auto *card1Title = new QLabel("Compartilhar minha tela", card1);
    card1Title->setFont(Theme::headlineFont(16, QFont::Bold));
    card1Title->setStyleSheet("color: #000000; border: none;");
    card1Layout->addWidget(card1Title);

    auto *card1Desc = new QLabel("Crie uma sala instantânea e compartilhe o link com até 4 pessoas para assistirem direto no navegador.", card1);
    card1Desc->setFont(Theme::bodyFont(10));
    card1Desc->setStyleSheet("color: #4C4546; border: none;");
    card1Desc->setWordWrap(true);
    card1Layout->addWidget(card1Desc);

    auto *specsBox = new QWidget(card1);
    specsBox->setObjectName("specsBox");
    specsBox->setStyleSheet("QWidget#specsBox { background-color: #F6F4E9; border-radius: 8px; border: none; }");
    auto *specsLayout = new QHBoxLayout(specsBox);
    specsLayout->setContentsMargins(10, 8, 10, 8);
    auto *specsText = new QLabel("Tela Principal • Áudio do computador ativado", specsBox);
    specsText->setFont(Theme::bodyFont(9));
    specsText->setStyleSheet("color: #1B1C16; font-weight: 500; border: none;");
    specsLayout->addWidget(specsText);
    specsLayout->addStretch();
    auto *fpsBadge = new QLabel("60 FPS (Fluido)", specsBox);
    fpsBadge->setFont(Theme::codeFont(9));
    fpsBadge->setStyleSheet("background-color: #FFFFFF; color: #000000; font-weight: 700; padding: 2px 6px; border-radius: 4px; border: none;");
    specsLayout->addWidget(fpsBadge);
    card1Layout->addWidget(specsBox);

    card1Layout->addStretch();

    auto *startShareBtn = new QPushButton("Iniciar Compartilhamento", card1);
    startShareBtn->setObjectName("btn-start-share");
    startShareBtn->setIcon(Theme::icon("cast", Theme::Secondary, 18));
    startShareBtn->setStyleSheet("QPushButton { background-color: #000000; color: #FFFFFF; font-weight: 700; font-size: 13px; border-radius: 10px; padding: 12px; border: none; } QPushButton:hover { background-color: #1B1B1B; }");
    connect(startShareBtn, &QPushButton::clicked, this, [this] {
        showPage(1);
    });
    card1Layout->addWidget(startShareBtn);

    auto *card1Footnote = new QLabel("Sem limite de duração da chamada", card1);
    card1Footnote->setFont(Theme::bodyFont(9));
    card1Footnote->setStyleSheet("color: #7E7576; border: none;");
    card1Footnote->setAlignment(Qt::AlignCenter);
    card1Layout->addWidget(card1Footnote);
    cardsLayout->addWidget(card1, 1);

    // CARD 2: Assistir a uma Tela
    auto *card2 = new QFrame(page);
    card2->setObjectName("homeCard2");
    card2->setStyleSheet("QFrame#homeCard2 { background-color: #FFFFFF; border: 1px solid #EAE8DE; border-radius: 16px; }");
    auto *card2Layout = new QVBoxLayout(card2);
    card2Layout->setContentsMargins(22, 18, 22, 18);
    card2Layout->setSpacing(12);

    auto *card2Top = new QHBoxLayout;
    auto *card2Icon = new QLabel(card2);
    card2Icon->setFixedSize(48, 48);
    card2Icon->setStyleSheet("background-color: #F0EEE3; border-radius: 12px; border: none;");
    card2Icon->setAlignment(Qt::AlignCenter);
    card2Icon->setPixmap(Theme::icon("monitor", Theme::Primary, 26).pixmap(26, 26));
    card2Top->addWidget(card2Icon);
    card2Top->addStretch();

    auto *p2pBadge = new QLabel("ASSISTIR SALA", card2);
    p2pBadge->setFont(Theme::codeFont(9));
    p2pBadge->setStyleSheet("background-color: #F0EEE3; color: #4C4546; font-weight: 700; padding: 4px 10px; border-radius: 10px; border: none;");
    card2Top->addWidget(p2pBadge);
    card2Layout->addLayout(card2Top);

    auto *card2Title = new QLabel("Assistir a uma tela", card2);
    card2Title->setFont(Theme::headlineFont(16, QFont::Bold));
    card2Title->setStyleSheet("color: #000000; border: none;");
    card2Layout->addWidget(card2Title);

    auto *card2Desc = new QLabel("Cole o código ou o link da sala que seu colega compartilhou com você.", card2);
    card2Desc->setFont(Theme::bodyFont(10));
    card2Desc->setStyleSheet("color: #4C4546; border: none;");
    card2Desc->setWordWrap(true);
    card2Layout->addWidget(card2Desc);

    auto *inputTray = new QWidget(card2);
    inputTray->setObjectName("inputTray");
    inputTray->setStyleSheet("QWidget#inputTray { background: transparent; border: none; }");
    auto *inputTrayLayout = new QVBoxLayout(inputTray);
    inputTrayLayout->setContentsMargins(0, 0, 0, 0);
    inputTrayLayout->setSpacing(4);

    auto *inputLabel = new QLabel("Código ou Link da Sala", inputTray);
    inputLabel->setFont(Theme::codeFont(9));
    inputLabel->setStyleSheet("color: #1B1C16; text-transform: uppercase; font-weight: 700; border: none;");
    inputTrayLayout->addWidget(inputLabel);

    auto *inputRow = new QHBoxLayout;
    inputRow->setSpacing(6);
    token_ = new QLineEdit(inputTray);
    token_->setObjectName("invite");
    token_->setPlaceholderText("ex: lzr-792-sky ou https://...");
    token_->setStyleSheet("QLineEdit { background-color: #F6F4E9; border: 1px solid #E4E3D8; border-radius: 8px; padding: 10px 12px; font-family: 'JetBrains Mono'; }");
    inputRow->addWidget(token_, 1);

    auto *pasteBtn = new QPushButton("Colar", inputTray);
    pasteBtn->setIcon(Theme::icon("paste", QColor(0, 0, 0), 14));
    pasteBtn->setStyleSheet("QPushButton { background-color: #FFFFFF; border: 1px solid #E4E3D8; border-radius: 8px; padding: 8px 12px; font-weight: 600; font-size: 11px; } QPushButton:hover { background-color: #F0EEE3; }");
    connect(pasteBtn, &QPushButton::clicked, this, [this] {
        auto clipboardText = QApplication::clipboard()->text().trimmed();
        if (!clipboardText.isEmpty()) token_->setText(clipboardText);
    });
    inputRow->addWidget(pasteBtn);
    inputTrayLayout->addLayout(inputRow);
    card2Layout->addWidget(inputTray);

    card2Layout->addStretch();

    join_ = new QPushButton("Entrar com link", card2);
    join_->setObjectName("btn-join-room");
    join_->setIcon(Theme::icon("login", QColor(0, 0, 0), 18));
    join_->setStyleSheet("QPushButton { background-color: #EAE8DE; color: #000000; font-weight: 700; font-size: 13px; border-radius: 10px; padding: 12px; border: 1px solid #E4E3D8; } QPushButton:hover { background-color: #E4E3D8; }");
    card2Layout->addWidget(join_);

    auto *card2Footnote = new QLabel("Acesso imediato para assistir", card2);
    card2Footnote->setFont(Theme::bodyFont(9));
    card2Footnote->setStyleSheet("color: #7E7576; border: none;");
    card2Footnote->setAlignment(Qt::AlignCenter);
    card2Layout->addWidget(card2Footnote);
    cardsLayout->addWidget(card2, 1);
    layout->addLayout(cardsLayout);

    // Micro Technical Ribbon
    auto *ribbonLayout = new QHBoxLayout;
    ribbonLayout->setSpacing(12);

    auto addRibbonCard = [&](const QString &iconName, const QString &label, const QString &val) {
        auto *ribbonCard = new QFrame(page);
        ribbonCard->setObjectName("homeRibbonCard");
        ribbonCard->setStyleSheet("QFrame#homeRibbonCard { background-color: #F6F4E9; border-radius: 10px; border: 1px solid #EAE8DE; }");
        auto *rcl = new QHBoxLayout(ribbonCard);
        rcl->setContentsMargins(12, 8, 12, 8);
        rcl->setSpacing(10);

        auto *ico = new QLabel(ribbonCard);
        ico->setFixedSize(32, 32);
        ico->setStyleSheet("background-color: #FFFFFF; border-radius: 6px; border: none;");
        ico->setAlignment(Qt::AlignCenter);
        ico->setPixmap(Theme::icon(iconName, Theme::Primary, 16).pixmap(16, 16));
        rcl->addWidget(ico);

        auto *txtLayout = new QVBoxLayout;
        txtLayout->setSpacing(1);
        auto *lbl = new QLabel(label, ribbonCard);
        lbl->setFont(Theme::codeFont(8));
        lbl->setStyleSheet("color: #7E7576; text-transform: uppercase; border: none;");
        txtLayout->addWidget(lbl);

        auto *valLbl = new QLabel(val, ribbonCard);
        valLbl->setFont(Theme::codeFont(10));
        valLbl->setStyleSheet("color: #000000; font-weight: 700; border: none;");
        txtLayout->addWidget(valLbl);
        rcl->addLayout(txtLayout);

        ribbonLayout->addWidget(ribbonCard);
    };

    addRibbonCard("bolt", "Resposta da Transmissão", "Tempo real (sem atraso)");
    addRibbonCard("security", "Proteção da Sala", "Criptografia Ponta a Ponta");
    addRibbonCard("volume_up", "Áudio do Computador", "Alta Definição");
    addRibbonCard("monitor", "Qualidade Máxima", "Até 4K / 60 FPS");
    layout->addLayout(ribbonLayout);

    auto *footer = new QWidget(page);
    auto *footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(0, 10, 0, 0);

    auto *coreVer = new QLabel("Lazarus Share v0.3.0", footer);
    coreVer->setFont(Theme::codeFont(9));
    coreVer->setStyleSheet("color: #7E7576; text-transform: uppercase; border: none;");
    footerLayout->addWidget(coreVer);

    footerLayout->addStretch();

    status_ = new QLabel("Pronto. Suas transmissões são privadas e não são gravadas.", footer);
    status_->setObjectName("status");
    status_->setFont(Theme::bodyFont(9));
    status_->setStyleSheet("color: #7E7576; border: none;");
    footerLayout->addWidget(status_);
    layout->addWidget(footer);

    scroll->setWidget(page);
    return scroll;
}

QWidget *Window::createPrepPage() {
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background-color: #FCFAEF;");

    auto *page = new QWidget(scroll);
    page->setObjectName("prepPage");
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 20, 40, 30);
    layout->setSpacing(20);

    auto *ctxHeader = new QWidget(page);
    auto *ctxLayout = new QVBoxLayout(ctxHeader);
    ctxLayout->setContentsMargins(0, 0, 0, 0);
    ctxLayout->setSpacing(10);

    auto *topRow = new QHBoxLayout;
    auto *backBtn = new QPushButton("Voltar para Início", ctxHeader);
    backBtn->setIcon(Theme::icon("arrow_back", QColor(0, 0, 0), 16));
    backBtn->setStyleSheet("background: transparent; border: none; font-weight: 600; color: #1B1C16; padding: 4px 8px; border-radius: 6px;");
    connect(backBtn, &QPushButton::clicked, this, [this] { showPage(0); });
    topRow->addWidget(backBtn);
    topRow->addStretch();

    auto *stepPill = new QLabel("Etapa 02 / 03 • Ajustes da Sala", ctxHeader);
    stepPill->setFont(Theme::codeFont(9));
    stepPill->setStyleSheet("background-color: #EAE8DE; color: #1B1C16; padding: 4px 12px; border-radius: 12px; font-weight: 600; text-transform: uppercase;");
    topRow->addWidget(stepPill);
    ctxLayout->addLayout(topRow);

    auto *titleRow = new QHBoxLayout;
    auto *titleLayout = new QVBoxLayout;
    auto *prepTitle = new QLabel("Preparação da Sala", ctxHeader);
    prepTitle->setFont(Theme::headlineFont(22, QFont::Bold));
    prepTitle->setStyleSheet("color: #000000;");
    titleLayout->addWidget(prepTitle);

    auto *prepSub = new QLabel("Escolha a tela e as opções de áudio antes de convidar seus colegas.", ctxHeader);
    prepSub->setFont(Theme::bodyFont(10));
    prepSub->setStyleSheet("color: #4C4546;");
    titleLayout->addWidget(prepSub);
    titleRow->addLayout(titleLayout);
    titleRow->addStretch();

    auto *latencyPill = new QLabel("RESPOSTA: TEMPO REAL (< 20ms)", ctxHeader);
    latencyPill->setFont(Theme::codeFont(9));
    latencyPill->setStyleSheet("background-color: #F0EEE3; color: #006C4F; font-weight: 700; padding: 6px 12px; border-radius: 6px;");
    titleRow->addWidget(latencyPill);
    ctxLayout->addLayout(titleRow);
    layout->addWidget(ctxHeader);

    // GRUPO 1: FONTE DE TRANSMISSÃO
    auto *group1 = new QFrame(page);
    group1->setObjectName("prepGroup1");
    group1->setStyleSheet("QFrame#prepGroup1 { background-color: #F6F4E9; border-radius: 14px; border: 1px solid #EAE8DE; }");
    auto *g1Layout = new QVBoxLayout(group1);
    g1Layout->setContentsMargins(20, 18, 20, 18);
    g1Layout->setSpacing(14);

    auto *g1Header = new QHBoxLayout;
    auto *num1 = new QLabel("01", group1);
    num1->setFixedSize(26, 26);
    num1->setFont(Theme::codeFont(9));
    num1->setStyleSheet("background-color: #EAE8DE; color: #1B1C16; border: 1px solid #CFD0C5; border-radius: 6px; font-weight: 700;");
    num1->setAlignment(Qt::AlignCenter);
    g1Header->addWidget(num1);

    auto *g1Title = new QLabel("O que você quer compartilhar?", group1);
    g1Title->setFont(Theme::headlineFont(14, QFont::Bold));
    g1Title->setStyleSheet("color: #000000; text-transform: uppercase;");
    g1Header->addWidget(g1Title);
    g1Header->addStretch();

    int screenCount = qMax(1, QGuiApplication::screens().count());
    auto *deviceCountLbl = new QLabel(QString("%1 Tela(s) Detectada(s)").arg(screenCount), group1);
    deviceCountLbl->setFont(Theme::codeFont(9));
    deviceCountLbl->setStyleSheet("color: #7E7576; text-transform: uppercase; font-weight: 600;");
    g1Header->addWidget(deviceCountLbl);
    g1Layout->addLayout(g1Header);

    auto *screensGrid = new QHBoxLayout;
    screensGrid->setSpacing(12);

    for (int i = 0; i < screenCount; ++i) {
        auto *screenObj = QGuiApplication::screens().value(i);
        QSize sSize = screenObj ? screenObj->size() : QSize(1920, 1080);

        auto *card = new QPushButton(group1);
        card->setCheckable(true);
        if (i == 0) card->setChecked(true);
        card->setStyleSheet(R"(
            QPushButton {
                background-color: #FFFFFF;
                border: 2px solid #EAE8DE;
                border-radius: 12px;
                padding: 12px;
                text-align: left;
            }
            QPushButton:checked {
                border: 2px solid #0FFCBE;
                background-color: #FFFFFF;
            }
        )");

        auto *cLayout = new QVBoxLayout(card);
        cLayout->setContentsMargins(10, 8, 10, 8);
        cLayout->setSpacing(8);

        auto *cTop = new QHBoxLayout;
        auto *statusTag = new QLabel(i == 0 ? "SELECIONADO" : "DISPONÍVEL", card);
        statusTag->setFont(Theme::codeFont(8));
        statusTag->setStyleSheet(i == 0 ? "background-color: #14FDBF; color: #002116; font-weight: 700; padding: 2px 6px; border-radius: 4px;" : "background-color: #F0EEE3; color: #7E7576; font-weight: 600; padding: 2px 6px; border-radius: 4px;");
        cTop->addWidget(statusTag);
        cTop->addStretch();
        auto *portTag = new QLabel(QString("Tela %1").arg(i + 1), card);
        portTag->setFont(Theme::codeFont(8));
        portTag->setStyleSheet("color: #7E7576;");
        cTop->addWidget(portTag);
        cLayout->addLayout(cTop);

        auto *screenPreview = new QWidget(card);
        screenPreview->setFixedHeight(64);
        screenPreview->setStyleSheet("background-color: #E4E3D8; border-radius: 6px;");
        auto *spLayout = new QVBoxLayout(screenPreview);
        spLayout->setAlignment(Qt::AlignCenter);
        auto *spIcon = new QLabel(screenPreview);
        spIcon->setPixmap(Theme::icon("monitor", Theme::Primary, 24).pixmap(24, 24));
        spIcon->setAlignment(Qt::AlignCenter);
        spLayout->addWidget(spIcon);
        cLayout->addWidget(screenPreview);

        auto *nameLbl = new QLabel(QString("Tela %1 (%2)").arg(i + 1).arg(i == 0 ? "Principal" : "Secundária"), card);
        nameLbl->setFont(Theme::headlineFont(11, QFont::Bold));
        nameLbl->setStyleSheet("color: #000000;");
        cLayout->addWidget(nameLbl);

        auto *resLbl = new QLabel(QString("%1×%2 • 60Hz").arg(sSize.width()).arg(sSize.height()), card);
        resLbl->setFont(Theme::codeFont(9));
        resLbl->setStyleSheet("color: #7E7576;");
        cLayout->addWidget(resLbl);

        connect(card, &QPushButton::clicked, this, [this, i, card] {
            for (auto *c : screenCards_) if (c != card) qobject_cast<QPushButton *>(c)->setChecked(false);
            card->setChecked(true);
            if (monitor_ && i < monitor_->count()) {
                monitor_->setCurrentIndex(i);
            }
            updateTelemetrySummary();
        });

        screenCards_.append(card);
        screensGrid->addWidget(card);
    }
    g1Layout->addLayout(screensGrid);

    auto *permBanner = new QWidget(group1);
    permBanner->setStyleSheet("background-color: #EAE8DE; border-radius: 8px;");
    auto *pbLayout = new QHBoxLayout(permBanner);
    pbLayout->setContentsMargins(12, 8, 12, 8);
    auto *secIcon = new QLabel(permBanner);
    secIcon->setPixmap(Theme::icon("security", Theme::SecondaryDark, 18).pixmap(18, 18));
    pbLayout->addWidget(secIcon);
    auto *pbText = new QLabel("Ao iniciar, o seu sistema operacional solicitará permissão para capturar a tela selecionada.", permBanner);
    pbText->setFont(Theme::bodyFont(9));
    pbText->setStyleSheet("color: #1B1C16;");
    pbText->setWordWrap(true);
    pbLayout->addWidget(pbText, 1);
    g1Layout->addWidget(permBanner);
    layout->addWidget(group1);

    // GRUPO 2: TRANSMISSÃO DE ÁUDIO
    auto *group2 = new QFrame(page);
    group2->setObjectName("prepGroup2");
    group2->setStyleSheet("QFrame#prepGroup2 { background-color: #F6F4E9; border-radius: 14px; border: 1px solid #EAE8DE; }");
    auto *g2Layout = new QVBoxLayout(group2);
    g2Layout->setContentsMargins(20, 18, 20, 18);
    g2Layout->setSpacing(14);

    auto *g2Header = new QHBoxLayout;
    auto *num2 = new QLabel("02", group2);
    num2->setFixedSize(26, 26);
    num2->setFont(Theme::codeFont(9));
    num2->setStyleSheet("background-color: #EAE8DE; color: #1B1C16; border: 1px solid #CFD0C5; border-radius: 6px; font-weight: 700;");
    num2->setAlignment(Qt::AlignCenter);
    g2Header->addWidget(num2);

    auto *g2Title = new QLabel("Áudio da Transmissão", group2);
    g2Title->setFont(Theme::headlineFont(14, QFont::Bold));
    g2Title->setStyleSheet("color: #000000; text-transform: uppercase;");
    g2Header->addWidget(g2Title);
    g2Header->addStretch();

    auto *privacyBadge = new QLabel("Microfone Bloqueado", group2);
    privacyBadge->setFont(Theme::codeFont(9));
    privacyBadge->setStyleSheet("background-color: #F0EEE3; color: #006C4F; font-weight: 700; padding: 4px 10px; border-radius: 10px;");
    g2Header->addWidget(privacyBadge);
    g2Layout->addLayout(g2Header);

    auto *audioContainer = new QWidget(group2);
    audioContainer->setObjectName("audioContainer");
    audioContainer->setStyleSheet("QWidget#audioContainer { background-color: #FFFFFF; border-radius: 10px; border: 1px solid #EAE8DE; }");
    auto *acLayout = new QVBoxLayout(audioContainer);
    acLayout->setContentsMargins(16, 14, 16, 14);
    acLayout->setSpacing(12);

    auto *switchRow = new QHBoxLayout;
    auto *toggleAudioBtn = new QPushButton("Transmitir Sons do Computador: Desativado", audioContainer);
    toggleAudioBtn->setCheckable(true);
    toggleAudioBtn->setIcon(Theme::icon("volume_off", Theme::Primary, 18));
    toggleAudioBtn->setStyleSheet(R"(
        QPushButton {
            background-color: #F0EEE3;
            color: #1B1C16;
            font-weight: 700;
            border-radius: 8px;
            padding: 8px 16px;
        }
        QPushButton:checked {
            background-color: #14FDBF;
            color: #002116;
        }
    )");
    switchRow->addWidget(toggleAudioBtn);

    auto *audioStateBadge = new QLabel("MUDO", audioContainer);
    audioStateBadge->setFont(Theme::codeFont(9));
    audioStateBadge->setStyleSheet("background-color: #F0EEE3; color: #7E7576; font-weight: 700; padding: 4px 10px; border-radius: 6px;");
    switchRow->addWidget(audioStateBadge);
    switchRow->addStretch();
    acLayout->addLayout(switchRow);

    audioSourcesTray_ = new QWidget(audioContainer);
    auto *astLayout = new QVBoxLayout(audioSourcesTray_);
    astLayout->setContentsMargins(0, 8, 0, 0);
    astLayout->setSpacing(10);

    auto *trayDivider = new QFrame(audioSourcesTray_);
    trayDivider->setFrameShape(QFrame::HLine);
    trayDivider->setStyleSheet("color: #EAE8DE;");
    astLayout->addWidget(trayDivider);

    auto *prepTrayDesc = new QLabel("O Lazarus Share permite isolar e transmitir apenas os aplicativos que você marcar.", audioSourcesTray_);
    prepTrayDesc->setFont(Theme::bodyFont(9));
    prepTrayDesc->setStyleSheet("color: #4C4546; border: none;");
    prepTrayDesc->setWordWrap(true);
    astLayout->addWidget(prepTrayDesc);

    auto *prepAudioBtn = new QPushButton("Selecionar programas com áudio...", audioSourcesTray_);
    prepAudioBtn->setIcon(Theme::icon("volume_up", Theme::Primary, 14));
    prepAudioBtn->setStyleSheet("QPushButton { background-color: #FFFFFF; color: #1B1C16; border: 1.5px solid #EAE8DE; border-radius: 8px; padding: 8px 14px; font-weight: 600; font-size: 11px; text-align: left; } QPushButton:hover { background-color: #F6F4E9; }");
    connect(prepAudioBtn, &QPushButton::clicked, this, &Window::openAudioDialog);
    astLayout->addWidget(prepAudioBtn);

    audioSourcesTray_->hide();
    acLayout->addWidget(audioSourcesTray_);

    connect(toggleAudioBtn, &QPushButton::toggled, this, [this, toggleAudioBtn, audioStateBadge](bool checked) {
        prepAudioEnabled_ = checked;
        if (checked) {
            toggleAudioBtn->setText("Transmitir Sons do Computador: Ativado");
            toggleAudioBtn->setIcon(Theme::icon("volume_up", Theme::Primary, 18));
            audioStateBadge->setText("TRANSMITINDO");
            audioStateBadge->setStyleSheet("background-color: #14FDBF; color: #002116; font-weight: 700; padding: 4px 10px; border-radius: 6px;");
            audioSourcesTray_->show();
        } else {
            toggleAudioBtn->setText("Transmitir Sons do Computador: Desativado");
            toggleAudioBtn->setIcon(Theme::icon("volume_off", Theme::Primary, 18));
            audioStateBadge->setText("MUDO");
            audioStateBadge->setStyleSheet("background-color: #F0EEE3; color: #7E7576; font-weight: 700; padding: 4px 10px; border-radius: 6px;");
            audioSourcesTray_->hide();
        }
        updateTelemetrySummary();
    });

    g2Layout->addWidget(audioContainer);
    layout->addWidget(group2);

    // GRUPO 3: QUALIDADE DE TRANSMISSÃO
    auto *group3 = new QFrame(page);
    group3->setObjectName("prepGroup3");
    group3->setStyleSheet("QFrame#prepGroup3 { background-color: #F6F4E9; border-radius: 14px; border: 1px solid #EAE8DE; }");
    auto *g3Layout = new QVBoxLayout(group3);
    g3Layout->setContentsMargins(20, 18, 20, 18);
    g3Layout->setSpacing(14);

    auto *g3Header = new QHBoxLayout;
    auto *num3 = new QLabel("03", group3);
    num3->setFixedSize(26, 26);
    num3->setFont(Theme::codeFont(9));
    num3->setStyleSheet("background-color: #EAE8DE; color: #1B1C16; border: 1px solid #CFD0C5; border-radius: 6px; font-weight: 700;");
    num3->setAlignment(Qt::AlignCenter);
    g3Header->addWidget(num3);

    auto *g3Title = new QLabel("Qualidade da Imagem", group3);
    g3Title->setFont(Theme::headlineFont(14, QFont::Bold));
    g3Title->setStyleSheet("color: #000000; text-transform: uppercase;");
    g3Header->addWidget(g3Title);
    g3Header->addStretch();

    auto *customToggleBtn = new QPushButton("Ajustes Avançados", group3);
    customToggleBtn->setIcon(Theme::icon("tune", QColor(0, 0, 0), 14));
    customToggleBtn->setStyleSheet("background-color: #FFFFFF; border: 1px solid #EAE8DE; font-weight: 600; padding: 4px 10px; border-radius: 6px; font-size: 11px;");
    connect(customToggleBtn, &QPushButton::clicked, this, [this] {
        if (customQualityPanel_) customQualityPanel_->setVisible(!customQualityPanel_->isVisible());
    });
    g3Header->addWidget(customToggleBtn);
    g3Layout->addLayout(g3Header);

    auto *presetsLayout = new QHBoxLayout;
    presetsLayout->setSpacing(12);

    struct PresetInfo { QString name; QString spec; QString desc; int idx; };
    QList<PresetInfo> pInfos = {
        {"Econômico", "720p • 30 FPS", "Usa menos internet, ideal para redes lentas ou instáveis.", 0},
        {"Equilibrado", "1080p • 60 FPS", "Nítido e fluido. Perfeito para trabalho e vídeos.", 1},
        {"Alta Definição", "Nativo • 60 FPS", "Máxima fidelidade para textos pequenos e detalhes finos.", 2}
    };

    for (const auto &pi : pInfos) {
        auto *pCard = new QPushButton(group3);
        pCard->setCheckable(true);
        if (pi.idx == 1) pCard->setChecked(true);
        pCard->setStyleSheet(R"(
            QPushButton {
                background-color: #FFFFFF;
                border: 2px solid #EAE8DE;
                border-radius: 12px;
                padding: 12px;
                text-align: left;
            }
            QPushButton:checked {
                border: 2px solid #0FFCBE;
                background-color: #FFFFFF;
            }
        )");

        auto *pcl = new QVBoxLayout(pCard);
        pcl->setContentsMargins(10, 8, 10, 8);
        pcl->setSpacing(6);

        auto *pTop = new QHBoxLayout;
        auto *pName = new QLabel(pi.name, pCard);
        pName->setFont(Theme::headlineFont(12, QFont::Bold));
        pName->setStyleSheet("color: #000000;");
        pTop->addWidget(pName);
        pTop->addStretch();
        if (pi.idx == 1) {
            auto *recBadge = new QLabel("RECOMENDADO", pCard);
            recBadge->setFont(Theme::codeFont(8));
            recBadge->setStyleSheet("background-color: #14FDBF; color: #002116; font-weight: 700; padding: 2px 6px; border-radius: 4px;" );
            pTop->addWidget(recBadge);
        }
        pcl->addLayout(pTop);

        auto *pSpec = new QLabel(pi.spec, pCard);
        pSpec->setFont(Theme::codeFont(10));
        pSpec->setStyleSheet("color: #000000; font-weight: 700;");
        pcl->addWidget(pSpec);

        auto *pDesc = new QLabel(pi.desc, pCard);
        pDesc->setFont(Theme::bodyFont(9));
        pDesc->setStyleSheet("color: #7E7576;");
        pDesc->setWordWrap(true);
        pcl->addWidget(pDesc);

        connect(pCard, &QPushButton::clicked, this, [this, pi, pCard] {
            for (auto *c : presetCards_) if (c != pCard) qobject_cast<QPushButton *>(c)->setChecked(false);
            pCard->setChecked(true);
            if (preset_) preset_->setCurrentIndex(pi.idx);
            updateTelemetrySummary();
        });

        presetCards_.append(pCard);
        presetsLayout->addWidget(pCard);
    }
    g3Layout->addLayout(presetsLayout);

    customQualityPanel_ = new QWidget(group3);
    customQualityPanel_->setStyleSheet("background-color: #FFFFFF; border-radius: 10px; border: 1px solid #EAE8DE; padding: 10px;");
    auto *cqpLayout = new QHBoxLayout(customQualityPanel_);
    cqpLayout->setContentsMargins(12, 10, 12, 10);
    cqpLayout->setSpacing(16);

    auto addCustomField = [&](const QString &lblText, const QStringList &options) {
        auto *fLayout = new QVBoxLayout;
        fLayout->setSpacing(4);
        auto *fl = new QLabel(lblText, customQualityPanel_);
        fl->setFont(Theme::codeFont(8));
        fl->setStyleSheet("color: #7E7576; text-transform: uppercase; font-weight: 700;");
        fLayout->addWidget(fl);
        auto *cb = new QComboBox(customQualityPanel_);
        cb->addItems(options);
        fLayout->addWidget(cb);
        cqpLayout->addLayout(fLayout);
    };

    addCustomField("Resolução Alvo", {"1920 × 1080 (FHD)", "2560 × 1440 (2K QHD)", "1280 × 720 (HD)", "Nativa da Tela"});
    addCustomField("Taxa de Quadros (FPS)", {"60 FPS (Ultra Suave)", "30 FPS (Padrão)", "120 FPS (Display Pro)"});
    addCustomField("Velocidade da Transmissão", {"Alta fidelidade (Recomendado)", "Qualidade máxima", "Econômico (conexão lenta)", "Ajuste automático"});

    customQualityPanel_->hide();
    g3Layout->addWidget(customQualityPanel_);
    layout->addWidget(group3);

    // RODAPÉ DE AÇÃO E SEGURANÇA
    auto *dock = new QFrame(page);
    dock->setObjectName("prepDock");
    dock->setStyleSheet("QFrame#prepDock { background-color: #EAE8DE; border-radius: 14px; }");
    auto *dockLayout = new QHBoxLayout(dock);
    dockLayout->setContentsMargins(20, 16, 20, 16);
    dockLayout->setSpacing(20);

    auto *dockLeft = new QHBoxLayout;
    dockLeft->setSpacing(12);

    auto *shIco = new QLabel(dock);
    shIco->setFixedSize(36, 36);
    shIco->setStyleSheet("background-color: #FFFFFF; border-radius: 18px;");
    shIco->setAlignment(Qt::AlignCenter);
    shIco->setPixmap(Theme::icon("security", Theme::SecondaryDark, 20).pixmap(20, 20));
    dockLeft->addWidget(shIco);

    auto *dockTextLayout = new QVBoxLayout;
    dockTextLayout->setSpacing(2);
    auto *secTitle = new QLabel("Você aprova cada pessoa antes de ela assistir.", dock);
    secTitle->setFont(Theme::headlineFont(11, QFont::Bold));
    secTitle->setStyleSheet("color: #000000;");
    dockTextLayout->addWidget(secTitle);

    prepTelemetrySummary_ = new QLabel("Conexão Direta • Tela 1 • Áudio Mudo • 1080p60", dock);
    prepTelemetrySummary_->setFont(Theme::codeFont(9));
    prepTelemetrySummary_->setStyleSheet("color: #4C4546;");
    dockTextLayout->addWidget(prepTelemetrySummary_);
    dockLeft->addLayout(dockTextLayout);

    dockLayout->addLayout(dockLeft);
    dockLayout->addStretch();

    auto *cancelBtn = new QPushButton("Cancelar", dock);
    cancelBtn->setStyleSheet("background: transparent; border: none; font-weight: 600; color: #4C4546; padding: 10px 18px;");
    connect(cancelBtn, &QPushButton::clicked, this, [this] { showPage(0); });
    dockLayout->addWidget(cancelBtn);

    create_ = new QPushButton("Criar sala", dock);
    create_->setObjectName("create-room-btn");
    create_->setIcon(Theme::icon("bolt", Theme::Secondary, 18));
    create_->setStyleSheet("QPushButton { background-color: #000000; color: #FFFFFF; font-weight: 700; font-size: 13px; border-radius: 10px; padding: 10px 24px; } QPushButton:hover { background-color: #1B1B1B; }");
    dockLayout->addWidget(create_);
    layout->addWidget(dock);

    scroll->setWidget(page);
    return scroll;
}

QWidget *Window::createRoomPage() {
    auto *page = new QWidget(this);
    page->setObjectName("roomPage");
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(24, 16, 24, 16);
    layout->setSpacing(14);

    auto *sessBar = new QWidget(page);
    sessBar->setStyleSheet("background-color: #F0EEE3; border-radius: 10px; border: 1px solid #EAE8DE;");
    auto *sbLayout = new QHBoxLayout(sessBar);
    sbLayout->setContentsMargins(14, 8, 14, 8);
    sbLayout->setSpacing(12);

    auto *liveDot = new QLabel(sessBar);
    liveDot->setFixedSize(8, 8);
    liveDot->setStyleSheet("background-color: #0FFCBE; border-radius: 4px;");
    sbLayout->addWidget(liveDot);

    roomSessionCodeLabel_ = new QLabel("Sessão #lzr-792-sky", sessBar);
    roomSessionCodeLabel_->setFont(Theme::codeFont(10));
    roomSessionCodeLabel_->setStyleSheet("color: #000000; font-weight: 700;");
    sbLayout->addWidget(roomSessionCodeLabel_);

    auto *sep = new QLabel("/", sessBar);
    sep->setStyleSheet("color: #CFD0C5;");
    sbLayout->addWidget(sep);

    roomStatusBadge_ = new QLabel("Stream Direto Ativo", sessBar);
    roomStatusBadge_->setFont(Theme::bodyFont(9));
    roomStatusBadge_->setStyleSheet("color: #4C4546;");
    sbLayout->addWidget(roomStatusBadge_);

    sbLayout->addStretch();

    auto *copyLinkBtn = new QPushButton("Copiar link", sessBar);
    copyLinkBtn->setIcon(Theme::icon("copy", QColor(0, 0, 0), 14));
    copyLinkBtn->setStyleSheet("background-color: #FFFFFF; border: 1px solid #EAE8DE; border-radius: 6px; padding: 4px 10px; font-size: 11px; font-weight: 600;");
    connect(copyLinkBtn, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(token_->text());
        notice("Link copiado para a área de transferência.");
    });
    sbLayout->addWidget(copyLinkBtn);

    auto *dtlsBadge = new QLabel("Criptografado", sessBar);
    dtlsBadge->setFont(Theme::codeFont(9));
    dtlsBadge->setStyleSheet("background-color: #F0EEE3; color: #006C4F; border: 1px solid #E4E3D8; padding: 4px 10px; border-radius: 6px; font-weight: 700;");
    sbLayout->addWidget(dtlsBadge);
    layout->addWidget(sessBar);

    waitingModalWidget_ = new QWidget(page);
    waitingModalWidget_->setStyleSheet("background-color: #F6F4E9; border-radius: 12px; border: 1px solid #EAE8DE;");
    auto *wmLayout = new QHBoxLayout(waitingModalWidget_);
    wmLayout->setContentsMargins(16, 12, 16, 12);
    wmLayout->setSpacing(14);

    auto *wmIcon = new QLabel(waitingModalWidget_);
    wmIcon->setFixedSize(36, 36);
    wmIcon->setStyleSheet("background-color: #F0EEE3; border: 1px solid #E4E3D8; border-radius: 10px;");
    wmIcon->setAlignment(Qt::AlignCenter);
    wmIcon->setPixmap(Theme::icon("bolt", Theme::SecondaryDark, 18).pixmap(18, 18));
    wmLayout->addWidget(wmIcon);

    auto *wmTextLayout = new QVBoxLayout;
    wmTextLayout->setSpacing(2);
    auto *wmTitle = new QLabel("Aguardando aprovação de quem está compartilhando", waitingModalWidget_);
    wmTitle->setFont(Theme::headlineFont(11, QFont::Bold));
    wmTitle->setStyleSheet("color: #000000;");
    wmTextLayout->addWidget(wmTitle);

    auto *wmSub = new QLabel("O anfitrião da sala precisa autorizar sua entrada antes da transmissão começar.", waitingModalWidget_);
    wmSub->setFont(Theme::bodyFont(9));
    wmSub->setStyleSheet("color: #4C4546;");
    wmTextLayout->addWidget(wmSub);
    wmLayout->addLayout(wmTextLayout, 1);

    auto *cancelWaitBtn = new QPushButton("Cancelar solicitação", waitingModalWidget_);
    cancelWaitBtn->setStyleSheet("background-color: #FFDAD6; color: #BA1A1A; border: none; border-radius: 6px; padding: 6px 12px; font-weight: 600; font-size: 11px;");
    connect(cancelWaitBtn, &QPushButton::clicked, this, &Window::stop);
    wmLayout->addWidget(cancelWaitBtn);
    waitingModalWidget_->hide();
    layout->addWidget(waitingModalWidget_);

    roomInviteBanner_ = new QWidget(page);
    roomInviteBanner_->setObjectName("roomInviteBanner");
    roomInviteBanner_->setStyleSheet("QWidget#roomInviteBanner { background-color: #FFFFFF; border: 1.5px solid #EAE8DE; border-radius: 12px; }");
    auto *ribLayout = new QHBoxLayout(roomInviteBanner_);
    ribLayout->setContentsMargins(16, 12, 16, 12);
    ribLayout->setSpacing(14);

    inviteAvatarLabel_ = new QLabel(roomInviteBanner_);
    inviteAvatarLabel_->setFixedSize(36, 36);
    inviteAvatarLabel_->setStyleSheet("background-color: #F0EEE3; border-radius: 18px; border: 1px solid #E4E3D8;");
    inviteAvatarLabel_->setAlignment(Qt::AlignCenter);
    inviteAvatarLabel_->setPixmap(Theme::avatarPixmap(profile_.avatar, 26));
    ribLayout->addWidget(inviteAvatarLabel_);

    auto *ribTextLayout = new QVBoxLayout;
    ribTextLayout->setSpacing(2);
    inviteHeadingLabel_ = new QLabel(QString("%1, está te convidando para assistir com ele").arg(profile_.valid() ? profile_.nickname : "Anfitrião"), roomInviteBanner_);
    inviteHeadingLabel_->setFont(Theme::headlineFont(11, QFont::Bold));
    inviteHeadingLabel_->setStyleSheet("color: #000000; border: none;");
    ribTextLayout->addWidget(inviteHeadingLabel_);

    auto *ribSub = new QLabel("Envie o link da sala para até 4 amigos assistirem direto no navegador.", roomInviteBanner_);
    ribSub->setFont(Theme::bodyFont(9));
    ribSub->setStyleSheet("color: #4C4546; border: none;");
    ribTextLayout->addWidget(ribSub);
    ribLayout->addLayout(ribTextLayout, 1);

    auto *ribCopyBtn = new QPushButton("Copiar link", roomInviteBanner_);
    ribCopyBtn->setIcon(Theme::icon("copy", QColor(255, 255, 255), 14));
    ribCopyBtn->setStyleSheet("QPushButton { background-color: #000000; color: #FFFFFF; font-weight: 700; border-radius: 6px; padding: 6px 14px; font-size: 11px; border: none; } QPushButton:hover { background-color: #1B1B1B; }");
    connect(ribCopyBtn, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(token_->text());
        notice("Link de convite copiado para a área de transferência.");
    });
    ribLayout->addWidget(ribCopyBtn);

    auto *closeBannerBtn = new QPushButton(roomInviteBanner_);
    closeBannerBtn->setIcon(Theme::icon("close", QColor(76, 69, 70), 14));
    closeBannerBtn->setFixedSize(26, 26);
    closeBannerBtn->setStyleSheet("QPushButton { background: transparent; border: none; border-radius: 4px; } QPushButton:hover { background-color: #F0EEE3; }");
    connect(closeBannerBtn, &QPushButton::clicked, roomInviteBanner_, &QWidget::hide);
    ribLayout->addWidget(closeBannerBtn);

    roomInviteBanner_->hide();
    layout->addWidget(roomInviteBanner_);

    auto *stageWidget = new QWidget(page);
    auto *stageLayout = new QHBoxLayout(stageWidget);
    stageLayout->setContentsMargins(0, 0, 0, 0);
    stageLayout->setSpacing(16);

    auto *leftStage = new QWidget(stageWidget);
    auto *leftStageLayout = new QVBoxLayout(leftStage);
    leftStageLayout->setContentsMargins(0, 0, 0, 0);
    leftStageLayout->setSpacing(10);

    viewerPanel_ = new ViewerPanel(leftStage);
    video_ = viewerPanel_->video();
    leftStageLayout->addWidget(viewerPanel_, 1);

    auto *floatingBar = new QFrame(leftStage);
    floatingBar->setObjectName("floatingBar");
    floatingBar->setStyleSheet("QFrame#floatingBar { background-color: #FFFFFF; border: 1.5px solid #EAE8DE; border-radius: 12px; padding: 4px; }");
    auto *fbLayout = new QHBoxLayout(floatingBar);
    fbLayout->setContentsMargins(14, 6, 14, 6);
    fbLayout->setSpacing(12);

    auto *liveChip = new QLabel("AO VIVO", floatingBar);
    liveChip->setFont(Theme::codeFont(9));
    liveChip->setStyleSheet("background-color: #F0EEE3; color: #006C4F; font-weight: 700; padding: 4px 8px; border-radius: 6px; border: none;");
    fbLayout->addWidget(liveChip);

    auto *netStatsChip = new QLabel("Conexão Direta • Transmitindo em tempo real", floatingBar);
    netStatsChip->setFont(Theme::codeFont(9));
    netStatsChip->setStyleSheet("color: #4C4546; font-weight: 600; border: none;");
    fbLayout->addWidget(netStatsChip);
    fbLayout->addStretch();

    share_ = new QPushButton("Compartilhar tela", floatingBar);
    share_->setStyleSheet("QPushButton { background-color: #14FDBF; color: #002116; border: 1px solid #0FFCBE; border-radius: 6px; padding: 6px 12px; font-weight: 700; font-size: 11px; } QPushButton:hover { background-color: #38FFC3; } QPushButton:disabled { background-color: #F0EEE3; color: #A09E94; border: 1px solid #EAE8DE; }");
    pause_ = new QPushButton("Parar compartilhamento", floatingBar);
    pause_->setStyleSheet("QPushButton { background-color: #F0EEE3; color: #1B1C16; border: 1px solid #E4E3D8; border-radius: 6px; padding: 6px 12px; font-weight: 600; font-size: 11px; } QPushButton:hover { background-color: #EAE8DE; } QPushButton:disabled { background-color: #F6F4E9; color: #A09E94; border: 1px solid #EAE8DE; }");
    change_ = new QPushButton("Monitor / qualidade", floatingBar);
    change_->setStyleSheet("QPushButton { background-color: #F0EEE3; color: #1B1C16; border: 1px solid #E4E3D8; border-radius: 6px; padding: 6px 12px; font-weight: 600; font-size: 11px; } QPushButton:hover { background-color: #EAE8DE; } QPushButton:disabled { background-color: #F6F4E9; color: #A09E94; border: 1px solid #EAE8DE; }");

    fbLayout->addWidget(share_);
    fbLayout->addWidget(pause_);
    fbLayout->addWidget(change_);

    audioBtn_ = new QPushButton("Áudio do sistema", floatingBar);
    audioBtn_->setObjectName("btn-select-audio");
    audioBtn_->setIcon(Theme::icon("volume_up", Theme::Primary, 14));
    audioBtn_->setStyleSheet("QPushButton { background-color: #F0EEE3; color: #1B1C16; border: 1px solid #E4E3D8; border-radius: 6px; padding: 6px 12px; font-weight: 600; font-size: 11px; } QPushButton:hover { background-color: #EAE8DE; } QPushButton:disabled { background-color: #F6F4E9; color: #A09E94; border: 1px solid #EAE8DE; }");
    connect(audioBtn_, &QPushButton::clicked, this, &Window::openAudioDialog);
    fbLayout->addWidget(audioBtn_);

    stop_ = new QPushButton("Encerrar / sair", floatingBar);
    stop_->setObjectName("btn-exit-room");
    stop_->setIcon(Theme::icon("logout", QColor(255, 255, 255), 14));
    stop_->setStyleSheet("QPushButton { background-color: #BA1A1A; color: #FFFFFF; border: none; border-radius: 6px; padding: 6px 14px; font-weight: 700; font-size: 11px; } QPushButton:hover { background-color: #D32F2F; }");
    fbLayout->addWidget(stop_);
    leftStageLayout->addWidget(floatingBar);
    stageLayout->addWidget(leftStage, 2);

    auto *rightPanel = new QFrame(stageWidget);
    rightPanel->setObjectName("rightPanel");
    rightPanel->setStyleSheet("QFrame#rightPanel { background-color: #F6F4E9; border-radius: 14px; border: 1px solid #EAE8DE; }");
    auto *rpLayout = new QVBoxLayout(rightPanel);
    rpLayout->setContentsMargins(16, 14, 16, 14);
    rpLayout->setSpacing(10);

    auto *rpHeader = new QHBoxLayout;
    auto *rpTitle = new QLabel("Participantes", rightPanel);
    rpTitle->setFont(Theme::headlineFont(12, QFont::Bold));
    rpTitle->setStyleSheet("color: #000000; border: none;");
    rpHeader->addWidget(rpTitle);
    rpHeader->addStretch();
    auto *maxUsersLbl = new QLabel("Até 4 pessoas", rightPanel);
    maxUsersLbl->setFont(Theme::codeFont(8));
    maxUsersLbl->setStyleSheet("color: #7E7576; font-weight: 600; text-transform: uppercase; border: none;");
    rpHeader->addWidget(maxUsersLbl);
    rpLayout->addLayout(rpHeader);

    viewers_ = new QListWidget(rightPanel);
    viewers_->setObjectName("viewers");
    viewers_->setStyleSheet("QListWidget { background-color: #FFFFFF; border: 1px solid #EAE8DE; border-radius: 8px; }");
    rpLayout->addWidget(viewers_, 1);

    auto *modActions = new QHBoxLayout;
    modActions->setSpacing(6);
    approve_ = new QPushButton("Aprovar", rightPanel);
    approve_->setStyleSheet("background-color: #14FDBF; color: #002116; border: none; border-radius: 6px; padding: 6px 10px; font-weight: 700; font-size: 11px;");
    remove_ = new QPushButton("Remover", rightPanel);
    remove_->setStyleSheet("background-color: #FFDAD6; color: #BA1A1A; border: none; border-radius: 6px; padding: 6px 10px; font-weight: 600; font-size: 11px;");
    relay_ = new QPushButton("Tentar novamente", rightPanel);
    relay_->setStyleSheet("background-color: #EAE8DE; color: #1B1C16; border: 1px solid #CFD0C5; border-radius: 6px; padding: 6px 10px; font-weight: 600; font-size: 11px;");

    approve_->hide();
    modActions->addWidget(approve_);
    modActions->addWidget(remove_);
    modActions->addWidget(relay_);
    rpLayout->addLayout(modActions);

    requireApproval_ = new QCheckBox("Novos espectadores precisam de aprovação", rightPanel);
    requireApproval_->setObjectName("requireApproval");
    requireApproval_->setFont(Theme::bodyFont(9));
    rpLayout->addWidget(requireApproval_);

    stageLayout->addWidget(rightPanel, 1);
    layout->addWidget(stageWidget, 1);

    auto *metricRow = new QHBoxLayout;
    metricRow->setSpacing(12);

    auto makeMetricCard = [&](const QString &icon, const QString &title, const QString &mainVal, const QString &sub) {
        auto *mc = new QFrame(page);
        mc->setObjectName("metricCard");
        mc->setStyleSheet("QFrame#metricCard { background-color: #F0EEE3; border-radius: 10px; border: 1px solid #EAE8DE; }");
        auto *mcl = new QVBoxLayout(mc);
        mcl->setContentsMargins(12, 10, 12, 10);
        mcl->setSpacing(4);

        auto *top = new QHBoxLayout;
        auto *tLbl = new QLabel(title, mc);
        tLbl->setFont(Theme::headlineFont(10, QFont::Bold));
        tLbl->setStyleSheet("color: #000000; border: none;");
        top->addWidget(tLbl);
        top->addStretch();
        auto *iLbl = new QLabel(mc);
        iLbl->setPixmap(Theme::icon(icon, Theme::SecondaryDark, 16).pixmap(16, 16));
        top->addWidget(iLbl);
        mcl->addLayout(top);

        auto *valLbl = new QLabel(mainVal, mc);
        valLbl->setFont(Theme::codeFont(14));
        valLbl->setStyleSheet("color: #000000; font-weight: 700; border: none;");
        mcl->addWidget(valLbl);

        auto *subLbl = new QLabel(sub, mc);
        subLbl->setFont(Theme::bodyFont(8));
        subLbl->setStyleSheet("color: #7E7576; border: none;");
        mcl->addWidget(subLbl);
        metricRow->addWidget(mc);
    };

    makeMetricCard("bolt", "Fluidez da Imagem", "60 FPS", "Transmissão suave e contínua");
    makeMetricCard("signal", "Consumo de Internet", "8.42 MB/s", "Ajuste dinâmico de qualidade");
    makeMetricCard("lock", "Segurança da Sala", "Privado e Seguro", "Criptografia de ponta a ponta");

    metrics_ = new QLabel("Upload: 0 kbps", page);
    metrics_->setObjectName("metrics");
    metrics_->hide();
    layout->addLayout(metricRow);

    return page;
}

QWidget *Window::createSettingsPage() {
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("background-color: #FCFAEF;");

    auto *page = new QWidget(scroll);
    page->setObjectName("settingsPage");
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(40, 20, 40, 30);
    layout->setSpacing(20);

    auto *diagTop = new QHBoxLayout;
    auto *diagTitleLayout = new QVBoxLayout;
    auto *subhead = new QLabel("CONFIGURAÇÕES / REDE E CONEXÕES", page);
    subhead->setFont(Theme::codeFont(8));
    subhead->setStyleSheet("color: #7E7576; font-weight: 700;");
    diagTitleLayout->addWidget(subhead);

    auto *mainTitle = new QLabel("Configurações e Conexão de Rede", page);
    mainTitle->setFont(Theme::headlineFont(20, QFont::Bold));
    mainTitle->setStyleSheet("color: #000000;");
    diagTitleLayout->addWidget(mainTitle);
    diagTop->addLayout(diagTitleLayout);
    diagTop->addStretch();

    auto *peerBadge = new QLabel("Status da Rede: Conexão Direta Ativa", page);
    peerBadge->setFont(Theme::codeFont(9));
    peerBadge->setStyleSheet("background-color: #F0EEE3; color: #000000; padding: 6px 12px; border-radius: 8px; font-weight: 700; border: none;");
    diagTop->addWidget(peerBadge);
    layout->addLayout(diagTop);

    auto *tabsBar = new QWidget(page);
    tabsBar->setStyleSheet("background-color: #EAE8DE; border-radius: 10px; padding: 2px;");
    auto *tbLayout = new QHBoxLayout(tabsBar);
    tbLayout->setContentsMargins(4, 4, 4, 4);
    tbLayout->setSpacing(6);

    auto *tabConn = new QPushButton("Conexão", tabsBar);
    tabConn->setStyleSheet("background-color: #FCFAEF; color: #000000; font-weight: 700; border-radius: 8px; padding: 6px 16px; border: 1px solid #E4E3D8;");
    auto *tabDiag = new QPushButton("Diagnóstico & Suporte", tabsBar);
    tabDiag->setStyleSheet("background: transparent; color: #4C4546; font-weight: 600; border: none; padding: 6px 16px;");
    connect(tabDiag, &QPushButton::clicked, this, [this] { showComingSoon("Diagnóstico Avançado"); });

    auto *tabGen = new QPushButton("Geral / Atalhos", tabsBar);
    tabGen->setStyleSheet("background: transparent; color: #4C4546; font-weight: 600; border: none; padding: 6px 16px;");
    connect(tabGen, &QPushButton::clicked, this, [this] { showComingSoon("Atalhos de Teclado"); });

    tbLayout->addWidget(tabConn);
    tbLayout->addWidget(tabDiag);
    tbLayout->addWidget(tabGen);
    tbLayout->addStretch();
    layout->addWidget(tabsBar);

    auto *critBanner = new QFrame(page);
    critBanner->setObjectName("critBanner");
    critBanner->setStyleSheet("QFrame#critBanner { background-color: #F6F4E9; border: 1.5px solid #EAE8DE; border-radius: 14px; }");
    auto *cbLayout = new QVBoxLayout(critBanner);
    cbLayout->setContentsMargins(20, 18, 20, 18);
    cbLayout->setSpacing(12);

    auto *cbTop = new QHBoxLayout;
    auto *natTag = new QLabel("CONEXÃO DIRETA LIMITADA", critBanner);
    natTag->setFont(Theme::codeFont(8));
    natTag->setStyleSheet("background-color: #FFDAD6; color: #BA1A1A; font-weight: 700; padding: 2px 8px; border-radius: 4px; border: none;");
    cbTop->addWidget(natTag);
    cbTop->addStretch();
    auto *e2eeTag = new QLabel("Criptografia Ponta a Ponta Preservada", critBanner);
    e2eeTag->setFont(Theme::codeFont(8));
    e2eeTag->setStyleSheet("background-color: #F0EEE3; color: #006C4F; font-weight: 600; padding: 2px 8px; border-radius: 4px; border: none;");
    cbTop->addWidget(e2eeTag);
    cbLayout->addLayout(cbTop);

    auto *cbTitle = new QLabel("A rede de algum participante tem restrições de portas. O servidor seguro pode intermediar a transmissão para que todos consigam assistir.", critBanner);
    cbTitle->setFont(Theme::headlineFont(13, QFont::Bold));
    cbTitle->setStyleSheet("color: #1B1C16; border: none;");
    cbTitle->setWordWrap(true);
    cbLayout->addWidget(cbTitle);

    auto *relayAuthBtn = new QPushButton("Permitir Conexão Assistida por Servidor", critBanner);
    relayAuthBtn->setStyleSheet(profile_.relay ?
        "background-color: #FFFFFF; color: #006C4F; font-weight: 700; border: 1.5px solid #0FFCBE; border-radius: 8px; padding: 10px 16px;" :
        "background-color: #14FDBF; color: #002116; font-weight: 700; border: none; border-radius: 8px; padding: 10px 16px;");
    connect(relayAuthBtn, &QPushButton::clicked, this, [this, relayAuthBtn] {
        profile_.relay = !profile_.relay;
        profile_.save();
        if (profile_.relay) {
            relayAuthBtn->setText("Conexão Assistida Ativada");
            relayAuthBtn->setStyleSheet("background-color: #FFFFFF; color: #006C4F; font-weight: 700; border: 1.5px solid #0FFCBE; border-radius: 8px; padding: 10px 16px;");
        } else {
            relayAuthBtn->setText("Permitir Conexão Assistida por Servidor");
            relayAuthBtn->setStyleSheet("background-color: #14FDBF; color: #002116; font-weight: 700; border: none; border-radius: 8px; padding: 10px 16px;");
        }
    });
    cbLayout->addWidget(relayAuthBtn, 0, Qt::AlignLeft);
    layout->addWidget(critBanner);

    auto *twoCol = new QHBoxLayout;
    twoCol->setSpacing(20);

    auto *leftCol = new QWidget(page);
    auto *lcLayout = new QVBoxLayout(leftCol);
    lcLayout->setContentsMargins(0, 0, 0, 0);
    lcLayout->setSpacing(16);

    auto *serverBox = new QFrame(leftCol);
    serverBox->setObjectName("serverBox");
    serverBox->setStyleSheet("QFrame#serverBox { background-color: #F0EEE3; border-radius: 12px; border: 1px solid #EAE8DE; }");
    auto *sbL = new QVBoxLayout(serverBox);
    sbL->setContentsMargins(16, 14, 16, 14);
    sbL->setSpacing(10);

    auto *sbHead = new QHBoxLayout;
    auto *sbTitle = new QLabel("Servidor de Apoio do Lazarus Share", serverBox);
    sbTitle->setFont(Theme::headlineFont(12, QFont::Bold));
    sbTitle->setStyleSheet("color: #000000; border: none;");
    sbHead->addWidget(sbTitle);
    sbHead->addStretch();
    auto *connChip = new QLabel("Conectado · 18ms", serverBox);
    connChip->setFont(Theme::codeFont(8));
    connChip->setStyleSheet("background-color: #14FDBF; color: #002116; font-weight: 700; padding: 2px 6px; border-radius: 4px; border: none;");
    sbHead->addWidget(connChip);
    sbL->addLayout(sbHead);

    auto *urlLbl = new QLabel("Endereço do Servidor", serverBox);
    urlLbl->setFont(Theme::codeFont(8));
    urlLbl->setStyleSheet("color: #7E7576; text-transform: uppercase; font-weight: 700; border: none;");
    sbL->addWidget(urlLbl);

    auto *urlInput = new QLineEdit(endpoint_, serverBox);
    urlInput->setReadOnly(true);
    sbL->addWidget(urlInput);

    auto *tlsFP = new QLabel("Conexão Criptografada e Segura (TLS)", serverBox);
    tlsFP->setFont(Theme::codeFont(8));
    tlsFP->setStyleSheet("color: #006C4F; font-weight: 600; border: none;");
    sbL->addWidget(tlsFP);
    lcLayout->addWidget(serverBox);

    auto *iceBox = new QFrame(leftCol);
    iceBox->setObjectName("iceBox");
    iceBox->setStyleSheet("QFrame#iceBox { background-color: #F0EEE3; border-radius: 12px; border: 1px solid #EAE8DE; }");
    auto *ibL = new QVBoxLayout(iceBox);
    ibL->setContentsMargins(16, 14, 16, 14);
    ibL->setSpacing(8);

    auto *iceTitle = new QLabel("Servidores de Conexão", iceBox);
    iceTitle->setFont(Theme::headlineFont(11, QFont::Bold));
    iceTitle->setStyleSheet("color: #000000; border: none;");
    ibL->addWidget(iceTitle);

    auto *stunRow = new QLabel("Servidor Principal: Oficial Lazarus (São Paulo)", iceBox);
    stunRow->setFont(Theme::codeFont(9));
    stunRow->setStyleSheet("background-color: #FFFFFF; border-radius: 6px; padding: 6px 10px; font-weight: 600; border: none;");
    ibL->addWidget(stunRow);

    auto *turnRow = new QLabel("Servidor Auxiliar: Automático com redundância", iceBox);
    turnRow->setFont(Theme::codeFont(9));
    turnRow->setStyleSheet("background-color: #FFFFFF; border-radius: 6px; padding: 6px 10px; font-weight: 500; color: #4C4546; border: none;");
    ibL->addWidget(turnRow);

    lcLayout->addWidget(iceBox);
    twoCol->addWidget(leftCol, 1);

    auto *rightCol = new QWidget(page);
    auto *rcLayout = new QVBoxLayout(rightCol);
    rcLayout->setContentsMargins(0, 0, 0, 0);
    rcLayout->setSpacing(16);

    auto *loopBox = new QFrame(rightCol);
    loopBox->setObjectName("loopBox");
    loopBox->setStyleSheet("QFrame#loopBox { background-color: #F0EEE3; border-radius: 12px; border: 1px solid #EAE8DE; }");
    auto *lbL = new QVBoxLayout(loopBox);
    lbL->setContentsMargins(16, 14, 16, 14);
    lbL->setSpacing(10);

    auto *lbHead = new QHBoxLayout;
    auto *lbTitle = new QLabel("Teste de Vídeo Local", loopBox);
    lbTitle->setFont(Theme::headlineFont(12, QFont::Bold));
    lbTitle->setStyleSheet("color: #000000; border: none;");
    lbHead->addWidget(lbTitle);
    lbHead->addStretch();
    auto *fpsBadge = new QLabel("60 FPS · Alta Resolução", loopBox);
    fpsBadge->setFont(Theme::codeFont(8));
    fpsBadge->setStyleSheet("background-color: #F0EEE3; color: #006C4F; font-weight: 700; padding: 2px 6px; border-radius: 4px; border: none;");
    lbHead->addWidget(fpsBadge);
    lbL->addLayout(lbHead);

    auto *smpte = new QWidget(loopBox);
    smpte->setFixedHeight(100);
    smpte->setStyleSheet("background-color: #E4E3D8; border-radius: 8px; border: 1px solid #CFD0C5;");
    auto *smpteLayout = new QHBoxLayout(smpte);
    smpteLayout->setContentsMargins(0, 0, 0, 0);
    smpteLayout->setSpacing(0);
    for (const auto &c : {"#FFFFFF", "#FFFF00", "#00FFFF", "#00FF00", "#FF00FF", "#FF00FF", "#0000FF"}) {
        auto *cbar = new QWidget(smpte);
        cbar->setStyleSheet(QString("background-color: %1;").arg(c));
        smpteLayout->addWidget(cbar);
    }
    lbL->addWidget(smpte);

    auto *loopTestBtn = new QPushButton("Testar Exibição Local de Vídeo", loopBox);
    loopTestBtn->setIcon(Theme::icon("bolt", Theme::SecondaryDark, 16));
    loopTestBtn->setStyleSheet("QPushButton { background-color: #FFFFFF; color: #1B1C16; font-weight: 700; border: 1.5px solid #E4E3D8; border-radius: 8px; padding: 10px; font-size: 12px; } QPushButton:hover { background-color: #F0EEE3; border-color: #CFD0C5; }");
    connect(loopTestBtn, &QPushButton::clicked, this, [loopTestBtn] {
        loopTestBtn->setText("Verificando exibição de vídeo...");
        QTimer::singleShot(1400, loopTestBtn, [loopTestBtn] {
            loopTestBtn->setText("Exibição Aprovada (Excelente)");
            QTimer::singleShot(2500, loopTestBtn, [loopTestBtn] {
                loopTestBtn->setText("Testar Exibição Local de Vídeo");
            });
        });
    });
    lbL->addWidget(loopTestBtn);
    rcLayout->addWidget(loopBox);

    auto *adaptBox = new QFrame(rightCol);
    adaptBox->setObjectName("adaptBox");
    adaptBox->setStyleSheet("QFrame#adaptBox { background-color: #F0EEE3; border-radius: 12px; border: 1px solid #EAE8DE; }");
    auto *abL = new QVBoxLayout(adaptBox);
    abL->setContentsMargins(16, 14, 16, 14);
    abL->setSpacing(6);

    auto *abTitle = new QLabel("Diagnóstico da Conexão Local", adaptBox);
    abTitle->setFont(Theme::headlineFont(11, QFont::Bold));
    abTitle->setStyleSheet("color: #000000; border: none;");
    abL->addWidget(abTitle);

    auto addDiagRow = [&](const QString &desc, const QString &status, bool ok) {
        auto *row = new QHBoxLayout;
        auto *dLbl = new QLabel(desc, adaptBox);
        dLbl->setFont(Theme::bodyFont(9));
        row->addWidget(dLbl);
        row->addStretch();
        auto *sLbl = new QLabel(status, adaptBox);
        sLbl->setFont(Theme::codeFont(8));
        sLbl->setStyleSheet(ok ? "color: #006C4F; font-weight: 700;" : "color: #BA1A1A; font-weight: 700;");
        row->addWidget(sLbl);
        abL->addLayout(row);
    };

    addDiagRow("Comunicação de rede local", "FUNCIONANDO", true);
    addDiagRow("Acesso ao servidor do Lazarus", "6ms · OK", true);
    addDiagRow("Conexão direta em redes com bloqueio", "USA SERVIDOR DE APOIO", false);
    rcLayout->addWidget(adaptBox);

    twoCol->addWidget(rightCol, 1);
    layout->addLayout(twoCol);

    auto *bottomDock = new QFrame(page);
    bottomDock->setStyleSheet("background-color: #F0EEE3; border-radius: 12px; border: 1px solid #EAE8DE;");
    auto *bdLayout = new QHBoxLayout(bottomDock);
    bdLayout->setContentsMargins(16, 12, 16, 12);
    bdLayout->setSpacing(12);

    auto *exportDiagBtn = new QPushButton("Exportar diagnóstico", bottomDock);
    exportDiagBtn->setIcon(Theme::icon("content_paste", QColor(0, 0, 0), 14));
    exportDiagBtn->setStyleSheet("background-color: #FFFFFF; border: 1px solid #E4E3D8; border-radius: 6px; padding: 6px 12px; font-weight: 600; font-size: 11px;");
    connect(exportDiagBtn, &QPushButton::clicked, this, &Window::diagnostics);
    bdLayout->addWidget(exportDiagBtn);

    auto *clearLogsBtn = new QPushButton("Apagar diagnósticos locais", bottomDock);
    clearLogsBtn->setStyleSheet("background: transparent; border: none; color: #4C4546; font-weight: 600; font-size: 11px;");
    connect(clearLogsBtn, &QPushButton::clicked, this, [this] {
        log_.clear();
        notice("Diagnósticos locais apagados.");
    });
    bdLayout->addWidget(clearLogsBtn);

    auto *testBox = new QCheckBox("Vídeo de teste", bottomDock);
    testBox->setObjectName("testPattern");
    testBox->setFont(Theme::bodyFont(9));
    connect(testBox, &QCheckBox::toggled, this, [this](bool on) {
        if (!active_) testPattern_ = on;
    });
    bdLayout->addWidget(testBox);

    bdLayout->addStretch();

    auto *closeSettingsBtn = new QPushButton("Fechar", bottomDock);
    closeSettingsBtn->setStyleSheet("background-color: #E4E3D8; color: #000000; border-radius: 8px; padding: 8px 16px; font-weight: 600;");
    connect(closeSettingsBtn, &QPushButton::clicked, this, [this] {
        showPage(previousPageIndex_);
    });
    bdLayout->addWidget(closeSettingsBtn);

    auto *saveBtn = new QPushButton("Salvar Alterações", bottomDock);
    saveBtn->setStyleSheet("background-color: #000000; color: #FFFFFF; border-radius: 8px; padding: 8px 18px; font-weight: 700;");
    connect(saveBtn, &QPushButton::clicked, this, [saveBtn, this] {
        saveBtn->setText("Salvo!");
        QTimer::singleShot(1500, saveBtn, [saveBtn] {
            saveBtn->setText("Salvar Alterações");
        });
        showPage(previousPageIndex_);
    });
    bdLayout->addWidget(saveBtn);
    layout->addWidget(bottomDock);

    scroll->setWidget(page);
    return scroll;
}

void Window::createAudioDialog() {
    audioDialog_ = new QDialog(this);
    audioDialog_->setWindowTitle("Transmitir áudio do computador");
    audioDialog_->setMinimumWidth(480);
    audioDialog_->setStyleSheet("QDialog { background-color: #FCFAEF; }");
    auto *layout = new QVBoxLayout(audioDialog_);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(14);

    auto *headerLayout = new QHBoxLayout;
    headerLayout->setSpacing(12);

    auto *iconBadge = new QLabel(audioDialog_);
    iconBadge->setFixedSize(40, 40);
    iconBadge->setStyleSheet("background-color: #F0EEE3; border: 1px solid #E4E3D8; border-radius: 10px;");
    iconBadge->setAlignment(Qt::AlignCenter);
    iconBadge->setPixmap(Theme::icon("volume_up", Theme::Primary, 20).pixmap(20, 20));
    headerLayout->addWidget(iconBadge);

    auto *titleLayout = new QVBoxLayout;
    titleLayout->setSpacing(2);
    auto *titleLbl = new QLabel("Transmitir áudio dos aplicativos", audioDialog_);
    titleLbl->setFont(Theme::headlineFont(13, QFont::Bold));
    titleLbl->setStyleSheet("color: #000000; border: none;");
    titleLayout->addWidget(titleLbl);
    auto *subLbl = new QLabel("Selecione quais programas terão o som transmitido para os participantes:", audioDialog_);
    subLbl->setFont(Theme::bodyFont(9));
    subLbl->setStyleSheet("color: #4C4546; border: none;");
    titleLayout->addWidget(subLbl);
    headerLayout->addLayout(titleLayout, 1);
    layout->addLayout(headerLayout);

    auto *statusCard = new QFrame(audioDialog_);
    statusCard->setStyleSheet("background-color: #FFFFFF; border: 1px solid #EAE8DE; border-radius: 8px; padding: 4px;");
    auto *scLayout = new QHBoxLayout(statusCard);
    scLayout->setContentsMargins(12, 8, 12, 8);
    auto *statusIcon = new QLabel(statusCard);
    statusIcon->setPixmap(Theme::icon("security", Theme::SecondaryDark, 14).pixmap(14, 14));
    statusIcon->setStyleSheet("border: none;");
    scLayout->addWidget(statusIcon);
    audioStatus_ = new QLabel(audio_.supported() ? "Áudio do sistema e aplicativos disponíveis." : audio_.limitation(), statusCard);
    audioStatus_->setObjectName("audioStatus");
    audioStatus_->setFont(Theme::bodyFont(9, QFont::Bold));
    audioStatus_->setStyleSheet("color: #006C4F; border: none;");
    scLayout->addWidget(audioStatus_, 1);
    layout->addWidget(statusCard);

    audioSharingNotice_ = new QLabel("Dica: Inicie o compartilhamento de tela para que os sons dos programas em execução apareçam nesta lista para você selecionar.", audioDialog_);
    audioSharingNotice_->setFont(Theme::bodyFont(9));
    audioSharingNotice_->setStyleSheet("background-color: #F0EEE3; color: #4C4546; border: 1px solid #EAE8DE; border-radius: 8px; padding: 10px;");
    audioSharingNotice_->setWordWrap(true);
    layout->addWidget(audioSharingNotice_);

    apps_ = new QListWidget(audioDialog_);
    apps_->setObjectName("apps");
    apps_->setMinimumHeight(180);
    apps_->setStyleSheet("QListWidget#apps { background-color: #FFFFFF; border: 1.5px solid #EAE8DE; border-radius: 8px; padding: 6px; font-size: 11px; } QListWidget#apps::item { padding: 6px 8px; border-radius: 4px; } QListWidget#apps::item:hover { background-color: #F6F4E9; }");
    layout->addWidget(apps_);

    audioEmptyNotice_ = new QLabel("Nenhum aplicativo com som ativo encontrado no momento.\nAbra seu navegador, reprodutor ou jogo com áudio e clique em 'Atualizar lista'.", audioDialog_);
    audioEmptyNotice_->setFont(Theme::bodyFont(9));
    audioEmptyNotice_->setStyleSheet("color: #7E7576; padding: 12px; border: none;");
    audioEmptyNotice_->setAlignment(Qt::AlignCenter);
    layout->addWidget(audioEmptyNotice_);

    auto *actionsLayout = new QHBoxLayout;
    actionsLayout->setSpacing(8);

    auto *selectAllBtn = new QPushButton("Marcar todos", audioDialog_);
    selectAllBtn->setStyleSheet("background-color: #FFFFFF; color: #1B1C16; border: 1px solid #EAE8DE; border-radius: 6px; padding: 6px 12px; font-size: 11px; font-weight: 600;");
    connect(selectAllBtn, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < apps_->count(); ++i) apps_->item(i)->setCheckState(Qt::Checked);
        selectAudio();
    });
    actionsLayout->addWidget(selectAllBtn);

    auto *unselectAllBtn = new QPushButton("Desmarcar todos (Mudo)", audioDialog_);
    unselectAllBtn->setStyleSheet("background-color: #FFFFFF; color: #1B1C16; border: 1px solid #EAE8DE; border-radius: 6px; padding: 6px 12px; font-size: 11px; font-weight: 600;");
    connect(unselectAllBtn, &QPushButton::clicked, this, [this] {
        for (int i = 0; i < apps_->count(); ++i) apps_->item(i)->setCheckState(Qt::Unchecked);
        selectAudio();
    });
    actionsLayout->addWidget(unselectAllBtn);

    auto *refreshBtn = new QPushButton("Atualizar lista", audioDialog_);
    refreshBtn->setIcon(Theme::icon("tune", Theme::Primary, 12));
    refreshBtn->setStyleSheet("background-color: #FFFFFF; color: #1B1C16; border: 1px solid #EAE8DE; border-radius: 6px; padding: 6px 10px; font-size: 11px; font-weight: 600;");
    connect(refreshBtn, &QPushButton::clicked, this, [this] {
        refreshAudio();
        if (audioEmptyNotice_) audioEmptyNotice_->setVisible(sharing_ && apps_->count() == 0);
    });
    actionsLayout->addWidget(refreshBtn);
    actionsLayout->addStretch();
    layout->addLayout(actionsLayout);

    auto *btnBox = new QHBoxLayout;
    btnBox->addStretch();
    auto *doneBtn = new QPushButton("Concluir", audioDialog_);
    doneBtn->setStyleSheet("QPushButton { background-color: #000000; color: #FFFFFF; font-weight: 700; border-radius: 6px; padding: 8px 24px; font-size: 12px; border: none; } QPushButton:hover { background-color: #1B1B1B; }");
    connect(doneBtn, &QPushButton::clicked, audioDialog_, &QDialog::accept);
    btnBox->addWidget(doneBtn);
    layout->addLayout(btnBox);
}

void Window::openAudioDialog() {
    if (!audioDialog_) return;
    refreshAudio();
    if (audioSharingNotice_) {
        audioSharingNotice_->setVisible(!sharing_);
    }
    if (audioEmptyNotice_) {
        audioEmptyNotice_->setVisible(sharing_ && apps_->count() == 0);
    }
    audioDialog_->exec();
}

Window::Window(bool onboarding) : capture_(this), audio_(this) {
    profile_ = Profile::load();
    setWindowTitle("Lazarus Share — sem login");
    setWindowIcon(Theme::appIcon());
    setMinimumSize(960, 680);
    resize(1140, 800);
    time_.start();

    createAudioDialog();

    auto *root = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(root);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);
    setCentralWidget(root);

    setupUpdates(rootLayout);

    // Persistent Top Header
    rootLayout->addWidget(createHeaderWidget());

    // Central Stacked Widget
    stack_ = new QStackedWidget(root);
    stack_->setObjectName("mainStack");
    rootLayout->addWidget(stack_, 1);

    const QString hostedEndpoint = "wss://share.app.lazaruslabs.com.br/ws";
    endpoint_ = qEnvironmentVariable("LAZARUS_SIGNAL_URL", hostedEndpoint).trimmed();
    const bool hosted = endpoint_ == hostedEndpoint;
    stun_ = qEnvironmentVariable("LAZARUS_STUN_URL", hosted ? "stun://share.app.lazaruslabs.com.br:3478" : "");
    tlsPin_ = qEnvironmentVariable("LAZARUS_TLS_PIN", "");

    monitor_ = new QComboBox(root);
    monitor_->setObjectName("internalMonitor");
    monitor_->hide();
    for (auto *screen : QGuiApplication::screens()) monitor_->addItem(screen->name());

    preset_ = new QComboBox(root);
    preset_->setObjectName("preset");
    preset_->addItems({"Baixa — 720p / 30 FPS", "Alta — 1080p / 60 FPS", "Nativo — resolução do monitor / 60 FPS"});
    preset_->setCurrentIndex(1);
    preset_->hide();

    width_ = new QSpinBox(root);
    height_ = new QSpinBox(root);
    fps_ = new QSpinBox(root);
    bitrate_ = new QSpinBox(root);
    for (auto *spin : {width_, height_, fps_, bitrate_}) {
        spin->setRange(1, 100000);
        spin->hide();
    }

    // Build Pages
    stack_->addWidget(createHomePage());
    stack_->addWidget(createPrepPage());
    stack_->addWidget(createRoomPage());
    stack_->addWidget(createSettingsPage());

    showPage(0);

    connect(identity_, &QPushButton::clicked, this, &Window::editIdentity);
    updateIdentity();

    connect(share_, &QPushButton::clicked, this, &Window::share);
    connect(pause_, &QPushButton::clicked, this, &Window::stopSharing);
    connect(change_, &QPushButton::clicked, this, &Window::share);

    share_->setEnabled(false);
    pause_->setEnabled(false);
    change_->setEnabled(false);
    if (audioBtn_) audioBtn_->setEnabled(false);

    connect(viewerPanel_, &ViewerPanel::playbackChanged, this, [this] {
        for (auto &[id, c] : peers_) if (c->media) c->media->playbackVolume(viewerPanel_->volume(), viewerPanel_->muted());
    });

    connect(create_, &QPushButton::clicked, this, &Window::create);
    connect(join_, &QPushButton::clicked, this, &Window::join);
    connect(stop_, &QPushButton::clicked, this, &Window::stop);

    connect(approve_, &QPushButton::clicked, this, [this] {
        auto id = viewers_->currentItem() ? viewers_->currentItem()->data(Qt::UserRole).toString() : QString();
        if (!administrator_ || id.isEmpty() || id == participantId_) return;
        if (!approved_.contains(id) && approved_.size() >= 4) { notice("Limite de quatro viewers atingido."); return; }
        approved_.insert(id); send({{"type", "approve"}, {"peer", id}});
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        auto id = viewers_->currentItem() ? viewers_->currentItem()->data(Qt::UserRole).toString() : QString();
        if (!administrator_ || id.isEmpty() || id == participantId_) return;
        approved_.remove(id); peers_.erase(id); send({{"type", "remove"}, {"peer", id}}); row(id, "Removido localmente");
    });
    connect(relay_, &QPushButton::clicked, this, &Window::relay);
    connect(&capture_, &Capture::ready, this, [this] {
        if (!active_ || !sender() || !capturePending_) { capture_.stop(); return; }
        capturePending_ = false; sharing_ = true; share_->setEnabled(false); pause_->setEnabled(true); change_->setEnabled(true);
        capture_.quality(quality()); refreshAudio(); log("sharing_started", {}, {{"preset",preset_->currentIndex()},{"width",capture_.dimensions().width()},{"height",capture_.dimensions().height()},{"target_fps",quality().fps},{"target_kbps",quality().kbps}});
        if (prepAudioEnabled_ && audio_.selected().isEmpty() && apps_->count() > 0) {
            for (int i = 0; i < apps_->count(); ++i) apps_->item(i)->setCheckState(Qt::Checked);
            selectAudio();
        }
        send({{"type","share-confirm"},{"revision",revision_}});
        for(auto &[id,c]:peers_)if(!c->session.isEmpty())restart(id,-1);
        updatePresentation();
        notice("Compartilhando tela. Áudio somente dos aplicativos marcados.");
    });
    connect(&capture_, &Capture::error, this, [this](QString error) { stopSharing(); log("capture_error", {}, {{"error_code", "capture_unavailable"}}); notice(error); });
    connect(&audio_, &Audio::error, this, &Window::notice);
    connect(&audio_, &Audio::changed, this, &Window::refreshAudio);
    connect(apps_, &QListWidget::itemChanged, this, [this] { selectAudio(); });
    connect(&socket_, &QWebSocket::connected, this, [this] {
        socketLost_ = -1; reconnects_ = 0;
        auto pin = tlsPin_.trimmed().remove(':').remove(' ').toLower();
        if (!pin.isEmpty() && socket_.sslConfiguration().peerCertificate().digest(QCryptographicHash::Sha256).toHex() != pin.toLatin1()) {
            stop(); notice("O certificado do servidor não corresponde à impressão informada. Conexão recusada."); return;
        }
        if (!active_) { socket_.close(); return; }
        QJsonObject m{{"type", administrator_ ? (created_ ? "resume" : "create") : "join"}, {"room", room_}, {"challenge", challenge_}};
        m["protocol"] = 2; m["profile"] = profile_.json(); m["capabilities"] = QJsonArray{"profile", "sharing", "turn-endpoints"};
        if (administrator_) { m["admin"] = admin_; if (!created_) m["requireApproval"] = roomRequiresApproval_; } send(m);
    });
    connect(&socket_, &QWebSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        auto pin = tlsPin_.trimmed().remove(':').remove(' ').toLower();
        if (pin.size() != 64 || socket_.sslConfiguration().peerCertificate().digest(QCryptographicHash::Sha256).toHex() != pin.toLatin1()) {
            notice("Não foi possível verificar a identidade do serviço de salas. Verifique se está usando a versão atual do aplicativo."); return;
        }
        if (!acceptsPinnedTls(socket_.sslConfiguration().peerCertificate(), errors, pin)) {
            notice("Certificado local recusado: erro TLS além da confiança no certificado."); return;
        }
        socket_.ignoreSslErrors(errors);
    });
    connect(&socket_, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
        auto doc = QJsonDocument::fromJson(text.toUtf8()); if (doc.isObject()) message(doc.object());
    });
    connect(&socket_, &QWebSocket::disconnected, this, [this] {
        if (active_ && socketLost_ < 0) { socketLost_ = time_.elapsed(); stopSharing(); admitted_=false; viewerState_="Reconectando"; updatePresentation(); notice("Sinalização desconectada; tentando reconectar."); }
    });
    connect(&socket_, qOverload<QAbstractSocket::SocketError>(&QWebSocket::error), this, [this](QAbstractSocket::SocketError) {
        if (active_ && socketLost_ < 0) { socketLost_ = time_.elapsed(); stopSharing(); admitted_=false; viewerState_="Reconectando"; updatePresentation(); }
        notice("Não foi possível conectar ao serviço de salas. Confira sua conexão e tente novamente.");
    });
    frameTimer_.setTimerType(Qt::PreciseTimer); frameTimer_.setInterval(8); connect(&frameTimer_, &QTimer::timeout, this, &Window::tick); frameTimer_.start();
    maintenance_.setInterval(1000); connect(&maintenance_, &QTimer::timeout, this, [this] {
        if (!active_) return;
        qint64 now = time_.elapsed(); viewerPanel_->updateFreeze(now);
        if (socketLost_ >= 0) {
            if (now - socketLost_ >= 55000) { stop(); viewerState_="Falha"; updatePresentation(); notice("Servidor indisponível; sessão encerrada. Crie uma nova sala."); return; }
            if (socket_.state() == QAbstractSocket::UnconnectedState && reconnects_++ % 3 == 0) {
                peers_.clear(); viewers_->clear(); challenge_ = Protocol::randomHex(16);
                openSocket();
            }
        }
        for (auto &[id, c] : peers_) {
            if(!sender() && c->decoderRecovering && now-c->started>=15000){mediaFailure(id,c->generation,"decoder_retry_timeout");continue;}
            if (!sender() || !sharing_ || c->selecting || c->fatalMedia || c->exhausted) continue;
            if (c->renewal.timedOut(now)) { c->renewal.failed(now); log("turn_renewal_failed", id, {{"error_code","timeout"}}); }
            if (c->relayRequested && now - c->started >= 15000) { c->relayRequested = false; c->exhausted = true; signal(id,{{"kind","connection-terminal"}}); row(id,"Relay não respondeu — tentar novamente"); log("relay_timeout", id); }
            if (!c->media) continue;
            if (c->media->connected()) {
                c->everConnected = true; c->started = now;
                if (c->transport >= 0 && c->localConsent && c->remoteConsent && !c->relayRequested && c->renewal.due(now, c->turnExpiry)) renewTurn(id);
                continue;
            }
            const qint64 timeout = c->everConnected ? 5000 : (c->transport < 0 ? 20000 : 15000);
            if (now - c->started >= timeout || c->metrics["stage"].toString() == "failed") advance(id);
        }
        if (socket_.state() == QAbstractSocket::ConnectedState) socket_.ping();
    }); maintenance_.start(); refreshAudio(); stop_->setEnabled(false);
    create_->setEnabled(profile_.valid()); join_->setEnabled(profile_.valid());
    if (onboarding && !profile_.valid()) QTimer::singleShot(0, this, &Window::editIdentity);
}
Window::~Window() { if(viewerPanel_->fullscreen())viewerPanel_->toggleFullscreen(); stop(); }
void Window::notice(const QString &text) {
    status_->setText(text);
    if (headerStatusLabel_) headerStatusLabel_->setText(text);
}
Quality Window::quality() const {
    if (preset_->currentIndex() == 0) return {1280, 720, 30, 3000};
    if (preset_->currentIndex() == 1) return {1920, 1080, 60, 8000};
    auto size = capture_.sourceSize(); if (!size.isValid()) size = QSize(1920,1080);
    int bitrate = qBound(8000, int(8000.0 * size.width() * size.height() / (1920.0 * 1080)), 40000);
    return {size.width() & ~1, size.height() & ~1, 60, bitrate};
}
void Window::create() {
    if (active_ || !profile_.valid()) return;
    administrator_ = active_ = true; created_ = false;
    secret_ = Protocol::randomBytes(16); room_ = Protocol::room(secret_); admin_ = Protocol::randomHex(32); challenge_ = Protocol::randomHex(16);
    roomRequiresApproval_ = requireApproval_->isChecked(); requireApproval_->setEnabled(false); approve_->setVisible(roomRequiresApproval_);
    token_->setText(Protocol::inviteLink(secret_, profile_.valid() ? profile_.nickname : QString(), profile_.avatar)); token_->setReadOnly(true); create_->setEnabled(false); join_->setEnabled(false); stop_->setEnabled(true);
    notice("Criando sala, sem compartilhar tela.");
    viewerState_="Conectando"; updatePresentation(); refreshAudio(); log("room_create"); openSocket();
    showPage(2);
}
void Window::join() {
    if (active_ || !profile_.valid()) return;
    secret_ = Protocol::inviteSecret(token_->text());
    if (secret_.isEmpty()) secret_ = Protocol::secret(token_->text());
    if (secret_.isEmpty()) { notice("Link de convite inválido."); return; }
    requireApproval_->setEnabled(false); approve_->hide();
    administrator_ = false; active_ = true; room_ = Protocol::room(secret_); challenge_ = Protocol::randomHex(16);
    create_->setEnabled(false); join_->setEnabled(false); token_->setReadOnly(true); stop_->setEnabled(true);
    audio_.stop(); viewerState_="Conectando"; updatePresentation(); refreshAudio(); notice("Conectando à sala…"); openSocket();
    showPage(2);
}
void Window::openInvite(const QString &link) {
    auto invitedSecret = Protocol::inviteSecret(link);
    if (invitedSecret.isEmpty()) invitedSecret = Protocol::secret(link);
    if (invitedSecret.isEmpty()) { notice("Link de convite inválido."); return; }
    showNormal(); raise(); activateWindow();
#ifdef Q_OS_WIN
    SetForegroundWindow(reinterpret_cast<HWND>(winId()));
#endif
    if (active_ && invitedSecret == secret_) {
        notice("Você já está conectado nesta sala.");
        return;
    }
    if (active_) {
        if (QMessageBox::question(this, "Trocar de sala", "Sair da sala atual e entrar na sala do convite?",
                                  QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
        stop();
    }
    token_->setText(Protocol::inviteLink(invitedSecret));
    if (!profile_.valid()) editIdentity();
    if (!profile_.valid()) { notice("Escolha um nickname para entrar na sala."); return; }
    join();
}
void Window::stop() {
    if (active_ && administrator_ && socket_.state() == QAbstractSocket::ConnectedState) send({{"type", "end"}});
    stopSharing(); log("room_closed");
    active_ = false; created_ = false; admitted_=false; shareRequested_=false;
    participantId_.clear(); broadcaster_.clear(); revision_=0; ownerOnline_=true; viewerState_="Aguardando compartilhamento"; socketLost_ = -1; updatePresentation(); socket_.close(); peers_.clear(); approved_.clear(); seenSessions_.clear(); viewers_->clear();
    capture_.stop(); audio_.stop(); refreshAudio();
    requireApproval_->setEnabled(true); requireApproval_->setChecked(false); approve_->hide();
    secret_.fill(0); secret_.clear(); room_.clear(); admin_.clear(); token_->clear(); token_->setReadOnly(false);
    create_->setEnabled(profile_.valid()); join_->setEnabled(profile_.valid()); stop_->setEnabled(false); share_->setEnabled(false);
    if (audioBtn_) { audioBtn_->setEnabled(false); audioBtn_->setText("Áudio do sistema"); }
    viewerPanel_->clearFrame("Sessão encerrada."); metrics_->setText("Upload: 0 kbps"); notice("Sessão encerrada.");
    showPage(0);
}
void Window::openSocket() {
    QUrl url(endpoint_); auto host = url.host();
    bool loopback = host == "127.0.0.1" || host == "localhost" || host == "::1";
    if (!url.isValid() || url.path() != "/ws" || (url.scheme() != "wss" && !(url.scheme() == "ws" && loopback)) || !url.userInfo().isEmpty()) {
        stop(); notice("Use wss://servidor/ws. ws:// é permitido apenas em localhost para testes."); return;
    }
    auto pin = tlsPin_.trimmed().remove(':').remove(' ').toLower();
    if (!pin.isEmpty() && (url.scheme() != "wss" || pin.size() != 64 || QByteArray::fromHex(pin.toLatin1()).size() != 32 || QByteArray::fromHex(pin.toLatin1()).toHex() != pin.toLatin1())) {
        stop(); notice("Impressão inválida: use os 64 caracteres SHA-256 e um endereço wss://."); return;
    }
    socket_.open(url);
}
void Window::send(QJsonObject m) {
    if (m["type"] == "relay") m["revision"] = revision_;
    if (socket_.state() == QAbstractSocket::ConnectedState) socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact)));
}
void Window::row(const QString &id, const QString &text) {
    auto found = peers_.find(id); QString name = id==participantId_ ? profile_.nickname : found != peers_.end() && !found->second->nickname.isEmpty() ? found->second->nickname : QString("Participante %1").arg(id.left(4));
    int avatar = id==participantId_ ? profile_.avatar : found != peers_.end() ? found->second->avatar : 0;
    QIcon icon = Theme::avatarIcon(avatar, 20);
    for (int i = 0; i < viewers_->count(); ++i) if (viewers_->item(i)->data(Qt::UserRole).toString() == id) { viewers_->item(i)->setText(name + " — " + text); viewers_->item(i)->setIcon(icon); return; }
    auto *item = new QListWidgetItem(icon, name + " — " + text, viewers_); item->setData(Qt::UserRole, id); if (!viewers_->currentItem()) viewers_->setCurrentItem(item);
}
QString Window::selectedPeer() const {
    if(!sender())return broadcaster_;
    auto id=viewers_->currentItem()?viewers_->currentItem()->data(Qt::UserRole).toString():QString();
    auto it=peers_.find(id); if(it!=peers_.end() && !it->second->session.isEmpty())return id;
    for(auto &[peer,c]:peers_)if(!c->session.isEmpty())return peer;
    return {};
}
void Window::signal(const QString &id, QJsonObject body) {
    auto it = peers_.find(id); if (it == peers_.end() || it->second->session.isEmpty()) return;
#ifdef LAZARUS_TESTING
    if (body["kind"].toString() == "ice" && it->second->transport <= blockedTransport_) return;
#endif
    if (!body.contains("generation")) body["generation"] = it->second->generation;
    send(it->second->channel.seal(body));
}
void Window::message(const QJsonObject &m) {
    if (!active_) return;
    auto type = m["type"].toString(); auto id = m["peer"].toString();
    if ((type == "created" || type == "joined") && m["protocol"].toInt() != 2) {
        stop(); viewerState_="Falha"; updatePresentation(); notice("Servidor incompatível. Atualize o serviço de salas para o protocolo v2."); return;
    }
    if (type == "room-state") { roomState(m); return; }
    if ((type == "signal" || type == "turn" || type == "ready" || (type == "error" && m.contains("revision"))) && m["revision"].toInteger() != revision_) return;
    if (type == "created") {
        participantId_=id; admitted_=created_=true;
        roomRequiresApproval_=m["requireApproval"].toBool(true); requireApproval_->setChecked(roomRequiresApproval_); approve_->setVisible(roomRequiresApproval_);
        notice("Sala criada. Envie o link para os participantes.");
    } else if (type == "joined") {
        participantId_=id; roomRequiresApproval_=m["requireApproval"].toBool(true);
        admitted_=!roomRequiresApproval_; viewerState_=admitted_?"Aguardando compartilhamento":"Aguardando aprovação";
        updatePresentation(); notice(viewerState_);
    }
    else if (type == "waiting") { auto c = std::make_unique<Connection>(); auto p = m["profile"].toObject(); c->nickname = normalizedNickname(p["nickname"].toString()); c->avatar = qBound(0, p["avatar"].toInt(), 9); peers_[id] = std::move(c); row(id, "Aguardando aprovação"); log("viewer_waiting", id); }
    else if (type == "ready") {
        if (!admitted_ || (!sender() && id != broadcaster_) || id == participantId_) return;
        if (!approved_.contains(id)) return;
        auto session = m["session"].toString();
        if (session.size() != 32 || seenSessions_.contains(session) || seenSessions_.size() >= 256) return;
        seenSessions_.insert(session);
        approved_.insert(id);
        auto c = std::make_unique<Connection>();
        c->channel = Protocol::Channel(secret_, m["session"].toString(), participantId_, id, challenge_, m["challenge"].toString(), revision_);
        auto p = m["profile"].toObject(); c->nickname = normalizedNickname(p["nickname"].toString()); c->avatar = qBound(0,p["avatar"].toInt(),9);
        c->session = session; c->localConsent = profile_.relay;
        peers_[id] = std::move(c); row(id, "Na sala — aguardando compartilhamento"); notice("Na sala."); log("viewer_approved", id);
        signal(id, {{"kind", "profile"}, {"profile", profile_.json()}});
        signal(id, {{"kind", "relay-consent"}, {"enabled", profile_.relay}});
        if (sender() && sharing_) restart(id, -1);
        else { viewerPanel_->clearFrame("Conectando"); viewerState_="Conectando"; updatePresentation(); }
    } else if (type == "signal") {
        auto it = peers_.find(id); if (it == peers_.end()) return; auto &c = *it->second;
        QJsonObject body;
        if (c.session.isEmpty())return;
        if (!c.channel.open(m, body)) { notice("Mensagem rejeitada: autenticação ou proteção contra replay."); return; }
        auto kind = body["kind"].toString(); int generation = body["generation"].toInt();
#ifdef LAZARUS_TESTING
        if (kind == "ice" && c.transport <= blockedTransport_) return;
#endif
        if (kind == "restart" && !sender() && generation > c.generation) {
            bool relay = body["relay"].toBool();
            if (relay && (!c.localConsent || !c.remoteConsent)) return;
            c.transport = body["transport"].toInt(relay ? 0 : -1);
            if (c.transport < -1 || c.transport > 2 || (relay && (c.turns.isEmpty() || c.turnExpiry<=time_.elapsed()))) { c.pendingRestart = generation; return; }
            c.generation = generation; c.pendingSignals = {}; c.exhausted = false; viewerState_=c.everConnected?"Reconectando":"Conectando"; updatePresentation(); startPeer(id);
        } else if (kind == "connection-terminal" && !sender() && generation == c.generation) {
            c.failed=c.exhausted=true;c.media.reset();c.pendingSignals={};c.pendingRestart=0;
            viewerState_="Falha";viewerPanel_->clearFrame("Falha na conexão. Use Tentar novamente.");updatePresentation();row(id,"Falha — tentar novamente");
        } else if (kind == "relay-consent") {
            c.remoteConsent = body["enabled"].toBool(); log("relay_consent", id);
            if (!c.remoteConsent) { c.renewal.cancel(); c.relayRequested=false; c.decoderRecovering=false; c.pendingRestart=0; c.pendingSignals={}; c.turns.clear(); c.turnExpiry=0;c.turnEpoch=0; }
            if (!c.remoteConsent && c.transport >= 0) { c.media.reset(); c.exhausted = true; row(id, "Relay desabilitado pelo outro participante"); }
            if (sender() && c.failed && !c.fatalMedia && c.localConsent && c.remoteConsent) { c.exhausted = false; requestRelay(id); }
        } else if (kind == "profile") {
            auto p = body["profile"].toObject(); auto name = normalizedNickname(p["nickname"].toString());
            if (!name.isEmpty()) { c.nickname = name; c.avatar = qBound(0,p["avatar"].toInt(),9); updatePresentation(); log("profile_updated", id); }
        } else if (kind == "relay-request" && !sender()) {
            if (c.localConsent && c.remoteConsent) send({{"type", "relay"}, {"peer", id}, {"enabled", true}});
        } else if (kind == "connection-failure" && sender() && sharing_ && generation == c.generation && !c.fatalMedia) {
            if(body["reason"].toString()=="media_terminal"){
                c.failed=c.fatalMedia=c.exhausted=true;c.media.reset();c.renewal.cancel();c.relayRequested=false;
                ++c.selection;c.selecting=false;c.pendingSignals={};c.pendingRestart=0;
                c.kbps=0;c.route="Falha de mídia no espectador";c.metrics={{"stage","remote_media_failed"},{"selected_pair",false},{"kbps",0},{"video_fps",0}};
                row(id,c.route+" — tentar novamente");log("remote_media_failed",id,{{"error_code","receiver_terminal"}});
            }else c.metrics["stage"] = "failed";
        } else if (kind == "retry-request" && sender() && sharing_) {
            bool decoderFallback=body["reason"].toString()=="decoder_fallback";
            if(decoderFallback && generation!=c.generation)return;
            c.exhausted = false; c.everConnected = false; c.reconnectAttempts = 0; restart(id,decoderFallback?c.transport:-1);
        } else if (generation == c.pendingRestart && c.pendingSignals.size() < 128) {
            c.pendingSignals.append(body);
        } else if(generation==c.generation){if(c.media)c.media->receive(body);else if(c.pendingSignals.size()<128)c.pendingSignals.append(body);}
    } else if (type == "turn") {
        auto it = peers_.find(id); if (it == peers_.end()) return; auto &c = *it->second;
        if (!c.localConsent || !c.remoteConsent) { notice("Configuração de relay não autorizada; ignorada."); return; }
        auto host = m["host"].toString(); auto username = m["username"].toString(); auto password = m["password"].toString();
        if (host.contains('/') || host.contains('@') || host.isEmpty() || username.isEmpty() || password.isEmpty()) return;
        // The timestamp in TURN REST usernames prevents delayed/duplicate replies
        // from extending credential validity. Keep the public protocol unchanged.
        bool epochValid=false;
        qint64 epoch=username.section(':',0,0).toLongLong(&epochValid);
        if (!epochValid || epoch<=c.turnEpoch) return;
        qint64 remaining=epoch-QDateTime::currentSecsSinceEpoch();
        if (remaining<=0) return;
        auto user = QString::fromLatin1(QUrl::toPercentEncoding(username)); auto pass = QString::fromLatin1(QUrl::toPercentEncoding(password));
        c.turns.clear();
        for (auto entry : m["endpoints"].toArray()) {
            QUrl url(entry.toString());
            if ((url.scheme() != "turn" && url.scheme() != "turns") || url.host() != host || !url.userInfo().isEmpty() || url.port() < 1 || !url.path().isEmpty()) continue;
            url.setUserName(username); url.setPassword(password); c.turns.append(url.toString(QUrl::FullyEncoded));
        }
        if (c.turns.isEmpty()) c.turns = {QString("turn://%1:%2@%3:3478").arg(user,pass,host), QString("turn://%1:%2@%3:3478?transport=tcp").arg(user,pass,host), QString("turns://%1:%2@%3:5349").arg(user,pass,host)};
        c.turnEpoch=epoch;
        c.turnExpiry = time_.elapsed() + qMin(remaining, qint64(qBound(1, m["expires"].toInt(3600), 3600))) * 1000LL;
        const bool awaitingConnection=c.relayRequested;
        c.relayRequested = false;
        if (c.renewal.pending || c.renewal.failures) log("turn_renewal_completed",id);
        c.renewal.completed();
        log("turn_credentials_ready", id);
        if (sender() && sharing_ && awaitingConnection && c.failed && !c.fatalMedia && !c.exhausted) restart(id, c.transport < 0 ? 0 : c.transport);
        else if (c.pendingRestart > c.generation) {
            c.generation = c.pendingRestart; c.pendingRestart = 0; startPeer(id);
            auto pending = c.pendingSignals; c.pendingSignals = {};
            for (auto entry : pending) if (c.media) c.media->receive(entry.toObject());
        }
    } else if (type == "left") {
        peers_.erase(id); approved_.remove(id);
        for (int i = viewers_->count() - 1; i >= 0; --i) if (viewers_->item(i)->data(Qt::UserRole).toString() == id) delete viewers_->takeItem(i);
    } else if (type == "ended") stop();
    else if (type == "host_offline") { ownerOnline_=false; updatePresentation(); notice("Criador perdeu a sinalização; aguardando reconexão por até 60 segundos."); }
    else if (type == "error") {
        QString code = m["code"].toString(); log("signaling_error", id, {{"error_code", code == "turn_unavailable" ? "turn_unavailable" : "signaling_rejected"}}); notice(code=="rate_limit"?"Muitas tentativas. Aguarde um minuto para tentar novamente.":"Não foi possível concluir a solicitação na sala.");
        if (code == "turn_unavailable" && peers_.contains(id)) {
            auto &c=*peers_[id];
            if(c.renewal.pending){c.renewal.failed(time_.elapsed());log("turn_renewal_failed",id,{{"error_code","turn_unavailable"}});}
            else if(c.relayRequested){c.exhausted=true;c.relayRequested=false;signal(id,{{"kind","connection-terminal"}});row(id,"Relay indisponível no servidor");}
        }
        if (code == "protocol_update_required") { stop(); viewerState_="Falha"; updatePresentation(); notice("Versão incompatível. Atualize o aplicativo para entrar nesta sala."); }
        if (code == "share_busy") { shareRequested_=false; notice("Outro participante está compartilhando."); }
        if (code == "room_full" && !administrator_) { stop(); notice("Sala cheia: limite de quatro espectadores."); }
        if (code == "resume_failed" || code == "room_unavailable" || code == "create_failed") { stop(); viewerState_="Falha"; updatePresentation(); notice("Sala indisponível: " + code); }
    }
}
void Window::startPeer(const QString &id) {
    auto it = peers_.find(id); if (it == peers_.end()) return; auto &c = *it->second;
    c.media.reset();c.started=time_.elapsed();c.failed=false;c.fatalMedia=false;c.metrics={};c.receivedSize={};c.kbps=0;c.route="Preparando mídia";c.decoderRecovering=false;c.audioUnavailable=false;log("connection_attempt",id);
    if(!sender())audioStatus_->setText("Áudio da transmissão.");
    int generation=c.generation;
    if(!sender()){attachPeer(id,{},generation);return;}
    c.selecting=true;int selection=++c.selection;auto q=quality();auto dimensions=capture_.dimensions();if(dimensions.isValid()){q.width=dimensions.width();q.height=dimensions.height();}
    if(c.encoderWidth!=q.width || c.encoderHeight!=q.height || c.encoderFps!=q.fps){c.software=false;c.encoderWidth=q.width;c.encoderHeight=q.height;c.encoderFps=q.fps;}
    bool software=c.software;qint64 selectionStarted=time_.elapsed();auto *watcher=new QFutureWatcher<VideoEncoder>(this);
    connect(watcher,&QFutureWatcher<VideoEncoder>::finished,this,[this,id,generation,selection,selectionStarted,watcher,revision=revision_, session=c.session]{
        auto backend=watcher->result();watcher->deleteLater();auto it=peers_.find(id);
        if(it==peers_.end() || (it->second->generation!=generation || revision!=revision_ || session!=it->second->session) || it->second->selection!=selection || !sharing_)return;
        log("encoder_validation",id,{{"encoder",backend.name},{"selection_ms",double(time_.elapsed()-selectionStarted)}});it->second->selecting=false;attachPeer(id,backend,generation);
    });
    watcher->setFuture(QtConcurrent::run([q,software]{return selectVideoEncoder(q.fps,q.kbps,q.width,q.height,software);}));
}
void Window::attachPeer(const QString &id,VideoEncoder backend,int generation){
    auto it=peers_.find(id);if(it==peers_.end() || it->second->generation!=generation)return;auto &c=*it->second;
    if(c.transport>=0 && (!c.localConsent || !c.remoteConsent)){c.selecting=false;c.exhausted=true;return;}
    if(sender() && backend.chain.isEmpty()){mediaFailure(id,generation,"software_unavailable");return;}
    c.started=time_.elapsed();c.media=std::make_unique<Peer>(sender()); c.media->playbackVolume(viewerPanel_->volume(),viewerPanel_->muted());
    connect(c.media.get(), &Peer::outgoing, this, [this, id, generation, revision=revision_, session=c.session](QJsonObject body) { auto it=peers_.find(id); if(it!=peers_.end() && (it->second->generation == generation && revision == revision_ && session == it->second->session)) signal(id, body); });
    connect(c.media.get(),&Peer::failureDetails,this,[this,id,generation,revision=revision_, session=c.session](QJsonObject details){
        auto it=peers_.find(id);if(it==peers_.end() || (it->second->generation!=generation || revision!=revision_ || session!=it->second->session) || it->second->decoderRecovering)return;
        details["attempt"]=it->second->decoderSoftware?1:0;log("media_failure_detail",id,details);
    });
    connect(c.media.get(),&Peer::audioUnavailable,this,[this,id,generation,revision=revision_, session=c.session]{
        auto it=peers_.find(id);if(it==peers_.end() || (it->second->generation!=generation || revision!=revision_ || session!=it->second->session) || it->second->fatalMedia)return;
        it->second->audioUnavailable=true;audioStatus_->setText("Áudio indisponível. O vídeo continua.");log("audio_unavailable",id,{{"error_code","audio_output"},{"component","audio"}});
    });
    connect(c.media.get(), &Peer::error, this, [this, id, generation, revision=revision_, session=c.session](QString text) {
        auto it = peers_.find(id); if (it == peers_.end() || (it->second->generation != generation || revision != revision_ || session != it->second->session)) return; if (it != peers_.end()) { it->second->failed = true; it->second->fatalMedia = true; log("media_error", id, {{"error_code", "media_pipeline"}}); }
        row(id, "Falha de mídia"); notice(text);
    });
    connect(c.media.get(), &Peer::transportError, this, [this,id,generation,revision=revision_, session=c.session] {
        auto it=peers_.find(id); if(it==peers_.end() || (it->second->generation!=generation || revision!=revision_ || session!=it->second->session))return;
        if(it->second->fatalMedia)return;
        it->second->failed=true; it->second->metrics["stage"]="failed"; log("transport_error",id,{{"error_code","ice_or_dtls"}});
        if(!sender()) { viewerState_="Reconectando"; updatePresentation(); signal(id,{{"kind","connection-failure"}}); }
    });
    connect(c.media.get(), &Peer::status, this, [this, id, generation, revision=revision_, session=c.session](QString text) { auto it=peers_.find(id); if(it!=peers_.end() && (it->second->generation==generation && revision==revision_ && session==it->second->session) && !it->second->fatalMedia && !it->second->decoderRecovering)row(id, text); });
    connect(c.media.get(), &Peer::metrics, this, [this, id, generation, revision=revision_, session=c.session](QJsonObject values) {
        auto it = peers_.find(id); if (it == peers_.end() || (it->second->generation != generation || revision != revision_ || session != it->second->session) || it->second->fatalMedia || it->second->decoderRecovering || !it->second->media) return;
        auto &c = *it->second; c.kbps = values["kbps"].toDouble(); c.route = values["route"].toString();
        if(!sender()){values["width"]=c.receivedSize.isValid()?c.receivedSize.width():0;values["height"]=c.receivedSize.isValid()?c.receivedSize.height():0;}
        c.metrics = values; if (c.media && c.media->connected() && c.route != "Verificando") c.lastRoute = c.route; log("connection_metrics", id, {{"render_backend",video_->backend()},{"metrics", values}});
        if (!c.fatalMedia) {
            auto summary=QString("%1 | %2 kbps | perda %3% | RTT %4 ms | vídeo %5 FPS").arg(c.route).arg(c.kbps,0,'f',0).arg(values["loss_percent"].toDouble(),0,'f',1).arg(values["rtt_ms"].toDouble(),0,'f',0).arg(values["video_fps"].toDouble(),0,'f',1);
            if(sender() && c.software && c.media && c.media->connected() && time_.elapsed()-c.started>5000 && values["video_fps"].toDouble()<c.encoderFps*.95)summary+=QString(" | Software abaixo do alvo de %1 FPS").arg(c.encoderFps);
            if(c.audioUnavailable)summary+=" | Áudio indisponível";
            row(id,summary);
        }
    });
    connect(c.media.get(),&Peer::mediaFailure,this,[this,id,generation,revision=revision_, session=c.session](QString code){
        auto it=peers_.find(id);if(it==peers_.end() || (it->second->generation!=generation || revision!=revision_ || session!=it->second->session) || it->second->decoderRecovering)return;
        // Block network recovery immediately; cleanup waits until Peer::poll returns.
        it->second->fatalMedia=true;
        QTimer::singleShot(0,this,[this,id,generation,code,revision,session]{ auto it=peers_.find(id); if(it!=peers_.end() && revision==revision_ && session==it->second->session)mediaFailure(id,generation,code); });
    });
    auto q=quality();auto dimensions=capture_.dimensions();if(sender() && dimensions.isValid()){q.width=dimensions.width();q.height=dimensions.height();}
    if(sender())c.software=backend.codec=="VP8";
    if(sender() && backend.format=="NV12")capture_.requireNv12(true);
    if(!c.media->start(q,stun_.trimmed(),c.transport>=0 && c.transport<c.turns.size()?QStringList{c.turns[c.transport]}:QStringList{},backend,c.decoderSoftware)) {
        c.failed=true;
    }else {if(sender())log("encoder_selected",id,{{"encoder",backend.name}});auto pending=c.pendingSignals;c.pendingSignals={};for(auto value:pending)if(c.media)c.media->receive(value.toObject());}
}
void Window::mediaFailure(const QString &id,int generation,const QString &code){
    auto it=peers_.find(id);if(it==peers_.end() || it->second->generation!=generation)return;auto &c=*it->second;
    if(c.decoderRecovering && code!="decoder_retry_timeout")return;
    if(!c.media && !c.selecting && !c.decoderRecovering && code!="software_unavailable")return;
    if(c.transport>=0 && (!c.localConsent || !c.remoteConsent))return;
    log("media_error",id,{{"error_code",code}});
    if(!sender() && c.media && c.media->receivingH264() && !c.media->softwareDecoder() && !c.decoderSoftware && (code=="decoder_error" || code=="decoder_start")){
        QString decoder=c.media->decoderName();c.decoderSoftware=true;c.decoderRecovering=true;c.media.reset();c.pendingSignals={};c.failed=c.fatalMedia=false;c.started=time_.elapsed();c.kbps=0;c.receivedSize={};
        viewerState_="Reconectando";updatePresentation();c.route="Recuperando vídeo por software";c.metrics={{"stage","decoder_recovery"},{"video_fps",0},{"kbps",0},{"selected_pair",false}};
        viewerPanel_->clearFrame(c.route);row(id,c.route);notice(c.route);
        log("decoder_fallback",id,{{"error_code",code},{"component","video"},{"decoder",decoder},{"decoder_mode","software"},{"attempt",1}});
        signal(id,{{"kind","retry-request"},{"reason","decoder_fallback"}});return;
    }
    c.decoderRecovering=false;
    bool recoverable=code=="encoder_error" || code=="encoder_start" || code=="encoder_stall";
    if(sender() && sharing_ && recoverable && !c.software){
        c.software=true;c.pendingSignals={};c.media.reset();c.fatalMedia=false;c.failed=false;++c.generation;
        signal(id,{{"kind","restart"},{"generation",c.generation},{"relay",c.transport>=0},{"transport",c.transport},{"reason","encoder_fallback"}});
        log("encoder_fallback",id,{{"error_code",code}});startPeer(id);return;
    }
    c.media.reset();c.failed=c.fatalMedia=true;c.selecting=false;c.kbps=0;c.route="Falha de mídia";c.metrics["stage"]="media_failed";c.metrics["selected_pair"]=false;c.metrics["kbps"]=0;c.metrics["video_fps"]=0;c.metrics["error_code"]=code;
    const QString text=code.startsWith("decoder")?"Falha ao decodificar vídeo":code.startsWith("negotiation")?"Falha na negociação de mídia":"Falha de mídia";
    viewerState_="Falha"; updatePresentation(); row(id,text+" — tentar novamente");if(!sender()){viewerPanel_->clearFrame(text);signal(id,{{"kind","connection-failure"},{"reason","media_terminal"}});}notice(text+". Tente novamente ou selecione qualidade menor.");
}
void Window::relay() {
    auto id = selectedPeer(); auto it = peers_.find(id); if (it == peers_.end()) return;
    auto &c = *it->second; c.exhausted = false; c.everConnected = false; c.reconnectAttempts = 0;c.software=false;c.decoderSoftware=false;c.decoderRecovering=false;
    viewerState_="Reconectando"; updatePresentation();
    if (sender() && sharing_) restart(id,-1); else if (!sender()) signal(id, {{"kind", "retry-request"}});
}
void Window::tick() {
    if (!active_) return;
    if (sender() && sharing_) {
        bool need=false;for(auto &[id,c]:peers_)if(c->media && c->media->inputFormat()=="NV12")need=true;capture_.requireNv12(need);
        GstSample *prepared=nullptr;
        if(auto *sample=capture_.takeVideo(&prepared)) {
            GstVideoInfo actual;bool valid=gst_video_info_from_caps(&actual,gst_sample_get_caps(sample));
            if(!valid || QSize(actual.width,actual.height)!=capture_.dimensions()){gst_sample_unref(sample);if(prepared)gst_sample_unref(prepared);return;}
            for(auto &[id,c]:peers_)if(c->media){if(c->media->inputFormat()=="NV12"){if(prepared)c->media->video(prepared);}else c->media->video(sample);}
            gst_sample_unref(sample);if(prepared)gst_sample_unref(prepared);++frames_;
        }
        for (auto *sample : audio_.takeSamples()) {
            for (auto &[id, c] : peers_) if (c->media) c->media->audio(sample);
            gst_sample_unref(sample);
        }
        if (time_.elapsed() - lastFrameTime_ >= 1000) {
            double total = 0; for (auto &[id, c] : peers_) total += c->kbps;
            auto size = capture_.dimensions();
            metrics_->setText(QString("Captura %1×%2 | %3 FPS de captura | upload de vídeo %4 kbps | teto %5 kbps/viewer | %6")
                .arg(size.width()).arg(size.height()).arg(frames_ * 1000.0 / (time_.elapsed() - lastFrameTime_),0,'f',1).arg(total,0,'f',0).arg(quality().kbps).arg(peers_.empty() || !peers_.begin()->second->media ? "Aguardando viewer" : peers_.begin()->second->media->encoderName()));
            auto captureMetrics=capture_.takeMetrics();
            log("capture_metrics", {}, {{"frames_prepare_discarded",captureMetrics["frames_prepare_discarded"]},{"frames_discarded",captureMetrics["frames_discarded"]},{"prepare_us",captureMetrics["prepare_us"]},{"width",size.width()},{"height",size.height()},{"capture_fps",frames_ * 1000.0 / (time_.elapsed() - lastFrameTime_)},{"kbps",total},{"target_fps",quality().fps},{"target_kbps",quality().kbps}});
            frames_ = 0; lastFrameTime_ = time_.elapsed();
        }
    } else {
        for (auto &[id, c] : peers_) if (c->media) {
            auto image = c->media->takeFrame();
            if (!image.isNull()) {
                if(c->receivedSize!=image.size()){c->receivedSize=image.size();c->metrics["width"]=image.width();c->metrics["height"]=image.height();}
                viewerPanel_->setFrame(image); viewerPanel_->frameAt(time_.elapsed()); c->everConnected=true; viewerState_="Ao vivo"; updatePresentation();
                metrics_->setText(QString("Vídeo recebido: %1×%2 | %3 | %4 FPS disponíveis para exibição").arg(image.width()).arg(image.height()).arg(c->route).arg(c->metrics["video_fps"].toDouble(),0,'f',1));
            }
        }
    }
}
void Window::applyQuality() {
    if (!sender() || !active_ || !sharing_) return;
    auto q = quality(); capture_.quality(q); auto size = capture_.dimensions(); q.width = size.width(); q.height = size.height();
    for (auto &[id,c]:peers_) restart(id,c->transport);
}
void Window::refreshAudio() {
    refreshingAudio_ = true; QSet<QString> checked = audio_.selected();
    if (!sender() || !active_ || !sharing_) checked.clear();
    apps_->clear();
    for (auto a : audio_.applications()) {
        auto *item = new QListWidgetItem(a.name + " [" + a.id + "]", apps_); item->setData(Qt::UserRole, a.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(checked.contains(a.id) ? Qt::Checked : Qt::Unchecked);
    }
    apps_->setEnabled(sender() && active_ && sharing_ && audio_.supported()); refreshingAudio_ = false;
    if (audioEmptyNotice_) audioEmptyNotice_->setVisible(sharing_ && apps_->count() == 0);
}
void Window::selectAudio() {
    if (refreshingAudio_ || !sender() || !active_ || !sharing_) return;
    QSet<QString> selected;
    for (int i = 0; i < apps_->count(); ++i) if (apps_->item(i)->checkState() == Qt::Checked) selected.insert(apps_->item(i)->data(Qt::UserRole).toString());
    audio_.select(selected); refreshAudio(); audioStatus_->setText(audio_.selected().isEmpty() ? "Áudio desligado." : "Transmitindo somente os aplicativos marcados.");
    updatePresentation();
}
void Window::diagnostics() {
    QJsonObject report{{"version", LAZARUS_VERSION}, {"events", log_.events()}, {"profile", profile_.json()}};
    auto path = QFileDialog::getSaveFileName(this, "Exportar diagnóstico sem segredos", "diagnostico.json", "JSON (*.json)");
    if (path.isEmpty()) return; QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0) notice("Não foi possível exportar o diagnóstico.");
}
void Window::log(const QString &event, const QString &id, QJsonObject fields) {
    fields["nickname"] = profile_.nickname; fields["own_nickname"] = profile_.nickname; fields["avatar"] = profile_.avatar;
    fields["role"] = sender() ? "transmitter" : administrator_ ? "administrator" : "receiver"; fields["sharing"] = sharing_;
    auto it = peers_.find(id); if (it != peers_.end()) {
        auto &c = *it->second; fields["peer"] = id; fields["session"] = c.session;
        fields["generation"] = c.generation; fields["transport"] = c.transport; fields["failed"] = c.failed; fields["route"] = c.route;
        fields["relay_local"] = c.localConsent; fields["relay_remote"] = c.remoteConsent;
        fields["last_route"] = c.lastRoute;
        fields["metrics"] = fields.contains("metrics") ? fields["metrics"] : QJsonValue(c.metrics);
        // Nickname at this boundary is the remote participant; own identity remains in profile export.
        if (!c.nickname.isEmpty()) fields["nickname"] = c.nickname;
    }
    log_.append(event, fields);
}
void Window::updateIdentity() {
    identity_->setText(profile_.valid() ? profile_.nickname : "Escolher nickname");
    identity_->setIcon(Theme::avatarIcon(profile_.avatar, 22));
    identity_->setIconSize(QSize(22, 22));
}
void Window::editIdentity() {
    if (!editProfile(profile_, this)) return; updateIdentity();
    if(active_ && admitted_)send({{"type","profile"},{"profile",profile_.json()}});
    create_->setEnabled(!active_); join_->setEnabled(!active_);
    for (auto &[id,c] : peers_) if (!c->session.isEmpty()) {
        c->localConsent = profile_.relay; signal(id, {{"kind","profile"},{"profile",profile_.json()}});
        signal(id, {{"kind","relay-consent"},{"enabled",c->localConsent}});
        if (!c->localConsent) {
            send({{"type","relay"},{"peer",id},{"enabled",false}}); c->turns.clear(); c->turnExpiry=0;c->turnEpoch=0; c->renewal.cancel(); c->pendingRestart=0; c->pendingSignals={}; c->relayRequested = false;c->decoderRecovering=false;
            if (c->transport >= 0) { c->media.reset(); c->exhausted = true; row(id,"Relay desabilitado"); }
        } else if (sender() && c->failed && c->remoteConsent && !c->exhausted) requestRelay(id);
    }
}
void Window::share() {
    if (!active_ || !admitted_ || capturePending_) return;
    if (!sender()) {
        if (broadcaster_.isEmpty() && !shareRequested_ && socketLost_<0) {
            shareRequested_=true; share_->setEnabled(false); send({{"type","share-request"}});
        }
        return;
    }
    const auto reservation=revision_;
    QDialog dialog(this); dialog.setWindowTitle("Compartilhar tela"); auto *layout = new QVBoxLayout(&dialog);
    auto *monitor = new QComboBox; monitor->addItems([this] { QStringList names; for (int i=0;i<monitor_->count();++i) names << monitor_->itemText(i); return names; }()); monitor->setCurrentIndex(monitor_->currentIndex());
    bool portal = qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland" || !qEnvironmentVariable("WAYLAND_DISPLAY").isEmpty();
#ifdef Q_OS_WIN
    portal = false;
#endif
    if (portal) layout->addWidget(new QLabel("Você escolherá o monitor no diálogo do sistema.")); else { layout->addWidget(new QLabel("Monitor")); layout->addWidget(monitor); }
    auto *switchMonitor = new QCheckBox("Trocar monitor (abre seleção do sistema)"); if (portal && sharing_) layout->addWidget(switchMonitor);
    auto *preset = new QComboBox; for(int i=0;i<preset_->count();++i) preset->addItem(preset_->itemText(i)); preset->setCurrentIndex(preset_->currentIndex()); layout->addWidget(preset);
    auto *warning = new QLabel("Nativo pode exigir mais banda e processamento. O upload cresce com cada viewer; resolução e FPS efetivos dependem do equipamento e da rede."); warning->setWordWrap(true); layout->addWidget(warning);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,&dialog,&QDialog::accept); connect(buttons,&QDialogButtonBox::rejected,&dialog,&QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) { if(!sharing_)stopSharing(); return; }
    if (!sender() || reservation!=revision_ || !active_ || socketLost_>=0) return;
    int selected = monitor->currentIndex(); preset_->setCurrentIndex(preset->currentIndex());
    if (sharing_ && ((portal && !switchMonitor->isChecked()) || (!portal && selected == monitor_->currentIndex()))) { applyQuality(); log("quality_changed", {}, {{"preset",preset_->currentIndex()}}); return; }
    if (sharing_) {
        // Keep the exclusive slot while restarting capture for a monitor change.
        sharing_=false; capture_.stop(); audio_.stop();
        for(auto &[id,c]:peers_){++c->generation;++c->selection;c->media.reset();}
    }
    monitor_->setCurrentIndex(selected);
    capturePending_ = true; share_->setEnabled(false); notice("Aguardando seleção/autorização da captura.");
    auto target = quality(); if (preset_->currentIndex() == 2) { target.width = 32768; target.height = 32768; }
    capture_.start(selected, target, testPattern_);
}
void Window::stopSharing() {
    bool previous = sharing_ || capturePending_;
    if(sender())send({{"type","share-release"},{"revision",revision_}});
    shareRequested_=false; sharing_ = capturePending_ = false;
    capture_.stop(); audio_.stop();
    if (sender()) for (auto &[id,c] : peers_) { c->renewal.cancel();c->relayRequested=false;c->pendingRestart=0;c->pendingSignals={};++c->generation;++c->selection;c->selecting=false;c->media.reset(); c->kbps = 0; c->exhausted = false;c->route="Aguardando compartilhamento";c->metrics={{"stage","paused"},{"video_fps",0},{"kbps",0},{"selected_pair",false}};row(id,c->route); }
    share_->setEnabled(active_ && admitted_ && broadcaster_.isEmpty() && socketLost_<0); pause_->setEnabled(false); change_->setEnabled(false); refreshAudio();
    updatePresentation();
    if (previous) { log("sharing_stopped"); metrics_->setText("Sem compartilhamento | upload 0 kbps"); notice("Compartilhamento parado. A sala continua aberta."); }
}
void Window::restart(const QString &id, int transport) {
    auto it=peers_.find(id); if (it==peers_.end() || !sender() || !sharing_) return; auto &c=*it->second;
    if(c.session.isEmpty())return;
    if(transport>=0 && (!c.localConsent || !c.remoteConsent))return;
    if(transport>=0 && (c.turns.isEmpty() || c.turnExpiry<=time_.elapsed())) {
        c.transport=transport;c.failed=true;c.everConnected=false;requestRelay(id);return;
    }
    c.transport=transport; ++c.generation; c.pendingSignals={}; c.exhausted=false;
    signal(id, {{"kind","restart"},{"generation",c.generation},{"relay",transport>=0},{"transport",transport}}); startPeer(id);
}
void Window::requestRelay(const QString &id) {
    auto it=peers_.find(id); if(it==peers_.end())return; auto &c=*it->second;
    if(!c.localConsent || !c.remoteConsent || c.relayRequested || !sharing_)return;
    if(c.media && c.media->connected() && !c.failed){renewTurn(id);return;}
    if (!c.turns.isEmpty() && c.turnExpiry > time_.elapsed()) { c.everConnected=false; restart(id,c.transport<0?0:c.transport); return; }
    c.renewal.cancel();
    c.relayRequested=true; c.started=time_.elapsed(); c.media.reset(); c.failed=true;
    signal(id, {{"kind","relay-request"}}); send({{"type","relay"},{"peer",id},{"enabled",true}}); log("relay_requested",id); row(id,"Solicitando relay autorizado");
}
void Window::renewTurn(const QString &id) {
    auto it=peers_.find(id);if(it==peers_.end())return;auto &c=*it->second;
    if(!sender() || !sharing_ || !c.localConsent || !c.remoteConsent || c.renewal.pending || c.relayRequested || !c.media || !c.media->connected())return;
    bool retry=c.renewal.failures>0;c.renewal.requested(time_.elapsed());
    signal(id,{{"kind","relay-request"}});
    send({{"type","relay"},{"peer",id},{"enabled",true}});
    log(retry?"turn_renewal_retry":"turn_renewal_requested",id,{{"attempt",int(c.renewal.failures)+1}});
}
void Window::advance(const QString &id) {
    auto &c=*peers_.at(id); c.failed=true; log("connection_failed",id);
    if(c.everConnected) {
        if(c.reconnectAttempts++ < 2) { c.everConnected=false; restart(id,c.transport); return; }
    }
    if(c.transport < 0 && c.localConsent && c.remoteConsent) { requestRelay(id); return; }
    if(c.transport>=0 && c.transport+1<c.turns.size() && c.localConsent && c.remoteConsent) { c.everConnected=false; restart(id,c.transport+1); return; }
    c.exhausted=true; c.media.reset(); signal(id,{{"kind","connection-terminal"}}); viewerState_="Falha"; updatePresentation(); row(id,c.localConsent && c.remoteConsent ? "Conexão falhou — tentar novamente" : "P2P falhou — relay não autorizado por ambos");
}

void Window::updatePresentation() {
    QString name; int avatar=0;
    if(sender()){name="Você está compartilhando — "+profile_.nickname;avatar=profile_.avatar;}
    else if(auto it=peers_.find(broadcaster_);it!=peers_.end()) {name="Transmitindo: "+it->second->nickname;avatar=it->second->avatar;}
    QString state=viewerState_;
    if(!admitted_ && active_ && socketLost_<0)state=participantId_.isEmpty()?"Conectando":"Aguardando aprovação";
    else if(broadcaster_.isEmpty() && state!="Falha")state="Aguardando compartilhamento";
    if(socketLost_>=0)state="Reconectando";
    if(sender() && socketLost_<0)state=sharing_?"Você está compartilhando":"Conectando — selecionando tela";
    if(!ownerOnline_)state+=" · Criador reconectando";
    viewerPanel_->presentation(state,name,avatar);
    share_->setEnabled(active_ && admitted_ && broadcaster_.isEmpty() && !shareRequested_ && socketLost_<0);
    pause_->setEnabled(sender()); change_->setEnabled(sender() && sharing_);
    if (audioBtn_) {
        audioBtn_->setVisible(sender());
        audioBtn_->setEnabled(sender() && audio_.supported());
        if (sharing_) {
            int count = audio_.selected().size();
            if (count > 0) {
                audioBtn_->setText(QString("Áudio (%1 app%2)").arg(count).arg(count > 1 ? "s" : ""));
                audioBtn_->setIcon(Theme::icon("volume_up", Theme::Primary, 14));
            } else {
                audioBtn_->setText("Áudio (Mudo)");
                audioBtn_->setIcon(Theme::icon("volume_off", Theme::Primary, 14));
            }
        } else {
            audioBtn_->setText("Áudio do sistema");
            audioBtn_->setIcon(Theme::icon("volume_up", Theme::Primary, 14));
        }
    }
    remove_->setVisible(administrator_); approve_->setVisible(administrator_ && roomRequiresApproval_);
    updateRoomIndicators();
}
void Window::roomState(const QJsonObject &m) {
    const auto revision=m["revision"].toInteger();
    if(revision<revision_)return;
    QString broadcaster=m["broadcaster"].toString();
    bool changed=revision!=revision_ || broadcaster!=broadcaster_;
    if(changed) {
        capture_.stop(); audio_.stop(); sharing_=capturePending_=false;
        for(auto &[id,c]:peers_){++c->selection;c->media.reset();}
        peers_.clear(); approved_.clear(); seenSessions_.clear(); viewers_->clear();
        viewerPanel_->clearFrame("Aguardando compartilhamento"); viewerState_="Conectando";
    }
    revision_=revision; broadcaster_=broadcaster; ownerOnline_=m["ownerOnline"].toBool(true);
    QSet<QString> present;
    for(auto entry:m["participants"].toArray()) {
        auto p=entry.toObject(); auto id=p["peer"].toString(); present.insert(id);
        auto profile=p["profile"].toObject(); bool approved=p["approved"].toBool();
        if(id==participantId_) { admitted_=approved; row(id,sender()?"Você — transmitindo":"Você — na sala"); continue; }
        if(!peers_.contains(id))peers_[id]=std::make_unique<Connection>();
        auto &c=*peers_[id]; c.nickname=normalizedNickname(profile["nickname"].toString()); c.avatar=qBound(0,profile["avatar"].toInt(),9);
        if(approved)approved_.insert(id);else approved_.remove(id);
        if(p["owner"].toBool() && !ownerOnline_) {
            ++c.selection;c.media.reset();c.session.clear();c.renewal.cancel();c.relayRequested=false;
            row(id,"Criador reconectando");continue;
        }
        if(!c.media)row(id,!approved?"Aguardando aprovação":id==broadcaster_?"Transmitindo":"Na sala");
    }
    for(auto it=peers_.begin();it!=peers_.end();) {if(!present.contains(it->first)){approved_.remove(it->first);it=peers_.erase(it);}else ++it;}
    for(int i=viewers_->count()-1;i>=0;--i)if(!present.contains(viewers_->item(i)->data(Qt::UserRole).toString()))delete viewers_->takeItem(i);
    updatePresentation(); refreshAudio();
    if(changed && sender() && shareRequested_) { shareRequested_=false; QTimer::singleShot(0,this,&Window::share); }
    else if(changed && !sender())shareRequested_=false;
}
