#include "videoview.h"
#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLBuffer>
#include <QPainter>
#include <QGuiApplication>
#include <QTimer>
class TextureView : public QOpenGLWidget,protected QOpenGLFunctions {
public:
    explicit TextureView(VideoView *owner):QOpenGLWidget(owner),owner_(owner){setAttribute(Qt::WA_TransparentForMouseEvents);}
    ~TextureView(){if(context() && isValid()){makeCurrent();if(texture_)glDeleteTextures(1,&texture_);buffer_.destroy();program_.reset();doneCurrent();}}
    void frame(QImage image){image_=std::move(image);dirty_=true;update();}
protected:
    void initializeGL() override {
        initializeOpenGLFunctions();program_=std::make_unique<QOpenGLShaderProgram>();
        const char *vertex="attribute vec2 position; attribute vec2 uv; varying vec2 coord; void main(){coord=uv;gl_Position=vec4(position,0.,1.);}";
        QByteArray fragment=(context()->isOpenGLES()?"precision mediump float;":"")+QByteArray("varying vec2 coord; uniform sampler2D frame; void main(){gl_FragColor=texture2D(frame,coord);}");
        ready_=program_->addShaderFromSourceCode(QOpenGLShader::Vertex,vertex) && program_->addShaderFromSourceCode(QOpenGLShader::Fragment,fragment) && program_->link() && buffer_.create();
        if(!ready_){owner_->gpuFailed_=true;QTimer::singleShot(0,owner_,[this]{hide();owner_->QLabel::setPixmap(QPixmap::fromImage(owner_->image_));owner_->update();});return;}
        glGenTextures(1,&texture_);glBindTexture(GL_TEXTURE_2D,texture_);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    }
    void paintGL() override {
        glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);if(!ready_ || image_.isNull())return;
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,texture_);
        if(dirty_){
            glPixelStorei(GL_UNPACK_ALIGNMENT,4);
            if(image_.size()!=textureSize_){glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,image_.width(),image_.height(),0,GL_RGB,GL_UNSIGNED_BYTE,image_.constBits());textureSize_=image_.size();}
            else glTexSubImage2D(GL_TEXTURE_2D,0,0,0,image_.width(),image_.height(),GL_RGB,GL_UNSIGNED_BYTE,image_.constBits());dirty_=false;
        }
        QSize fit=image_.size();fit.scale(size(),Qt::KeepAspectRatio);float x=float(fit.width())/qMax(1,width()),y=float(fit.height())/qMax(1,height());
        const float vertices[]={-x,-y,0,1,x,-y,1,1,-x,y,0,0,x,y,1,0};
        program_->bind();buffer_.bind();buffer_.allocate(vertices,sizeof(vertices));
        int position=program_->attributeLocation("position"),uv=program_->attributeLocation("uv");program_->enableAttributeArray(position);program_->enableAttributeArray(uv);program_->setAttributeBuffer(position,GL_FLOAT,0,2,4*sizeof(float));program_->setAttributeBuffer(uv,GL_FLOAT,2*sizeof(float),2,4*sizeof(float));program_->setUniformValue("frame",0);glDrawArrays(GL_TRIANGLE_STRIP,0,4);buffer_.release();program_->release();
    }
private:
    VideoView *owner_;QImage image_;QSize textureSize_;bool dirty_=false,ready_=false;GLuint texture_=0;
    QOpenGLBuffer buffer_;std::unique_ptr<QOpenGLShaderProgram> program_;
};
VideoView::VideoView(QWidget *parent):QLabel(parent){
    auto platform=QGuiApplication::platformName();
    if(platform!="offscreen" && platform!="minimal" && qEnvironmentVariable("LAZARUS_RENDERER")!="software"){
        QOpenGLContext probe;QSurfaceFormat format;format.setVersion(2,0);probe.setFormat(format);
        if(probe.create()){gpu_=new TextureView(this);gpu_->setFormat(format);gpu_->hide();}else gpuFailed_=true;
    }
}
void VideoView::setFrame(QImage image){
    if(image.isNull())return;image_=std::move(image);
    if(gpu_ && !gpuFailed_){QLabel::setPixmap({});gpu_->setGeometry(rect());gpu_->frame(image_);gpu_->show();}else {QLabel::setPixmap(QPixmap::fromImage(image_));update();}
}
void VideoView::clearFrame(QString message){image_={};QLabel::setPixmap({});setText(message);if(gpu_){gpu_->frame({});gpu_->hide();}update();}
QString VideoView::backend()const{return gpu_ && !gpuFailed_?"OpenGL":"Software";}
void VideoView::resizeEvent(QResizeEvent *e){QLabel::resizeEvent(e);if(gpu_)gpu_->setGeometry(rect());}
void VideoView::paintEvent(QPaintEvent *e){if(image_.isNull()){QLabel::paintEvent(e);return;}if(gpu_ && !gpuFailed_)return;QPainter p(this);p.fillRect(rect(),QColor("#151515"));QSize fit=image_.size();fit.scale(size(),Qt::KeepAspectRatio);p.setRenderHint(QPainter::SmoothPixmapTransform);p.drawPixmap(QRect(QPoint((width()-fit.width())/2,(height()-fit.height())/2),fit),pixmap());}
