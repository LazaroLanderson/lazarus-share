#include "audio.h"
#include <QCoreApplication>
#include <QMutexLocker>
#include <gst/app/gstappsrc.h>
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <psapi.h>
#else
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw-utils.h>
#include <spa/utils/result.h>
#include <map>
#endif

struct Audio::Impl {
    QList<AudioApplication> apps;
    bool available = false;
    QString reason;
#ifndef Q_OS_WIN
    pw_thread_loop *loop = nullptr;
    pw_context *context = nullptr;
    pw_core *core = nullptr;
    pw_registry *registry = nullptr;
    spa_hook registryHook{};
    QHash<uint32_t, AudioApplication> nodes;
    QHash<uint32_t, qint64> clients, nodePids;
    QHash<uint32_t, uint32_t> nodeClients;
    struct Client { Impl *owner; uint32_t id; pw_client *proxy; spa_hook hook{}; };
    std::map<uint32_t, std::unique_ptr<Client>> clientObjects;
    static void clientInfo(void *data, const pw_client_info *info) {
        auto *client = static_cast<Client *>(data);
        auto *pid = info->props ? spa_dict_lookup(info->props, PW_KEY_APP_PROCESS_ID) : nullptr;
        if (pid) client->owner->clients[client->id] = QString::fromUtf8(pid).toLongLong();
    }
    struct Stream { pw_stream *stream = nullptr; GstElement *source = nullptr; spa_hook hook{}; };
    std::vector<std::unique_ptr<Stream>> streams;
    static void global(void *data, uint32_t id, uint32_t, const char *type, uint32_t, const spa_dict *props) {
        auto *self = static_cast<Impl *>(data);
        if (!props) return;
        if (!strcmp(type, PW_TYPE_INTERFACE_Client)) {
            auto *pid = spa_dict_lookup(props, PW_KEY_APP_PROCESS_ID);
            if (pid) self->clients[id] = QString::fromUtf8(pid).toLongLong();
            auto c = std::make_unique<Client>(); c->owner = self; c->id = id;
            c->proxy = static_cast<pw_client *>(pw_registry_bind(self->registry, id, PW_TYPE_INTERFACE_Client, PW_VERSION_CLIENT, 0));
            if (c->proxy) {
                static const pw_client_events events{.version = PW_VERSION_CLIENT_EVENTS, .info = clientInfo};
                pw_client_add_listener(c->proxy, &c->hook, &events, c.get()); self->clientObjects[id] = std::move(c);
            }
            return;
        }
        if (strcmp(type, PW_TYPE_INTERFACE_Node)) return;
        auto *media = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
        auto *pid = spa_dict_lookup(props, PW_KEY_APP_PROCESS_ID);
        auto *serial = spa_dict_lookup(props, PW_KEY_OBJECT_SERIAL);
        if (!media || strcmp(media, "Stream/Output/Audio") || !serial) return;
        auto *client = spa_dict_lookup(props, PW_KEY_CLIENT_ID);
        self->nodePids[id] = pid ? QString::fromUtf8(pid).toLongLong() : 0;
        if (client) self->nodeClients[id] = QString::fromUtf8(client).toUInt();
        auto *name = spa_dict_lookup(props, PW_KEY_APP_NAME);
        if (!name) name = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
        if (!name) name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
        quint64 target = QString::fromUtf8(serial).toULongLong();
        self->nodes[id] = {QString::number(target), name ? QString::fromUtf8(name) : QString("Aplicativo %1").arg(target), target};
    }
    static void removed(void *data, uint32_t id) {
        auto *self = static_cast<Impl *>(data);
        self->nodes.remove(id); self->nodePids.remove(id); self->nodeClients.remove(id); self->clients.remove(id);
        if (auto it = self->clientObjects.find(id); it != self->clientObjects.end()) {
            spa_hook_remove(&it->second->hook); pw_proxy_destroy(reinterpret_cast<pw_proxy *>(it->second->proxy)); self->clientObjects.erase(it);
        }
    }
    static void process(void *data) {
        auto *self = static_cast<Stream *>(data);
        auto *pwbuffer = pw_stream_dequeue_buffer(self->stream); if (!pwbuffer) return;
        auto *b = pwbuffer->buffer;
        if (b->n_datas && b->datas[0].data && b->datas[0].chunk) {
            auto &d = b->datas[0]; uint32_t size = qMin(d.chunk->size, d.maxsize);
            if (d.chunk->offset + size <= d.maxsize && size) {
                auto *buffer = gst_buffer_new_allocate(nullptr, size, nullptr);
                gst_buffer_fill(buffer, 0, static_cast<char *>(d.data) + d.chunk->offset, size);
                GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale(size / 8, GST_SECOND, 48000);
                gst_app_src_push_buffer(GST_APP_SRC(self->source), buffer);
            }
        }
        pw_stream_queue_buffer(self->stream, pwbuffer);
    }
    void clearStreams() {
        if (loop) pw_thread_loop_lock(loop);
        for (auto &s : streams) { pw_stream_destroy(s->stream); gst_object_unref(s->source); }
        streams.clear();
        if (loop) pw_thread_loop_unlock(loop);
    }
#endif
};
Audio::Audio(QObject *parent) : QObject(parent), impl_(std::make_unique<Impl>()) {
#ifdef Q_OS_WIN
    using VersionFn = LONG(WINAPI *)(OSVERSIONINFOW *);
    auto version = reinterpret_cast<VersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW info{}; info.dwOSVersionInfoSize = sizeof(info);
    auto *factory = gst_element_factory_find("wasapi2src");
    impl_->available = version && version(&info) == 0 && info.dwBuildNumber >= 20348 && factory;
    if (factory) gst_object_unref(factory);
    if (!impl_->available) impl_->reason = "Áudio por aplicativo indisponível nesta versão do Windows ou no plugin WASAPI. Vídeo continua disponível.";
#else
    pw_init(nullptr, nullptr);
    impl_->loop = pw_thread_loop_new("lazarus-audio", nullptr);
    if (impl_->loop) impl_->context = pw_context_new(pw_thread_loop_get_loop(impl_->loop), nullptr, 0);
    if (impl_->context) impl_->core = pw_context_connect(impl_->context, nullptr, 0);
    if (impl_->core) {
        impl_->registry = pw_core_get_registry(impl_->core, PW_VERSION_REGISTRY, 0);
        static const pw_registry_events events{.version = PW_VERSION_REGISTRY_EVENTS, .global = Impl::global, .global_remove = Impl::removed};
        pw_registry_add_listener(impl_->registry, &impl_->registryHook, &events, impl_.get());
        impl_->available = pw_thread_loop_start(impl_->loop) == 0;
    }
    if (!impl_->available) impl_->reason = "PipeWire não está disponível. Nenhum áudio será capturado.";
#endif
    timer_.setInterval(1000); connect(&timer_, &QTimer::timeout, this, &Audio::refresh); timer_.start();
}
Audio::~Audio() {
    timer_.stop(); stop();
#ifndef Q_OS_WIN
    if (impl_->loop) pw_thread_loop_stop(impl_->loop);
    for (auto &[id, c] : impl_->clientObjects) { spa_hook_remove(&c->hook); pw_proxy_destroy(reinterpret_cast<pw_proxy *>(c->proxy)); }
    impl_->clientObjects.clear();
    if (impl_->registry) { spa_hook_remove(&impl_->registryHook); pw_proxy_destroy(reinterpret_cast<pw_proxy *>(impl_->registry)); }
    if (impl_->core) pw_core_disconnect(impl_->core);
    if (impl_->context) pw_context_destroy(impl_->context);
    if (impl_->loop) pw_thread_loop_destroy(impl_->loop);
#endif
}
bool Audio::supported() const { return impl_->available; }
QString Audio::limitation() const { return impl_->reason; }
QList<AudioApplication> Audio::applications() const { return impl_->apps; }
void Audio::refresh() {
    QList<AudioApplication> next;
#ifdef Q_OS_WIN
    if (impl_->available) {
        IMMDeviceEnumerator *devices = nullptr;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&devices)))) {
            IMMDeviceCollection *collection = nullptr;
            if (SUCCEEDED(devices->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection))) {
                UINT count = 0; collection->GetCount(&count); QSet<QString> seen;
                for (UINT i = 0; i < count; ++i) {
                    IMMDevice *device = nullptr; collection->Item(i, &device); if (!device) continue;
                    IAudioSessionManager2 *manager = nullptr;
                    if (SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void **>(&manager)))) {
                        IAudioSessionEnumerator *sessions = nullptr;
                        if (SUCCEEDED(manager->GetSessionEnumerator(&sessions))) {
                            int n = 0; sessions->GetCount(&n);
                            for (int j = 0; j < n; ++j) {
                                IAudioSessionControl *control = nullptr; sessions->GetSession(j, &control); if (!control) continue;
                                IAudioSessionControl2 *s = nullptr;
                                if (SUCCEEDED(control->QueryInterface(IID_PPV_ARGS(&s)))) {
                                    DWORD pid = 0; s->GetProcessId(&pid);
                                    HANDLE process = pid && pid != GetCurrentProcessId() ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid) : nullptr;
                                    if (process) {
                                        FILETIME created{}, exited{}, kernel{}, user{};
                                        if (GetProcessTimes(process, &created, &exited, &kernel, &user)) {
                                            QString id = QString("%1:%2").arg(pid).arg((quint64(created.dwHighDateTime) << 32) | created.dwLowDateTime);
                                            if (!seen.contains(id)) {
                                                wchar_t path[32768]; DWORD len = 32768;
                                                QString name = QueryFullProcessImageNameW(process, 0, path, &len) ? QString::fromWCharArray(path, len).section('\\', -1) : QString("Processo %1").arg(pid);
                                                next.append({id, name, pid}); seen.insert(id);
                                            }
                                        }
                                        CloseHandle(process);
                                    }
                                    s->Release();
                                }
                                control->Release();
                            }
                            sessions->Release();
                        }
                        manager->Release();
                    }
                    device->Release();
                }
                collection->Release();
            }
            devices->Release();
        }
    }
#else
    if (impl_->available) {
        pw_thread_loop_lock(impl_->loop);
        for (auto it = impl_->nodes.begin(); it != impl_->nodes.end(); ++it) {
            auto pid = impl_->nodePids.value(it.key());
            if (!pid) pid = impl_->clients.value(impl_->nodeClients.value(it.key()));
            if (pid && pid != QCoreApplication::applicationPid()) next.append(it.value());
        }
        pw_thread_loop_unlock(impl_->loop);
    }
#endif
    QSet<QString> existing, previous;
    for (auto &a : next) existing.insert(a.id);
    for (auto &a : impl_->apps) previous.insert(a.id);
    impl_->apps = next;
    auto safe = selected_ & existing;
    if (safe != selected_) select(safe);
    if (existing != previous) emit changed();
    if (pipeline_) {
        auto *bus = gst_element_get_bus(pipeline_);
        if (auto *msg = gst_bus_pop_filtered(bus, GST_MESSAGE_ERROR)) {
            gst_message_unref(msg); stop(); emit error("Captura seletiva de áudio falhou; áudio desligado por segurança."); emit changed();
        }
        gst_object_unref(bus);
    }
}
void Audio::clearPipeline() {
#ifndef Q_OS_WIN
    impl_->clearStreams();
#endif
    if (pipeline_) { gst_element_set_state(pipeline_, GST_STATE_NULL); gst_object_unref(pipeline_); pipeline_ = nullptr; }
    QMutexLocker lock(&mutex_); for (auto *s : samples_) gst_sample_unref(s); samples_.clear();
}
void Audio::stop() { clearPipeline(); selected_.clear(); }
void Audio::select(const QSet<QString> &ids) {
    clearPipeline(); selected_.clear();
    if (!supported() || ids.isEmpty()) return;
    QString text = "audiomixer name=mix ignore-inactive-pads=true latency=10000000 ! audioconvert ! audioresample ! "
        "audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved ! appsink name=samples emit-signals=true sync=false max-buffers=10 drop=true "
        "audiotestsrc is-live=true wave=silence samplesperbuffer=480 ! audio/x-raw,rate=48000,channels=2 ! mix. ";
    QList<AudioApplication> chosen;
    for (const auto &a : impl_->apps) if (ids.contains(a.id)) {
        chosen.append(a);
#ifdef Q_OS_WIN
        text += QString("wasapi2src loopback=true loopback-mode=include-process-tree loopback-target-pid=%1 low-latency=true ! audioconvert ! audioresample ! audio/x-raw,rate=48000,channels=2 ! queue ! mix. ").arg(a.target);
#else
        text += QString("appsrc name=audio%1 is-live=true format=time do-timestamp=true block=false max-buffers=10 leaky-type=downstream "
                        "caps=audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved ! queue max-size-time=100000000 leaky=downstream ! mix. ").arg(a.target);
#endif
    }
    if (chosen.isEmpty()) return;
    GError *e = nullptr; pipeline_ = gst_parse_launch(text.toUtf8().constData(), &e);
    if (e) { g_error_free(e); clearPipeline(); emit error("Plugins de áudio seletivo indisponíveis."); return; }
    auto *sink = gst_bin_get_by_name(GST_BIN(pipeline_), "samples");
    g_signal_connect(sink, "new-sample", G_CALLBACK(sample), this); gst_object_unref(sink);
#ifndef Q_OS_WIN
    pw_thread_loop_lock(impl_->loop);
    bool ok = true;
    for (const auto &a : chosen) {
        auto s = std::make_unique<Impl::Stream>();
        s->source = gst_bin_get_by_name(GST_BIN(pipeline_), QString("audio%1").arg(a.target).toUtf8().constData());
        auto serial = QString::number(a.target).toUtf8();
        auto *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture", PW_KEY_MEDIA_ROLE, "Screen",
            PW_KEY_TARGET_OBJECT, serial.constData(), PW_KEY_STREAM_CAPTURE_SINK, "true", PW_KEY_NODE_DONT_RECONNECT, "true",
            "node.dont-fallback", "true", "node.dont-move", "true",
            PW_KEY_NODE_PASSIVE, "true", PW_KEY_NODE_NAME, "lazarus-share-authorized-audio", nullptr);
        s->stream = pw_stream_new(impl_->core, "Lazarus Share authorized audio", props);
        static const pw_stream_events events{.version = PW_VERSION_STREAM_EVENTS, .process = Impl::process};
        pw_stream_add_listener(s->stream, &s->hook, &events, s.get());
        uint8_t buffer[1024]; spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        spa_audio_info_raw info{}; info.format = SPA_AUDIO_FORMAT_F32; info.rate = 48000; info.channels = 2;
        info.position[0] = SPA_AUDIO_CHANNEL_FL; info.position[1] = SPA_AUDIO_CHANNEL_FR;
        const spa_pod *format = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);
        // No RT_PROCESS: GStreamer allocation and mutexes must not run on a realtime thread.
        int result = pw_stream_connect(s->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
            pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), &format, 1);
        ok &= result >= 0; impl_->streams.push_back(std::move(s));
    }
    pw_thread_loop_unlock(impl_->loop);
    if (!ok) { clearPipeline(); emit error("PipeWire recusou o áudio autorizado."); return; }
#endif
    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) { clearPipeline(); emit error("Áudio autorizado indisponível."); return; }
    for (auto &a : chosen) selected_.insert(a.id);
}
GstFlowReturn Audio::sample(GstAppSink *sink, gpointer data) {
    auto *self = static_cast<Audio *>(data); auto *s = gst_app_sink_pull_sample(sink); if (!s) return GST_FLOW_EOS;
    QMutexLocker lock(&self->mutex_);
    while (self->samples_.size() >= 10) gst_sample_unref(self->samples_.takeFirst());
    self->samples_.append(s); return GST_FLOW_OK;
}
QList<GstSample *> Audio::takeSamples() { QMutexLocker lock(&mutex_); auto out = samples_; samples_.clear(); return out; }
