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
struct PeerTestAccess {
    static std::shared_ptr<std::atomic<unsigned>> watchFrames(Peer &peer){
        auto count=std::make_shared<std::atomic<unsigned>>(0);
        auto *sink=gst_bin_get_by_name(GST_BIN(peer.pipeline_),"frames");if(!sink)return {};
        auto *pad=gst_element_get_static_pad(sink,"sink");
        gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad *,GstPadProbeInfo *,gpointer data){++**static_cast<std::shared_ptr<std::atomic<unsigned>> *>(data);return GST_PAD_PROBE_OK;},
                          new std::shared_ptr<std::atomic<unsigned>>(count),[](gpointer data){delete static_cast<std::shared_ptr<std::atomic<unsigned>> *>(data);});
        gst_object_unref(pad);gst_object_unref(sink);return count;
    }
    static void failEncoder(Peer &peer){auto *failure=g_error_new_literal(GST_STREAM_ERROR,GST_STREAM_ERROR_ENCODE,"Controlled encoder failure");auto *bus=gst_element_get_bus(peer.pipeline_);gst_bus_post(bus,gst_message_new_error(GST_OBJECT(peer.encoder_),failure,nullptr));g_error_free(failure);gst_object_unref(bus);}
};
struct WindowTestAccess {
    struct Snapshot {QString id;int generation,transport;Peer *media;QString encoder;};
    static std::vector<Snapshot> snapshots(Window &w){std::vector<Snapshot> result;for(auto &[id,c]:w.peers_)result.push_back({id,c->generation,c->transport,c->media.get(),c->media?c->media->encoderName():QString{}});return result;}
    static void fail(Window &w,const Snapshot &s){auto &c=*w.peers_.at(s.id);c.software=false;PeerTestAccess::failEncoder(*c.media);}
    static bool recovered(Window &w,const std::vector<Snapshot> &saved){
        for(size_t i=0;i<saved.size();++i){auto &before=saved[i];auto &c=*w.peers_.at(before.id);
            if(c.transport!=before.transport || c.relayRequested)return false;
            if(i==0){if(c.generation!=before.generation+1 || !c.media || !c.media->connected() || c.media->encoderFactory()!="vp8enc")return false;}
            else if(c.generation!=before.generation || c.media.get()!=before.media || !c.media->connected())return false;
        }return true;
    }
    static bool terminal(Window &w,const Snapshot &s){auto &c=*w.peers_.at(s.id);int generation=c.generation;w.mediaFailure(s.id,s.generation,"encoder_error");if(c.generation!=generation || !c.media)return false;w.mediaFailure(s.id,generation,"encoder_error");if(c.generation!=generation)return false;w.mediaFailure(s.id,generation,"encoder_error");return c.generation==generation && c.fatalMedia && !c.media && !c.relayRequested;}
    static void renewalDue(Window &w) {for(auto &[id,c]:w.peers_)c->turnExpiry=w.time_.elapsed()+299000;}
    static bool renewalDone(Window &w,const std::vector<Snapshot> &saved){
        for(auto &before:saved){auto &c=*w.peers_.at(before.id);
            if(c.renewal.pending || c.renewal.failures || c.turnExpiry<w.time_.elapsed()+300000)return false;
            if(c.media.get()!=before.media || c.generation!=before.generation || c.transport!=before.transport || c.media->encoderName()!=before.encoder || c.failed || c.exhausted)return false;
        }return true;
    }
    static bool lateAndFailure(Window &w,const std::vector<Snapshot> &saved){
        for(auto &before:saved){auto &c=*w.peers_.at(before.id);auto expiry=c.turnExpiry;auto epoch=c.turnEpoch;
            c.renewal.requested(w.time_.elapsed());
            w.message({{"type","error"},{"peer",before.id},{"code","turn_unavailable"}});
            if(c.renewal.pending || c.renewal.failures!=1 || c.media.get()!=before.media || c.exhausted)return false;
            // A duplicate credential response must neither extend validity nor reset retry state.
            w.message({{"type","turn"},{"peer",before.id},{"host","127.0.0.1"},{"username",QString::number(epoch)+":duplicate"},{"password","test-only"},{"expires",3600}});
            if(c.turnExpiry!=expiry || c.media.get()!=before.media || c.renewal.failures!=1)return false;
            c.renewal.cancel();c.renewal.requested(w.time_.elapsed()-15000);
            QMetaObject::invokeMethod(&w.maintenance_,"timeout",Qt::DirectConnection);
            if(c.renewal.pending || c.renewal.failures!=1 || c.media.get()!=before.media || c.generation!=before.generation)return false;
            c.renewal.cancel();
        }return true;
    }
    static bool reconnectExpired(Window &w,const Snapshot &before){
        auto &c=*w.peers_.at(before.id);c.turnExpiry=0;
        w.restart(before.id,c.transport);
        return c.relayRequested && !c.media && c.generation==before.generation && !c.renewal.pending;
    }
    static bool reconnected(Window &w,const std::vector<Snapshot> &saved){
        for(size_t i=0;i<saved.size();++i){auto &before=saved[i];auto &c=*w.peers_.at(before.id);
            if(!c.media || !c.media->connected())return false;
            if(c.transport!=before.transport || c.relayRequested || c.turnExpiry<=w.time_.elapsed())return false;
            if(i==0){if(c.generation!=before.generation+1)return false;}
            else if(c.generation!=before.generation || c.media.get()!=before.media)return false;
        }return true;
    }
    static void revoke(Window &guest){for(auto &[id,c]:guest.peers_)guest.signal(id,{{"kind","relay-consent"},{"enabled",false}});}
    static void allow(Window &guest){for(auto &[id,c]:guest.peers_)guest.signal(id,{{"kind","relay-consent"},{"enabled",true}});}
    static bool revokedReply(Window &host,const QString &id){
        auto &c=*host.peers_.at(id);if(c.remoteConsent || c.media || c.renewal.pending || c.relayRequested || c.pendingRestart)return false;
        auto epoch=c.turnEpoch;
        host.message({{"type","turn"},{"peer",id},{"host","127.0.0.1"},{"username",QString::number(epoch+100)+":revoked"},{"password","test-only"},{"expires",3600}});
        return !c.media && c.turnEpoch==epoch && c.turns.isEmpty();
    }
    static void beginRenewal(Window &w){for(auto &[id,c]:w.peers_)c->renewal.requested(w.time_.elapsed());}
    static std::shared_ptr<std::atomic<unsigned>> watchFrames(Window &w){return PeerTestAccess::watchFrames(*w.peers_.begin()->second->media);}
    static bool unchanged(Window &w,const std::vector<Snapshot> &saved){for(auto &s:saved){auto &c=*w.peers_.at(s.id);if(c.media.get()!=s.media || c.generation!=s.generation || c.transport!=s.transport)return false;}return true;}
    static bool pausedReply(Window &w){
        for(auto &[id,c]:w.peers_){
            auto generation=c->generation;auto turns=c->turns;auto expiry=c->turnExpiry;auto epoch=c->turnEpoch;
            w.message({{"type","turn"},{"peer",id},{"host","127.0.0.1"},{"username",QString::number(c->turnEpoch+100)+":late-paused"},{"password","test-only"},{"expires",3600}});
            if(c->media || c->generation!=generation || c->renewal.pending || c->relayRequested)return false;
            c->turns=turns;c->turnExpiry=expiry;c->turnEpoch=epoch;
        }return true;
    }
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
    auto *start=button(window,"Compartilhar tela");
    (start->isEnabled()?start:button(window,"Monitor / qualidade"))->click();
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
    QTimer timer; QElapsedTimer elapsed; elapsed.start(); timer.setInterval(20); int stage=0; qint64 approvedAt=0,renewalAt=0; bool passed=false;std::vector<WindowTestAccess::Snapshot> saved;
    std::vector<std::vector<WindowTestAccess::Snapshot>> guestSaved;
    std::vector<std::shared_ptr<std::atomic<unsigned>>> frameCounters(count);
    std::vector<unsigned> decoded(count),initialDecoded(count);std::vector<qint64> frameAt(count);qint64 maxGap=0;
    QObject::connect(host.findChild<Capture *>(),&Capture::error,&app,[&](QString){std::cerr<<"Capture failed at stage "<<stage<<'\n';app.quit();});
    QObject::connect(&timer,&QTimer::timeout,&app,[&] {
        if(stage==15 || stage==16){
            for(int i=0;i<count;++i){
                if(!WindowTestAccess::unchanged(*guests[i],guestSaved[i])){std::cerr<<"Viewer restarted during renewal\n";app.quit();return;}
                auto frames=frameCounters[i]->load();
                if(frames!=decoded[i]){maxGap=qMax(maxGap,elapsed.elapsed()-frameAt[i]);frameAt[i]=elapsed.elapsed();decoded[i]=frames;}
                if(elapsed.elapsed()-frameAt[i]>500){std::cerr<<"Renewal paused video over 500 ms\n";app.quit();return;}
            }
        }
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
            if(app.arguments().contains("--turn-renewal")){saved=WindowTestAccess::snapshots(host);
                for(int i=0;i<count;++i){guestSaved.push_back(WindowTestAccess::snapshots(*guests[i]));frameCounters[i]=WindowTestAccess::watchFrames(*guests[i]);if(!frameCounters[i]){std::cerr<<"Missing receive frame probe\n";app.quit();return;}initialDecoded[i]=decoded[i]=frameCounters[i]->load();frameAt[i]=elapsed.elapsed();}
                approvedAt=elapsed.elapsed();stage=15;}
            else if(app.arguments().contains("--quality-live")){share(host,1);stage=14;}
            else if(app.arguments().contains("--encoder-fallback")){saved=WindowTestAccess::snapshots(host);WindowTestAccess::fail(host,saved.front());stage=13;}
            else {button(host,"Parar compartilhamento")->click();++stage;}
        } else if(stage==15 && elapsed.elapsed()-approvedAt>1500){
            if(!WindowTestAccess::lateAndFailure(host,saved)){std::cerr<<"Renewal error/duplicate damaged media\n";app.quit();return;}
            WindowTestAccess::renewalDue(host);renewalAt=elapsed.elapsed();stage=16;
        } else if(stage==16 && elapsed.elapsed()-renewalAt>2000 && WindowTestAccess::renewalDone(host,saved)){
            for(int i=0;i<count;++i)if(decoded[i]-initialDecoded[i]<30){std::cerr<<"Renewal did not receive new frames\n";app.quit();return;}
            std::cout<<"Renewal frame continuity: max gap="<<maxGap<<" ms\n";
            approvedAt=elapsed.elapsed();stage=17;
        } else if(stage==17 && elapsed.elapsed()-approvedAt>1500){
            if(!WindowTestAccess::reconnectExpired(host,saved.front())){std::cerr<<"Expired credentials started an allocation\n";app.quit();return;}stage=18;
        } else if(stage==18 && WindowTestAccess::reconnected(host,saved)){
            WindowTestAccess::beginRenewal(host);for(auto &g:guests)WindowTestAccess::revoke(*g);stage=19;
        } else if(stage==19 && std::all_of(saved.begin(),saved.end(),[&](auto &s){return WindowTestAccess::revokedReply(host,s.id);})){
            for(auto &g:guests)WindowTestAccess::allow(*g);
            button(host,"Parar compartilhamento")->click();approvedAt=elapsed.elapsed();stage=20;
        } else if(stage==20 && elapsed.elapsed()-approvedAt>300){stage=4;
        } else if(stage==13 && WindowTestAccess::recovered(host,saved)){
            if(!WindowTestAccess::terminal(host,saved.front())){std::cerr<<"Fallback was not bounded or stale callback was accepted\n";app.quit();return;}
            share(host,1);stage=14;
        } else if(stage==14 && std::all_of(guests.begin(),guests.end(),[](auto &g){return g->template findChild<QLabel *>("metrics")->text().contains("1920×1080");})){
            button(host,"Parar compartilhamento")->click();stage=4;
        } else if(stage==4 && std::all_of(guests.begin(),guests.end(),[](auto &g) { return g->template findChild<QLabel *>("video")->pixmap().isNull(); })) {
            if(app.arguments().contains("--turn-renewal") && !WindowTestAccess::pausedReply(host)){std::cerr<<"Late reply restarted paused media\n";app.quit();return;}
            if(host.findChild<QLineEdit *>("invite")->text().size()!=26 || button(host,"Criar sala")->isEnabled()) { app.quit(); return; }
            share(host,1); ++stage;
        } else if(stage==5 && std::all_of(guests.begin(),guests.end(),[](auto &g) { return g->template findChild<QLabel *>("metrics")->text().contains("1920×1080"); })) {
            button(host,"Encerrar / sair")->click(); ++stage;
        } else if(stage==6 && std::all_of(guests.begin(),guests.end(),[](auto &g) { return button(*g,"Entrar com token")->isEnabled(); })) { passed=true; app.quit(); }
    }); timer.start(); app.exec();
    if(passed) std::cout<<count<<" viewer(s): approved idle room, explicit share, pause, resume and teardown passed\n";
    return passed?0:1;
}
