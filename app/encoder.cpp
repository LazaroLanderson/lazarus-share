#include "encoder.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <QProcessEnvironment>
#include <algorithm>

static QString hardwareChain(const char *factory, int fps, int kbps) {
    auto *element = gst_element_factory_make(factory, nullptr);
    if (!element) return {};
    QString chain = QString("%1 name=encoder bitrate=%2").arg(factory).arg(kbps);
    auto property = [&](const char *name, int value) {
        if (g_object_class_find_property(G_OBJECT_GET_CLASS(element), name))
            chain += QString(" %1=%2").arg(name).arg(value);
    };
    property("b-frames", 0); property("key-int-max", std::min(fps,1024));
    property("gop-size", fps); property("zerolatency", 1);
    // VA speed/quality compromise; no frame reordering on any backend.
    property("target-usage", 6);
    auto enumOption = [&](const char *name, const char *nick) {
        auto *spec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), name);
        if (!spec || !G_IS_PARAM_SPEC_ENUM(spec)) return;
        auto *values = static_cast<GEnumClass *>(g_type_class_ref(G_PARAM_SPEC_VALUE_TYPE(spec)));
        if (auto *value = g_enum_get_value_by_nick(values, nick)) property(name, value->value);
        g_type_class_unref(values);
    };
    enumOption("rc-mode", "cbr"); enumOption("rate-control", "cbr");
    enumOption("tune", "ultra-low-latency");
    enumOption("preset", "low-latency-hq");
    gst_object_unref(element);
    if (QString::fromLatin1(factory).startsWith("va"))
        chain.prepend("vapostproc disable-passthrough=true ! video/x-raw(memory:VAMemory),format=NV12 ! ");
    return chain;
}
static bool validate(const QString &chain) {
    GError *error = nullptr;
    auto text = QString("videotestsrc num-buffers=8 ! video/x-raw,format=NV12,width=320,height=240,framerate=30/1 ! %1 ! h264parse ! openh264dec ! appsink name=probe sync=false max-buffers=1 drop=true").arg(chain);
    auto *pipeline = gst_parse_launch(text.toUtf8().constData(), &error);
    if (error || !pipeline) {
        if (error) g_error_free(error);
        if (pipeline) gst_object_unref(pipeline);
        return false;
    }
    auto *sink = gst_bin_get_by_name(GST_BIN(pipeline), "probe");
    bool ok = false;
    if (sink && gst_element_set_state(pipeline, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE) {
        auto *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 3 * GST_SECOND);
        ok = sample != nullptr;
        if (sample) gst_sample_unref(sample);
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);
    if (sink) gst_object_unref(sink);
    gst_object_unref(pipeline);
    return ok;
}
VideoEncoder selectVideoEncoder(int fps, int kbps) {
    // The selection is cached, while bitrate and GOP remain specific to each viewer.
    static const QString selected = [] {
        if (qEnvironmentVariable("LAZARUS_VIDEO_ENCODER") == "vp8") return QString();
        for (const char *factory : {"nvh264enc", "nvd3d11h264enc", "qsvh264enc", "vah264enc", "vah264lpenc"}) {
            QString chain = hardwareChain(factory, 30, 2000);
            if (!chain.isEmpty() && validate(chain)) return QString::fromLatin1(factory);
        }
        return QString();
    }();
    VideoEncoder result;
    if (!selected.isEmpty()) {
        result.chain = hardwareChain(selected.toLatin1().constData(), fps, kbps);
        result.format = "NV12"; result.codec = "H264";
        result.name = selected.startsWith("nv") ? "H.264 / NVIDIA NVENC" :
                      selected.startsWith("qsv") ? "H.264 / Intel Quick Sync" : "H.264 / VA-API (GPU)";
        result.bitrateProperty = "bitrate"; result.bitrateMultiplier = 1;
    } else {
        result.chain = QString("vp8enc name=encoder deadline=1 cpu-used=6 threads=4 lag-in-frames=0 keyframe-max-dist=%1 target-bitrate=%2").arg(fps).arg(kbps * 1000);
    }
    return result;
}
