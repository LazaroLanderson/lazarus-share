#include "capture.h"
#include "encoder.h"
#include "protocol.h"
#include <QGuiApplication>
#include <QScreen>
#include <QMutexLocker>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
#ifndef Q_OS_WIN
#include "portal.h"
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusArgument>
#include <QDBusUnixFileDescriptor>
#include <fcntl.h>
#include <unistd.h>
#endif

Capture::Capture(QObject *parent) : QObject(parent) {
    timer_.setInterval(200);
    connect(&timer_, &QTimer::timeout, this, [this] {
        if (!pipeline_) return;
        auto *bus = gst_element_get_bus(pipeline_);
        auto *m = gst_bus_pop_filtered(bus, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
        if (m) {
            if (GST_MESSAGE_TYPE(m) == GST_MESSAGE_ERROR) {
                GError *e = nullptr; gst_message_parse_error(m, &e, nullptr);
                emit error(QString::fromUtf8(e->message)); g_error_free(e);
            } else emit error("A captura foi encerrada pelo sistema.");
            gst_message_unref(m);
        }
        gst_object_unref(bus);
    });
}
Capture::~Capture() { stop(); }
void Capture::stop() {
    requested_ = false;
    ++operation_;
    timer_.stop();
    if (pipeline_) { gst_element_set_state(pipeline_, GST_STATE_NULL); gst_object_unref(pipeline_); pipeline_ = nullptr; }
    if (filter_) { gst_object_unref(filter_); filter_ = nullptr; }
    { QMutexLocker lock(&mutex_); if (latest_) gst_sample_unref(latest_); latest_ = nullptr;if(nv12_)gst_sample_unref(nv12_);nv12_=nullptr; }
#ifndef Q_OS_WIN
    for (auto it = requests_.begin(); it != requests_.end(); ++it) {
        QDBusInterface request("org.freedesktop.portal.Desktop", it.key(), "org.freedesktop.portal.Request");
        request.asyncCall("Close");
        QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", it.key(), "org.freedesktop.portal.Request", "Response", this, SLOT(response(uint,QVariantMap,QDBusMessage)));
    }
    requests_.clear();
    if (!session_.isEmpty()) {
        QDBusInterface session("org.freedesktop.portal.Desktop", session_, "org.freedesktop.portal.Session");
        session.asyncCall("Close"); session_.clear();
    }
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
#endif
}
void Capture::start(int monitor, Quality quality, bool testPattern) {
    stop(); requested_ = true; quality_ = quality;
    rawFormat_ = "I420";
    // I420 forces the VA upload path to renegotiate allocation on size changes.
    // NV12 passthrough in GStreamer 1.24 can retain a pool with the old dimensions.

    auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) { emit error("Nenhum monitor disponível."); return; }
    monitor = qBound(0, monitor, int(screens.size()) - 1);
    sourceSize_ = screens[monitor]->size() * screens[monitor]->devicePixelRatio();
    if (testPattern) { sourceSize_ = QSize(1920,1080); launch("videotestsrc is-live=true pattern=ball"); return; }
#ifdef Q_OS_WIN
    struct Selection { QString name; HMONITOR handle = nullptr; } selection{screens[monitor]->name()};
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR handle, HDC, LPRECT, LPARAM context) -> BOOL {
        auto *selection = reinterpret_cast<Selection *>(context); MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        if (GetMonitorInfoW(handle, &info) && QString::fromWCharArray(info.szDevice) == selection->name) selection->handle = handle;
        return TRUE;
    }, reinterpret_cast<LPARAM>(&selection));
    launch(selection.handle ? QString("d3d11screencapturesrc monitor-handle=%1 show-cursor=true ! d3d11download").arg(quintptr(selection.handle))
                            : QString("d3d11screencapturesrc monitor-index=%1 show-cursor=true ! d3d11download").arg(monitor));
#else
    if (qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland" || QGuiApplication::platformName().contains("wayland")) {
        selectPortal();
    } else {
        auto r = screens[monitor]->geometry();
        launch(QString("ximagesrc use-damage=false show-pointer=true startx=%1 starty=%2 endx=%3 endy=%4")
            .arg(r.left()).arg(r.top()).arg(r.right()).arg(r.bottom()));
    }
#endif
}
void Capture::quality(Quality quality) {
    const auto previousSize=dimensions_;const int previousFps=quality_.fps;
    quality_ = quality;
    QSize fit = sourceSize_;
    if (fit.width() > quality.width || fit.height() > quality.height) fit.scale(QSize(quality.width, quality.height), Qt::KeepAspectRatio);
    dimensions_ = QSize(qMax(2, fit.width() & ~1), qMax(2, fit.height() & ~1));
    if (filter_) {
        GstState state=GST_STATE_NULL;if(pipeline_)gst_element_get_state(pipeline_,&state,nullptr,0);
        // Flush negotiated source/rate caps before changing resolution or FPS.
        // Updating a live capsfilter alone can retain the previous input rate.
        bool resume=state>=GST_STATE_PAUSED && (previousSize!=dimensions_ || previousFps!=quality.fps);
        if(resume && gst_element_set_state(pipeline_,GST_STATE_READY)==GST_STATE_CHANGE_FAILURE){emit error("Não foi possível preparar a alteração da captura.");return;}
        auto *caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, rawFormat_.toUtf8().constData(), "width", G_TYPE_INT, dimensions_.width(),
            "height", G_TYPE_INT, dimensions_.height(), "framerate", GST_TYPE_FRACTION, quality.fps, 1, nullptr);
        g_object_set(filter_, "caps", caps, nullptr); gst_caps_unref(caps);
        if(resume && gst_element_set_state(pipeline_,GST_STATE_PLAYING)==GST_STATE_CHANGE_FAILURE)emit error("Não foi possível retomar a captura com essa qualidade.");
    }
}
void Capture::launch(const QString &source) {
    GError *e = nullptr;
    auto text = source + " ! queue max-size-buffers=2 leaky=downstream ! videorate drop-only=true ! videoconvert ! videoscale add-borders=false ! capsfilter name=quality ! appsink name=frames emit-signals=true sync=false max-buffers=1 drop=true";
    pipeline_ = gst_parse_launch(text.toUtf8().constData(), &e);
    if (e) { emit error(QString::fromUtf8(e->message)); g_error_free(e); stop(); return; }
    filter_ = gst_bin_get_by_name(GST_BIN(pipeline_), "quality"); quality(quality_);
    auto *sink = gst_bin_get_by_name(GST_BIN(pipeline_), "frames");
    g_signal_connect(sink, "new-sample", G_CALLBACK(sample), this); gst_object_unref(sink);
    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        emit error("Captura indisponível. Verifique PipeWire/portal ou o plugin de captura Windows."); stop(); return;
    }
    timer_.start(); emit ready();
}
GstFlowReturn Capture::sample(GstAppSink *sink, gpointer data) {
    auto *self = static_cast<Capture *>(data);
    auto *sample = gst_app_sink_pull_sample(sink); if (!sample) return GST_FLOW_EOS;
    QMutexLocker lock(&self->mutex_);
    if (self->latest_) {gst_sample_unref(self->latest_);++self->discarded_;}
    self->latest_ = sample;
    if(self->nv12_)gst_sample_unref(self->nv12_);
    QElapsedTimer clock;clock.start();
    bool required=self->nv12Required_.load();
    self->nv12_=required?self->preparer_.nv12(sample):nullptr;
    if(required && !self->nv12_)++self->preparationDiscarded_;
    if(self->nv12_){self->prepareNs_+=clock.nsecsElapsed();++self->prepared_;}
    return GST_FLOW_OK;
}
QJsonObject Capture::takeMetrics() {
    QMutexLocker lock(&mutex_);QJsonObject result{{"frames_prepare_discarded",int(preparationDiscarded_)},{"frames_discarded",int(discarded_)},{"prepare_us",prepared_?double(prepareNs_)/prepared_/1000:0}};
    discarded_=prepared_=preparationDiscarded_=0;prepareNs_=0;return result;
}
GstSample *Capture::takeVideo(GstSample **nv12) {
    QMutexLocker lock(&mutex_); auto *out = latest_; latest_ = nullptr;
    if(nv12){*nv12=nv12_;nv12_=nullptr;}else if(nv12_){gst_sample_unref(nv12_);nv12_=nullptr;}return out;
}
#ifndef Q_OS_WIN
void Capture::request(const QString &method, const QVariantList &args, const QVariantMap &extra,
                      std::function<void(QVariantMap)> callback) {
    QString token = "lazarus" + Protocol::randomHex(8);
    QString sender = QDBusConnection::sessionBus().baseService().mid(1).replace('.', '_');
    QString path = "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
    requests_.insert(path, std::move(callback));
    if (!QDBusConnection::sessionBus().connect("org.freedesktop.portal.Desktop", path, "org.freedesktop.portal.Request", "Response", this, SLOT(response(uint,QVariantMap,QDBusMessage)))) {
        requests_.remove(path); emit error("Não foi possível observar o portal ScreenCast."); return;
    }
    QVariantMap options = extra; options["handle_token"] = token;
    QVariantList callArgs = args; callArgs.append(options);
    QDBusInterface portal("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.ScreenCast");
    auto *watcher = new QDBusPendingCallWatcher(portal.asyncCallWithArgumentList(method, callArgs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        if (reply.isError() && requests_.contains(path)) { requests_.remove(path); emit error("Portal ScreenCast indisponível: " + reply.error().message()); }
        w->deleteLater();
    });
}
void Capture::response(uint code, QVariantMap results, const QDBusMessage &message) {
    auto it = requests_.find(message.path()); if (it == requests_.end()) return;
    auto callback = it.value(); requests_.erase(it);
    QDBusConnection::sessionBus().disconnect("org.freedesktop.portal.Desktop", message.path(), "org.freedesktop.portal.Request", "Response", this, SLOT(response(uint,QVariantMap,QDBusMessage)));
    if (code != 0) { emit error(code == 1 ? "Seleção de tela cancelada." : "Portal recusou a captura."); return; }
    callback(results);
}
void Capture::selectPortal() {
    request("CreateSession", {}, {{"session_handle_token", "lazarus" + Protocol::randomHex(8)}}, [this](QVariantMap r) {
        session_ = qvariant_cast<QDBusObjectPath>(r["session_handle"]).path();
        if (session_.isEmpty()) session_ = r["session_handle"].toString();
        if (session_.isEmpty()) { emit error("Portal não retornou uma sessão."); return; }
        QDBusInterface portal("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.ScreenCast");
        auto modes = portal.property("AvailableCursorModes").toUInt();
        request("SelectSources", {QVariant::fromValue(QDBusObjectPath(session_))},
            {{"types", uint(1)}, {"multiple", false}, {"cursor_mode", uint(modes & 2 ? 2 : 1)}}, [this](QVariantMap) {
            request("Start", {QVariant::fromValue(QDBusObjectPath(session_)), QString()}, {}, [this](QVariantMap r) {
                auto selection = readPortalSelection(r);
                auto node = selection.node;
                if (!node) { emit error("Portal não retornou um monitor."); return; }
                if (selection.size.isValid()) sourceSize_ = selection.size;
                QDBusInterface portal("org.freedesktop.portal.Desktop", "/org/freedesktop/portal/desktop", "org.freedesktop.portal.ScreenCast");
                auto *watcher = new QDBusPendingCallWatcher(portal.asyncCall("OpenPipeWireRemote", QVariant::fromValue(QDBusObjectPath(session_)), QVariantMap()), this);
                auto serial = selection.serial;
                auto operation = operation_;
                connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, node, serial, operation](QDBusPendingCallWatcher *w) {
                    if (!requested_ || operation != operation_) { w->deleteLater(); return; }
                    QDBusPendingReply<QDBusUnixFileDescriptor> reply = *w;
                    if (reply.isError()) emit error("Não foi possível abrir o fluxo PipeWire.");
                    else {
                        fd_ = fcntl(reply.value().fileDescriptor(), F_DUPFD_CLOEXEC, 3);
                        if (fd_ < 0) emit error("Não foi possível acessar o descritor PipeWire.");
                        else launch(serial ? QString("pipewiresrc fd=%1 target-object=%2 do-timestamp=true").arg(fd_).arg(serial)
                                           : QString("pipewiresrc fd=%1 path=%2 do-timestamp=true").arg(fd_).arg(node));
                    }
                    w->deleteLater();
                });
            });
        });
    });
}
#endif
