#include "profile.h"
#include <QSettings>
#include <QTextBoundaryFinder>
#include <QDialog>
#include <QDialogButtonBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QCheckBox>
#include <QButtonGroup>
QString normalizedNickname(QString value) {
    value = value.normalized(QString::NormalizationForm_C).trimmed();
    for (auto ch : value) if (ch.category() == QChar::Other_Control || ch.category() == QChar::Other_Format || ch.category() == QChar::Separator_Line || ch.category() == QChar::Separator_Paragraph) return {};
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, value);
    int count = 0; while (finder.toNextBoundary() >= 0) ++count;
    return count > 0 && count <= 10 && value.size() <= 80 ? value : QString();
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
    QDialog dialog(parent); dialog.setWindowTitle("Seu perfil — salvo somente neste PC");
    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(new QLabel("Nickname (até 10 caracteres):"));
    auto *name = new QLineEdit(profile.nickname); name->setObjectName("nickname"); name->setMaxLength(80); layout->addWidget(name);
    auto *colors = new QHBoxLayout; auto *group = new QButtonGroup(&dialog);
    for (int i = 0; i < 10; ++i) {
        auto *button = new QPushButton; button->setCheckable(true); button->setFixedSize(32,32);
        button->setAccessibleName(QString("Avatar %1").arg(i + 1));
        button->setStyleSheet(QString("QPushButton {background:%1; border-radius:14px;} QPushButton:checked {border:3px solid white;}").arg(avatarColor(i).name()));
        group->addButton(button, i); colors->addWidget(button); if (i == profile.avatar) button->setChecked(true);
    }
    layout->addLayout(colors);
    auto *relay = new QCheckBox("Permitir relay automático quando o P2P falhar"); relay->setChecked(profile.relay); layout->addWidget(relay);
    auto *explanation = new QLabel("Se ambos permitirem, vídeo e áudio criptografados poderão passar pela VPS do Lazarus Share. O servidor processa IPs e volume de tráfego. Você pode mudar esta escolha clicando no seu nome.\nDiagnósticos técnicos ficam somente neste PC por até 7 dias e 10 MB, sem envio automático.");
    explanation->setWordWrap(true); layout->addWidget(explanation);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); layout->addWidget(buttons);
    auto validate = [=] { buttons->button(QDialogButtonBox::Save)->setEnabled(!normalizedNickname(name->text()).isEmpty() && group->checkedId() >= 0); };
    QObject::connect(name, &QLineEdit::textChanged, &dialog, validate); QObject::connect(group, &QButtonGroup::idClicked, &dialog, validate); validate();
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return false;
    profile = {normalizedNickname(name->text()), group->checkedId(), relay->isChecked()}; profile.save(); return true;
}
