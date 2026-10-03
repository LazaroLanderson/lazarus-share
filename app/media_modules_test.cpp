#include "bitrate.h"
#include "frameprep.h"
#include "encoder.h"
#include "videoview.h"
#include "videostats.h"
#include "mediawatch.h"
#include "frametime.h"
#include <gst/webrtc/webrtc.h>
#include <QApplication>
#include <gst/gst.h>
#include <stdexcept>
#include <iostream>
#include <vector>
static void check(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int main(int argc,char **argv){gst_init(&argc,&argv);QApplication app(argc,argv);
 try {
    VideoTimeline timeline;
    check(timeline.map(100*GST_SECOND,GST_SECOND)==GST_SECOND,"Late viewer clock anchor");
    check(timeline.map(100*GST_SECOND+33333333,GST_SECOND+40000000)==GST_SECOND+33333333,"UI jitter changed capture spacing");
    check(timeline.map(0,2*GST_SECOND)==2*GST_SECOND,"Capture clock restart");
    check(timeline.map(GST_CLOCK_TIME_NONE,3*GST_SECOND)==GST_CLOCK_TIME_NONE,"Unknown timestamp was invented");
    check(timeline.map(33333333,4*GST_SECOND)==4*GST_SECOND,"Missing timestamp did not reanchor");
    check(timeline.map(66666666,GST_CLOCK_TIME_NONE)==GST_CLOCK_TIME_NONE,"Unknown pipeline clock was invented");
    check(!encoderStalled(9000,8990,2000,2000,false),"Disconnected stall");check(!encoderStalled(9000,7000,2000,2000,true),"Capture pause triggered encoder stall");check(!encoderStalled(6999,6990,2000,2000,true),"Early stall");check(encoderStalled(7000,6990,2000,2000,true),"Five-second stall missing");check(!encoderStalled(7000,6990,6500,2000,true),"Output did not reset watchdog");
    BitrateController rate;for(int i=0;i<20;++i)check(rate.update(0,650,true)==8000,"Stable high RTT degraded bitrate");
    check(rate.update(.05,650,true)==6800,"One severe loss interval");
    int held=rate.value();for(int i=0;i<8;++i)check(rate.update(0,0,false)==held,"Missing feedback changed bitrate");
    for(int i=0;i<3;++i)rate.update(0,650,true);check(rate.value()>held,"Healthy recovery");
    rate.reset(8000);rate.update(.025,30,true);rate.update(0,0,false);check(rate.update(.025,30,true)==8000,"Absent feedback joined nonconsecutive loss samples");
    rate.reset(8000);rate.update(.025,30,true);check(rate.value()==8000,"Premature moderate loss reduction");check(rate.update(.025,30,true)==6800,"Sustained moderate loss");
    rate.reset(8000);for(int i=0;i<5;++i)rate.update(0,40,true);for(int i=0;i<7;++i)rate.update(0,300,true);check(rate.value()<8000,"RTT growth ignored");
    for(int i=0;i<80;++i)rate.update(.2,300,true);check(rate.value()==300,"Bitrate floor");rate.target(200);check(rate.value()==200,"Low target ceiling");
    rate.reset(8000);for(int i=0;i<100;++i)rate.update(0,30,true);check(rate.value()==8000,"Target overshoot");
    auto *reply=gst_structure_new_empty("stats");
    auto *out=gst_structure_new("out","type",GST_TYPE_WEBRTC_STATS_TYPE,GST_WEBRTC_STATS_OUTBOUND_RTP,"ssrc",G_TYPE_UINT,123u,"kind",G_TYPE_STRING,"audio","bytes-sent",G_TYPE_UINT64,guint64(1000),"packets-sent",G_TYPE_UINT64,guint64(10),"remote-id",G_TYPE_STRING,"remote",nullptr);
    auto *remote=gst_structure_new("remote","type",GST_TYPE_WEBRTC_STATS_TYPE,GST_WEBRTC_STATS_REMOTE_INBOUND_RTP,"ssrc",G_TYPE_UINT,123u,"packets-lost",G_TYPE_INT64,gint64(2),"round-trip-time",G_TYPE_DOUBLE,.65,nullptr);
    auto *audio=gst_structure_copy(out);gst_structure_set(audio,"ssrc",G_TYPE_UINT,999u,"bytes-sent",G_TYPE_UINT64,guint64(900000),nullptr);
    gst_structure_set(reply,"out",GST_TYPE_STRUCTURE,out,"remote",GST_TYPE_STRUCTURE,remote,"audio",GST_TYPE_STRUCTURE,audio,nullptr);
    auto video=videoStats(reply,true,123);check(video.bytes==1000 && video.packets==10 && video.lost==2 && video.rttMs==650 && video.feedback,"Video SSRC feedback correlation or audio isolation");
    gst_structure_free(out);gst_structure_free(remote);gst_structure_free(audio);gst_structure_free(reply);
    FramePreparer prepare;GstVideoInfo info{};gst_video_info_set_format(&info,GST_VIDEO_FORMAT_I420,1280,720);info.fps_n=30;info.fps_d=1;
    auto *caps=gst_video_info_to_caps(&info);auto *buffer=gst_buffer_new_allocate(nullptr,info.size,nullptr);gst_buffer_memset(buffer,0,128,info.size);GST_BUFFER_PTS(buffer)=123;
    auto *sample=gst_sample_new(buffer,caps,nullptr,nullptr);auto *converted=prepare.nv12(sample);check(converted,"NV12 conversion failed");
    GstVideoInfo actual;check(gst_video_info_from_caps(&actual,gst_sample_get_caps(converted)),"Converted caps invalid");check(actual.width==1280 && actual.height==720 && GST_VIDEO_INFO_FORMAT(&actual)==GST_VIDEO_FORMAT_NV12,"Converted shape");check(GST_BUFFER_PTS(gst_sample_get_buffer(converted))==123,"Frame timestamp mismatch");
    GstVideoFrame frame;check(gst_video_frame_map(&frame,&actual,gst_sample_get_buffer(converted),GST_MAP_READ),"Converted map");check(static_cast<unsigned char *>(GST_VIDEO_FRAME_PLANE_DATA(&frame,0))[0]==128,"Conversion changed pixels");gst_video_frame_unmap(&frame);
    gst_sample_unref(converted);converted=nullptr;
    std::vector<GstSample *> heldBuffers;for(int i=0;i<8;++i){auto *item=prepare.nv12(sample);check(item,"Pool exhausted too early");heldBuffers.push_back(item);}
    check(!prepare.nv12(sample),"Pool grew beyond eight buffers");
    for(auto *item:heldBuffers)gst_sample_unref(item);converted=prepare.nv12(sample);check(converted,"Pool did not recover released buffers");
    gst_sample_unref(converted);gst_sample_unref(sample);gst_buffer_unref(buffer);gst_caps_unref(caps);
    VideoView view;view.resize(640,360);view.show();
    for(auto format:{QImage::Format_RGB888,QImage::Format_RGB32,QImage::Format_RGBA8888,QImage::Format_RGB888}){
        QImage picture(1280,720,format);picture.fill(QColor(12,34,56));if(format==QImage::Format_RGB32){for(int y=0;y<picture.height();++y){auto *row=reinterpret_cast<quint32 *>(picture.scanLine(y));for(int x=0;x<picture.width();++x)row[x]&=0x00ffffff;}}view.setFrame(picture);app.processEvents();
        check(view.hasFrame(),"Render frame lost");auto rendered=view.grab().toImage();
        check(rendered.pixelColor(rendered.width()/2,rendered.height()/2)==QColor(12,34,56),"Rendered pixel integrity or format switch");
    }
    if(app.arguments().contains("--require-gl"))check(view.backend()=="OpenGL","OpenGL path unavailable");
    view.clearFrame("Stopped");check(!view.hasFrame(),"Stop retained frame");
    auto cpu=selectVideoEncoder(30,3000,1280,720,true);check(cpu.factory=="vp8enc" && cpu.codec=="VP8","Software selection");
    auto *feature=gst_registry_find_feature(gst_registry_get(),"vp8enc",GST_TYPE_ELEMENT_FACTORY);check(feature,"Missing VP8 test fixture");
    gst_registry_remove_feature(gst_registry_get(),feature);gst_object_unref(feature);
    check(selectVideoEncoder(30,3000,1280,720,true).chain.isEmpty(),"Unavailable software was selected");
    std::cout<<"Bitrate, frame preparation, rendering and software selection passed\n";
 }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
