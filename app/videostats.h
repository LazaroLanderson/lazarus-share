#pragma once
#include <gst/gst.h>
struct VideoStats {double bytes=0,packets=0,lost=0,rttMs=0;bool feedback=false;};
VideoStats videoStats(const GstStructure *reply,bool host,unsigned ssrc=0);
