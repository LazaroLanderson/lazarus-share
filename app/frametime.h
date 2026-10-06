#pragma once
#include <gst/gst.h>

// Capture pipelines may start at different times. Their running times are not
// interchangeable; retain the absolute capture time on our shared system clock.
inline void useMediaClock(GstElement *pipeline) {
    auto *clock = gst_system_clock_obtain();
    gst_pipeline_use_clock(GST_PIPELINE(pipeline), clock);
    gst_object_unref(clock);
}
inline GstClockTime sampleRunningTime(GstSample *sample) {
    auto pts = GST_BUFFER_PTS(gst_sample_get_buffer(sample));
    auto *segment = gst_sample_get_segment(sample);
    return segment && segment->format == GST_FORMAT_TIME
        ? gst_segment_to_running_time(segment, GST_FORMAT_TIME, pts) : pts;
}
inline GstCaps *captureTimeCaps() {
    static auto *caps = gst_caps_new_empty_simple("timestamp/x-lazarus-capture");
    return caps;
}
inline GstSample *withCaptureTime(GstSample *sample, GstElement *pipeline) {
    auto running = sampleRunningTime(sample);
    auto base = gst_element_get_base_time(pipeline);
    if (!GST_CLOCK_TIME_IS_VALID(running) || !GST_CLOCK_TIME_IS_VALID(base) || running >= GST_CLOCK_TIME_NONE - base)
        return gst_sample_ref(sample);
    auto *buffer = gst_buffer_copy(gst_sample_get_buffer(sample));
    gst_buffer_add_reference_timestamp_meta(buffer, captureTimeCaps(), base + running, GST_BUFFER_DURATION(buffer));
    auto *out = gst_sample_new(buffer, gst_sample_get_caps(sample), gst_sample_get_segment(sample), nullptr);
    gst_buffer_unref(buffer);
    return out;
}
inline GstClockTime captureRunningTime(GstSample *sample, GstElement *pipeline) {
    auto *meta = gst_buffer_get_reference_timestamp_meta(gst_sample_get_buffer(sample), captureTimeCaps());
    auto base = gst_element_get_base_time(pipeline);
    if (!meta || !GST_CLOCK_TIME_IS_VALID(base) || meta->timestamp < base) return GST_CLOCK_TIME_NONE;
    return meta->timestamp - base;
}

// Keep capture spacing, independently of UI delivery jitter. Each connection
// anchors its capture timeline to its own running time, including late joiners.
class VideoTimeline {
public:
    GstClockTime map(GstClockTime source,GstClockTime running) {
        if(!GST_CLOCK_TIME_IS_VALID(source) || !GST_CLOCK_TIME_IS_VALID(running)) {
            sourceBase_=GST_CLOCK_TIME_NONE;return GST_CLOCK_TIME_NONE;
        }
        if(!GST_CLOCK_TIME_IS_VALID(sourceBase_) || source<lastSource_) {
            sourceBase_=source;outputBase_=running;
        }
        lastSource_=source;return outputBase_+source-sourceBase_;
    }
private:
    GstClockTime sourceBase_=GST_CLOCK_TIME_NONE,outputBase_=0,lastSource_=0;
};
