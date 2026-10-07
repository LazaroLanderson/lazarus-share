#include "window.h"
#include "update.h"
#include "update_transaction.h"
#include "theme.h"
#include <QApplication>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QProgressBar>
#include <QTextBrowser>
#include <QFileInfo>
#include <QPointer>

void Window::setupUpdates(QVBoxLayout *layout) {
    auto *banner = new QPushButton("Nova atualização disponível · Ver detalhes", this);
    banner->setObjectName("updateBanner"); banner->setFlat(true);
    banner->setStyleSheet("QPushButton { text-align: left; padding: 6px 12px; background-color: #14FDBF; color: #002116; font-weight: 700; border: 1px solid #0FFCBE; border-radius: 6px; } QPushButton:hover { background-color: #38FFC3; }");
    layout->insertWidget(0, banner); banner->hide();
    auto *client = new UpdateClient(this);
    connect(client, &UpdateClient::available, banner, &QWidget::show);
    connect(banner, &QPushButton::clicked, this, [this, client] {
        QDialog dialog(this);
        dialog.setWindowTitle("Atualização do Lazarus Share");
        dialog.setObjectName("updateDialog");
        Theme::setupDialog(&dialog);
        dialog.resize(500, 370);
        auto *layout = new QVBoxLayout(&dialog);
        const auto release = client->release();
        layout->addWidget(new QLabel(QString("Versão atual: %1   ·   Nova versão: %2\nDownload: %3 MB")
            .arg(QCoreApplication::applicationVersion(), release.version).arg(double(release.size) / 1048576, 0, 'f', 1)));
        auto *notes = new QTextBrowser; notes->setOpenLinks(false); notes->setOpenExternalLinks(false);
        notes->setPlainText(release.notes.isEmpty() ? "Esta versão não tem notas de atualização." : release.notes);
        layout->addWidget(notes);
        auto *progress = new QProgressBar; progress->setRange(0, 100); layout->addWidget(progress);
        auto *status = new QLabel; status->setWordWrap(true); layout->addWidget(status);
        auto *buttons = new QHBoxLayout; auto *action = new QPushButton; auto *cancel = new QPushButton;
        buttons->addWidget(action); buttons->addWidget(cancel); layout->addLayout(buttons);
        const bool portable = !Updates::portablePath().isEmpty();
        auto refresh = [=] {
            action->setText(client->ready() ? "Reiniciar e atualizar" : "Baixar atualização");
            action->setEnabled(portable && !client->downloading());
            cancel->setText(client->downloading() ? "Cancelar download" : "Agora não");
            if (client->ready()) { progress->setValue(100); status->setText("Atualização pronta para instalar."); }
        };
        refresh();
        if (!portable) status->setText("Build de desenvolvimento: a atualização automática está disponível nos pacotes EXE e AppImage.");
        connect(client, &UpdateClient::progress, &dialog, [=](qint64 received, qint64 total) { progress->setValue(int(received * 100 / total)); });
        connect(client, &UpdateClient::downloaded, &dialog, refresh);
        connect(client, &UpdateClient::error, &dialog, [=](const QString &error) { refresh(); status->setText(error); });
        connect(client, &UpdateClient::cancelled, &dialog, [=] { progress->setValue(0); refresh(); status->setText("Download cancelado."); });
        connect(cancel, &QPushButton::clicked, &dialog, [=, this, &dialog] {
            if (client->downloading()) client->cancel(); else dialog.reject();
        });
        auto pending = std::make_shared<QString>();
        connect(&dialog, &QDialog::finished, &dialog, [client, pending] {
            client->cancel(); if (!pending->isEmpty()) Updates::abandon(*pending);
        });
        connect(action, &QPushButton::clicked, &dialog, [=, this, &dialog] {
            if (!client->ready()) { status->setText("Baixando atualização…"); client->download(); refresh(); return; }
            if (active_ && QMessageBox::question(&dialog, "Encerrar sala para atualizar",
                "A atualização precisa fechar o aplicativo. Deseja encerrar a sala e reiniciar agora?",
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
            QString error;
            const auto journal = Updates::prepare(client->downloadedFile(), release.size, release.sha256, release.version, &error);
            if (journal.isEmpty()) { status->setText(error); return; }
            *pending = journal;
            if (!Updates::startHelper(journal)) {
                Updates::abandon(journal); pending->clear();
                status->setText("Não foi possível iniciar o atualizador. O aplicativo continua aberto."); return;
            }
            action->setEnabled(false); cancel->setEnabled(false);
            status->setText("Preparando reinicialização segura…");
            auto *timer = new QTimer(&dialog); timer->setInterval(50);
            auto attempts = std::make_shared<int>(0);
            connect(timer, &QTimer::timeout, &dialog, [=, this, &dialog] {
                const auto ready = Updates::readJson(QFileInfo(journal).absolutePath() + "/ready.json");
                if (ready["id"] == Updates::readJson(journal)["id"] && !ready["id"].isUndefined()) {
                    timer->stop(); pending->clear(); stop(); dialog.accept(); QCoreApplication::quit();
                } else if (++*attempts >= 200) {
                    timer->stop(); Updates::abandon(journal); pending->clear(); refresh(); cancel->setEnabled(true);
                    status->setText("O atualizador não ficou pronto. A versão atual continua aberta; tente novamente.");
                }
            });
            timer->start();
        });
        dialog.exec();
    });
#if !defined(LAZARUS_TESTING)
    if (!QCoreApplication::arguments().contains("--smoke-test")) QTimer::singleShot(0, client, &UpdateClient::check);
#endif
}
