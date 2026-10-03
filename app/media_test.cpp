#include "peer.h"
#include "protocol.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <iostream>
#include <gst/video/video.h>
#include <cmath>
struct PeerTestAccess {
    static bool slowDisplay(Peer &peer){auto *convert=gst_bin_get_by_name(GST_BIN(peer.pipeline_),"video_convert");if(!convert)return false;auto *pad=gst_element_get_static_pad(convert,"sink");gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad *,GstPadProbeInfo *,gpointer){g_usleep(60000);return GST_PAD_PROBE_OK;},nullptr,nullptr);gst_object_unref(pad);gst_object_unref(convert);return true;}
    static unsigned queueDepth(Peer &peer){auto *queue=gst_bin_get_by_name(GST_BIN(peer.pipeline_),"decoded_queue");guint depth=0;if(queue){g_object_get(queue,"current-level-buffers",&depth,nullptr);gst_object_unref(queue);}return depth;}
    static bool failDecoder(Peer &peer){
        auto *iterator=gst_bin_iterate_recurse(GST_BIN(peer.pipeline_));GValue value=G_VALUE_INIT;bool found=false;
        while(gst_iterator_next(iterator,&value)==GST_ITERATOR_OK){
            auto *element=GST_ELEMENT(g_value_get_object(&value));auto *factory=gst_element_get_factory(element);
            const char *klass=factory?gst_element_factory_get_metadata(factory,GST_ELEMENT_METADATA_KLASS):nullptr;
            if(klass && strstr(klass,"Decoder") && strstr(klass,"Video")){
                auto *failure=g_error_new_literal(GST_STREAM_ERROR,GST_STREAM_ERROR_DECODE,"Controlled decoder failure");
                auto *bus=gst_element_get_bus(peer.pipeline_);gst_bus_post(bus,gst_message_new_error(GST_OBJECT(element),failure,nullptr));gst_object_unref(bus);g_error_free(failure);found=true;g_value_reset(&value);break;
            }g_value_reset(&value);
        }if(G_VALUE_TYPE(&value))g_value_unset(&value);gst_iterator_free(iterator);return found;
    }
    static void dropEncoderInput(Peer &peer){auto *pad=gst_element_get_static_pad(peer.encoder_,"sink");gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad *,GstPadProbeInfo *,gpointer){return GST_PAD_PROBE_DROP;},nullptr,nullptr);gst_object_unref(pad);}
};
int main(int argc, char **argv) {
    gst_init(&argc, &argv); QCoreApplication app(argc, argv);
    if(app.arguments().contains("--encoder-creation-failure")) {
        Peer peer(true);QString classified;
        QObject::connect(&peer,&Peer::mediaFailure,&app,[&](QString code){classified=code;});
        VideoEncoder broken;broken.factory="missing-test-encoder";broken.chain="missing-test-encoder name=encoder";
        if(peer.start(Quality{}, {}, {}, broken) || classified!="encoder_start")return 1;
        std::cout<<"Encoder creation failure classified separately from transport\n";return 0;
    }
    bool decoderFailure=app.arguments().contains("--decoder-failure"),decoderInjected=false;int decoderFailures=0,transportFailures=0;
    bool stall=app.arguments().contains("--encoder-stall"),blocked=false;int stalls=0;
    bool slow=app.arguments().contains("--slow-display"),slowed=false;unsigned maxDepth=0;double knownDrops=0,rawFps=0,availableFps=0;
    bool stress = app.arguments().contains("--stress-quality");
    Peer host(true), guest(false); Quality q = stress ? Quality{1920,1080,60,8000} : Quality{640,360,30,1200};
    auto key = Protocol::randomBytes(16);
    Protocol::Channel h(key, "session", "peer", "host", "guest", true), g(key, "session", "peer", "guest", "host", false);
    QObject::connect(&host, &Peer::outgoing, &guest, [&](QJsonObject message) { QJsonObject decoded; if (g.open(h.seal(message), decoded)) guest.receive(decoded); });
    QObject::connect(&guest, &Peer::outgoing, &host, [&](QJsonObject message) { QJsonObject decoded; if (h.open(g.seal(message), decoded)) host.receive(decoded); });
    bool failed = false; auto error = [&](QString e) { std::cerr << e.toStdString() << '\n'; failed = true; app.quit(); };
    QObject::connect(&host,&Peer::mediaFailure,&app,[&](QString code){if(stall && code=="encoder_stall")++stalls;else failed=true;app.quit();});
    QObject::connect(&guest,&Peer::mediaFailure,&app,[&](QString code){if(decoderFailure && code=="decoder_error")++decoderFailures;else failed=true;app.quit();});
    QObject::connect(&guest,&Peer::transportError,&app,[&]{++transportFailures;});
    QString decoder,route;
    QObject::connect(&guest,&Peer::metrics,&app,[&](QJsonObject values){decoder=values["decoder"].toString();knownDrops+=values["frames_discarded"].toDouble();rawFps=values["decoder_fps"].toDouble();availableFps=values["video_fps"].toDouble();});
    QObject::connect(&host, &Peer::metrics, &app, [&](QJsonObject v) { route = v["route"].toString(); });
    QObject::connect(&host, &Peer::error, &app, error); QObject::connect(&guest, &Peer::error, &app, error);
    QStringList turns;
    for (auto argument : app.arguments()) if (argument.startsWith("--turn=")) turns.append(argument.mid(7));
    if (!guest.start(q, {}, turns) || !host.start(q, {}, turns)) return 1;
    QString sourceText = QString("videotestsrc is-live=true pattern=%1 ! video/x-raw,format=I420,width=%2,height=%3,framerate=%4/1 ! tee name=t "
        "t. ! queue max-size-buffers=2 leaky=downstream ! appsink name=sink sync=false max-buffers=1 drop=true "
        "t. ! queue max-size-buffers=2 leaky=downstream ! videoconvert ! video/x-raw,format=RGB ! appsink name=reference sync=false max-buffers=1 drop=true")
        .arg(stress || slow ? "smpte" : "ball").arg(q.width).arg(q.height).arg(q.fps);
    auto *source = gst_parse_launch(sourceText.toUtf8().constData(), nullptr);
    auto *sink = gst_bin_get_by_name(GST_BIN(source), "sink");
    auto *reference = gst_bin_get_by_name(GST_BIN(source), "reference");
    gst_element_set_state(source, GST_STATE_PLAYING);
    auto *silence = gst_parse_launch("audiotestsrc is-live=true wave=silence samplesperbuffer=480 ! audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved ! appsink name=sink sync=false max-buffers=10 drop=true", nullptr);
    auto *audio = gst_bin_get_by_name(GST_BIN(silence), "sink"); gst_element_set_state(silence, GST_STATE_PLAYING);
    QImage expected; int badFrames = 0; double maxError = 0;
    QTimer timer; timer.setInterval(8); int frames = 0; QElapsedTimer elapsed; elapsed.start();
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (auto *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 0)) { host.video(sample); gst_sample_unref(sample); }
        while (auto *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(audio), 0)) { if (!app.arguments().contains("--video-only")) host.audio(sample); gst_sample_unref(sample); }
        if (auto *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(reference), 0)) {
            GstVideoInfo info; GstVideoFrame frame;
            if (gst_video_info_from_caps(&info, gst_sample_get_caps(sample)) && gst_video_frame_map(&frame, &info, gst_sample_get_buffer(sample), GST_MAP_READ)) {
                expected = QImage(static_cast<const uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&frame, 0)), q.width, q.height, GST_VIDEO_FRAME_PLANE_STRIDE(&frame, 0), QImage::Format_RGB888).copy();
                gst_video_frame_unmap(&frame);
            }
            gst_sample_unref(sample);
        }
        auto image = guest.takeFrame(); if (!image.isNull()) {
            if (image.size() != QSize(q.width,q.height)) failed = true;
            if ((stress || slow) && !expected.isNull()) {
                double sum = 0; int count = 0;
                for (int y = 30; y < q.height * 2 / 3 - 30; y += 17) for (int x = 20; x < q.width - 20; x += 19) {
                    auto a = image.pixelColor(x,y), b = expected.pixelColor(x,y);
                    sum += std::abs(a.red()-b.red()) + std::abs(a.green()-b.green()) + std::abs(a.blue()-b.blue()); count += 3;
                }
                double error = sum / count; maxError = std::max(maxError,error); if (error > 10) ++badFrames;
            }
            ++frames; if (!stress && !stall && !decoderFailure && !slow && frames >= 90) app.quit();
            if(slow && !slowed && frames>=15)slowed=PeerTestAccess::slowDisplay(guest);
            if(decoderFailure && !decoderInjected && frames>=15)decoderInjected=PeerTestAccess::failDecoder(guest);
            if(stall && !blocked && frames>=15 && host.connected()){blocked=true;PeerTestAccess::dropEncoderInput(host);}
        }
        if(slow){maxDepth=std::max(maxDepth,PeerTestAccess::queueDepth(guest));if(elapsed.elapsed()>=4500)app.quit();}
        if (stress && elapsed.elapsed() >= 8000) app.quit();
        if (elapsed.elapsed() > 20000) { failed = true; app.quit(); }
    }); timer.start(); app.exec();
    gst_element_set_state(source, GST_STATE_NULL); gst_object_unref(sink); gst_object_unref(reference); gst_object_unref(source);
    gst_element_set_state(silence, GST_STATE_NULL); gst_object_unref(audio); gst_object_unref(silence);
    if(decoderFailure){if(failed || !decoderInjected || decoderFailures!=1 || transportFailures || !host.connected())return 1;std::cout<<"Decoder failure classified without transport recovery\n";return 0;}
    if(app.arguments().contains("--require-software-decoder")){
        auto *factory=gst_element_factory_find(decoder.toUtf8().constData());const char *klass=factory?gst_element_factory_get_metadata(factory,GST_ELEMENT_METADATA_KLASS):nullptr;
        bool software=klass && !strstr(klass,"Hardware");if(factory)gst_object_unref(factory);if(!software)return 1;
    }
    if(stall){if(failed || stalls!=1 || !blocked || !host.connected() || route!="P2P direto")return 1;std::cout<<"Active encoder stall classified once without transport failure\n";return 0;}
    if(slow){if(!slowed || maxDepth>1 || !knownDrops || rawFps<=availableFps || badFrames>1)return 1;std::cout<<"Decoded queue stayed bounded: depth="<<maxDepth<<" dropped="<<knownDrops<<" raw FPS="<<rawFps<<" available FPS="<<availableFps<<"\n";}
    if (stress) std::cout << "1080p/60 pixel integrity: frames=" << frames << " bad=" << badFrames << " max RGB error=" << maxError << '\n';
    if (stress && (frames < 60 || badFrames > 1)) failed = true;
    if (failed || frames < (stress ? 60 : slow ? 30 : 90) || !host.connected() || !guest.connected() || route != (turns.isEmpty() ? "P2P direto" : "Relay criptografado")) return 1;
    std::cout << "Route: " << route.toStdString() << '\n';
    std::cout << "Encrypted local WebRTC video: " << frames << " decoded frames / " << host.encoderName().toStdString() << " / decoder " << decoder.toStdString() << "\n";
}
