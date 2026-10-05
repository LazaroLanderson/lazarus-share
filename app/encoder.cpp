#include "encoder.h"
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <QProcessEnvironment>
#include <algorithm>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>

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
static bool validate(const QString &chain,int width,int height,int fps) {
    GError *error = nullptr;
    auto text = QString("videotestsrc num-buffers=8 ! video/x-raw,format=NV12,width=%2,height=%3,framerate=%4/1 ! %1 ! h264parse ! openh264dec ! appsink name=probe sync=false max-buffers=1 drop=true").arg(chain).arg(width).arg(height).arg(fps);
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
VideoEncoder selectVideoEncoder(int fps,int kbps,int width,int height,bool software) {
    VideoEncoder result;
#ifdef LAZARUS_TESTING
    // Exercise H.264 receiver recovery on runners without a hardware encoder.
    if(qEnvironmentVariableIsSet("LAZARUS_TEST_H264")){
        result.factory="openh264enc";result.codec="H264";result.format="I420";result.name="H.264 / test encoder";
        result.chain=QString("openh264enc name=encoder bitrate=%1 gop-size=%2").arg(kbps*1000).arg(fps);
        result.bitrateProperty="bitrate";result.bitrateMultiplier=1000;return result;
    }
#endif
    if(!software && qEnvironmentVariable("LAZARUS_VIDEO_ENCODER")!="vp8") {
        static QMutex mutex;static QHash<QString,bool> probes;
        QMutexLocker lock(&mutex);
        for(const char *factory:{"nvh264enc","nvd3d11h264enc","qsvh264enc","vah264enc","vah264lpenc"}) {
            QString chain=hardwareChain(factory,fps,kbps);if(chain.isEmpty())continue;
            // Factories select their default device for this process. Include its reported
            // identity and the complete requested configuration in the runtime cache.
            auto *device=gst_element_factory_make(factory,nullptr);QString identity;
            for(const char *property:{"render-device","device-path","adapter-luid","cuda-device-id"}) {
                if(device && g_object_class_find_property(G_OBJECT_GET_CLASS(device),property)) {
                    GValue value=G_VALUE_INIT;auto *spec=g_object_class_find_property(G_OBJECT_GET_CLASS(device),property);
                    g_value_init(&value,G_PARAM_SPEC_VALUE_TYPE(spec));g_object_get_property(G_OBJECT(device),property,&value);
                    gchar *serialized=gst_value_serialize(&value);if(serialized){identity+=QString::fromUtf8(serialized);g_free(serialized);}g_value_unset(&value);
                }
            }
            if(device)gst_object_unref(device);
            QString key=QString("%1:%2:%3:%4:%5:%6").arg(factory).arg(identity).arg(width).arg(height).arg(fps).arg(kbps);
            if(!probes.contains(key))probes[key]=validate(chain,width,height,fps);
            if(!probes[key])continue;
            result.factory=QString::fromLatin1(factory);result.chain=chain;result.format="NV12";result.codec="H264";
            result.name=result.factory.startsWith("nv")?"H.264 / NVIDIA NVENC":result.factory.startsWith("qsv")?"H.264 / Intel Quick Sync":"H.264 / VA-API (GPU)";
            result.bitrateProperty="bitrate";result.bitrateMultiplier=1;return result;
        }
    }
    auto *factory=gst_element_factory_find("vp8enc");if(!factory){result.name="Software indisponível";return result;}gst_object_unref(factory);
    result.factory="vp8enc";
    result.chain=QString("vp8enc name=encoder deadline=1 cpu-used=8 threads=4 lag-in-frames=0 end-usage=cbr buffer-size=1000 buffer-initial-size=200 buffer-optimal-size=500 overshoot=10 keyframe-max-dist=%1 target-bitrate=%2").arg(fps).arg(kbps*1000);
    return result;
}
