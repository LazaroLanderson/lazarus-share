#include "videostats.h"
#include <gst/webrtc/webrtc.h>
#include <cstring>
#include <algorithm>

static double number(const GstStructure *s,const char *key){auto *v=gst_structure_get_value(s,key);if(!v)return 0;if(G_VALUE_HOLDS_UINT64(v))return g_value_get_uint64(v);if(G_VALUE_HOLDS_UINT(v))return g_value_get_uint(v);if(G_VALUE_HOLDS_INT64(v))return g_value_get_int64(v);if(G_VALUE_HOLDS_INT(v))return g_value_get_int(v);if(G_VALUE_HOLDS_DOUBLE(v))return g_value_get_double(v);if(G_VALUE_HOLDS_ENUM(v))return g_value_get_enum(v);return 0;}
static const GstStructure *nested(const GstStructure *reply,const char *key){auto *v=key?gst_structure_get_value(reply,key):nullptr;return v && GST_VALUE_HOLDS_STRUCTURE(v)?gst_value_get_structure(v):nullptr;}
static bool video(const GstStructure *s,const GstStructure *reply){
 auto *codec=nested(reply,gst_structure_get_string(s,"codec-id"));const char *mime=codec?gst_structure_get_string(codec,"mime-type"):nullptr;
 if(mime)return g_ascii_strncasecmp(mime,"video/",6)==0;
 auto *kind=gst_structure_get_value(s,"kind");if(kind){if(G_VALUE_HOLDS_STRING(kind))return !strcmp(g_value_get_string(kind),"video");if(G_VALUE_HOLDS_ENUM(kind))return g_value_get_enum(kind)==GST_WEBRTC_KIND_VIDEO;}
 return false;
}
VideoStats videoStats(const GstStructure *reply,bool host,unsigned ssrc){VideoStats result;if(!reply)return result;
 for(int i=0;i<gst_structure_n_fields(reply);++i){auto *s=nested(reply,gst_structure_nth_field_name(reply,i));if(!s || (ssrc?number(s,"ssrc")!=ssrc:!video(s,reply)))continue;int type=int(number(s,"type"));
  if(type!=(host?GST_WEBRTC_STATS_OUTBOUND_RTP:GST_WEBRTC_STATS_INBOUND_RTP))continue;
  result.bytes+=number(s,host?"bytes-sent":"bytes-received");result.packets+=number(s,host?"packets-sent":"packets-received");
  if(!host){result.lost+=number(s,"packets-lost");continue;}
  auto *remote=nested(reply,gst_structure_get_string(s,"remote-id"));
  const char *localId=gst_structure_get_string(s,"id");
  auto matches=[&](const GstStructure *r){if(!r || int(number(r,"type"))!=GST_WEBRTC_STATS_REMOTE_INBOUND_RTP)return false;const char *id=gst_structure_get_string(r,"local-id");return number(r,"ssrc")==number(s,"ssrc") || (id && localId && !strcmp(id,localId));};
  if(!matches(remote)){
   remote=nullptr;for(int j=0;j<gst_structure_n_fields(reply);++j){auto *candidate=nested(reply,gst_structure_nth_field_name(reply,j));if(matches(candidate)){remote=candidate;break;}}
  }
  if(matches(remote) && gst_structure_has_field(remote,"round-trip-time")){
   result.lost+=number(remote,"packets-lost");result.rttMs=std::max(result.rttMs,number(remote,"round-trip-time")*1000);result.feedback=true;
  }
 }return result;
}
