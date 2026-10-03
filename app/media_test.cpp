#include "peer.h"
#include "protocol.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <iostream>
#include <gst/video/video.h>
#include <cmath>
int main(int argc, char **argv) {
    gst_init(&argc, &argv); QCoreApplication app(argc, argv);
    if(app.arguments().contains("--encoder-creation-failure")) {
        Peer peer(true);QString classified;
        QObject::connect(&peer,&Peer::mediaFailure,&app,[&](QString code){classified=code;});
        VideoEncoder broken;broken.factory="missing-test-encoder";broken.chain="missing-test-encoder name=encoder";
        if(peer.start(Quality{}, {}, {}, broken) || classified!="encoder_start")return 1;
        std::cout<<"Encoder creation failure classified separately from transport\n";return 0;
    }
    bool stress = app.arguments().contains("--stress-quality");
    Peer host(true), guest(false); Quality q = stress ? Quality{1920,1080,60,8000} : Quality{640,360,30,1200};
    auto key = Protocol::randomBytes(16);
    Protocol::Channel h(key, "session", "peer", "host", "guest", true), g(key, "session", "peer", "guest", "host", false);
    QObject::connect(&host, &Peer::outgoing, &guest, [&](QJsonObject message) { QJsonObject decoded; if (g.open(h.seal(message), decoded)) guest.receive(decoded); });
    QObject::connect(&guest, &Peer::outgoing, &host, [&](QJsonObject message) { QJsonObject decoded; if (h.open(g.seal(message), decoded)) host.receive(decoded); });
    bool failed = false; auto error = [&](QString e) { std::cerr << e.toStdString() << '\n'; failed = true; app.quit(); };
    QString route;
    QObject::connect(&host, &Peer::metrics, &app, [&](QJsonObject v) { route = v["route"].toString(); });
    QObject::connect(&host, &Peer::error, &app, error); QObject::connect(&guest, &Peer::error, &app, error);
    QStringList turns;
    for (auto argument : app.arguments()) if (argument.startsWith("--turn=")) turns.append(argument.mid(7));
    if (!guest.start(q, {}, turns) || !host.start(q, {}, turns)) return 1;
    QString sourceText = QString("videotestsrc is-live=true pattern=%1 ! video/x-raw,format=I420,width=%2,height=%3,framerate=%4/1 ! tee name=t "
        "t. ! queue max-size-buffers=2 leaky=downstream ! appsink name=sink sync=false max-buffers=1 drop=true "
        "t. ! queue max-size-buffers=2 leaky=downstream ! videoconvert ! video/x-raw,format=RGB ! appsink name=reference sync=false max-buffers=1 drop=true")
        .arg(stress ? "smpte" : "ball").arg(q.width).arg(q.height).arg(q.fps);
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
            if (stress && !expected.isNull()) {
                double sum = 0; int count = 0;
                for (int y = 30; y < q.height * 2 / 3 - 30; y += 17) for (int x = 20; x < q.width - 20; x += 19) {
                    auto a = image.pixelColor(x,y), b = expected.pixelColor(x,y);
                    sum += std::abs(a.red()-b.red()) + std::abs(a.green()-b.green()) + std::abs(a.blue()-b.blue()); count += 3;
                }
                double error = sum / count; maxError = std::max(maxError,error); if (error > 10) ++badFrames;
            }
            ++frames; if (!stress && frames >= 90) app.quit();
        }
        if (stress && elapsed.elapsed() >= 8000) app.quit();
        if (elapsed.elapsed() > 20000) { failed = true; app.quit(); }
    }); timer.start(); app.exec();
    gst_element_set_state(source, GST_STATE_NULL); gst_object_unref(sink); gst_object_unref(reference); gst_object_unref(source);
    gst_element_set_state(silence, GST_STATE_NULL); gst_object_unref(audio); gst_object_unref(silence);
    if (stress) std::cout << "1080p/60 pixel integrity: frames=" << frames << " bad=" << badFrames << " max RGB error=" << maxError << '\n';
    if (stress && (frames < 60 || badFrames > 1)) failed = true;
    if (failed || frames < (stress ? 60 : 90) || !host.connected() || !guest.connected() || route != (turns.isEmpty() ? "P2P direto" : "Relay criptografado")) return 1;
    std::cout << "Route: " << route.toStdString() << '\n';
    std::cout << "Encrypted local WebRTC video: " << frames << " decoded frames / " << host.encoderName().toStdString() << "\n";
}
