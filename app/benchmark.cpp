// Local-only synthetic fixture. Never captures a screen or exports SDP.
#include "peer.h"
#ifndef LAZARUS_BASELINE
#include "frameprep.h"
#include "videoview.h"
#endif
#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QTimer>
#include <QElapsedTimer>
#include <QUuid>
#include <gst/video/video.h>
#include <vector>
#include <map>
#include <iostream>
static qint64 mono(){QElapsedTimer t;t.start();return t.msecsSinceReference();}
static void print(QJsonObject object){std::cout<<QJsonDocument(object).toJson(QJsonDocument::Compact).constData()<<std::endl;}
static void send(QLocalSocket *socket,QJsonObject object){socket->write(QJsonDocument(object).toJson(QJsonDocument::Compact)+'\n');}
static void read(QLocalSocket *socket,std::function<void(QJsonObject)> handler){while(socket->canReadLine()){auto line=socket->readLine();handler(QJsonDocument::fromJson(line).object());}}
static quint16 checksum(quint32 id){return quint16(id)^quint16(id>>16)^0xa53c;}
static void stamp(GstSample *sample,quint32 id){GstVideoInfo info;GstVideoFrame frame;
 if(gst_video_info_from_caps(&info,gst_sample_get_caps(sample)) && gst_video_frame_map(&frame,&info,gst_sample_get_buffer(sample),GST_MAP_WRITE)){
  quint64 code=(quint64(id)<<32)|(quint64(checksum(id))<<16)|0x5ac3;
  auto *y=static_cast<uchar *>(GST_VIDEO_FRAME_PLANE_DATA(&frame,0));int stride=GST_VIDEO_FRAME_PLANE_STRIDE(&frame,0);
  for(int bit=0;bit<64;++bit)for(int row=0;row<8;++row)for(int col=0;col<8;++col)y[(bit/8*8+row)*stride+bit%8*8+col]=(code&(quint64(1)<<bit))?235:16;
  gst_video_frame_unmap(&frame);
 }
}
static quint32 identify(const QImage &image){if(image.width()<64 || image.height()<64)return 0;quint64 code=0;
 for(int bit=0;bit<64;++bit){int sum=0;for(int y=2;y<6;++y)for(int x=2;x<6;++x)sum+=qGray(image.pixel(bit%8*8+x,bit/8*8+y));if(sum>16*128)code|=quint64(1)<<bit;}
 quint32 id=code>>32;return quint16(code)==0x5ac3 && quint16(code>>16)==checksum(id)?id:0;
}
int main(int argc,char **argv){gst_init(&argc,&argv);QApplication app(argc,argv);auto args=app.arguments();
 auto value=[&](QString key,int fallback){for(auto a:args)if(a.startsWith(key+'='))return a.section('=',1).toInt();return fallback;};
 int count=value("--viewers",1),seconds=value("--seconds",75),warmup=value("--warmup",15),measurement=value("--measurement",60);Quality q{value("--width",1920),value("--height",1080),value("--fps",60),value("--kbps",8000)};
 QString channel;for(auto a:args)if(a.startsWith("--viewer="))channel=a.section('=',1);
 if(!channel.isEmpty()){
  Peer peer(false);QLocalSocket socket;QByteArray pending;
#ifdef LAZARUS_BASELINE
  QLabel display;display.setMinimumSize(640,360);
#else
  VideoView display;display.setMinimumSize(640,360);
#endif
#ifndef LAZARUS_BASELINE
  QObject::connect(&peer,&Peer::mediaFailure,&app,[&](QString){QTimer::singleShot(0,&app,[&]{app.exit(3);});});
#endif
  display.show();QObject::connect(&peer,&Peer::outgoing,&app,[&](QJsonObject message){send(&socket,message);});
  QObject::connect(&socket,&QLocalSocket::readyRead,&app,[&]{read(&socket,[&](QJsonObject message){peer.receive(message);});});
  socket.connectToServer(channel);if(!socket.waitForConnected(5000) || !peer.start(q,{}))return 2;
  QObject::connect(&peer,&Peer::metrics,&app,[&](QJsonObject m){send(&socket,{{"fixture","metrics"},{"metrics",m}});});
  QTimer frames;frames.setInterval(8);QObject::connect(&frames,&QTimer::timeout,&app,[&]{auto image=peer.takeFrame();if(image.isNull())return;quint32 id=identify(image);
#ifdef LAZARUS_BASELINE
   display.setPixmap(QPixmap::fromImage(image).scaled(display.size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
#else
   display.setFrame(image);
#endif
   // Flush raster/GL presentation before acknowledging this synthetic frame.
   display.repaint();if(id)send(&socket,{{"fixture","presented"},{"frame",double(id)},{"presented_ms",double(mono())}});
  });frames.start();QTimer::singleShot((seconds+15)*1000,Qt::PreciseTimer,&app,&QCoreApplication::quit);return app.exec();
 }
 QLocalServer server;server.setSocketOptions(QLocalServer::UserAccessOption);channel="lazarus-bench-"+QUuid::createUuid().toString(QUuid::Id128);if(!server.listen(channel))return 2;
 std::vector<std::unique_ptr<Peer>> peers;std::vector<std::unique_ptr<QProcess>> children;std::vector<QLocalSocket *> sockets;std::map<quint32,qint64> produced;
 std::vector<QJsonObject> hostMetrics(count),viewerMetrics(count);std::vector<std::vector<double>> latency(count);int accepted=0;bool failed=false;
 QObject::connect(&server,&QLocalServer::newConnection,&app,[&]{while(server.hasPendingConnections()){
  auto *socket=server.nextPendingConnection();int index=accepted++;if(index>=count){socket->close();continue;}sockets.push_back(socket);
  auto peer=std::make_unique<Peer>(true);auto *raw=peer.get();
  QObject::connect(raw,&Peer::outgoing,&app,[socket](QJsonObject message){send(socket,message);});
  QObject::connect(raw,&Peer::metrics,&app,[&,index](QJsonObject m){hostMetrics[index]=m;});
#ifndef LAZARUS_BASELINE
  QObject::connect(raw,&Peer::mediaFailure,&app,[&](QString){failed=true;QTimer::singleShot(0,&app,&QCoreApplication::quit);});
#endif
  QObject::connect(raw,&Peer::error,&app,[&](QString){failed=true;});
  QObject::connect(socket,&QLocalSocket::readyRead,&app,[&,socket,index,raw]{read(socket,[&,index,raw](QJsonObject message){
   QString fixture=message["fixture"].toString();if(fixture=="metrics")viewerMetrics[index]=message["metrics"].toObject();
   else if(fixture=="presented"){auto found=produced.find(quint32(message["frame"].toDouble()));if(found!=produced.end()){double elapsed=message["presented_ms"].toDouble()-found->second;if(elapsed>=0 && elapsed<10000)latency[index].push_back(elapsed);}}
   else raw->receive(message);
  });});
  if(!raw->start(q,{}))failed=true;peers.push_back(std::move(peer));
 }});
 QJsonArray pids;for(int i=0;i<count;++i){auto child=std::make_unique<QProcess>();child->setProcessChannelMode(QProcess::ForwardedErrorChannel);child->start(QCoreApplication::applicationFilePath(),{"--viewer="+channel,"--width="+QString::number(q.width),"--height="+QString::number(q.height),"--fps="+QString::number(q.fps),"--kbps="+QString::number(q.kbps),"--seconds="+QString::number(seconds)});if(!child->waitForStarted(5000))return 2;pids.append(double(child->processId()));children.push_back(std::move(child));}
 print({{"event","processes"},{"host_pid",double(QCoreApplication::applicationPid())},{"viewers",pids}});
 auto *source=gst_parse_launch(QString("videotestsrc is-live=true pattern=smpte ! video/x-raw,format=I420,width=%1,height=%2,framerate=%3/1 ! appsink name=source sync=false max-buffers=1 drop=true").arg(q.width).arg(q.height).arg(q.fps).toUtf8().constData(),nullptr);if(!source)return 2;
 auto *sink=gst_bin_get_by_name(GST_BIN(source),"source");gst_element_set_state(source,GST_STATE_PLAYING);quint32 serial=0;unsigned captured=0;
#ifndef LAZARUS_BASELINE
 FramePreparer prepare;
#endif
 QTimer pump;pump.setInterval(2);QObject::connect(&pump,&QTimer::timeout,&app,[&]{if(auto *original=gst_app_sink_try_pull_sample(GST_APP_SINK(sink),0)){
  auto *copy=gst_buffer_copy_deep(gst_sample_get_buffer(original));auto *sample=gst_sample_new(copy,gst_sample_get_caps(original),nullptr,nullptr);gst_buffer_unref(copy);gst_sample_unref(original);
  ++serial;++captured;produced[serial]=mono();while(produced.size()>4096)produced.erase(produced.begin());stamp(sample,serial);
#ifndef LAZARUS_BASELINE
  bool needs=false;for(auto &p:peers)if(p->inputFormat()=="NV12")needs=true;auto *nv12=needs?prepare.nv12(sample):nullptr;
#endif
  for(auto &p:peers){
#ifndef LAZARUS_BASELINE
    p->video(p->inputFormat()=="NV12" && nv12?nv12:sample);
#else
    p->video(sample);
#endif
  }
#ifndef LAZARUS_BASELINE
  if(nv12)gst_sample_unref(nv12);
#endif
  gst_sample_unref(sample);
 }});pump.start();QElapsedTimer elapsed;elapsed.start();QTimer report;report.setInterval(2000);report.setTimerType(Qt::PreciseTimer);
 QObject::connect(&report,&QTimer::timeout,&app,[&]{QJsonArray connections;for(int i=0;i<count;++i){QJsonArray delays;for(double d:latency[i])delays.append(d);latency[i].clear();connections.append(QJsonObject{{"encoded",hostMetrics[i]},{"decoded",viewerMetrics[i]},{"presentation_ms",delays}});}print({{"event","sample"},{"elapsed_ms",double(elapsed.elapsed())},{"captured",int(captured)},{"connections",connections}});captured=0;});
 QTimer::singleShot(warmup*1000,Qt::PreciseTimer,&app,[&]{captured=0;for(auto &values:latency)values.clear();print({{"event","measurement_started"},{"elapsed_ms",double(elapsed.elapsed())}});report.start();});
 QTimer::singleShot((warmup+measurement)*1000+500,Qt::PreciseTimer,&app,[&]{report.stop();});
 QTimer::singleShot(seconds*1000,Qt::PreciseTimer,&app,&QCoreApplication::quit);app.exec();gst_element_set_state(source,GST_STATE_NULL);gst_object_unref(sink);gst_object_unref(source);peers.clear();for(auto &child:children){child->terminate();if(!child->waitForFinished(3000)){child->kill();child->waitForFinished();}}
 return failed || accepted!=count?1:0;
}
