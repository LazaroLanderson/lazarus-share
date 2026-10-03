#include "peer.h"
#include "encoder.h"
#include <QJsonArray>
#include <QMutexLocker>
#include <QPointer>
#include <gst/sdp/sdp.h>
#include <gst/video/video.h>
#include <cmath>
#include "mediawatch.h"
#include <chrono>
#include "videostats.h"

static qint64 mediaNow(){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}

Peer::Peer(bool host, QObject *parent) : QObject(parent), host_(host) {
    statsTime_.start();
    timer_.setInterval(100);
    connect(&timer_, &QTimer::timeout, this, &Peer::poll);
}
Peer::~Peer() {
    timer_.stop();
    if (rtc_) g_signal_handlers_disconnect_by_data(rtc_, this);
    if (pipeline_) gst_element_set_state(pipeline_, GST_STATE_NULL);
    if (video_) gst_object_unref(video_);
    if (audio_) gst_object_unref(audio_);
    if (encoder_) gst_object_unref(encoder_);if(pay_)gst_object_unref(pay_);
    if (rtc_) gst_object_unref(rtc_);
    if (pipeline_) gst_object_unref(pipeline_);
}
bool Peer::start(Quality q, const QString &stun, const QStringList &turn,const VideoEncoder &chosen) {
    quality_ = q; targetKbps_ = currentKbps_ = q.kbps; control_.reset(q.kbps); statsTime_.restart();lastEncodedMs_=mediaNow();
    QString pipeline = "webrtcbin name=rtc bundle-policy=max-bundle latency=30 ";
    if (host_) {
        auto backend = chosen.chain.isEmpty()?selectVideoEncoder(q.fps,q.kbps,q.width,q.height):chosen;
        if(backend.chain.isEmpty()){emit mediaFailure("software_unavailable");return false;}
        inputFormat_=backend.format;encoderFactory_=backend.factory;
        encoderName_ = backend.name;
        bitrateProperty_ = backend.bitrateProperty; bitrateMultiplier_ = backend.bitrateMultiplier;
        QString payload = backend.codec == "H264" ?
            "h264parse ! rtph264pay name=video_pay pt=96 config-interval=-1 aggregate-mode=zero-latency ! application/x-rtp,media=video,encoding-name=H264,payload=96 ! rtc. " :
            "rtpvp8pay name=video_pay pt=96 picture-id-mode=15-bit ! application/x-rtp,media=video,encoding-name=VP8,payload=96 ! rtc. ";
        pipeline += QString("appsrc name=video is-live=true format=time do-timestamp=true block=false max-buffers=2 leaky-type=downstream "
            "caps=video/x-raw,format=I420,width=%1,height=%2,framerate=%3/1 ! queue name=video_queue max-size-buffers=2 max-size-bytes=0 max-size-time=0 leaky=downstream ! "
            "videoconvert ! video/x-raw,format=%5 ! "
            "%4 ! "
            "%6 "
            "appsrc name=audio is-live=true format=time do-timestamp=true block=false max-buffers=10 leaky-type=downstream "
            "caps=audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved ! queue max-size-time=200000000 leaky=downstream ! audiomix. "
            "audiotestsrc is-live=true wave=silence samplesperbuffer=480 ! audio/x-raw,format=F32LE,rate=48000,channels=2 ! audiomix. "
            "audiomixer name=audiomix ignore-inactive-pads=true latency=10000000 ! audioconvert ! audioresample ! opusenc bitrate=96000 frame-size=10 audio-type=restricted-lowdelay ! "
            "rtpopuspay pt=97 ! application/x-rtp,media=audio,encoding-name=OPUS,payload=97 ! rtc.")
            .arg(q.width).arg(q.height).arg(q.fps).arg(backend.chain).arg(backend.format).arg(payload);
    }
    GError *e = nullptr;
    pipeline_ = gst_parse_launch(pipeline.toUtf8().constData(), &e);
    if (e) {QString text=QString::fromUtf8(e->message);emit mediaFailure(host_ && !encoderFactory_.isEmpty() && text.contains(encoderFactory_)?"encoder_start":"media_pipeline");g_error_free(e);return false;}
    if (!pipeline_) { emit mediaFailure("media_pipeline"); return false; }
    // A standalone webrtcbin must be owned by a pipeline for its clock and bus.
    if (!GST_IS_PIPELINE(pipeline_)) {
        GstElement *element = pipeline_; pipeline_ = gst_pipeline_new(nullptr);
        gst_bin_add(GST_BIN(pipeline_), element);
    }
    rtc_ = gst_bin_get_by_name(GST_BIN(pipeline_), "rtc");
    if (!stun.isEmpty()) g_object_set(rtc_, "stun-server", stun.toUtf8().constData(), nullptr);
    if (!turn.isEmpty()) {
        g_object_set(rtc_, "ice-transport-policy", GST_WEBRTC_ICE_TRANSPORT_POLICY_RELAY, nullptr);
        for (const auto &uri : turn) {
            gboolean accepted = FALSE;
            g_signal_emit_by_name(rtc_, "add-turn-server", uri.toUtf8().constData(), &accepted);
            if (!accepted) { emit transportError(); return false; }
        }
    }
    g_signal_connect(rtc_, "on-ice-candidate", G_CALLBACK(iceCandidate), this);
    g_signal_connect(rtc_, "pad-added", G_CALLBACK(padAdded), this);
    if (host_) {
        video_ = gst_bin_get_by_name(GST_BIN(pipeline_), "video");
        audio_ = gst_bin_get_by_name(GST_BIN(pipeline_), "audio");
        encoder_ = gst_bin_get_by_name(GST_BIN(pipeline_), "encoder");
        auto *queue=gst_bin_get_by_name(GST_BIN(pipeline_),"video_queue");
        g_signal_connect(queue,"overrun",G_CALLBACK(+[](GstElement *,gpointer data){++static_cast<Peer *>(data)->queueDrops_;}),this);gst_object_unref(queue);
        pay_=gst_bin_get_by_name(GST_BIN(pipeline_),"video_pay");
        auto *encoded = gst_element_get_static_pad(encoder_, "src");
        gst_pad_add_probe(encoded, GST_PAD_PROBE_TYPE_BUFFER, [](GstPad *, GstPadProbeInfo *, gpointer data) {
            auto *peer=static_cast<Peer *>(data);++peer->encodedFrames_;peer->lastEncodedMs_=mediaNow(); return GST_PAD_PROBE_OK;
        }, this, nullptr);
        gst_object_unref(encoded);
        g_signal_connect(rtc_, "on-negotiation-needed", G_CALLBACK(offerNeeded), this);
    }
    if (gst_element_set_state(pipeline_, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
        poll();emit mediaFailure("media_pipeline");return false;
    }
    timer_.start(); return true;
}
void Peer::offerNeeded(GstElement *, gpointer data) {
    auto *self = static_cast<Peer *>(data);
    QMetaObject::invokeMethod(self, [self] { if (!self->offered_) { self->offered_ = true; self->createDescription(true); } }, Qt::QueuedConnection);
}
void Peer::createDescription(bool offer) {
    auto *context = new QPointer<Peer>(this);
    GstPromise *promise = gst_promise_new_with_change_func(descriptionCreated, context,
        [](gpointer p) { delete static_cast<QPointer<Peer> *>(p); });
    g_signal_emit_by_name(rtc_, offer ? "create-offer" : "create-answer", nullptr, promise);
}
void Peer::descriptionCreated(GstPromise *promise, gpointer data) {
    QPointer<Peer> self = *static_cast<QPointer<Peer> *>(data);
    const GstStructure *reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription *desc = nullptr;
    if (reply) {
        if (!gst_structure_get(reply, "offer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &desc, nullptr))
            gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &desc, nullptr);
    }
    if (desc && self) {
        char *text = gst_sdp_message_as_text(desc->sdp);
        QString sdp = QString::fromUtf8(text); g_free(text);
        bool offer = desc->type == GST_WEBRTC_SDP_TYPE_OFFER;
        // Queue SDP before publishing ICE candidates, while preserving thread safety.
        QMetaObject::invokeMethod(self, [self, sdp, offer] {
            if (!self) return;
            GstSDPMessage *parsed = nullptr; gst_sdp_message_new(&parsed);
            auto bytes = sdp.toUtf8();
            gst_sdp_message_parse_buffer(reinterpret_cast<const guint8 *>(bytes.constData()), bytes.size(), parsed);
            auto *local = gst_webrtc_session_description_new(offer ? GST_WEBRTC_SDP_TYPE_OFFER : GST_WEBRTC_SDP_TYPE_ANSWER, parsed);
            emit self->outgoing({{"kind", offer ? "offer" : "answer"}, {"sdp", sdp}});
            GstPromise *p = gst_promise_new();
            g_signal_emit_by_name(self->rtc_, "set-local-description", local, p);
            gst_promise_interrupt(p); gst_promise_unref(p); gst_webrtc_session_description_free(local);
        }, Qt::QueuedConnection);
    } else if (self) {
        QMetaObject::invokeMethod(self, [self] { if (self) emit self->error("Falha ao negociar SDP."); }, Qt::QueuedConnection);
    }
    if (desc) gst_webrtc_session_description_free(desc);
    gst_promise_unref(promise);
}
static void countCandidate(QJsonObject &counts, const QString &candidate) {
    auto fields = candidate.split(' ', Qt::SkipEmptyParts);
    if (fields.size() < 8) return;
    QString transport = fields[2].toLower(); int at = fields.indexOf("typ");
    if (at >= 0 && at+1 < fields.size()) { auto type = fields[at+1]; if (QStringList{"host","srflx","prflx","relay"}.contains(type)) counts[type] = counts[type].toInt() + 1; }
    if (transport == "udp" || transport == "tcp") counts[transport] = counts[transport].toInt() + 1;
}
void Peer::iceCandidate(GstElement *, guint line, gchar *candidate, gpointer data) {
    auto *self = static_cast<Peer *>(data);
    QJsonObject message{{"kind", "ice"}, {"line", int(line)}, {"candidate", QString::fromUtf8(candidate)}};
    QMetaObject::invokeMethod(self, [self, message] { ++self->localCandidates_; countCandidate(self->localCounts_, message["candidate"].toString()); emit self->outgoing(message); }, Qt::QueuedConnection);
}
void Peer::receive(const QJsonObject &m) {
    if (!rtc_) return;
    auto kind = m["kind"].toString();
    if (kind == "ice") {
        if (!remoteSet_) { if (pendingIce_.size() < 128) pendingIce_.append(m); return; }
        int line = m["line"].toInt(-1);
        auto candidate = m["candidate"].toString().toUtf8();
        if (line >= 0 && line < 2 && candidate.size() < 2048)
            { ++remoteCandidates_; countCandidate(remoteCounts_, QString::fromUtf8(candidate)); g_signal_emit_by_name(rtc_, "add-ice-candidate", guint(line), candidate.constData()); }
    } else if ((kind == "offer" && !host_) || (kind == "answer" && host_)) {
        auto text = m["sdp"].toString().toUtf8();
        if (text.size() > 65536 || !text.contains("a=fingerprint:") || remoteSet_) { emit error("SDP inválido ou inesperado."); return; }
        GstSDPMessage *sdp = nullptr; gst_sdp_message_new(&sdp);
        if (gst_sdp_message_parse_buffer(reinterpret_cast<const guint8 *>(text.constData()), text.size(), sdp) != GST_SDP_OK) {
            gst_sdp_message_free(sdp); emit error("SDP inválido."); return;
        }
        auto *desc = gst_webrtc_session_description_new(kind == "offer" ? GST_WEBRTC_SDP_TYPE_OFFER : GST_WEBRTC_SDP_TYPE_ANSWER, sdp);
        auto *context = new QPointer<Peer>(this);
        auto *promise = gst_promise_new_with_change_func([](GstPromise *p, gpointer data) {
            QPointer<Peer> self = *static_cast<QPointer<Peer> *>(data);
            auto result = gst_promise_wait(p);
            bool ok = result == GST_PROMISE_RESULT_REPLIED;
            auto reply = ok ? gst_promise_get_reply(p) : nullptr;
            if (reply && gst_structure_has_field(reply, "error")) ok = false;
            if (self) QMetaObject::invokeMethod(self, [self, ok] {
                if (!self) return;
                if (!ok) { emit self->error("Descrição remota rejeitada."); return; }
                self->remoteSet_ = true;
                auto pending = self->pendingIce_; self->pendingIce_ = {};
                for (auto entry : pending) self->receive(entry.toObject());
                if (!self->host_) self->createDescription(false);
            }, Qt::QueuedConnection);
            gst_promise_unref(p);
        }, context, [](gpointer p) { delete static_cast<QPointer<Peer> *>(p); });
        g_signal_emit_by_name(rtc_, "set-remote-description", desc, promise);
        gst_webrtc_session_description_free(desc);
    }
}
void Peer::padAdded(GstElement *, GstPad *pad, gpointer data) {
    auto *self = static_cast<Peer *>(data);
    if (GST_PAD_DIRECTION(pad) != GST_PAD_SRC) return;
    GstCaps *caps = gst_pad_get_current_caps(pad);
    if (!caps) caps = gst_pad_query_caps(pad, nullptr);
    const char *media = caps && gst_caps_get_size(caps) ? gst_structure_get_string(gst_caps_get_structure(caps, 0), "media") : nullptr;
    const char *encoding = caps && gst_caps_get_size(caps) ? gst_structure_get_string(gst_caps_get_structure(caps, 0), "encoding-name") : nullptr;
    bool h264 = encoding && !strcmp(encoding, "H264");
    bool video = media && !strcmp(media, "video");
    bool audio = media && !strcmp(media, "audio");
    if(video){guint ssrc=0;if(gst_structure_get_uint(gst_caps_get_structure(caps,0),"ssrc",&ssrc))self->videoSsrc_=ssrc;}
    if (caps) gst_caps_unref(caps);
    if ((!video && !audio) || self->host_) return;
    GError *error = nullptr;
    // Never drop RTP fragments or compressed references. A bounded decoded-frame
    // queue separates decoding from conversion and discards complete old images.
    QString receive = video ? QString("queue name=receive_queue max-size-buffers=0 max-size-bytes=0 max-size-time=2000000000 ! %1 ! queue name=decoded_queue max-size-buffers=1 max-size-bytes=0 max-size-time=0 leaky=downstream ! videoconvert name=video_convert ! video/x-raw,format=BGRx ! appsink name=frames emit-signals=true sync=false max-buffers=1 drop=true")
        .arg(h264 ? "rtph264depay request-keyframe=true wait-for-keyframe=true ! video/x-h264,alignment=au ! h264parse ! decodebin name=decoder caps=video/x-raw" : "rtpvp8depay request-keyframe=true wait-for-keyframe=true ! vp8dec") :
        "queue name=receive_queue max-size-time=200000000 leaky=downstream ! rtpopusdepay ! opusdec ! audioconvert ! audioresample ! autoaudiosink sync=false";
    auto *bin = gst_parse_bin_from_description(receive.toUtf8().constData(), FALSE, &error);
    if (error) { g_error_free(error); if (bin) gst_object_unref(bin); QMetaObject::invokeMethod(self,[self]{emit self->mediaFailure("decoder_error");},Qt::QueuedConnection);return; }
    // decodebin has a dynamic output: automatic ghosting can expose the raw
    // converter's sink instead of the RTP queue. Always expose the intended input.
    auto *inputQueue=gst_bin_get_by_name(GST_BIN(bin),"receive_queue");
    auto *inputPad=gst_element_get_static_pad(inputQueue,"sink");
    gst_element_add_pad(bin,gst_ghost_pad_new("sink",inputPad));gst_object_unref(inputPad);gst_object_unref(inputQueue);
    if (video) {
        auto *decodedQueue=gst_bin_get_by_name(GST_BIN(bin),"decoded_queue");
        g_signal_connect(decodedQueue,"overrun",G_CALLBACK(+[](GstElement *,gpointer data){++static_cast<Peer *>(data)->displayDrops_;}),self);
        auto *decodedPad=gst_element_get_static_pad(decodedQueue,"sink");
        gst_pad_add_probe(decodedPad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad *,GstPadProbeInfo *,gpointer data){++static_cast<Peer *>(data)->decoderFrames_;return GST_PAD_PROBE_OK;},self,nullptr);
        gst_object_unref(decodedPad);gst_object_unref(decodedQueue);
        if(auto *decoder=gst_bin_get_by_name(GST_BIN(bin),"decoder")){
            g_object_set(decoder,"force-sw-decoders",qEnvironmentVariable("LAZARUS_VIDEO_DECODER")=="software",nullptr);
            g_signal_connect(decoder,"deep-element-added",G_CALLBACK(+[](GstBin *,GstBin *,GstElement *element,gpointer data){
                auto *factory=gst_element_get_factory(element);const char *klass=factory?gst_element_factory_get_metadata(factory,GST_ELEMENT_METADATA_KLASS):nullptr;
                if(!klass || !strstr(klass,"Decoder") || !strstr(klass,"Video"))return;
                QString name=QString::fromUtf8(gst_plugin_feature_get_name(GST_PLUGIN_FEATURE(factory)));
                auto *peer=static_cast<Peer *>(data);QMetaObject::invokeMethod(peer,[peer,name]{peer->decoderName_=name;},Qt::QueuedConnection);
            }),self);gst_object_unref(decoder);
        }else QMetaObject::invokeMethod(self,[self]{self->decoderName_="vp8dec";},Qt::QueuedConnection);
        auto *sink = gst_bin_get_by_name(GST_BIN(bin), "frames");
        g_signal_connect(sink, "new-sample", G_CALLBACK(newFrame), self); gst_object_unref(sink);
    }
    gst_bin_add(GST_BIN(self->pipeline_), bin);
    auto *sink = gst_element_get_static_pad(bin, "sink");
    gst_pad_link(pad, sink); gst_object_unref(sink);
    gst_element_sync_state_with_parent(bin);
}
GstFlowReturn Peer::newFrame(GstAppSink *sink, gpointer data) {
    auto *self = static_cast<Peer *>(data); auto *sample = gst_app_sink_pull_sample(sink);
    if (!sample) return GST_FLOW_EOS;
    GstVideoInfo info; GstVideoFrame frame;
    if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) &&
        gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
        QImage image(static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)),
            GST_VIDEO_INFO_WIDTH(&info), GST_VIDEO_INFO_HEIGHT(&info), GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), QImage::Format_RGB32);
        { QMutexLocker lock(&self->frameMutex_); if(!self->frame_.isNull())++self->displayDrops_;self->frame_ = image.copy(); ++self->decodedFrames_; }
        gst_video_frame_unmap(&frame);
    }
    gst_sample_unref(sample); return GST_FLOW_OK;
}
QImage Peer::takeFrame() { QMutexLocker lock(&frameMutex_); QImage out = frame_; frame_ = {}; return out; }
void Peer::push(GstElement *source, GstSample *sample,GstClockTime timestamp) {
    if (!source || !sample) return;
    gst_app_src_set_caps(GST_APP_SRC(source), gst_sample_get_caps(sample));
    GstBuffer *original = gst_sample_get_buffer(sample);
    GstBuffer *buffer = gst_buffer_copy(original);
    // Keep pooled capture memory leased until every encoder releases its copy.
    gst_buffer_add_parent_buffer_meta(buffer, original);
    GST_BUFFER_PTS(buffer) = timestamp; GST_BUFFER_DTS(buffer) = GST_CLOCK_TIME_NONE;
    gst_app_src_push_buffer(GST_APP_SRC(source), buffer);
}
void Peer::video(GstSample *s) {
    if(!s)return;++inputFrames_;lastInputMs_=mediaNow();GstClockTime timestamp=GST_CLOCK_TIME_NONE;
    auto *clock=gst_element_get_clock(pipeline_);auto base=gst_element_get_base_time(pipeline_);
    GstClockTime running=GST_CLOCK_TIME_NONE;
    if(clock){auto now=gst_clock_get_time(clock);gst_object_unref(clock);if(GST_CLOCK_TIME_IS_VALID(base) && now>=base)running=now-base;}
    timestamp=videoTimeline_.map(GST_BUFFER_PTS(gst_sample_get_buffer(s)),running);
    push(video_,s,timestamp);
}
void Peer::audio(GstSample *s) { push(audio_, s); }
void Peer::quality(Quality q) {
    quality_ = q; targetKbps_ = q.kbps; currentKbps_ = qMin(currentKbps_, targetKbps_); control_.target(q.kbps);
    if (encoder_) {
        g_object_set(encoder_, bitrateProperty_.constData(), currentKbps_ * bitrateMultiplier_, nullptr);
        for (const char *property : {"keyframe-max-dist", "key-int-max", "gop-size"})
            if (g_object_class_find_property(G_OBJECT_GET_CLASS(encoder_), property)) g_object_set(encoder_, property, q.fps, nullptr);
    }
    // Size/FPS changes come from the shared capture caps in stream order.
    // Setting a second size here races queued frames and GPU buffer pools.

}
void Peer::poll() {
    GstBus *bus = gst_element_get_bus(pipeline_);
    while (auto *msg = gst_bus_pop_filtered(bus, GstMessageType(GST_MESSAGE_ERROR | GST_MESSAGE_EOS))) {
        if (GST_MESSAGE_TYPE(msg) == GST_MESSAGE_ERROR) {
            GError *e = nullptr; gchar *debug = nullptr; gst_message_parse_error(msg, &e, &debug);
            // Raw debug strings can contain SDP or device identifiers; never export them.
            QString source = QString::fromUtf8(GST_OBJECT_NAME(GST_MESSAGE_SRC(msg)));
            if (source.startsWith("nice") || source.startsWith("dtls") || source.startsWith("rtc")) emit transportError();
            else {
                bool encoderFailure=host_ && (GST_MESSAGE_SRC(msg)==GST_OBJECT(encoder_) || source.contains("encoder") || source.contains("vapostproc"));
                emit mediaFailure(encoderFailure?"encoder_error":host_?"media_pipeline":"decoder_error");
            }
            g_error_free(e); g_free(debug);
        } else emit error("Fluxo encerrado.");
        gst_message_unref(msg);
    }
    gst_object_unref(bus);
    GstWebRTCPeerConnectionState state; g_object_get(rtc_, "connection-state", &state, nullptr);
    const char *states[] = {"new", "connecting", "connected", "disconnected", "failed", "closed"};
    stage_ = state >= 0 && state <= GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED ? QString::fromLatin1(states[state]) : "unknown";
    GstWebRTCICEConnectionState ice; GstWebRTCICEGatheringState gathering;
    g_object_get(rtc_, "ice-connection-state", &ice, "ice-gathering-state", &gathering, nullptr);
    const char *iceStates[] = {"new","checking","connected","completed","failed","disconnected","closed"};
    const char *gatheringStates[] = {"new","gathering","complete"};
    iceState_ = int(ice) >= 0 && int(ice) < 7 ? iceStates[int(ice)] : "unknown";
    gatheringState_ = int(gathering) >= 0 && int(gathering) < 3 ? gatheringStates[int(gathering)] : "unknown";
    bool now = state == GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED;
    if (now != connected_) { if(now){connectedAt_=mediaNow();lastEncodedMs_=connectedAt_;}connected_ = now; emit status(now ? "Conectado" : "Reconectando"); }
    if(host_ && !stalled_ && encoderStalled(mediaNow(),lastInputMs_,lastEncodedMs_,connectedAt_,connected_)){stalled_=true;emit mediaFailure("encoder_stall");}
    if (++pollCount_ % 20 == 0) {
        GArray *transceivers=nullptr; g_signal_emit_by_name(rtc_,"get-transceivers",&transceivers);
        if(transceivers) {
            for(guint i=0;i<transceivers->len;++i) {
                auto *transceiver=g_array_index(transceivers,GstWebRTCRTPTransceiver *,i);
                GObject *sender=nullptr,*receiver=nullptr,*transport=nullptr;
                g_object_get(transceiver,"sender",&sender,"receiver",&receiver,nullptr);
                if(sender)g_object_get(sender,"transport",&transport,nullptr);
                if(!transport && receiver)g_object_get(receiver,"transport",&transport,nullptr);
                if(transport) { GstWebRTCDTLSTransportState dtls; g_object_get(transport,"state",&dtls,nullptr);
                    const char *names[]={"new","closed","failed","connecting","connected"};
                    if(int(dtls)>=0 && int(dtls)<5)dtlsState_=names[int(dtls)]; g_object_unref(transport);
                }
                if(sender)g_object_unref(sender); if(receiver)g_object_unref(receiver);
            }
            g_array_unref(transceivers);
        }
        stats();
    }
}
static double number(const GstStructure *s, const char *key) {
    auto *v = gst_structure_get_value(s, key); if (!v) return 0;
    if (G_VALUE_HOLDS_DOUBLE(v)) return g_value_get_double(v);
    if (G_VALUE_HOLDS_UINT64(v)) return double(g_value_get_uint64(v));
    if (G_VALUE_HOLDS_INT64(v)) return double(g_value_get_int64(v));
    if (G_VALUE_HOLDS_UINT(v)) return g_value_get_uint(v);
    if (G_VALUE_HOLDS_INT(v)) return g_value_get_int(v);
    if (G_VALUE_HOLDS_ENUM(v)) return g_value_get_enum(v);
    return 0;
}
void Peer::stats() {
    if(pay_){auto *pad=gst_element_get_static_pad(pay_,"src");auto *caps=gst_pad_get_current_caps(pad);guint ssrc=0;
        if(caps && gst_caps_get_size(caps) && gst_structure_get_uint(gst_caps_get_structure(caps,0),"ssrc",&ssrc))videoSsrc_=ssrc;
        if(caps)gst_caps_unref(caps);gst_object_unref(pad);}

    auto *context = new QPointer<Peer>(this);
    auto *p = gst_promise_new_with_change_func([](GstPromise *promise, gpointer data) {
        QPointer<Peer> self = *static_cast<QPointer<Peer> *>(data);
        auto *reply = gst_promise_get_reply(promise); QJsonObject values;
        double bytes = 0, lost = 0, packets = 0, rtt = 0; bool feedback=false; QString dtls = "unknown"; bool selected = false; QString route = "Verificando";
        if (reply) {
            for (int i = 0; i < gst_structure_n_fields(reply); ++i) {
                auto *v = gst_structure_get_value(reply, gst_structure_nth_field_name(reply, i));
                if (!GST_VALUE_HOLDS_STRUCTURE(v)) continue;
                auto *s = gst_value_get_structure(v);
                auto type = int(number(s, "type"));

                if (!self) break;
                if (type == GST_WEBRTC_STATS_TRANSPORT) {
                    const char *dtlsValue = gst_structure_get_string(s, "dtls-state"); if (dtlsValue) dtls = QString::fromLatin1(dtlsValue);
                    const char *pairId = gst_structure_get_string(s, "selected-candidate-pair-id");
                    auto *pv = pairId ? gst_structure_get_value(reply, pairId) : nullptr;
                    auto *pair = pv && GST_VALUE_HOLDS_STRUCTURE(pv) ? gst_value_get_structure(pv) : nullptr;
                    if (pair) {
                        bool relay = false;
                        for (const char *field : {"local-candidate-id", "remote-candidate-id"}) {
                            auto *id = gst_structure_get_string(pair, field);
                            auto *cv = id ? gst_structure_get_value(reply, id) : nullptr;
                            if (cv && GST_VALUE_HOLDS_STRUCTURE(cv)) {
                                auto *candidate = gst_value_get_structure(cv);
                                auto *ct = gst_structure_get_string(candidate, "candidate-type");
                                relay |= ct && !strcmp(ct, "relay");
                            }
                        }
                        selected = true; route = relay ? "Relay criptografado" : "P2P direto";
                    }
                }
            }
        }
        auto video=videoStats(reply,self && self->host_,self?self->videoSsrc_.load():0);bytes=video.bytes;packets=video.packets;lost=video.lost;rtt=video.rttMs/1000;feedback=video.feedback;
        values = {{"feedback_valid",feedback},{"bytes", bytes}, {"lost", lost}, {"packets", packets}, {"rtt_ms", rtt * 1000}, {"route", route}, {"dtls_state", dtls}, {"selected_pair", selected}};
        if (self) QMetaObject::invokeMethod(self, [self, values] { if (self) self->applyStats(values); }, Qt::QueuedConnection);
        gst_promise_unref(promise);
    }, context, [](gpointer p) { delete static_cast<QPointer<Peer> *>(p); });
    g_signal_emit_by_name(rtc_, "get-stats", nullptr, p);
}
void Peer::applyStats(const QJsonObject &v) {
    quint64 bytes = quint64(v["bytes"].toDouble());
    qint64 packets = qint64(v["packets"].toDouble()), lost = qint64(v["lost"].toDouble());
    double loss = packets > lastPackets_ ? double(qMax<qint64>(0, lost - lastLost_)) / double(packets - lastPackets_) : 0;
    double seconds=qMax<qint64>(1,statsTime_.restart())/1000.0;
    bool countersValid=packets>=lastPackets_ && lost>=lastLost_ && bytes>=lastBytes_;
    double kbps=lastBytes_ && countersValid?double(bytes-lastBytes_)*8/(seconds*1000):0;
    bool valid=countersValid && packets>lastPackets_ && v["feedback_valid"].toBool();
    lastBytes_=bytes;lastPackets_=packets;lastLost_=lost;
    if(!countersValid)control_.resetFeedback();
    int previous=currentKbps_;
    if(host_ && connected_){currentKbps_=control_.update(loss,v["rtt_ms"].toDouble(),valid);if(currentKbps_!=previous)g_object_set(encoder_,bitrateProperty_.constData(),currentKbps_*bitrateMultiplier_,nullptr);}
    unsigned encoded=encodedFrames_.exchange(0),received=decodedFrames_.exchange(0),inputs=inputFrames_.exchange(0);
    double videoFps = (host_ ? encoded : received) / seconds;
    emit metrics({{"bitrate_changed",previous!=currentKbps_},{"frames_discarded",int(queueDrops_.exchange(0)+displayDrops_.exchange(0))},{"frames_pending_estimate",int(inputs>encoded?inputs-encoded:0)},{"feedback_valid",valid},{"decoder_fps",decoderFrames_.exchange(0)/seconds},{"video_fps", videoFps},{"route", connected_ && v["selected_pair"].toBool() ? v["route"] : QJsonValue("Verificando")}, {"selected_pair", v["selected_pair"]}, {"dtls_state", dtlsState_}, {"ice_state", iceState_}, {"gathering_state", gatheringState_}, {"local_description", offered_ || remoteSet_}, {"remote_description", remoteSet_}, {"candidate_counts", QJsonObject{{"local",localCounts_},{"remote",remoteCounts_}}}, {"bytes",v["bytes"]}, {"packets",v["packets"]}, {"kbps", kbps}, {"loss_percent", loss * 100},
                  {"rtt_ms", v["rtt_ms"]}, {"decoder",decoderName_},{"encoder", encoderName_}, {"encoder_kbps", currentKbps_}, {"width", quality_.width}, {"height", quality_.height},
                  {"local_candidates", localCandidates_}, {"remote_candidates", remoteCandidates_}, {"stage", stage_}});
}
