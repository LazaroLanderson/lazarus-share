#include "videoview.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iostream>
#include <vector>

// Supplemental renderer-only measurement; no capture, signaling or media.
// The endpoint is completed drawing into Qt's surface, not monitor scanout.
int main(int argc,char **argv){
    QString renderer="software";
    for(int i=1;i<argc;++i)if(QString::fromLocal8Bit(argv[i]).startsWith("--renderer="))renderer=QString::fromLocal8Bit(argv[i]).mid(11);
    if(renderer!="legacy" && renderer!="software" && renderer!="opengl")return 2;
    qputenv("LAZARUS_RENDERER",renderer=="opengl"?"opengl":"software");
    QApplication app(argc,argv);
    QLabel legacy;VideoView modern;QLabel *view=renderer=="legacy"?&legacy:&modern;
    view->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);view->setAlignment(Qt::AlignCenter);
    view->resize(640,360);view->setWindowTitle("Lazarus Share — teste sintético de exibição");view->show();
    QImage source(1920,1080,QImage::Format_RGB32);source.fill(QColor(32,140,210));
    const int warmup=5,measurement=10;
    bool measuring=false;unsigned frames=0,serial=0;std::vector<double> times;
    QElapsedTimer elapsed;std::clock_t cpuStart=0;QTimer timer;timer.setTimerType(Qt::PreciseTimer);timer.setInterval(17);
    QObject::connect(&timer,&QTimer::timeout,&app,[&]{
        // A unique image prevents cached pixmaps from masquerading as new frames.
        QImage image=source.copy();image.setPixelColor(serial++%1920,0,Qt::white);
        QElapsedTimer processing;processing.start();
        if(renderer=="legacy")legacy.setPixmap(QPixmap::fromImage(image).scaled(view->size(),Qt::KeepAspectRatio,Qt::SmoothTransformation));
        else modern.setFrame(std::move(image));
        view->repaint();
        if(renderer=="opengl")if(auto *gl=modern.findChild<QOpenGLWidget*>();gl && gl->isValid()){
            gl->repaint();gl->makeCurrent();gl->context()->functions()->glFinish();gl->doneCurrent();
        }
        if(measuring){++frames;times.push_back(processing.nsecsElapsed()/1e6);}
    });
    QTimer::singleShot(warmup*1000,Qt::PreciseTimer,&app,[&]{
        if(renderer=="opengl" && modern.backend()!="OpenGL"){std::cerr<<"OpenGL unavailable\n";app.exit(3);return;}
        frames=0;times.clear();cpuStart=std::clock();elapsed.start();measuring=true;
    });
    QTimer::singleShot((warmup+measurement)*1000,Qt::PreciseTimer,&app,[&]{
        measuring=false;timer.stop();double seconds=elapsed.nsecsElapsed()/1e9;
        std::sort(times.begin(),times.end());auto percentile=[&](double fraction){return times.empty()?0.0:times[std::min(times.size()-1,size_t(std::ceil(fraction*times.size()))-1)];};
        QJsonObject result{{"renderer",renderer},{"effective_renderer",renderer=="legacy"?"legacy":modern.backend()},{"frames",int(frames)},{"fps",frames/seconds},{"processing_p50_ms",percentile(.5)},{"processing_p95_ms",percentile(.95)},{"measurement_elapsed_s",seconds},{"warmup_s",warmup},{"measurement_s",measurement},{"width",1920},{"height",1080},{"display_width",640},{"display_height",360},{"platform",QGuiApplication::platformName()}};
#ifdef Q_OS_WIN
        result["cpu_pct"]=QJsonValue(); // std::clock is wall time on Windows.
#else
        result["cpu_pct"]=100.0*(std::clock()-cpuStart)/CLOCKS_PER_SEC/seconds;
#endif
        std::cout<<QJsonDocument(result).toJson(QJsonDocument::Compact).constData()<<std::endl;
        modern.clearFrame("Teste concluído.");app.quit();
    });timer.start();return app.exec();
}
