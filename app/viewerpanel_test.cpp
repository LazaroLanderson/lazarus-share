#include "viewerpanel.h"
#include <QApplication>
#include <QBoxLayout>
#include <QLabel>
#include <QComboBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QCheckBox>
#include <iostream>
#include <stdexcept>
static void check(bool ok,const char *text){if(!ok)throw std::runtime_error(text);}
int main(int argc,char **argv){
    QApplication app(argc,argv);
    try {
        QWidget container; auto *layout=new QHBoxLayout(&container); auto *viewer=new ViewerPanel;
        layout->addWidget(viewer); container.resize(700,520); container.show(); app.processEvents();
        QImage image(1280,720,QImage::Format_RGB32); image.fill(QColor(12,34,56));
        check(viewer->volume()==1 && !viewer->muted(),"Playback defaults");
        viewer->findChild<QSlider *>("receivedVolume")->setValue(35);
        viewer->findChild<QCheckBox *>("receivedMute")->setChecked(true);
        viewer->presentation("Ao vivo","Transmitindo: <Alice>",2);
        check(viewer->findChild<QLabel *>("transmitter")->text()=="Transmitindo: <Alice>","Literal participant name");
        viewer->setFrame(image); viewer->frameAt(100); viewer->updateFreeze(5099);
        auto *warning=viewer->findChild<QLabel *>("frozenWarning");
        check(warning->isHidden(),"Early freeze warning"); viewer->updateFreeze(5100);
        check(!warning->isHidden() && warning->text().contains("5 segundos"),"Missing freeze warning");
        check(viewer->video()->hasFrame(),"Freeze removed last image");
        // An identical newly received image resets the watchdog.
        viewer->setFrame(image); viewer->frameAt(5200); viewer->updateFreeze(5300);
        check(warning->isHidden(),"Static image considered frozen");
        viewer->findChild<QComboBox *>("viewerSize")->setCurrentIndex(1); app.processEvents();
        check(viewer->video()->size()==image.size(),"Native size scaled video");
        check(!viewer->findChild<QScrollArea *>()->widgetResizable(),"Native mode lacks scrolling");
        auto *scroll=viewer->findChild<QScrollArea *>();
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        viewer->updateFreeze(10300); app.processEvents();
        check(!warning->isHidden() && warning->parentWidget()==scroll->viewport() && warning->geometry().top()==0,"Scrolled native image hid warning");
        viewer->setFrame(image); viewer->frameAt(10400);
        viewer->toggleFullscreen(); app.processEvents();
        auto rendered=viewer->video()->grab().toImage();
        check(rendered.pixelColor(rendered.width()/2,rendered.height()/2)==QColor(12,34,56),"Fullscreen lost texture contents");
        check(viewer->fullscreen() && viewer->isWindow(),"Fullscreen did not detach viewer");
        check(viewer->findChild<QLabel *>("viewerState")->text()=="Ao vivo", "Fullscreen lost state");
        viewer->close(); app.processEvents(); check(!viewer->fullscreen() && viewer->parentWidget()==&container,"Fullscreen close did not restore viewer");
        rendered=viewer->video()->grab().toImage();
        check(rendered.pixelColor(rendered.width()/2,rendered.height()/2)==QColor(12,34,56),"Restoration lost texture contents");
        viewer->toggleFullscreen(); app.processEvents(); viewer->toggleFullscreen(); app.processEvents();
        viewer->findChild<QComboBox *>("viewerSize")->setCurrentIndex(0); app.processEvents();
        check(viewer->findChild<QScrollArea *>()->widgetResizable(),"Fit mode did not restore resizing");
        viewer->clearFrame("Aguardando compartilhamento"); viewer->updateFreeze(99999);
        check(!viewer->video()->hasFrame() && warning->isHidden(),"Stopped stream retained frame/warning");
        check(viewer->volume()==.35 && viewer->muted(),"Session playback settings lost");
        if(app.arguments().contains("--require-gl"))check(viewer->video()->backend()=="OpenGL","OpenGL unavailable");
        std::cout<<"Viewer sizing, fullscreen restoration, playback settings and freeze watchdog passed\n";
    }catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
