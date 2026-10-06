#include "frameprep.h"
#include "frametime.h"
void FramePreparer::clear(){if(converter_)gst_video_converter_free(converter_);converter_=nullptr;if(pool_){gst_buffer_pool_set_active(pool_,FALSE);gst_object_unref(pool_);}pool_=nullptr;if(caps_)gst_caps_unref(caps_);caps_=nullptr;}
FramePreparer::~FramePreparer(){clear();}
GstSample *FramePreparer::nv12(GstSample *source){
    GstVideoInfo info;if(!source || !gst_video_info_from_caps(&info,gst_sample_get_caps(source)))return nullptr;
    if(GST_VIDEO_INFO_FORMAT(&info)==GST_VIDEO_FORMAT_NV12)return gst_sample_ref(source);
    if(!converter_ || !gst_video_info_is_equal(&input_,&info)){
        clear();input_=info;gst_video_info_set_format(&output_,GST_VIDEO_FORMAT_NV12,info.width,info.height);output_.colorimetry=info.colorimetry;output_.chroma_site=info.chroma_site;output_.par_n=info.par_n;output_.par_d=info.par_d;output_.fps_n=info.fps_n;output_.fps_d=info.fps_d;
        converter_=gst_video_converter_new(&input_,&output_,nullptr);if(!converter_)return nullptr;
        caps_=gst_video_info_to_caps(&output_);pool_=gst_video_buffer_pool_new();auto *config=gst_buffer_pool_get_config(pool_);
        gst_buffer_pool_config_set_params(config,caps_,output_.size,0,8);gst_buffer_pool_config_add_option(config,GST_BUFFER_POOL_OPTION_VIDEO_META);
        if(!gst_buffer_pool_set_config(pool_,config) || !gst_buffer_pool_set_active(pool_,TRUE)){clear();return nullptr;}
    }
    GstBuffer *buffer=nullptr;GstBufferPoolAcquireParams params{};params.flags=GST_BUFFER_POOL_ACQUIRE_FLAG_DONTWAIT;
    if(gst_buffer_pool_acquire_buffer(pool_,&buffer,&params)!=GST_FLOW_OK)return nullptr;
    GstVideoFrame in{},out{};bool a=gst_video_frame_map(&in,&input_,gst_sample_get_buffer(source),GST_MAP_READ),b=gst_video_frame_map(&out,&output_,buffer,GST_MAP_WRITE);
    if(a && b)gst_video_converter_frame(converter_,&in,&out);
    if(a)gst_video_frame_unmap(&in);if(b)gst_video_frame_unmap(&out);
    if(!a || !b){gst_buffer_unref(buffer);return nullptr;}
    gst_buffer_copy_into(buffer,gst_sample_get_buffer(source),GST_BUFFER_COPY_TIMESTAMPS,0,-1);
    if(auto *meta=gst_buffer_get_reference_timestamp_meta(gst_sample_get_buffer(source),captureTimeCaps()))
        gst_buffer_add_reference_timestamp_meta(buffer,meta->reference,meta->timestamp,meta->duration);
    auto *sample=gst_sample_new(buffer,caps_,gst_sample_get_segment(source),nullptr);gst_buffer_unref(buffer);return sample;
}
