#include "window.h"
#include "encoder.h"
#include <QApplication>
#include <QTimer>
#include <atomic>
#include <iostream>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
int main(int argc, char **argv) {
#ifdef Q_OS_WIN
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
#endif
    // Disable GStreamer diagnostic logging by default: it can print SDP and IPs.
    qputenv("GST_DEBUG", "0");
    gst_init(&argc, &argv);
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == "--check-runtime") {
        bool ok = true;
        for (const char *name : {"queue", "capsfilter", "appsrc", "appsink", "webrtcbin", "nicesrc", "dtlssrtpenc", "srtpenc", "rtpvp8pay", "vp8enc", "opusenc", "videoconvert", "audiomixer", "h264parse", "openh264dec", "decodebin", "rtph264pay", "rtph264depay"}) {
            auto *factory = gst_element_factory_find(name);
            if (factory) gst_object_unref(factory);
            else { std::cerr << "Missing runtime element: " << name << '\n'; ok = false; }
        }
        // Exercise autoplugging from the bundled plugins, not only factories.
        // No Qt profile, capture, network or hardware is involved in this check.
        if (ok) {
            GError *error=nullptr;
            auto *pipeline=gst_parse_launch("videotestsrc num-buffers=3 ! video/x-raw,format=I420,width=320,height=240,framerate=30/1 ! openh264enc ! h264parse ! decodebin force-sw-decoders=true ! video/x-raw,format=I420 ! fakesink name=decoded sync=false signal-handoffs=true",&error);
            if(error){g_error_free(error);ok=false;}
            if(pipeline && ok){
                std::atomic<int> frames{0};auto *sink=gst_bin_get_by_name(GST_BIN(pipeline),"decoded");
                g_signal_connect(sink,"handoff",G_CALLBACK(+[](GstElement*,GstBuffer*,GstPad*,gpointer data){++*static_cast<std::atomic<int>*>(data);}),&frames);
                auto *bus=gst_element_get_bus(pipeline);
                bool started=gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE;
                auto *message=started?gst_bus_timed_pop_filtered(bus,5*GST_SECOND,GstMessageType(GST_MESSAGE_EOS|GST_MESSAGE_ERROR)):nullptr;
                ok=message && GST_MESSAGE_TYPE(message)==GST_MESSAGE_EOS && frames.load()>0;
                if(message)gst_message_unref(message);
                gst_element_set_state(pipeline,GST_STATE_NULL);gst_object_unref(bus);gst_object_unref(sink);
            }else ok=false;
            if(pipeline)gst_object_unref(pipeline);
            if(!ok)std::cerr<<"Bundled H.264 decoder could not produce synthetic video\n";
        }
        if (ok) std::cout << "Runtime dependencies and synthetic H.264 decoding available\n";
        return ok ? 0 : 1;
    }
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == "--check-encoder") {
        std::cout << selectVideoEncoder(60, 8000).name.toStdString() << '\n'; return 0;
    }
    QApplication app(argc, argv);
    if (app.arguments().contains("--smoke-test")) QTimer::singleShot(500, &app, &QCoreApplication::quit);
    QCoreApplication::setOrganizationName("LazarusLabs");
    QCoreApplication::setApplicationName("LazarusShare");
    QCoreApplication::setApplicationVersion("0.2.2");
    int result;
    { Window window(!app.arguments().contains("--smoke-test")); window.show(); result = app.exec(); }
#ifdef Q_OS_WIN
    CoUninitialize();
#endif
    return result;
}
