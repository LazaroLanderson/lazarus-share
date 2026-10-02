#include "window.h"
#include <QApplication>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QTimer>
#include <iostream>
#include <algorithm>
#include <memory>
#include <vector>
static QPushButton *button(Window &w, const QString &text) {
    for (auto *b : w.findChildren<QPushButton *>()) if (b->text() == text) return b;
    return nullptr;
}
int main(int argc, char **argv) {
    gst_init(&argc, &argv); QApplication app(argc, argv);
    Window host; host.show();
    if (app.arguments().contains("--probe-capture-start") || app.arguments().contains("--probe-capture-interactive")) {
        bool interactive = app.arguments().contains("--probe-capture-interactive");
        host.setWindowTitle("Lazarus Share — diagnóstico de captura, sem viewers");
        QTimer::singleShot(50, &host, [&] { button(host, "Criar sala")->click(); });
        if (!interactive) QTimer::singleShot(3000, &app, [&] {
            std::cout << "Create-room capture path remained alive: " << host.findChild<QLabel *>("status")->text().toStdString() << '\n';
            app.quit();
        });
        QTimer observations; QString previous; observations.setInterval(1000);
        QObject::connect(&observations, &QTimer::timeout, &app, [&] {
            auto status = host.findChild<QLabel *>("status")->text();
            auto metrics = host.findChild<QLabel *>("metrics")->text();
            if (status != previous) { previous = status; std::cout << "Capture status: " << status.toStdString() << '\n' << std::flush; }
            if (interactive && status.startsWith("Sala criada")) std::cout << "Real capture metrics: " << metrics.toStdString() << '\n' << std::flush;
        });
        if (interactive) observations.start();
        return app.exec();
    }
    if (app.arguments().contains("--probe-tls-refusal")) {
        host.findChild<QCheckBox *>()->setChecked(true);
        button(host, "Criar sala")->click();
        int result = 1;
        QTimer::singleShot(3000, &app, [&] {
            bool created = host.findChild<QLabel *>("status")->text().startsWith("Sala criada");
            result = created ? 1 : 0;
            std::cout << (created ? "Wrong certificate pin was accepted" : "Wrong certificate pin refused") << '\n';
            app.quit();
        });
        app.exec(); return result;
    }
    const int count = app.arguments().contains("--four-viewers") ? 4 : 1;
    std::vector<std::unique_ptr<Window>> guests;
    for (int i = 0; i < count; ++i) { guests.push_back(std::make_unique<Window>()); guests.back()->show(); }
    auto &guest = *guests.front();
    host.findChild<QSpinBox *>("width")->setValue(640); host.findChild<QSpinBox *>("height")->setValue(360);
    host.findChild<QSpinBox *>("fps")->setValue(30); host.findChild<QCheckBox *>()->setChecked(true);
    button(host, "Criar sala")->click();
    QTimer timer; QElapsedTimer elapsed; elapsed.start(); timer.setInterval(20);
    int stage = 0; bool passed = false;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (elapsed.elapsed() > 20000) {
            std::cerr << "Session test failed at stage " << stage << ": " << host.findChild<QLabel *>("status")->text().toStdString() << " / " << guest.findChild<QLabel *>("status")->text().toStdString() << '\n'; app.quit(); return;
        }
        if (stage == 0 && host.findChild<QLabel *>("status")->text().startsWith("Sala criada")) {
            auto token = host.findChild<QLineEdit *>("invite")->text();
            if (token.size() != 26) { app.quit(); return; }
            for (auto &g : guests) { g->findChild<QLineEdit *>("invite")->setText(token); button(*g, "Entrar com token")->click(); } ++stage;
        } else if (stage == 1 && host.findChild<QListWidget *>("viewers")->count() == count) {
            for (int i = 0; i < count; ++i) { host.findChild<QListWidget *>("viewers")->setCurrentRow(i); button(host, "Aprovar")->click(); } ++stage;
        } else if (stage == 2 && std::all_of(guests.begin(), guests.end(), [](auto &g) { return !g->template findChild<QLabel *>("video")->pixmap().isNull(); })) {
            host.findChild<QSpinBox *>("width")->setValue(320); host.findChild<QSpinBox *>("height")->setValue(180);
            host.findChild<QSpinBox *>("fps")->setValue(15); button(host, "Aplicar")->click(); ++stage;
        } else if (stage == 3 && std::all_of(guests.begin(), guests.end(), [](auto &g) { return g->template findChild<QLabel *>("metrics")->text().contains("320×180"); })) {
            if (count == 4) {
                host.findChild<QListWidget *>("viewers")->setCurrentRow(0); button(host, "Remover")->click(); stage = 5;
            } else { button(host, "Encerrar / sair")->click(); stage = 4; }
        } else if (stage == 5 && std::count_if(guests.begin(), guests.end(), [](auto &g) { return button(*g, "Entrar com token")->isEnabled(); }) == 1) {
            host.findChild<QSpinBox *>("width")->setValue(640); host.findChild<QSpinBox *>("height")->setValue(360);
            button(host, "Aplicar")->click(); stage = 6;
        } else if (stage == 6 && std::all_of(guests.begin(), guests.end(), [](auto &g) {
            return button(*g, "Entrar com token")->isEnabled() || g->template findChild<QLabel *>("metrics")->text().contains("640×360");
        })) {
            button(host, "Encerrar / sair")->click(); stage = 4;
        } else if (stage == 4 && std::all_of(guests.begin(), guests.end(), [](auto &g) { return button(*g, "Entrar com token")->isEnabled() && g->template findChild<QListWidget *>("viewers")->count() == 0; })) {
            passed = true; app.quit();
        }
    }); timer.start(); app.exec();
    if (passed) std::cout << count << " viewer(s): room creation, token join, approval, authenticated video, live quality change, selective removal and teardown passed\n";
    return passed ? 0 : 1;
}
