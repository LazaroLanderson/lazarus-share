#include "window.h"
#include "update.h"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QTimer>
#include <QTemporaryDir>
#include <QSettings>
#include <QMessageBox>
#include <iostream>
#include <algorithm>
#include <memory>
#include <vector>
struct PeerTestAccess {
    static bool failReceiver(Peer &peer,bool audio){
        auto *iterator=gst_bin_iterate_recurse(GST_BIN(audio?peer.audioPlayback_:peer.pipeline_));GValue value=G_VALUE_INIT;bool found=false;
        while(gst_iterator_next(iterator,&value)==GST_ITERATOR_OK){
            auto *element=GST_ELEMENT(g_value_get_object(&value));auto *factory=gst_element_get_factory(element);
            const char *klass=factory?gst_element_factory_get_metadata(factory,GST_ELEMENT_METADATA_KLASS):nullptr;
            const char *name=factory?gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)):"";
            if((audio && !strcmp(name,"autoaudiosink")) || (!audio && klass && strstr(klass,"Decoder") && strstr(klass,"Video"))){
                auto *error=g_error_new_literal(audio?GST_RESOURCE_ERROR:GST_STREAM_ERROR,audio?int(GST_RESOURCE_ERROR_OPEN_WRITE):int(GST_STREAM_ERROR_DECODE),"Controlled receiver failure");
                auto *bus=gst_element_get_bus(audio?peer.audioPlayback_:peer.pipeline_);gst_bus_post(bus,gst_message_new_error(GST_OBJECT(element),error,nullptr));gst_object_unref(bus);g_error_free(error);found=true;g_value_reset(&value);break;
            }g_value_reset(&value);
        }if(G_VALUE_TYPE(&value))g_value_unset(&value);gst_iterator_free(iterator);return found;
    }
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
struct UpdateTestAccess {
    static void ready(UpdateClient &client) {
        client.best_ = {"0.3.0", "Novidades", {}, QByteArray(64, 'a'), 100};
        client.ready_ = true; emit client.available();
    }
};
struct WindowTestAccess {
    static Window::Connection &connection(Window &w) {
        for(auto &[id,c]:w.peers_)if(!c->session.isEmpty())return *c;
        return *w.peers_.begin()->second;
    }
    static bool roomControls(Window &w) {
        w.active_=true; w.administrator_=false; w.secret_=Protocol::randomBytes(16); w.challenge_="b"+QString(31,'b');
        auto stateLabel=w.findChild<QLabel *>("viewerState");
        w.message({{"type","joined"},{"protocol",2},{"peer","me"},{"requireApproval",true}});
        if(stateLabel->text()!="Aguardando aprovação" || w.share_->isEnabled())return false;
        QJsonArray roster{QJsonObject{{"peer","alice"},{"approved",true},{"profile",QJsonObject{{"nickname","Alice"},{"avatar",3}}}},
                          QJsonObject{{"peer","me"},{"approved",true},{"profile",w.profile_.json()}}};
        w.roomState({{"revision",0},{"broadcaster",""},{"participants",roster}});
        if(stateLabel->text()!="Aguardando compartilhamento" || !w.share_->isEnabled())return false;
        w.roomState({{"revision",1},{"broadcaster","alice"},{"participants",roster}});
        if(stateLabel->text()!="Conectando" || w.share_->isEnabled() || w.sharing_)return false;
        w.message({{"type","ready"},{"protocol",2},{"peer","alice"},{"revision",1},{"session",QString(32,'s')},{"challenge",QString(32,'a')}});
        w.viewerState_="Ao vivo";w.updatePresentation();
        Protocol::Channel remote(w.secret_,QString(32,'s'),"alice","me",QString(32,'a'),w.challenge_,1);
        auto profile=remote.seal({{"kind","profile"},{"profile",QJsonObject{{"nickname","Bob"},{"avatar",4}}}});
        profile["peer"]="alice";w.message(profile);
        if(stateLabel->text()!="Ao vivo" || !w.findChild<QLabel *>("transmitter")->text().contains("Bob"))return false;
        QImage frame(640,360,QImage::Format_RGB32);frame.fill(Qt::blue);w.viewerPanel_->setFrame(frame);w.viewerPanel_->frameAt(0);
        w.viewerPanel_->updateFreeze(5000);if(stateLabel->text()!="Imagem congelada")return false;
        auto terminal=remote.seal({{"kind","connection-terminal"},{"generation",1}});terminal["peer"]="alice";w.message(terminal);
        if(stateLabel->text()!="Falha" || w.video_->hasFrame())return false;
        w.socketLost_=0;w.updatePresentation();if(stateLabel->text()!="Reconectando")return false;w.socketLost_=-1;
        w.roomState({{"revision",2},{"broadcaster",""},{"participants",roster}});
        if(w.video_->hasFrame() || stateLabel->text()!="Aguardando compartilhamento")return false;
        w.message({{"type","created"}});
        return !w.active_ && stateLabel->text()=="Falha" && w.status_->text().contains("Servidor incompatível");
    }
    static void updateRoom(Window &window, const QString &link) { window.active_ = true; window.secret_ = Protocol::inviteSecret(link); }
    static bool inRoom(Window &w, const QString &link) { return w.active_ && w.secret_ == Protocol::inviteSecret(link); }
    struct Snapshot {QString id;int generation,transport;Peer *media;QString encoder,session;};
    static std::vector<Snapshot> snapshots(Window &w){std::vector<Snapshot> result;for(auto &[id,c]:w.peers_)if(!c->session.isEmpty())result.push_back({id,c->generation,c->transport,c->media.get(),c->media?c->media->encoderName():QString{},c->session});return result;}
    static void disconnectOwner(Window &w){w.socket_.close();}
    static bool resumedSession(Window &w,const Snapshot &before){
        auto saved=snapshots(w);return saved.size()==1 && saved[0].session!=before.session && saved[0].media && saved[0].media->connected() && w.video_->hasFrame() && !w.sharing_;
    }
    static void fail(Window &w,const Snapshot &s){auto &c=*w.peers_.at(s.id);c.software=false;PeerTestAccess::failEncoder(*c.media);}
    static bool failReceiver(Window &w,bool audio){return PeerTestAccess::failReceiver(*connection(w).media,audio);}
    static bool receiverRecovered(Window &w,const Snapshot &s){
        auto &c=*w.peers_.at(s.id);
        if(c.generation!=s.generation+1 || c.transport!=s.transport || !c.media || !c.media->connected() || c.fatalMedia || !c.decoderSoftware || !c.media->softwareDecoder() || c.media->takeFrame().isNull())return false;
        auto *factory=gst_element_factory_find(c.media->decoderName().toUtf8().constData());const char *klass=factory?gst_element_factory_get_metadata(factory,GST_ELEMENT_METADATA_KLASS):nullptr;
        bool software=klass && !strstr(klass,"Hardware");if(factory)gst_object_unref(factory);return software;
    }
    static bool receiverTerminal(Window &w,const Snapshot &s){
        auto &c=*w.peers_.at(s.id);int generation=c.generation;
        w.mediaFailure(s.id,s.generation,"decoder_error");if(c.generation!=generation || !c.media)return false;
        return failReceiver(w,false) && failReceiver(w,false);
    }
    static bool receiverStopped(Window &w){auto &c=connection(w);return c.fatalMedia && !c.media && !c.relayRequested && !c.decoderRecovering;}
    static bool receiverRunning(Window &w){auto &c=connection(w);return c.media && c.media->connected() && !c.fatalMedia && !c.decoderRecovering && !c.media->takeFrame().isNull();}
    static bool decoderTimeout(Window &w){
        w.active_=true;w.administrator_=false;auto c=std::make_unique<Window::Connection>();c->decoderRecovering=c->decoderSoftware=true;c->started=w.time_.elapsed()-16000;w.peers_["timeout"]=std::move(c);
        QMetaObject::invokeMethod(&w.maintenance_,"timeout",Qt::DirectConnection);
        return receiverStopped(w) && w.peers_.at("timeout")->metrics["error_code"]=="decoder_retry_timeout";
    }
    static bool audioWarning(Window &w){return w.audioStatus_->text().contains("Áudio indisponível");}
    static void staleRetry(Window &w,int generation){w.signal(w.broadcaster_,{{"kind","retry-request"},{"reason","decoder_fallback"},{"generation",generation}});}
    static void errors(Window &w){for(auto value:w.log_.events()){auto e=value.toObject();if(e["event"].toString()=="media_failure_detail")std::cerr<<"Receiver detail: "<<e["error_code"].toString().toStdString()<<' '<<e["component"].toString().toStdString()<<' '<<e["factory"].toString().toStdString()<<'\n';}}
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
            w.message({{"type","error"},{"revision",w.revision_},{"peer",before.id},{"code","turn_unavailable"}});
            if(c.renewal.pending || c.renewal.failures!=1 || c.media.get()!=before.media || c.exhausted)return false;
            // A duplicate credential response must neither extend validity nor reset retry state.
            w.message({{"type","turn"},{"revision",w.revision_},{"peer",before.id},{"host","127.0.0.1"},{"username",QString::number(epoch)+":duplicate"},{"password","test-only"},{"expires",3600}});
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
        host.message({{"type","turn"},{"revision",host.revision_},{"peer",id},{"host","127.0.0.1"},{"username",QString::number(epoch+100)+":revoked"},{"password","test-only"},{"expires",3600}});
        return !c.media && c.turnEpoch==epoch && c.turns.isEmpty();
    }
    static void beginRenewal(Window &w){for(auto &[id,c]:w.peers_)c->renewal.requested(w.time_.elapsed());}
    static std::shared_ptr<std::atomic<unsigned>> watchFrames(Window &w){return PeerTestAccess::watchFrames(*connection(w).media);}
    static bool unchanged(Window &w,const std::vector<Snapshot> &saved){for(auto &s:saved){auto &c=*w.peers_.at(s.id);if(c.media.get()!=s.media || c.generation!=s.generation || c.transport!=s.transport)return false;}return true;}
    static bool pausedReply(Window &w){
        for(auto &[id,c]:w.peers_){
            auto generation=c->generation;auto turns=c->turns;auto expiry=c->turnExpiry;auto epoch=c->turnEpoch;
            w.message({{"type","turn"},{"revision",w.revision_},{"peer",id},{"host","127.0.0.1"},{"username",QString::number(c->turnEpoch+100)+":late-paused"},{"password","test-only"},{"expires",3600}});
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
    auto *dialogTimer = new QTimer(&window); dialogTimer->setInterval(10);
    QObject::connect(dialogTimer,&QTimer::timeout,&window,[preset,dialogTimer]{
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto combos = dialog->findChildren<QComboBox *>(); combos.last()->setCurrentIndex(preset);
        dialogTimer->stop(); dialogTimer->deleteLater(); dialog->accept();
    }); dialogTimer->start();
    auto *start=button(window,"Compartilhar tela");
    (start->isEnabled()?start:button(window,"Monitor / qualidade"))->click();
}
int main(int argc, char **argv) {
    gst_init(&argc, &argv); QApplication app(argc, argv); QTemporaryDir state;
    qputenv("XDG_DATA_HOME",state.path().toUtf8());
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,state.path());
    QCoreApplication::setOrganizationName("LazarusTests"); QCoreApplication::setApplicationName("Session");
    bool automatic=app.arguments().contains("--relay-auto"), denial=app.arguments().contains("--deny-relay"); int transport=0;
    const bool audioFlowFailure=app.arguments().contains("--audio-flow-failure");
    if(audioFlowFailure)qputenv("LAZARUS_TEST_AUDIO_FLOW_FAILURE","1");
    const bool decoderRecovery=app.arguments().contains("--decoder-fallback"),audioRecovery=app.arguments().contains("--audio-failure") || audioFlowFailure;
    if(decoderRecovery)qputenv("LAZARUS_TEST_H264","1");
    for(auto argument:app.arguments())if(argument.startsWith("--relay-transport="))transport=argument.section('=',1).toInt();
    Profile{"José",3,true}.save(); Window host; host.show();
    if(app.arguments().contains("--room-controls")){if(!WindowTestAccess::roomControls(host))return 1;std::cout<<"Room states, identity, freeze, revision change and server incompatibility passed\n";return 0;}
    if(app.arguments().contains("--decoder-timeout")){if(!WindowTestAccess::decoderTimeout(host))return 1;std::cout<<"Unanswered decoder recovery terminates after timeout\n";return 0;}
    if (app.arguments().contains("--update-controls")) {
        const auto link = Protocol::inviteLink(Protocol::randomBytes(16));
        WindowTestAccess::updateRoom(host, link);
        auto *client = host.findChild<UpdateClient *>(); auto *banner = host.findChild<QPushButton *>("updateBanner");
        if (!client || !banner || !banner->isHidden()) return 1;
#ifdef Q_OS_WIN
        qputenv("LAZARUS_LAUNCHER_PATH", state.filePath("portable.exe").toUtf8());
#else
        qputenv("APPIMAGE", state.filePath("portable.AppImage").toUtf8());
#endif
        UpdateTestAccess::ready(*client); if (banner->isHidden()) return 1;
        bool asked = false, preserved = false;
        QTimer::singleShot(0, &host, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            QTimer::singleShot(0, &host, [&] {
                auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                if (box) { asked = true; box->button(QMessageBox::No)->click(); }
            });
            for (auto *b : dialog->findChildren<QPushButton *>()) if (b->text() == "Reiniciar e atualizar") { b->click(); break; }
            preserved = WindowTestAccess::inRoom(host, link); dialog->reject();
        });
        banner->click();
        const auto profile = Profile::load();
        if (!asked || !preserved || profile.nickname != "José" || profile.avatar != 3 || !profile.relay) return 1;
        std::cout << "Update banner and declined restart preserve active room and profile\n"; return 0;
    }
    if (app.arguments().contains("--invite-controls")) {
        auto fail = [] { std::cerr << "Invitation UI check failed\n"; return 1; };
        auto *policy = host.findChild<QCheckBox *>("requireApproval");
        if (policy->isChecked()) return fail();
        button(host,"Criar sala")->click();
        const auto original = host.findChild<QLineEdit *>("invite")->text();
        if (policy->isEnabled() || !button(host,"Aprovar")->isHidden()) return fail();
        host.openInvite(original); host.openInvite("https://invalid.example/join#INVALID");
        if (!WindowTestAccess::inRoom(host, original)) return fail();
        const auto next = Protocol::inviteLink(Protocol::randomBytes(16));
        QTimer::singleShot(0, &host, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::No)->click(); });
        host.openInvite(next); if (!WindowTestAccess::inRoom(host, original)) return fail();
        QTimer::singleShot(0, &host, [] { if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) box->button(QMessageBox::Yes)->click(); });
        host.openInvite(next); if (!WindowTestAccess::inRoom(host, next)) return fail();
        button(host,"Encerrar / sair")->click();
        if (!policy->isEnabled()) return fail();
        Profile{}.save(); Window newcomer(false);
        QTimer::singleShot(0, &newcomer, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) {
                dialog->findChild<QLineEdit *>("nickname")->setText("Novo"); dialog->accept();
            }
        });
        newcomer.openInvite(next); if (!WindowTestAccess::inRoom(newcomer, next)) return fail();
        std::cout << "Repeated and invalid links, cancel/confirm room switch and initial profile passed\n"; return 0;
    }
    if (app.arguments().contains("--probe-tls-refusal")) {
        host.findChild<QCheckBox *>("requireApproval")->setChecked(true); button(host,"Criar sala")->click(); int result=1;
        QTimer::singleShot(3000,&app,[&] { result=host.findChild<QLabel *>("status")->text().startsWith("Sala criada")?1:0; app.quit(); }); app.exec(); return result;
    }
    const bool ownerReconnect=app.arguments().contains("--owner-reconnect");
    const bool guestBroadcast=app.arguments().contains("--guest-broadcast");
    const bool autoAdmission=app.arguments().contains("--auto-admission");
    const int count=app.arguments().contains("--four-viewers")?4:1;
    std::vector<std::unique_ptr<Window>> guests;
    for(int i=0;i<count;++i) { Profile{QString("Viewer%1").arg(i),i,automatic}.save(); guests.push_back(std::make_unique<Window>()); guests.back()->show(); }
    if(guestBroadcast)guests[0]->findChild<QCheckBox *>("testPattern")->setChecked(true);
    if(automatic || denial) { WindowTestAccess::block(host,transport-1); for(auto &g:guests)WindowTestAccess::block(*g,transport-1); }
    host.findChild<QCheckBox *>("testPattern")->setChecked(true); host.findChild<QCheckBox *>("requireApproval")->setChecked(!autoAdmission); button(host,"Criar sala")->click();
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
        if(elapsed.elapsed()>(app.arguments().contains("--turn-renewal")?30000:20000)) { std::cerr<<"Session failed at stage "<<stage<<": "<<host.findChild<QLabel *>("status")->text().toStdString()<<'\n';WindowTestAccess::errors(host);for(auto &g:guests)WindowTestAccess::errors(*g); app.quit(); return; }
        if(stage==0 && host.findChild<QLabel *>("status")->text().startsWith("Sala criada")) {
            if(button(host,"Parar compartilhamento")->isEnabled()) { app.quit(); return; }
            auto token=host.findChild<QLineEdit *>("invite")->text();
            for(auto &g:guests) { g->openInvite(token); } ++stage;
        } else if(stage==1 && host.findChild<QListWidget *>("viewers")->count()==count+1) {
            if (!autoAdmission) for(int i=0;i<count+1;++i) {
                auto *list=host.findChild<QListWidget *>("viewers");
                if(list->item(i)->text().contains("Você"))continue;
                list->setCurrentRow(i); button(host,"Aprovar")->click();
            }
            approvedAt=elapsed.elapsed(); ++stage;
        } else if(stage==2 && elapsed.elapsed()-approvedAt>500) {
            if(std::any_of(guests.begin(),guests.end(),[](auto &g) { return !g->template findChild<QLabel *>("video")->pixmap().isNull(); })) { std::cerr<<"Room captured without consent\n"; app.quit(); return; }
            share(host,0); ++stage;
        } else if(stage==3 && std::all_of(guests.begin(),guests.end(),[](auto &g) { return !g->template findChild<QLabel *>("video")->pixmap().isNull(); })) {
            if(decoderRecovery || audioRecovery){
                saved=WindowTestAccess::snapshots(host);guestSaved.push_back(WindowTestAccess::snapshots(*guests[0]));
                if(!audioFlowFailure && !WindowTestAccess::failReceiver(*guests[0],audioRecovery)){std::cerr<<"Missing receive component for injection\n";app.quit();return;}
                if(audioRecovery)frameCounters[0]=WindowTestAccess::watchFrames(*guests[0]);
                stage=decoderRecovery?21:23;
            }else if(app.arguments().contains("--turn-renewal")){saved=WindowTestAccess::snapshots(host);
                for(int i=0;i<count;++i){guestSaved.push_back(WindowTestAccess::snapshots(*guests[i]));frameCounters[i]=WindowTestAccess::watchFrames(*guests[i]);if(!frameCounters[i]){std::cerr<<"Missing receive frame probe\n";app.quit();return;}initialDecoded[i]=decoded[i]=frameCounters[i]->load();frameAt[i]=elapsed.elapsed();}
                approvedAt=elapsed.elapsed();stage=15;}
            else if(app.arguments().contains("--quality-live")){share(host,1);stage=14;}
            else if(app.arguments().contains("--encoder-fallback")){saved=WindowTestAccess::snapshots(host);WindowTestAccess::fail(host,saved.front());stage=13;}
            else {button(host,"Parar compartilhamento")->click();++stage;}
        } else if(stage==21 && WindowTestAccess::receiverRecovered(*guests[0],guestSaved[0][0])){
            auto current=WindowTestAccess::snapshots(host);
            if(current[0].transport!=saved[0].transport || current[0].generation!=saved[0].generation+1){std::cerr<<"Decoder fallback changed transport or restarted twice\n";app.quit();return;}
            WindowTestAccess::staleRetry(*guests[0],guestSaved[0][0].generation);
            frameCounters[0]=WindowTestAccess::watchFrames(*guests[0]);stage=22;
        } else if(stage==22 && frameCounters[0]->load()>=30){
            if(!WindowTestAccess::receiverTerminal(*guests[0],guestSaved[0][0])){std::cerr<<"Software failure was not terminal or stale callback accepted\n";app.quit();return;}
            approvedAt=elapsed.elapsed();stage=24;
        } else if(stage==24 && elapsed.elapsed()-approvedAt>1200){
            if(WindowTestAccess::snapshots(host)[0].generation!=saved[0].generation+1 || !WindowTestAccess::receiverStopped(*guests[0]) || !WindowTestAccess::receiverStopped(host)){std::cerr<<"Terminal decoder failure restarted again\n";app.quit();return;}
            std::cout<<"Decoder recovery preserved transport, resumed frames and bounded retries\n";
            button(*guests[0],"Tentar novamente")->click();stage=25;
        } else if(stage==25 && WindowTestAccess::receiverRunning(*guests[0])){
            std::cout<<"Manual retry restored video after terminal decoder failure\n";passed=true;app.quit();
        } else if(stage==23 && frameCounters[0]->load()>=90){
            if(!WindowTestAccess::unchanged(*guests[0],guestSaved[0]) || !WindowTestAccess::unchanged(host,saved) || !WindowTestAccess::audioWarning(*guests[0])){std::cerr<<"Audio failure interrupted media or lacked warning\n";app.quit();return;}
            std::cout<<"Audio failure preserved connection and 90 subsequent video frames\n";passed=true;app.quit();
        } else if(stage==15 && elapsed.elapsed()-approvedAt>1500){
            if(!WindowTestAccess::lateAndFailure(host,saved)){std::cerr<<"Renewal error/duplicate damaged media\n";app.quit();return;}
            WindowTestAccess::renewalDue(host);renewalAt=elapsed.elapsed();stage=16;
        } else if(stage==16 && elapsed.elapsed()-renewalAt>2000 && WindowTestAccess::renewalDone(host,saved) &&
                  std::all_of(decoded.begin(),decoded.end(),[](unsigned frames){return frames>=30;})){
            for(int i=0;i<count;++i)std::cout<<"Viewer "<<i<<" received "<<decoded[i]-initialDecoded[i]<<" frames during renewal\n";
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
            if(Protocol::inviteSecret(host.findChild<QLineEdit *>("invite")->text()).isEmpty() || button(host,"Criar sala")->isEnabled()) { app.quit(); return; }
            share(guestBroadcast?*guests[0]:host,1); ++stage;
        } else if(stage==5 && (guestBroadcast
                   ? host.findChild<QLabel *>("metrics")->text().contains("1920×1080") && std::all_of(guests.begin()+1,guests.end(),[](auto &g){return g->template findChild<QLabel *>("metrics")->text().contains("1920×1080");})
                   : std::all_of(guests.begin(),guests.end(),[](auto &g) { return g->template findChild<QLabel *>("metrics")->text().contains("1920×1080"); }))) {
            if(guestBroadcast)std::cout<<"Guest broadcasts directly to creator and other participants\n";
            if(ownerReconnect) {
                saved=WindowTestAccess::snapshots(host); guestSaved.clear();
                for(auto &g:guests)guestSaved.push_back(WindowTestAccess::snapshots(*g));
                WindowTestAccess::disconnectOwner(host);stage=30;
            } else {button(host,"Encerrar / sair")->click();++stage;}
        } else if(stage==30 && WindowTestAccess::resumedSession(host,saved[0])) {
            for(int i=1;i<count;++i)if(!WindowTestAccess::unchanged(*guests[i],guestSaved[i])){std::cerr<<"Owner resume restarted another receiver\n";app.quit();return;}
            std::cout<<"Creator resumed with a fresh receive session; other receivers continued\n";
            button(host,"Encerrar / sair")->click();stage=6;
        } else if(stage==6 && std::all_of(guests.begin(),guests.end(),[](auto &g) { return button(*g,"Entrar com link")->isEnabled(); })) { passed=true; app.quit(); }
    }); timer.start(); app.exec();
    if(passed) std::cout<<count<<" viewer(s): approved idle room, explicit share, pause, resume and teardown passed\n";
    return passed?0:1;
}
