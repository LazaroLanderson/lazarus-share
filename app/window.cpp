#include "window.h"
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

Window::Window() : capture_(this), audio_(this) {
    setWindowTitle("Lazarus Share — sem login"); resize(940, 730); time_.start();
    auto *root = new QWidget(this); auto *layout = new QVBoxLayout(root); setCentralWidget(root);
    auto *form = new QFormLayout;
    endpoint_ = new QLineEdit(qEnvironmentVariable("LAZARUS_SIGNAL_URL", "ws://127.0.0.1:8080/ws"));
    stun_ = new QLineEdit(qEnvironmentVariable("LAZARUS_STUN_URL"));
    tlsPin_ = new QLineEdit(qEnvironmentVariable("LAZARUS_TLS_PIN"));
    tlsPin_->setObjectName("tlsPin");
    tlsPin_->setPlaceholderText("Opcional: copie a impressão SHA-256 do servidor local");
    token_ = new QLineEdit; token_->setPlaceholderText("Token do convite (26 caracteres)");
    endpoint_->setObjectName("endpoint"); token_->setObjectName("invite");
    form->addRow("Servidor de salas", endpoint_); form->addRow("STUN próprio (stun://host:3478)", stun_);
    form->addRow("Certificado local (SHA-256)", tlsPin_);
    form->addRow("Convite", token_); layout->addLayout(form);
    auto *buttons = new QHBoxLayout;
    create_ = new QPushButton("Criar sala"); join_ = new QPushButton("Entrar com token"); stop_ = new QPushButton("Encerrar / sair");
    auto *copy = new QPushButton("Copiar token"); auto *exportButton = new QPushButton("Exportar diagnóstico");
    for (auto *b : {create_, join_, copy, stop_, exportButton}) buttons->addWidget(b);
    layout->addLayout(buttons);
    auto *controls = new QHBoxLayout; monitor_ = new QComboBox;
    for (auto *s : QGuiApplication::screens()) monitor_->addItem(s->name());
    controls->addWidget(new QLabel("Monitor")); controls->addWidget(monitor_);
    auto spin = [controls](const QString &label, int min, int max, int value) {
        controls->addWidget(new QLabel(label)); auto *s = new QSpinBox; s->setRange(min, max); s->setValue(value); controls->addWidget(s); return s;
    };
    width_ = spin("Largura máx.", 320, 7680, 1920); height_ = spin("Altura máx.", 180, 4320, 1080);
    fps_ = spin("FPS", 1, 120, 60); bitrate_ = spin("kbps/viewer", 100, 100000, 8000);
    auto *apply = new QPushButton("Aplicar"); controls->addWidget(apply); layout->addLayout(controls);
    width_->setObjectName("width"); height_->setObjectName("height"); fps_->setObjectName("fps"); bitrate_->setObjectName("bitrate");
    auto *test = new QCheckBox("Vídeo de teste (diagnóstico de conexão)"); layout->addWidget(test);
    status_ = new QLabel("Pronto. Vídeo/áudio não são gravados. Servidores processam metadados de conexão.");
    status_->setWordWrap(true); layout->addWidget(status_);
    status_->setObjectName("status");
    auto *middle = new QHBoxLayout;
    auto *left = new QVBoxLayout; left->addWidget(new QLabel("Viewers — selecione para aprovar, remover ou autorizar relay"));
    viewers_ = new QListWidget; left->addWidget(viewers_);
    viewers_->setObjectName("viewers");
    auto *actions = new QHBoxLayout; approve_ = new QPushButton("Aprovar"); remove_ = new QPushButton("Remover"); relay_ = new QPushButton("Autorizar relay");
    actions->addWidget(approve_); actions->addWidget(remove_); actions->addWidget(relay_); left->addLayout(actions);
    audioStatus_ = new QLabel(audio_.supported() ? "Áudio desligado. Marque somente aplicativos autorizados." : audio_.limitation()); audioStatus_->setWordWrap(true);
    left->addWidget(audioStatus_); apps_ = new QListWidget; left->addWidget(apps_); middle->addLayout(left, 1);
    video_ = new QLabel("O viewer verá a tela aqui."); video_->setAlignment(Qt::AlignCenter); video_->setMinimumSize(400, 260);
    video_->setStyleSheet("background: #151515; color: white"); middle->addWidget(video_, 2); layout->addLayout(middle);
    video_->setObjectName("video");
    metrics_ = new QLabel("Upload: 0 kbps"); metrics_->setWordWrap(true); layout->addWidget(metrics_);
    metrics_->setObjectName("metrics");
    connect(create_, &QPushButton::clicked, this, &Window::create);
    connect(join_, &QPushButton::clicked, this, &Window::join);
    connect(stop_, &QPushButton::clicked, this, &Window::stop);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(token_->text()); });
    connect(exportButton, &QPushButton::clicked, this, &Window::diagnostics);
    connect(apply, &QPushButton::clicked, this, &Window::applyQuality);
    connect(test, &QCheckBox::toggled, this, [this](bool on) { if (!active_) testPattern_ = on; });
    connect(approve_, &QPushButton::clicked, this, [this] {
        auto id = selectedPeer(); if (!host_ || id.isEmpty()) return;
        if (!approved_.contains(id) && approved_.size() >= 4) { notice("Limite de quatro viewers atingido."); return; }
        approved_.insert(id); send({{"type", "approve"}, {"peer", id}});
    });
    connect(remove_, &QPushButton::clicked, this, [this] {
        auto id = selectedPeer(); if (!host_ || id.isEmpty()) return;
        approved_.remove(id); peers_.erase(id); send({{"type", "remove"}, {"peer", id}}); row(id, "Removido localmente");
    });
    connect(relay_, &QPushButton::clicked, this, &Window::relay);
    connect(&capture_, &Capture::ready, this, [this] { if (active_) openSocket(); });
    connect(&capture_, &Capture::error, this, [this](QString error) { stop(); notice(error); });
    connect(&audio_, &Audio::error, this, &Window::notice);
    connect(&audio_, &Audio::changed, this, &Window::refreshAudio);
    connect(apps_, &QListWidget::itemChanged, this, [this] { selectAudio(); });
    connect(&socket_, &QWebSocket::connected, this, [this] {
        socketLost_ = -1; reconnects_ = 0;
        auto pin = tlsPin_->text().trimmed().remove(':').remove(' ').toLower();
        if (!pin.isEmpty() && socket_.sslConfiguration().peerCertificate().digest(QCryptographicHash::Sha256).toHex() != pin.toLatin1()) {
            stop(); notice("O certificado do servidor não corresponde à impressão informada. Conexão recusada."); return;
        }
        if (!active_) { socket_.close(); return; }
        QJsonObject m{{"type", host_ ? (created_ ? "resume" : "create") : "join"}, {"room", room_}, {"challenge", challenge_}};
        if (host_) m["admin"] = admin_; send(m);
    });
    connect(&socket_, &QWebSocket::sslErrors, this, [this](const QList<QSslError> &errors) {
        auto pin = tlsPin_->text().trimmed().remove(':').remove(' ').toLower();
        if (pin.size() != 64 || socket_.sslConfiguration().peerCertificate().digest(QCryptographicHash::Sha256).toHex() != pin.toLatin1()) {
            notice("Certificado TLS não confiável. Para o servidor local, copie sua impressão SHA-256."); return;
        }
        for (const auto &error : errors) {
            if (error.error() != QSslError::SelfSignedCertificate && error.error() != QSslError::SelfSignedCertificateInChain) {
                notice("Certificado local recusado: " + error.errorString()); return;
            }
        }
        socket_.ignoreSslErrors(errors);
    });
    connect(&socket_, &QWebSocket::textMessageReceived, this, [this](const QString &text) {
        auto doc = QJsonDocument::fromJson(text.toUtf8()); if (doc.isObject()) message(doc.object());
    });
    connect(&socket_, &QWebSocket::disconnected, this, [this] {
        if (active_ && socketLost_ < 0) { socketLost_ = time_.elapsed(); notice("Sinalização desconectada; tentando reconectar."); }
    });
    connect(&socket_, qOverload<QAbstractSocket::SocketError>(&QWebSocket::error), this, [this](QAbstractSocket::SocketError) {
        if (active_ && socketLost_ < 0) socketLost_ = time_.elapsed();
        notice("Não foi possível conectar ao serviço de salas. Confira endereço e certificado TLS.");
    });
    frameTimer_.setTimerType(Qt::PreciseTimer); frameTimer_.setInterval(8); connect(&frameTimer_, &QTimer::timeout, this, &Window::tick); frameTimer_.start();
    maintenance_.setInterval(1000); connect(&maintenance_, &QTimer::timeout, this, [this] {
        if (!active_) return;
        qint64 now = time_.elapsed();
        if (socketLost_ >= 0) {
            if (now - socketLost_ >= 55000) { stop(); notice("Servidor indisponível; sessão encerrada. Crie uma nova sala."); return; }
            if (socket_.state() == QAbstractSocket::UnconnectedState && reconnects_++ % 3 == 0) {
                if (!host_) { peers_.clear(); viewers_->clear(); viewerId_.clear(); challenge_ = Protocol::randomHex(16); }
                else challenge_ = Protocol::randomHex(16);
                openSocket();
            }
        }
        for (auto &[id, c] : peers_) {
            if (c->media && !c->media->connected() && now - c->started >= 20000) {
                c->failed = true; row(id, "Conexão falhou. Relay disponível mediante autorização dos dois lados.");
                if (host_ && c->retries < 2) {
                    ++c->retries; ++c->generation;
                    signal(id, {{"kind", "restart"}, {"generation", c->generation}, {"relay", !c->turns.isEmpty()}});
                    startPeer(id);
                }
            } else if (c->media && c->media->connected()) c->started = now;
        }
        if (socket_.state() == QAbstractSocket::ConnectedState) socket_.ping();
    }); maintenance_.start(); refreshAudio(); stop_->setEnabled(false);
}
Window::~Window() { stop(); }
void Window::notice(const QString &text) { status_->setText(text); }
Quality Window::quality() const { return {width_->value() & ~1, height_->value() & ~1, fps_->value(), bitrate_->value()}; }
void Window::create() {
    if (active_) return;
    host_ = active_ = true; created_ = false;
    secret_ = Protocol::randomBytes(16); room_ = Protocol::room(secret_); admin_ = Protocol::randomHex(32); challenge_ = Protocol::randomHex(16);
    token_->setText(Protocol::token(secret_)); token_->setReadOnly(true); create_->setEnabled(false); join_->setEnabled(false); stop_->setEnabled(true);
    notice("Selecione o monitor no diálogo do sistema. Captura começará após a autorização.");
    refreshAudio();
    capture_.start(monitor_->currentIndex(), quality(), testPattern_);
}
void Window::join() {
    if (active_) return;
    secret_ = Protocol::secret(token_->text()); if (secret_.isEmpty()) { notice("Token inválido: use os 26 caracteres do convite."); return; }
    host_ = false; active_ = true; room_ = Protocol::room(secret_); challenge_ = Protocol::randomHex(16);
    create_->setEnabled(false); join_->setEnabled(false); token_->setReadOnly(true); stop_->setEnabled(true);
    audio_.stop(); refreshAudio(); openSocket();
}
void Window::stop() {
    if (active_ && host_ && socket_.state() == QAbstractSocket::ConnectedState) send({{"type", "end"}});
    active_ = false; created_ = false; socketLost_ = -1; socket_.close(); peers_.clear(); approved_.clear(); seenSessions_.clear(); viewers_->clear();
    capture_.stop(); audio_.stop(); refreshAudio();
    secret_.fill(0); secret_.clear(); room_.clear(); admin_.clear(); viewerId_.clear(); token_->clear(); token_->setReadOnly(false);
    create_->setEnabled(true); join_->setEnabled(true); stop_->setEnabled(false);
    endpoint_->setEnabled(true); stun_->setEnabled(true); tlsPin_->setEnabled(true);
    video_->setPixmap({}); video_->setText("Sessão encerrada."); metrics_->setText("Upload: 0 kbps"); notice("Sessão encerrada.");
}
void Window::openSocket() {
    QUrl url(endpoint_->text().trimmed()); auto host = url.host();
    bool loopback = host == "127.0.0.1" || host == "localhost" || host == "::1";
    if (!url.isValid() || url.path() != "/ws" || (url.scheme() != "wss" && !(url.scheme() == "ws" && loopback)) || !url.userInfo().isEmpty()) {
        stop(); notice("Use wss://servidor/ws. ws:// é permitido apenas em localhost para testes."); return;
    }
    auto pin = tlsPin_->text().trimmed().remove(':').remove(' ').toLower();
    if (!pin.isEmpty() && (url.scheme() != "wss" || pin.size() != 64 || QByteArray::fromHex(pin.toLatin1()).size() != 32 || QByteArray::fromHex(pin.toLatin1()).toHex() != pin.toLatin1())) {
        stop(); notice("Impressão inválida: use os 64 caracteres SHA-256 e um endereço wss://."); return;
    }
    endpoint_->setEnabled(false); stun_->setEnabled(false); tlsPin_->setEnabled(false); socket_.open(url);
}
void Window::send(QJsonObject m) {
    if (socket_.state() == QAbstractSocket::ConnectedState) socket_.sendTextMessage(QString::fromUtf8(QJsonDocument(m).toJson(QJsonDocument::Compact)));
}
void Window::row(const QString &id, const QString &text) {
    for (int i = 0; i < viewers_->count(); ++i) if (viewers_->item(i)->data(Qt::UserRole).toString() == id) { viewers_->item(i)->setText(id + " — " + text); return; }
    auto *item = new QListWidgetItem(id + " — " + text, viewers_); item->setData(Qt::UserRole, id); if (!viewers_->currentItem()) viewers_->setCurrentItem(item);
}
QString Window::selectedPeer() const { return host_ ? (viewers_->currentItem() ? viewers_->currentItem()->data(Qt::UserRole).toString() : QString()) : viewerId_; }
void Window::signal(const QString &id, QJsonObject body) {
    auto it = peers_.find(id); if (it == peers_.end()) return;
    if (!body.contains("generation")) body["generation"] = it->second->generation;
    send(it->second->channel.seal(body));
}
void Window::message(const QJsonObject &m) {
    if (!active_) return;
    auto type = m["type"].toString(); auto id = m["peer"].toString();
    if (type == "created") { created_ = true; notice("Sala criada. Envie o token; aprove cada viewer antes de transmitir."); }
    else if (type == "joined") { viewerId_ = id; row(id, "Aguardando aprovação do host"); notice("Aguardando aprovação."); }
    else if (type == "waiting") { row(id, "Aguardando aprovação"); }
    else if (type == "ready") {
        if (!host_ && id != viewerId_) return;
        if (host_ && !approved_.contains(id)) { notice("Viewer sem aprovação local; negociação recusada."); return; }
        auto session = m["session"].toString();
        if (session.size() != 32 || seenSessions_.contains(session) || seenSessions_.size() >= 256) return;
        seenSessions_.insert(session);
        auto c = std::make_unique<Connection>();
        c->channel = Protocol::Channel(secret_, m["session"].toString(), id, challenge_, m["challenge"].toString(), host_);
        peers_[id] = std::move(c); row(id, "Negociando P2P"); startPeer(id);
    } else if (type == "signal") {
        auto it = peers_.find(id); if (it == peers_.end()) return; auto &c = *it->second;
        QJsonObject body;
        if (!c.channel.open(m, body)) { notice("Mensagem rejeitada: autenticação ou proteção contra replay."); return; }
        auto kind = body["kind"].toString(); int generation = body["generation"].toInt();
        if (kind == "restart" && !host_ && generation > c.generation) {
            bool relay = body["relay"].toBool();
            if (relay && (!c.localConsent || !c.remoteConsent)) return;
            if (relay && c.turns.isEmpty()) { c.pendingRestart = generation; return; }
            c.generation = generation; startPeer(id);
        } else if (kind == "relay-consent") {
            c.remoteConsent = body["enabled"].toBool(); row(id, "Outro lado autorizou relay; aguardando ambas as autorizações");
        } else if (generation == c.pendingRestart && c.pendingSignals.size() < 128) {
            c.pendingSignals.append(body);
        } else if (generation == c.generation && c.media) c.media->receive(body);
    } else if (type == "turn") {
        auto it = peers_.find(id); if (it == peers_.end()) return; auto &c = *it->second;
        if (!c.localConsent || !c.remoteConsent) { notice("Configuração de relay não autorizada; ignorada."); return; }
        auto host = m["host"].toString(); auto username = m["username"].toString(); auto password = m["password"].toString();
        if (host.contains('/') || host.contains('@') || host.isEmpty() || username.isEmpty() || password.isEmpty()) return;
        auto user = QString::fromLatin1(QUrl::toPercentEncoding(username)); auto pass = QString::fromLatin1(QUrl::toPercentEncoding(password));
        c.turns = {QString("turn://%1:%2@%3:3478").arg(user, pass, host), QString("turns://%1:%2@%3:443").arg(user, pass, host)};
        if (host_) {
            ++c.generation; c.retries = 0;
            signal(id, {{"kind", "restart"}, {"generation", c.generation}, {"relay", true}}); startPeer(id);
        } else if (c.pendingRestart > c.generation) {
            c.generation = c.pendingRestart; c.pendingRestart = 0; startPeer(id);
            auto pending = c.pendingSignals; c.pendingSignals = {};
            for (auto entry : pending) if (c.media) c.media->receive(entry.toObject());
        }
    } else if (type == "left") {
        peers_.erase(id); approved_.remove(id);
        for (int i = viewers_->count() - 1; i >= 0; --i) if (viewers_->item(i)->data(Qt::UserRole).toString() == id) delete viewers_->takeItem(i);
    } else if (type == "ended") stop();
    else if (type == "host_offline") notice("Host perdeu a sinalização; aguardando reconexão por até 60 segundos.");
    else if (type == "error") {
        QString code = m["code"].toString(); notice("Serviço de salas: " + code);
        if (code == "resume_failed" || code == "room_unavailable" || code == "create_failed") { stop(); notice("Sala indisponível: " + code); }
    }
}
void Window::startPeer(const QString &id) {
    auto it = peers_.find(id); if (it == peers_.end()) return; auto &c = *it->second;
    c.media = std::make_unique<Peer>(host_); c.started = time_.elapsed();
    connect(c.media.get(), &Peer::outgoing, this, [this, id](QJsonObject body) { signal(id, body); });
    connect(c.media.get(), &Peer::error, this, [this, id](QString text) {
        auto it = peers_.find(id); if (it != peers_.end()) it->second->failed = true;
        row(id, "Falha de mídia"); notice(text);
    });
    connect(c.media.get(), &Peer::status, this, [this, id](QString text) { row(id, text); });
    connect(c.media.get(), &Peer::metrics, this, [this, id](QJsonObject values) {
        auto it = peers_.find(id); if (it == peers_.end()) return;
        auto &c = *it->second; c.kbps = values["kbps"].toDouble(); c.route = values["route"].toString();
        c.metrics = values;
        row(id, QString("%1 | %2 kbps | perda %3% | RTT %4 ms | vídeo %5 FPS").arg(c.route).arg(c.kbps,0,'f',0).arg(values["loss_percent"].toDouble(),0,'f',1).arg(values["rtt_ms"].toDouble(),0,'f',0).arg(values["video_fps"].toDouble(),0,'f',1));
    });
    auto q = quality(); auto dimensions = capture_.dimensions();
    if (host_ && dimensions.isValid()) { q.width = dimensions.width(); q.height = dimensions.height(); }
    if (!c.media->start(q, stun_->text().trimmed(), c.turns)) { c.media.reset(); c.failed = true; }
}
void Window::relay() {
    auto id = selectedPeer(); auto it = peers_.find(id);
    if (it == peers_.end()) { notice("Selecione um viewer aprovado."); return; }
    auto &c = *it->second;
    if (!c.failed) { notice("Relay disponível após falha da conexão direta (até 20 segundos)."); return; }
    if (c.localConsent) { notice("Relay já autorizado; aguardando o outro lado."); return; }
    if (QMessageBox::question(this, "Autorizar relay", "O vídeo/áudio criptografado passará pelo seu servidor. Ele verá IPs e volume de tráfego. Autorizar para este viewer nesta sessão?") != QMessageBox::Yes) return;
    c.localConsent = true; signal(id, {{"kind", "relay-consent"}, {"enabled", true}});
    send({{"type", "relay"}, {"peer", id}, {"enabled", true}}); row(id, "Relay autorizado localmente; aguardando o outro lado");
}
void Window::tick() {
    if (!active_) return;
    if (host_) {
        if (auto *sample = capture_.takeVideo()) {
            for (auto &[id, c] : peers_) if (c->media) c->media->video(sample);
            gst_sample_unref(sample); ++frames_;
        }
        for (auto *sample : audio_.takeSamples()) {
            for (auto &[id, c] : peers_) if (c->media) c->media->audio(sample);
            gst_sample_unref(sample);
        }
        if (time_.elapsed() - lastFrameTime_ >= 1000) {
            double total = 0; for (auto &[id, c] : peers_) total += c->kbps;
            auto size = capture_.dimensions();
            metrics_->setText(QString("Captura %1×%2 | %3 FPS de captura | upload total %4 kbps | teto %5 kbps/viewer | %6")
                .arg(size.width()).arg(size.height()).arg(frames_ * 1000.0 / (time_.elapsed() - lastFrameTime_),0,'f',1).arg(total,0,'f',0).arg(bitrate_->value()).arg(peers_.empty() || !peers_.begin()->second->media ? "Aguardando viewer" : peers_.begin()->second->media->encoderName()));
            frames_ = 0; lastFrameTime_ = time_.elapsed();
        }
    } else {
        for (auto &[id, c] : peers_) if (c->media) {
            auto image = c->media->takeFrame();
            if (!image.isNull()) {
                video_->setPixmap(QPixmap::fromImage(image).scaled(video_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
                metrics_->setText(QString("Vídeo recebido: %1×%2 | %3 | %4 FPS decodificados").arg(image.width()).arg(image.height()).arg(c->route).arg(c->metrics["video_fps"].toDouble(),0,'f',1));
            }
        }
    }
}
void Window::applyQuality() {
    if (!host_ || !active_) return;
    auto q = quality(); capture_.quality(q); auto size = capture_.dimensions(); q.width = size.width(); q.height = size.height();
    for (auto &[id, c] : peers_) if (c->media) c->media->quality(q);
}
void Window::refreshAudio() {
    refreshingAudio_ = true; QSet<QString> checked = audio_.selected();
    if (!host_ || !active_) checked.clear();
    apps_->clear();
    for (auto a : audio_.applications()) {
        auto *item = new QListWidgetItem(a.name + " [" + a.id + "]", apps_); item->setData(Qt::UserRole, a.id);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable); item->setCheckState(checked.contains(a.id) ? Qt::Checked : Qt::Unchecked);
    }
    apps_->setEnabled(host_ && active_ && audio_.supported()); refreshingAudio_ = false;
}
void Window::selectAudio() {
    if (refreshingAudio_ || !host_ || !active_) return;
    QSet<QString> selected;
    for (int i = 0; i < apps_->count(); ++i) if (apps_->item(i)->checkState() == Qt::Checked) selected.insert(apps_->item(i)->data(Qt::UserRole).toString());
    audio_.select(selected); refreshAudio(); audioStatus_->setText(audio_.selected().isEmpty() ? "Áudio desligado." : "Transmitindo somente os aplicativos marcados.");
}
void Window::diagnostics() {
    QJsonArray connections;
    for (auto &[id, c] : peers_) connections.append(QJsonObject{{"route", c->route}, {"metrics", c->metrics}, {"relay_local", c->localConsent}, {"relay_remote", c->remoteConsent}, {"failed", c->failed}});
    QJsonObject report{{"version", "0.1.2"}, {"role", host_ ? "host" : "viewer"}, {"connections", connections},
        {"target_width", width_->value()}, {"target_height", height_->value()}, {"target_fps", fps_->value()}, {"target_kbps", bitrate_->value()}, {"audio_selective_available", audio_.supported()}};
    auto path = QFileDialog::getSaveFileName(this, "Exportar diagnóstico sem segredos", "diagnostico.json", "JSON (*.json)");
    if (path.isEmpty()) return; QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QJsonDocument(report).toJson()) < 0) notice("Não foi possível exportar o diagnóstico.");
}
