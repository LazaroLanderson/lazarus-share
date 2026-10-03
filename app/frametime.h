#pragma once
#include <gst/gst.h>

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
