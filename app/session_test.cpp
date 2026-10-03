#include "window.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>
#include <QTemporaryDir>
#include <QSettings>
#include <iostream>
#include <algorithm>
#include <memory>
#include <vector>
struct WindowTestAccess {
    static void block(Window &w,int transport) { w.blockedTransport_=transport; }
    static void expire(Window &w) { for(auto &[id,c]:w.peers_) if(c->media && c->transport<=w.blockedTransport_) c->started=w.time_.elapsed()-21000; }
    static bool denied(Window &w) { return !w.peers_.empty() && std::all_of(w.peers_.begin(),w.peers_.end(),[](auto &entry){return entry.second->exhausted && entry.second->turns.isEmpty();}); }
};
static QPushButton *button(Window &w, const QString &text) {
    for (auto *b : w.findChildren<QPushButton *>()) if (b->text() == text) return b;
    return nullptr;
}
static void share(Window &window, int preset = 0) {
    QTimer::singleShot(0, &window, [preset] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto combos = dialog->findChildren<QComboBox *>(); combos.last()->setCurrentIndex(preset); dialog->accept();
    });
    button(window, "Compartilhar tela")->click();
}
int main(int argc, char **argv) {
    gst_init(&argc, &argv); QApplication app(argc, argv); QTemporaryDir state;
    qputenv("XDG_DATA_HOME",state.path().toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,state.path());
    QCoreApplication::setOrganizationName("LazarusTests"); QCoreApplication::setApplicationName("Session");
    bool automatic=app.arguments().contains("--relay-auto"), denial=app.arguments().contains("--deny-relay"); int transport=0;
    for(auto argument:app.arguments())if(argument.startsWith("--relay-transport="))transport=argument.section('=',1).toInt();
    Profile{"José",3,true}.save(); Window host; host.show();
    if (app.arguments().contains("--probe-tls-refusal")) {
        button(host,"Criar sala")->click(); int result=1;
        QTimer::singleShot(3000,&app,[&] { result=host.findChild<QLabel *>("status")->text().startsWith("Sala criada")?1:0; app.quit(); }); app.exec(); return result;
    }
    const int count=app.arguments().contains("--four-viewers")?4:1;
    std::vector<std::unique_ptr<Window>> guests;
    for(int i=0;i<count;++i) { Profile{QString("Viewer%1").arg(i),i,automatic}.save(); guests.push_back(std::make_unique<Window>()); guests.back()->show(); }
    if(automatic || denial) { WindowTestAccess::block(host,transport-1); for(auto &g:guests)WindowTestAccess::block(*g,transport-1); }
    host.findChild<QCheckBox *>()->setChecked(true); button(host,"Criar sala")->click();
    QTimer timer; QElapsedTimer elapsed; elapsed.start(); timer.setInterval(20); int stage=0; qint64 approvedAt=0; bool passed=false;
    QObject::connect(&timer,&QTimer::timeout,&app,[&] {
        if(automatic || denial)WindowTestAccess::expire(host);
        if(denial && stage==3 && WindowTestAccess::denied(host)) { passed=true; app.quit(); return; }
        if(elapsed.elapsed()>20000) { std::cerr<<"Session failed at stage "<<stage<<": "<<host.findChild<QLabel *>("status")->text().toStdString()<<'\n'; app.quit(); return; }
        if(stage==0 && host.findChild<QLabel *>("status")->text().startsWith("Sala criada")) {
            if(button(host,"Parar compartilhamento")->isEnabled()) { app.quit(); return; }
            auto token=host.findChild<QLineEdit *>("invite")->text();
            for(auto &g:guests) { g->findChild<QLineEdit *>("invite")->setText(token); button(*g,"Entrar com token")->click(); } ++stage;
        } else if(stage==1 && host.findChild<QListWidget *>("viewers")->count()==count) {
            for(int i=0;i<count;++i) { host.findChild<QListWidget *>("viewers")->setCurrentRow(i); button(host,"Aprovar")->click(); }
            approvedAt=elapsed.elapsed(); ++stage;
        } else if(stage==2 && elapsed.elapsed()-approvedAt>500) {
            if(std::any_of(guests.begin(),guests.end(),[](auto &g) { return !g->template findChild<QLabel *>("video")->pixmap().isNull(); })) { std::cerr<<"Room captured without consent\n"; app.quit(); return; }
            share(host,0); ++stage;
        } else if(stage==3 && std::all_of(guests.begin(),guests.end(),[](auto &g) { return !g->template findChild<QLabel *>("video")->pixmap().isNull(); })) {
            button(host,"Parar compartilhamento")->click(); ++stage;
        } else if(stage==4 && std::all_of(guests.begin(),guests.end(),[](auto &g) { return g->template findChild<QLabel *>("video")->pixmap().isNull(); })) {
            if(host.findChild<QLineEdit *>("invite")->text().size()!=26 || button(host,"Criar sala")->isEnabled()) { app.quit(); return; }
            share(host,1); ++stage;
        } else if(stage==5 && std::all_of(guests.begin(),guests.end(),[](auto &g) { return g->template findChild<QLabel *>("metrics")->text().contains("1920×1080"); })) {
            button(host,"Encerrar / sair")->click(); ++stage;
        } else if(stage==6 && std::all_of(guests.begin(),guests.end(),[](auto &g) { return button(*g,"Entrar com token")->isEnabled(); })) { passed=true; app.quit(); }
    }); timer.start(); app.exec();
    if(passed) std::cout<<count<<" viewer(s): approved idle room, explicit share, pause, resume and teardown passed\n";
    return passed?0:1;
}
