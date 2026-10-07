#include "profile.h"
#include "theme.h"
#include <QSettings>
#include <QTextBoundaryFinder>
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QButtonGroup>
#include <QScrollArea>
QString normalizedNickname(QString value) {
    value = value.normalized(QString::NormalizationForm_C).trimmed();
    for (auto ch : value) if (ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format || ch.category() == QChar::Separator_Line || ch.category() == QChar::Separator_Paragraph) return {};
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, value);
    int count = 0; while (finder.toNextBoundary() >= 0) ++count;
    return count > 0 && count <= 12 && value.size() <= 80 ? value : QString();
}
bool Profile::valid() const { return !normalizedNickname(nickname).isEmpty() && avatar >= 0 && avatar < 10; }
QJsonObject Profile::json() const { return {{"nickname", normalizedNickname(nickname)}, {"avatar", avatar}}; }
Profile Profile::load() { QSettings s; return {normalizedNickname(s.value("profile/nickname").toString()), s.value("profile/avatar", 0).toInt(), s.value("profile/relay", false).toBool()}; }
void Profile::save() const { QSettings s; s.setValue("profile/nickname", normalizedNickname(nickname)); s.setValue("profile/avatar", avatar); s.setValue("profile/relay", relay); s.sync(); }
QColor avatarColor(int index) {
    static const char *colors[] = {"#EF5350", "#EC407A", "#AB47BC", "#5C6BC0", "#42A5F5", "#26C6DA", "#26A69A", "#66BB6A", "#FFA726", "#8D6E63"};
    return QColor(colors[qBound(0, index, 9)]);
}
bool editProfile(Profile &profile, QWidget *parent) {
    QDialog dialog(parent);
    dialog.setWindowTitle("Seu Perfil — Lazarus Share");
    dialog.setObjectName("profileDialog");
    dialog.setStyleSheet("QDialog#profileDialog, QDialog { background-color: #FCFAEF; } QWidget#profileContent { background-color: #FCFAEF; } QScrollArea { background-color: #FCFAEF; border: none; }");
    dialog.setAutoFillBackground(true);
    QPalette pal = dialog.palette();
    pal.setColor(QPalette::Window, QColor("#FCFAEF"));
    dialog.setPalette(pal);
    dialog.resize(500, 540);
    dialog.setMinimumSize(420, 420);

    auto *mainLayout = new QVBoxLayout(&dialog);
    mainLayout->setContentsMargins(18, 18, 18, 18);
    mainLayout->setSpacing(12);

    auto *scroll = new QScrollArea(&dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet("QScrollArea { background-color: #FCFAEF; border: none; }");

    auto *content = new QWidget(scroll);
    content->setObjectName("profileContent");
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 6, 6, 6);
    layout->setSpacing(12);

    auto *nameLabel = new QLabel("Seu nome ou apelido (até 12 caracteres):", content);
    nameLabel->setStyleSheet("font-weight: 700; color: #1B1C16;");
    layout->addWidget(nameLabel);

    auto *name = new QLineEdit(profile.nickname, content);
    name->setObjectName("nickname");
    name->setMaxLength(80);
    name->setPlaceholderText("Ex: Pedro, Ana, Lucas...");
    layout->addWidget(name);

    auto *avatarLabel = new QLabel("Escolha seu avatar:", content);
    avatarLabel->setStyleSheet("font-weight: 600; color: #1B1C16; margin-top: 4px;");
    layout->addWidget(avatarLabel);

    auto *grid = new QGridLayout;
    grid->setSpacing(8);
    auto *group = new QButtonGroup(&dialog);
    for (int i = 0; i < 10; ++i) {
        auto *button = new QPushButton(content);
        button->setCheckable(true);
        button->setFixedSize(54, 54);
        button->setIcon(Theme::avatarIcon(i, 36));
        button->setIconSize(QSize(36, 36));
        button->setToolTip(Theme::avatarName(i));
        button->setAccessibleName(Theme::avatarName(i));
        button->setStyleSheet(
            "QPushButton { background-color: #FFFFFF; border: 2px solid #EAE8DE; border-radius: 12px; } "
            "QPushButton:hover { border-color: #0FFCBE; background-color: #F0EEE3; } "
            "QPushButton:checked { border: 2.5px solid #000000; background-color: #EAE8DE; } "
        );
        group->addButton(button, i);
        grid->addWidget(button, i / 5, i % 5);
        if (i == profile.avatar) button->setChecked(true);
    }
    layout->addLayout(grid);

    auto *relay = new QCheckBox("Permitir conexão assistida se a ligação direta falhar", content);
    relay->setChecked(profile.relay);
    relay->setStyleSheet("font-weight: 600; color: #1B1C16; margin-top: 8px;");
    layout->addWidget(relay);

    auto *explanation = new QLabel(
        "Se ambos os participantes autorizarem, a transmissão poderá utilizar o servidor seguro do Lazarus Share como intermediário para contornar bloqueios de rede local ou de roteador.\n\n"
        "Todo o vídeo e áudio continuam totalmente criptografados de ponta a ponta. Você pode alterar essa escolha a qualquer momento clicando no seu nome.",
        content
    );
    explanation->setWordWrap(true);
    explanation->setStyleSheet("color: #4C4546; font-size: 11px; background-color: #F6F4E9; border: 1px solid #EAE8DE; border-radius: 8px; padding: 10px;");
    layout->addWidget(explanation);
    layout->addStretch();

    scroll->setWidget(content);
    mainLayout->addWidget(scroll, 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    if (buttons->button(QDialogButtonBox::Save)) {
        buttons->button(QDialogButtonBox::Save)->setText("Salvar");
        buttons->button(QDialogButtonBox::Save)->setStyleSheet(
            "QPushButton { background-color: #000000; color: #FFFFFF; font-weight: 700; border-radius: 8px; padding: 8px 18px; } "
            "QPushButton:hover { background-color: #1B1B1B; } "
            "QPushButton:disabled { background-color: #EAE8DE; color: #A09E94; }"
        );
    }
    if (buttons->button(QDialogButtonBox::Cancel)) {
        buttons->button(QDialogButtonBox::Cancel)->setText("Cancelar");
        buttons->button(QDialogButtonBox::Cancel)->setStyleSheet(
            "QPushButton { background-color: #F0EEE3; color: #1B1C16; font-weight: 600; border: 1px solid #E4E3D8; border-radius: 8px; padding: 8px 18px; } "
            "QPushButton:hover { background-color: #EAE8DE; }"
        );
    }
    mainLayout->addWidget(buttons);

    auto validate = [=] {
        if (buttons->button(QDialogButtonBox::Save)) {
            buttons->button(QDialogButtonBox::Save)->setEnabled(!normalizedNickname(name->text()).isEmpty() && group->checkedId() >= 0);
        }
    };
    QObject::connect(name, &QLineEdit::textChanged, &dialog, validate);
    QObject::connect(group, &QButtonGroup::idClicked, &dialog, validate);
    validate();
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) return false;
    profile = {normalizedNickname(name->text()), group->checkedId(), relay->isChecked()};
    profile.save();
    return true;
}
