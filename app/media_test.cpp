#include "peer.h"
#include "protocol.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <iostream>
#include <gst/video/video.h>
#include <cmath>
#include <vector>
#include <numeric>
#include <memory>
struct PeerTestAccess {
    static bool watchAudio(Peer &peer,std::atomic<unsigned> *frames){
        QMutexLocker receiveLock(&peer.receiveMutex_);
        if(!peer.audioPlayback_)return false;auto *convert=gst_bin_get_by_name(GST_BIN(peer.audioPlayback_),"audio_convert");if(!convert)return false;
        auto *pad=gst_element_get_static_pad(convert,"src");gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad *,GstPadProbeInfo *,gpointer data){++*static_cast<std::atomic<unsigned> *>(data);return GST_PAD_PROBE_OK;},frames,nullptr);
        gst_object_unref(pad);gst_object_unref(convert);return true;
    }
    static bool failAudio(Peer &peer){
        auto *sink=gst_bin_get_by_name(GST_BIN(peer.audioPlayback_),"audio_output");if(!sink)return false;
        auto *failure=g_error_new_literal(GST_RESOURCE_ERROR,GST_RESOURCE_ERROR_OPEN_WRITE,"Controlled audio failure");
        auto *bus=gst_element_get_bus(peer.audioPlayback_);gst_bus_post(bus,gst_message_new_error(GST_OBJECT(sink),failure,nullptr));g_error_free(failure);
        // A device can report another error after its playback branch is removed.
        auto lateSource=std::shared_ptr<GstElement>(sink,[](GstElement *p){gst_object_unref(p);});auto lateBus=std::shared_ptr<GstBus>(bus,[](GstBus *p){gst_object_unref(p);});
        QTimer::singleShot(400,&peer,[lateSource,lateBus]{auto *e=g_error_new_literal(GST_RESOURCE_ERROR,GST_RESOURCE_ERROR_WRITE,"Controlled late audio failure");gst_bus_post(lateBus.get(),gst_message_new_error(GST_OBJECT(lateSource.get()),e,nullptr));g_error_free(e);});return true;
    }
    static void frameCounters(Peer &peer,std::atomic<unsigned> *counts){auto *pad=gst_element_get_static_pad(peer.encoder_,"src");gst_pad_add_probe(pad,GST_PAD_PROBE_TYPE_BUFFER,[](GstPad *,GstPadProbeInfo *info,gpointer data){auto *counts=static_cast<std::atomic<unsigned> *>(data);++counts[0];if(!GST_BUFFER_FLAG_IS_SET(GST_PAD_PROBE_INFO_BUFFER(info),GST_BUFFER_FLAG_DELTA_UNIT))++counts[1];return GST_PAD_PROBE_OK;},counts,nullptr);gst_object_unref(pad);}
    static void byteCounters(Peer &peer,std::atomic<quint64> *encoded,std::atomic<quint64> *rtp){
        for(auto pair:{std::pair{peer.encoder_,encoded},std::pair{peer.pay_,rtp}}){auto *pad=gst_element_get_static_pad(pair.first,"src");gst_pad_add_probe(pad,GstPadProbeType(GST_PAD_PROBE_TYPE_BUFFER|GST_PAD_PROBE_TYPE_BUFFER_LIST),[](GstPad *,GstPadProbeInfo *info,gpointer data){quint64 bytes=0;if(GST_PAD_PROBE_INFO_TYPE(info)&GST_PAD_PROBE_TYPE_BUFFER)bytes=gst_buffer_get_size(GST_PAD_PROBE_INFO_BUFFER(info));else {auto *list=GST_PAD_PROBE_INFO_BUFFER_LIST(info);for(guint i=0;i<gst_buffer_list_length(list);++i)bytes+=gst_buffer_get_size(gst_buffer_list_get(list,i));}*static_cast<std::atomic<quint64> *>(data)+=bytes;return GST_PAD_PROBE_OK;},pair.second,nullptr);gst_object_unref(pad);}
    }
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
    if(app.arguments().contains("--h264"))qputenv("LAZARUS_TEST_H264","1");
    const bool audioFlowFailure=app.arguments().contains("--audio-flow-failure");
    if(audioFlowFailure)qputenv("LAZARUS_TEST_AUDIO_FLOW_FAILURE","1");
    const bool audioStartFailure=app.arguments().contains("--audio-start-failure");
    if(audioStartFailure)qputenv("LAZARUS_TEST_AUDIO_START_FAILURE","1");
    if(app.arguments().contains("--negotiation-failure")){
        Peer guest(false);QString code;QJsonObject details;
        QObject::connect(&guest,&Peer::mediaFailure,&app,[&](QString value){code=value;});QObject::connect(&guest,&Peer::failureDetails,&app,[&](QJsonObject value){details=value;});
        if(!guest.start(Quality{},{}))return 1;guest.receive({{"kind","offer"},{"sdp","SECRET"}});
        if(code!="negotiation_invalid" || details["component"]!="negotiation" || QJsonDocument(details).toJson().contains("SECRET"))return 1;
        std::cout<<"Negotiation failure classified without exporting SDP\n";return 0;
    }
    if(app.arguments().contains("--encoder-creation-failure")) {
        Peer peer(true);QString classified;
        QObject::connect(&peer,&Peer::mediaFailure,&app,[&](QString code){classified=code;});
        VideoEncoder broken;broken.factory="missing-test-encoder";broken.chain="missing-test-encoder name=encoder";
        if(peer.start(Quality{}, {}, {}, broken) || classified!="encoder_start")return 1;
        std::cout<<"Encoder creation failure classified separately from transport\n";return 0;
    }
    bool decoderFailure=app.arguments().contains("--decoder-failure"),decoderInjected=false;int decoderFailures=0,transportFailures=0;
    bool audioFailure=app.arguments().contains("--audio-failure") || audioFlowFailure || audioStartFailure,audioInjected=audioFlowFailure || audioStartFailure;int audioWarnings=0,audioFrames=0;QString failureComponent;
    bool stall=app.arguments().contains("--encoder-stall"),blocked=false;int stalls=0;
    bool bitrateCeiling=app.arguments().contains("--bitrate-ceiling");QElapsedTimer elapsed;elapsed.start();std::vector<double> videoRates;
    bool slow=app.arguments().contains("--slow-display"),slowed=false;unsigned maxDepth=0;double knownDrops=0,rawFps=0,availableFps=0;
    int relayDuration=0,credentialLifetime=0;
    for(auto argument:app.arguments()){
        if(argument.startsWith("--relay-duration="))relayDuration=argument.section('=',1).toInt()*1000;
        if(argument.startsWith("--credential-lifetime="))credentialLifetime=argument.section('=',1).toInt()*1000;
    }
    qint64 lastFrameAt=0,maxRelayGap=0;int framesAfterExpiry=0;
    bool stress = app.arguments().contains("--stress-quality");
    std::atomic<quint64> encodedBytes{0},rtpBytes{0};std::atomic<unsigned> frameCounts[2]{};bool bytesReset=false;double byteStart=0;
    std::atomic<unsigned> decodedAudio{0};bool audioWatched=false;const bool audioPlayback=app.arguments().contains("--audio-playback");
    Peer host(true), guest(false); Quality q = stress ? Quality{1920,1080,60,8000} : Quality{640,360,30,1200};
    auto key = Protocol::randomBytes(16);
    Protocol::Channel h(key, "session", "peer", "host", "guest", true), g(key, "session", "peer", "guest", "host", false);
    QObject::connect(&host, &Peer::outgoing, &guest, [&](QJsonObject message) { QJsonObject decoded; if (g.open(h.seal(message), decoded)) guest.receive(decoded); });
    QObject::connect(&guest, &Peer::outgoing, &host, [&](QJsonObject message) { QJsonObject decoded; if (h.open(g.seal(message), decoded)) host.receive(decoded); });
    bool failed = false; auto error = [&](QString e) { std::cerr << e.toStdString() << '\n'; failed = true; app.quit(); };
    QObject::connect(&host,&Peer::mediaFailure,&app,[&](QString code){if(stall && code=="encoder_stall")++stalls;else failed=true;app.quit();});
    QObject::connect(&guest,&Peer::mediaFailure,&app,[&](QString code){if(decoderFailure && code=="decoder_error")++decoderFailures;else failed=true;app.quit();});
    QObject::connect(&guest,&Peer::transportError,&app,[&]{++transportFailures;});
    QObject::connect(&guest,&Peer::audioUnavailable,&app,[&]{++audioWarnings;});
    QObject::connect(&guest,&Peer::failureDetails,&app,[&](QJsonObject detail){failureComponent=detail["component"].toString();});
    QString decoder,route;
    auto detail=[](QJsonObject value){std::cerr<<QJsonDocument(value).toJson(QJsonDocument::Compact).constData()<<'\n';};
    QObject::connect(&host,&Peer::failureDetails,&app,detail);QObject::connect(&guest,&Peer::failureDetails,&app,detail);
    QObject::connect(&guest,&Peer::metrics,&app,[&](QJsonObject values){decoder=values["decoder"].toString();knownDrops+=values["frames_discarded"].toDouble();rawFps=values["decoder_fps"].toDouble();availableFps=values["video_fps"].toDouble();});
    QObject::connect(&host, &Peer::metrics, &app, [&](QJsonObject v) { route = v["route"].toString();if(bitrateCeiling && elapsed.elapsed()>5000)videoRates.push_back(v["kbps"].toDouble()); });
    QObject::connect(&host, &Peer::error, &app, error); QObject::connect(&guest, &Peer::error, &app, error);
    QStringList turns;
    for (auto argument : app.arguments()) if (argument.startsWith("--turn=")) turns.append(argument.mid(7));
    if (!guest.start(q, {}, turns) || !host.start(q, {}, turns)) return 1;
    if(bitrateCeiling){PeerTestAccess::byteCounters(host,&encodedBytes,&rtpBytes);PeerTestAccess::frameCounters(host,frameCounts);}
    QString sourceText = QString("videotestsrc is-live=true pattern=%1 ! video/x-raw,format=I420,width=%2,height=%3,framerate=%4/1 ! tee name=t "
        "t. ! queue max-size-buffers=2 leaky=downstream ! appsink name=sink sync=false max-buffers=1 drop=true "
        "t. ! queue max-size-buffers=2 leaky=downstream ! videoconvert ! video/x-raw,format=RGB ! appsink name=reference sync=false max-buffers=1 drop=true")
        .arg(stress || slow || bitrateCeiling ? "smpte" : "ball").arg(q.width).arg(q.height).arg(q.fps);
    auto *source = gst_parse_launch(sourceText.toUtf8().constData(), nullptr);
    auto *sink = gst_bin_get_by_name(GST_BIN(source), "sink");
    auto *reference = gst_bin_get_by_name(GST_BIN(source), "reference");
    gst_element_set_state(source, GST_STATE_PLAYING);
    auto *silence = gst_parse_launch("audiotestsrc is-live=true wave=silence samplesperbuffer=480 ! audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved ! appsink name=sink sync=false max-buffers=10 drop=true", nullptr);
    auto *audio = gst_bin_get_by_name(GST_BIN(silence), "sink"); gst_element_set_state(silence, GST_STATE_PLAYING);
    QImage expected; int badFrames = 0; double maxError = 0;
    QTimer timer; timer.setInterval(8); int frames = 0;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if(audioPlayback && !audioWatched)audioWatched=PeerTestAccess::watchAudio(guest,&decodedAudio);
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
            if(relayDuration){auto now=elapsed.elapsed();if(lastFrameAt && now>5000)maxRelayGap=qMax(maxRelayGap,now-lastFrameAt);lastFrameAt=now;if(now>credentialLifetime)++framesAfterExpiry;}
            ++frames; if (!relayDuration && !stress && !stall && !decoderFailure && !slow && !bitrateCeiling && !audioFailure && frames >= 90) app.quit();
            if(audioFailure && !audioInjected && frames>=15)audioInjected=PeerTestAccess::failAudio(guest);
            if(audioFailure && audioWarnings && ++audioFrames>=90)app.quit();
            if(slow && !slowed && frames>=15)slowed=PeerTestAccess::slowDisplay(guest);
            if(decoderFailure && !decoderInjected && frames>=15)decoderInjected=PeerTestAccess::failDecoder(guest);
            if(stall && !blocked && frames>=15 && host.connected()){blocked=true;PeerTestAccess::dropEncoderInput(host);}
        }
        if(bitrateCeiling && !bytesReset && elapsed.elapsed()>5000){encodedBytes=rtpBytes=0;frameCounts[0]=frameCounts[1]=0;bytesReset=true;byteStart=elapsed.elapsed();}
        if(bitrateCeiling && elapsed.elapsed()>=15000)app.quit();
        if(slow){maxDepth=std::max(maxDepth,PeerTestAccess::queueDepth(guest));if(elapsed.elapsed()>=4500)app.quit();}
        if (stress && elapsed.elapsed() >= 8000) app.quit();
        if(relayDuration && elapsed.elapsed()>=relayDuration)app.quit();
        if (elapsed.elapsed() > (relayDuration?relayDuration+1000:20000)) { failed = true; app.quit(); }
    }); timer.start(); app.exec();
    gst_element_set_state(source, GST_STATE_NULL); gst_object_unref(sink); gst_object_unref(reference); gst_object_unref(source);
    gst_element_set_state(silence, GST_STATE_NULL); gst_object_unref(audio); gst_object_unref(silence);
    if(decoderFailure){if(failed || !decoderInjected || decoderFailures!=1 || transportFailures || !host.connected() || failureComponent!="video")return 1;std::cout<<"Decoder failure classified without transport recovery\n";return 0;}
    if(audioFailure && (!audioInjected || audioWarnings!=1 || audioFrames<90 || transportFailures || failureComponent!="audio"))return 1;
    if(audioPlayback){if(!audioWatched || decodedAudio<30 || audioWarnings)return 1;std::cout<<"Independent audio playback decoded "<<decodedAudio<<" PCM buffers\n";}
    if(app.arguments().contains("--require-software-decoder")){
        auto *factory=gst_element_factory_find(decoder.toUtf8().constData());const char *klass=factory?gst_element_factory_get_metadata(factory,GST_ELEMENT_METADATA_KLASS):nullptr;
        bool software=klass && !strstr(klass,"Hardware");if(factory)gst_object_unref(factory);if(!software)return 1;
    }
    if(stall){if(failed || stalls!=1 || !blocked || !host.connected() || route!="P2P direto")return 1;std::cout<<"Active encoder stall classified once without transport failure\n";return 0;}
    if(bitrateCeiling){double average=videoRates.empty()?0:std::accumulate(videoRates.begin(),videoRates.end(),0.)/videoRates.size();std::cout<<"Encoded frames="<<frameCounts[0]<<" keyframes="<<frameCounts[1]<<"\n";std::cout<<"Encoded="<<encodedBytes.load()*8/(elapsed.elapsed()-byteStart)<<" kbps RTP="<<rtpBytes.load()*8/(elapsed.elapsed()-byteStart)<<" kbps\n";std::cout<<"Video bitrate after warmup: "<<average<<" kbps / target "<<q.kbps<<"\n";if(videoRates.size()<3 || average<=0 || average>q.kbps*1.1)return 1;}
    if(slow){if(!slowed || maxDepth>1 || !knownDrops || rawFps<=availableFps || badFrames>1)return 1;std::cout<<"Decoded queue stayed bounded: depth="<<maxDepth<<" dropped="<<knownDrops<<" raw FPS="<<rawFps<<" available FPS="<<availableFps<<"\n";}
    if (stress) std::cout << "1080p/60 pixel integrity: frames=" << frames << " bad=" << badFrames << " max RGB error=" << maxError << '\n';
    if (stress && (frames < 60 || badFrames > 1)) failed = true;
    if(relayDuration){
        std::cout<<"Relay expiry continuity: post-expiry frames="<<framesAfterExpiry<<" max frame gap="<<maxRelayGap<<" ms\n";
        if(credentialLifetime<=0 || relayDuration<credentialLifetime+20000 || framesAfterExpiry<300 || maxRelayGap>500)return 1;
    }
    if (failed || frames < (stress ? 60 : slow ? 30 : 90) || !host.connected() || !guest.connected() || route != (turns.isEmpty() ? "P2P direto" : "Relay criptografado")) return 1;
    std::cout << "Route: " << route.toStdString() << '\n';
    std::cout << "Encrypted local WebRTC video: " << frames << " decoded frames / " << host.encoderName().toStdString() << " / decoder " << decoder.toStdString() << "\n";
}
