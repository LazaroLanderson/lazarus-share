#include "window.h"
#include "encoder.h"
#include <QApplication>
#include <QTimer>
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
        if (ok) std::cout << "Runtime dependencies available\n";
        return ok ? 0 : 1;
    }
    if (argc > 1 && QString::fromLocal8Bit(argv[1]) == "--check-encoder") {
        std::cout << selectVideoEncoder(60, 8000).name.toStdString() << '\n'; return 0;
    }
    QApplication app(argc, argv);
    if (app.arguments().contains("--smoke-test")) QTimer::singleShot(500, &app, &QCoreApplication::quit);
    QCoreApplication::setOrganizationName("LazarusLabs");
    QCoreApplication::setApplicationName("LazarusShare");
    QCoreApplication::setApplicationVersion("0.2.1");
    int result;
    { Window window(!app.arguments().contains("--smoke-test")); window.show(); result = app.exec(); }
#ifdef Q_OS_WIN
    CoUninitialize();
#endif
    return result;
}
