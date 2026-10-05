#include "window.h"
#include "tls.h"
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFile>
#include <QFormLayout>
#include <QHBoxLayout>
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

Window::Window(bool onboarding) : capture_(this), audio_(this) {
    profile_ = Profile::load();
    setWindowTitle("Lazarus Share — sem login"); resize(940, 730); time_.start();
    auto *root = new QWidget(this); auto *layout = new QVBoxLayout(root); setCentralWidget(root);
    setupUpdates(layout);
    auto *top = new QHBoxLayout; top->addStretch(); identity_ = new QPushButton; identity_->setObjectName("profile"); top->addWidget(identity_); layout->addLayout(top);
    connect(identity_, &QPushButton::clicked, this, &Window::editIdentity); updateIdentity();
    auto *form = new QFormLayout;
    const QString hostedEndpoint = "wss://share.app.lazaruslabs.com.br/ws";
    endpoint_ = qEnvironmentVariable("LAZARUS_SIGNAL_URL", hostedEndpoint).trimmed();
    const bool hosted = endpoint_ == hostedEndpoint;
    stun_ = qEnvironmentVariable("LAZARUS_STUN_URL", hosted ? "stun://share.app.lazaruslabs.com.br:3478" : "");
    tlsPin_ = qEnvironmentVariable("LAZARUS_TLS_PIN", "");
    token_ = new QLineEdit; token_->setPlaceholderText("Cole o link do convite");
    token_->setObjectName("invite");
    form->addRow("Link do convite", token_); layout->addLayout(form);
    auto *buttons = new QHBoxLayout;
    create_ = new QPushButton("Criar sala"); join_ = new QPushButton("Entrar com link"); stop_ = new QPushButton("Encerrar / sair");
    auto *copy = new QPushButton("Copiar link"); auto *exportButton = new QPushButton("Exportar diagnóstico");
    for (auto *b : {create_, join_, copy, stop_, exportButton}) buttons->addWidget(b);
    layout->addLayout(buttons);
    requireApproval_ = new QCheckBox("Novos espectadores precisam de aprovação");
    requireApproval_->setObjectName("requireApproval"); layout->addWidget(requireApproval_);
    monitor_ = new QComboBox(root); monitor_->hide();
    for (auto *screen : QGuiApplication::screens()) monitor_->addItem(screen->name());
    preset_ = new QComboBox(root); preset_->setObjectName("preset"); preset_->addItems({"Baixa — 720p / 30 FPS", "Alta — 1080p / 60 FPS", "Nativo — resolução do monitor / 60 FPS"}); preset_->setCurrentIndex(1); preset_->hide();
    // Internal targets are retained for diagnostics, never exposed as quality fields.
    width_ = new QSpinBox(root); height_ = new QSpinBox(root); fps_ = new QSpinBox(root); bitrate_ = new QSpinBox(root);
    for (auto *spin : {width_, height_, fps_, bitrate_}) { spin->setRange(1, 100000); spin->hide(); }
    auto *controls = new QHBoxLayout;
    share_ = new QPushButton("Compartilhar tela"); pause_ = new QPushButton("Parar compartilhamento"); change_ = new QPushButton("Monitor / qualidade");
    controls->addWidget(share_); controls->addWidget(pause_); controls->addWidget(change_); layout->addLayout(controls);
    share_->setEnabled(false); pause_->setEnabled(false); change_->setEnabled(false);
    connect(share_, &QPushButton::clicked, this, &Window::share);
    connect(pause_, &QPushButton::clicked, this, &Window::stopSharing);
    connect(change_, &QPushButton::clicked, this, &Window::share);
    auto *test = new QCheckBox("Vídeo de teste (diagnóstico de conexão)"); test->setObjectName("testPattern"); layout->addWidget(test);
    status_ = new QLabel("Pronto. Vídeo/áudio não são gravados. Servidores processam metadados de conexão.");
    status_->setWordWrap(true); layout->addWidget(status_);
    status_->setObjectName("status");
    auto *middleWidget = new QWidget; auto *middle = new QHBoxLayout(middleWidget);
    auto *left = new QVBoxLayout; left->addWidget(new QLabel("Participantes — selecione para gerenciar"));
    viewers_ = new QListWidget; left->addWidget(viewers_);
    viewers_->setObjectName("viewers");
    auto *actions = new QHBoxLayout; approve_ = new QPushButton("Aprovar"); remove_ = new QPushButton("Remover"); relay_ = new QPushButton("Tentar novamente");
    approve_->hide(); actions->addWidget(approve_); actions->addWidget(remove_); actions->addWidget(relay_); left->addLayout(actions);
    audioStatus_ = new QLabel(audio_.supported() ? "Áudio desligado. Marque somente aplicativos autorizados." : audio_.limitation()); audioStatus_->setWordWrap(true);
    left->addWidget(audioStatus_); apps_ = new QListWidget; left->addWidget(apps_); middle->addLayout(left, 1);
    viewerPanel_ = new ViewerPanel; video_ = viewerPanel_->video();
    middle->addWidget(viewerPanel_,2); layout->addWidget(middleWidget,1);
    connect(viewerPanel_,&ViewerPanel::playbackChanged,this,[this]{
        for(auto &[id,c]:peers_)if(c->media)c->media->playbackVolume(viewerPanel_->volume(),viewerPanel_->muted());
    });
    metrics_ = new QLabel("Upload: 0 kbps"); metrics_->setWordWrap(true); layout->addWidget(metrics_);
    metrics_->setObjectName("metrics");
    connect(create_, &QPushButton::clicked, this, &Window::create);
    connect(join_, &QPushButton::clicked, this, &Window::join);
    connect(stop_, &QPushButton::clicked, this, &Window::stop);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(token_->text()); });
    connect(exportButton, &QPushButton::clicked, this, &Window::diagnostics);
    auto *clearLogs = new QPushButton("Apagar diagnósticos locais"); layout->addWidget(clearLogs);
    connect(clearLogs, &QPushButton::clicked, this, [this] { log_.clear(); notice("Diagnósticos locais apagados."); });
    connect(test, &QCheckBox::toggled, this, [this](bool on) { if (!active_) testPattern_ = on; });
    connect(approve_, &QPushButton::clicked, this, [this] {
        auto id = viewers_->currentItem() ? viewers_->currentItem()->data(Qt::UserRole).toString() : QString(); if (!administrator_ || id.isEmpty() || id == participantId_) return;
        if (!approved_.contains(id) && approved_.size() >= 4) { notice("Limite de quatro viewers atingido."); return; }
        approved_.insert(id); send({{"type", "approve"}, {"peer", id}});
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        auto id = viewers_->currentItem() ? viewers_->currentItem()->data(Qt::UserRole).toString() : QString(); if (!administrator_ || id.isEmpty() || id == participantId_) return;
        approved_.remove(id); peers_.erase(id); send({{"type", "remove"}, {"peer", id}}); row(id, "Removido localmente");
    });
    connect(relay_, &QPushButton::clicked, this, &Window::relay);
    connect(&capture_, &Capture::ready, this, [this] {
        if (!active_ || !sender() || !capturePending_) { capture_.stop(); return; }
        capturePending_ = false; sharing_ = true; share_->setEnabled(false); pause_->setEnabled(true); change_->setEnabled(true);
        capture_.quality(quality()); refreshAudio(); log("sharing_started", {}, {{"preset",preset_->currentIndex()},{"width",capture_.dimensions().width()},{"height",capture_.dimensions().height()},{"target_fps",quality().fps},{"target_kbps",quality().kbps}});
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
void Window::notice(const QString &text) { status_->setText(text); }
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
    token_->setText(Protocol::inviteLink(secret_)); token_->setReadOnly(true); create_->setEnabled(false); join_->setEnabled(false); stop_->setEnabled(true);
    notice("Criando sala, sem compartilhar tela.");
    viewerState_="Conectando"; updatePresentation(); refreshAudio(); log("room_create"); openSocket();
}
void Window::join() {
    if (active_ || !profile_.valid()) return;
    secret_ = Protocol::inviteSecret(token_->text()); if (secret_.isEmpty()) { notice("Link de convite inválido."); return; }
    requireApproval_->setEnabled(false); approve_->hide();
    administrator_ = false; active_ = true; room_ = Protocol::room(secret_); challenge_ = Protocol::randomHex(16);
    create_->setEnabled(false); join_->setEnabled(false); token_->setReadOnly(true); stop_->setEnabled(true);
    audio_.stop(); viewerState_="Conectando"; updatePresentation(); refreshAudio(); notice("Conectando à sala…"); openSocket();
}
void Window::openInvite(const QString &link) {
    auto invitedSecret = Protocol::inviteSecret(link);
    if (invitedSecret.isEmpty()) { notice("Link de convite inválido."); return; }
    showNormal(); raise(); activateWindow();
    if (active_ && invitedSecret == secret_) return;
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
    viewerPanel_->clearFrame("Sessão encerrada."); metrics_->setText("Upload: 0 kbps"); notice("Sessão encerrada.");
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
    QPixmap circle(20,20); circle.fill(Qt::transparent); QPainter painter(&circle); painter.setBrush(avatarColor(avatar)); painter.setPen(Qt::NoPen); painter.drawEllipse(2,2,16,16); painter.end();
    for (int i = 0; i < viewers_->count(); ++i) if (viewers_->item(i)->data(Qt::UserRole).toString() == id) { viewers_->item(i)->setText(name + " — " + text); viewers_->item(i)->setIcon(QIcon(circle)); return; }
    auto *item = new QListWidgetItem(QIcon(circle), name + " — " + text, viewers_); item->setData(Qt::UserRole, id); if (!viewers_->currentItem()) viewers_->setCurrentItem(item);
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
}
void Window::selectAudio() {
    if (refreshingAudio_ || !sender() || !active_ || !sharing_) return;
    QSet<QString> selected;
    for (int i = 0; i < apps_->count(); ++i) if (apps_->item(i)->checkState() == Qt::Checked) selected.insert(apps_->item(i)->data(Qt::UserRole).toString());
    audio_.select(selected); refreshAudio(); audioStatus_->setText(audio_.selected().isEmpty() ? "Áudio desligado." : "Transmitindo somente os aplicativos marcados.");
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
    QPixmap image(22,22); image.fill(Qt::transparent); QPainter p(&image); p.setPen(Qt::NoPen); p.setBrush(avatarColor(profile_.avatar)); p.drawEllipse(2,2,18,18); p.end(); identity_->setIcon(QIcon(image));
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
    remove_->setVisible(administrator_); approve_->setVisible(administrator_ && roomRequiresApproval_);
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
