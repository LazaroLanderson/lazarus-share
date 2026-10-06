#include "peer.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <gst/video/video.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <mutex>
#include <vector>

struct RenderEvents {
    std::mutex mutex;
    std::vector<gint64> audio;
    bool sounding = false;
};
struct PeerTestAccess {
    static void timings(Peer &peer) {
        auto *clock=gst_element_get_clock(peer.pipeline_);auto now=gst_clock_get_time(clock)-gst_element_get_base_time(peer.pipeline_);gst_object_unref(clock);
        std::cout << "Pending frames=" << peer.frames_.size() << " front relative to now=" << (peer.frames_.empty()?0:(double(peer.frames_.front().running)-double(now))/GST_MSECOND) << " ms\n";
        std::cout << "Decoder frames=" << peer.decoderFrames_ << " available=" << peer.decodedFrames_ << " latency=" << double(peer.playbackLatency_.load())/GST_MSECOND << " ms base difference=" << (peer.audioPlayback_?double(gst_element_get_base_time(peer.audioPlayback_))-double(gst_element_get_base_time(peer.pipeline_)):0)/GST_MSECOND << " ms\n";
    }
    static bool watchAudio(Peer &peer, RenderEvents &events) {
        QMutexLocker lock(&peer.receiveMutex_);
        if (!peer.audioPlayback_) return false;
        auto *sink = gst_bin_get_by_name(GST_BIN(peer.audioPlayback_), "audio_output");
        if (!sink) return false;
        g_signal_connect(sink, "handoff", G_CALLBACK(+[](GstElement *, GstBuffer *buffer, GstPad *, gpointer data) {
            auto &events = *static_cast<RenderEvents *>(data);
            GstMapInfo map{};
            if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) return;
            auto *samples = reinterpret_cast<const float *>(map.data);
            double energy = 0; auto count = map.size / sizeof(float);
            for (size_t i = 0; i < count; ++i) energy += samples[i] * samples[i];
            bool sounding = count && energy / count > .005;
            gst_buffer_unmap(buffer, &map);
            std::lock_guard guard(events.mutex);
            if (sounding && !events.sounding) events.audio.push_back(g_get_monotonic_time());
            events.sounding = sounding;
        }), &events);
        gst_object_unref(sink); return true;
    }
};

// Generate matched flashes and pulses in a single capture clock domain, then
// exercise the production sender, RTP transport, decoder and render scheduling.
int main(int argc, char **argv) {
    gst_init(&argc, &argv); QCoreApplication app(argc, argv);
    qputenv("LAZARUS_TEST_AUDIO_RENDER", "1");
    qputenv("LAZARUS_VIDEO_ENCODER", "vp8");
    int duration = 8;
    for (auto arg : app.arguments()) if (arg.startsWith("--duration=")) duration = arg.section('=', 1).toInt();
    if (duration < 5) return 1;
    RenderEvents events; // Outlives the streaming callbacks during Peer teardown.
    Peer host(true), guest(false);
    QObject::connect(&host, &Peer::outgoing, &guest, &Peer::receive);
    QObject::connect(&guest, &Peer::outgoing, &host, &Peer::receive);
    bool failed = false;
    auto failure = [&](QString code) { std::cerr << "Media failure: " << code.toStdString() << '\n'; failed = true; app.quit(); };
    QObject::connect(&host, &Peer::mediaFailure, &app, failure);
    QObject::connect(&guest, &Peer::mediaFailure, &app, failure);
    QObject::connect(&guest, &Peer::audioUnavailable, &app, [&] { failed = true; app.quit(); });
    Quality quality{160, 90, 60, 500};
    if (!guest.start(quality, {}) || !host.start(quality, {})) return 1;
    auto *capture = gst_parse_launch(
        "videotestsrc is-live=true pattern=black ! video/x-raw,format=I420,width=160,height=90,framerate=60/1 ! appsink name=video sync=false max-buffers=1 drop=true "
        "audiotestsrc is-live=true wave=silence samplesperbuffer=480 ! audio/x-raw,format=F32LE,rate=48000,channels=2,layout=interleaved ! appsink name=audio sync=false max-buffers=10 drop=true", nullptr);
    if (!capture) return 1;
    useMediaClock(capture);
    auto *video = gst_bin_get_by_name(GST_BIN(capture), "video");
    auto *audio = gst_bin_get_by_name(GST_BIN(capture), "audio");
    gst_element_set_state(capture, GST_STATE_PLAYING);
    QElapsedTimer elapsed; elapsed.start();
    int minPixel=255,maxPixel=0; int sentFlashes=0, sentPulses=0; int receivedFrames=0; bool watched = false, white = false; qint64 lastAudioDelivery = 0;
    std::vector<gint64> flashes;
    QTimer timer; timer.setTimerType(Qt::PreciseTimer); timer.setInterval(4);
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (!watched) watched = PeerTestAccess::watchAudio(guest, events);
        if (auto *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(video), 0)) {
            auto *buffer = gst_buffer_copy_deep(gst_sample_get_buffer(sample));
            GST_BUFFER_FLAG_UNSET(buffer,GST_BUFFER_FLAG_GAP);
            auto t = sampleRunningTime(sample);
            bool flash = t % GST_SECOND < 100 * GST_MSECOND; if(flash)++sentFlashes;
            gst_buffer_memset(buffer, 0, flash ? 235 : 16, 160 * 90);
            auto *modified = gst_sample_new(buffer, gst_sample_get_caps(sample), gst_sample_get_segment(sample), nullptr);
            auto *timed = withCaptureTime(modified, capture);
            host.video(timed);
            gst_sample_unref(timed); gst_sample_unref(modified); gst_buffer_unref(buffer); gst_sample_unref(sample);
        }
        // Exercise real batches: audio waits in capture for up to 60 ms while
        // video continues. Correct timestamps must retain the earlier event time.
        if (elapsed.elapsed() - lastAudioDelivery >= 60) {
            lastAudioDelivery = elapsed.elapsed();
            while (auto *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(audio), 0)) {
                auto *buffer = gst_buffer_copy_deep(gst_sample_get_buffer(sample));
            GST_BUFFER_FLAG_UNSET(buffer,GST_BUFFER_FLAG_GAP);
                auto t = sampleRunningTime(sample); if(t % GST_SECOND < 100 * GST_MSECOND)++sentPulses; GstMapInfo map{};
                gst_buffer_map(buffer, &map, GST_MAP_WRITE);
                auto *values = reinterpret_cast<float *>(map.data);
                for (size_t i = 0; i < map.size / sizeof(float) / 2; ++i) {
                    auto at = t + gst_util_uint64_scale(i, GST_SECOND, 48000);
                    float value = at % GST_SECOND < 100 * GST_MSECOND ? float(.5 * std::sin(2 * M_PI * 440 * double(at) / GST_SECOND)) : 0;
                    values[2 * i] = values[2 * i + 1] = value;
                }
                gst_buffer_unmap(buffer, &map);
                auto *modified = gst_sample_new(buffer, gst_sample_get_caps(sample), gst_sample_get_segment(sample), nullptr);
                auto *timed = withCaptureTime(modified, capture);
                host.audio(timed);
                gst_sample_unref(timed); gst_sample_unref(modified); gst_buffer_unref(buffer); gst_sample_unref(sample);
            }
        }
        auto frame = guest.takeFrame();
        if (!frame.isNull()) {
            ++receivedFrames;
            int pixel=frame.pixelColor(80,45).red(); minPixel=std::min(pixel,minPixel); maxPixel=std::max(pixel,maxPixel);
            bool nowWhite = pixel > 180;
            if (nowWhite && !white && elapsed.elapsed() > 2000) flashes.push_back(g_get_monotonic_time());
            white = nowWhite;
        }
        if (elapsed.elapsed() >= duration * 1000) app.quit();
    });
    timer.start(); app.exec(); timer.stop();
    gst_element_set_state(capture, GST_STATE_NULL);
    gst_object_unref(video); gst_object_unref(audio); gst_object_unref(capture);
    PeerTestAccess::timings(guest);
    std::vector<double> offsets;
    { std::lock_guard guard(events.mutex);
        for (auto flash : flashes) {
            if (events.audio.empty()) break;
            auto nearest = *std::min_element(events.audio.begin(), events.audio.end(), [flash](auto a, auto b) { return std::abs(a - flash) < std::abs(b - flash); });
            offsets.push_back(double(nearest - flash) / 1000);
        }
    }
    double worst = 0;
    for (auto offset : offsets) { worst = std::max(worst, std::abs(offset)); std::cout << "offset=" << offset << " ms\n"; }
    double drift = offsets.size() > 1 ? std::abs(offsets.back() - offsets.front()) : 0;
    std::cout << "Pixel range=" << minPixel << ".." << maxPixel << " Sent flash frames=" << sentFlashes << " pulse buffers=" << sentPulses << " watched=" << watched << " Video frames=" << receivedFrames << " connected=" << host.connected() << "/" << guest.connected() << " audio pulses=" << events.audio.size() << " Flash/pulse pairs=" << offsets.size() << " worst skew=" << worst << " ms drift=" << drift << " ms duration=" << duration << " s\n";
    return failed || !watched || !host.connected() || !guest.connected() || offsets.size() < size_t(duration - 4) || worst > 40 || drift > 40 ? 1 : 0;
}
