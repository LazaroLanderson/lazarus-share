#pragma once
#include <gst/video/video.h>
class FramePreparer {
public:
    ~FramePreparer();
    GstSample *nv12(GstSample *source);
private:
    GstVideoInfo input_{},output_{};
    GstVideoConverter *converter_=nullptr;
    GstBufferPool *pool_=nullptr;
    GstCaps *caps_=nullptr;
    void clear();
};
