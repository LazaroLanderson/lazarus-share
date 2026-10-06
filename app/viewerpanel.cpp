#include "viewerpanel.h"
#include "profile.h"
#include "theme.h"
#include <QScrollArea>
#include <QScrollBar>
#include <QComboBox>
#include <QSlider>
#include <QCheckBox>
#include <QPushButton>
#include <QBoxLayout>
#include <QShortcut>
#include <QCloseEvent>
#include <QPainter>
ViewerPanel::ViewerPanel(QWidget *parent) : QWidget(parent) {
    setObjectName("viewerPanel");
    auto *layout = new QVBoxLayout(this); layout->setContentsMargins(0,0,0,0);
    transmitter_ = new QLabel("Ninguém está compartilhando"); transmitter_->setObjectName("transmitter");
    state_ = new QLabel("Aguardando compartilhamento"); state_->setObjectName("viewerState");
    avatar_ = new QLabel; avatar_->setFixedSize(22,22);
    auto *identity = new QHBoxLayout; identity->addWidget(avatar_); identity->addWidget(transmitter_,1);
    layout->addLayout(identity); layout->addWidget(state_);
    scroll_ = new QScrollArea; scroll_->setAlignment(Qt::AlignCenter); scroll_->setWidgetResizable(true);
    scroll_->setMinimumSize(400,260); scroll_->viewport()->installEventFilter(this);
    scroll_->setStyleSheet("QScrollArea { background-color: #F0EEE3; border: 1px solid #EAE8DE; border-radius: 12px; }");
    frozen_ = new QLabel(scroll_->viewport()); frozen_->setObjectName("frozenWarning"); frozen_->setWordWrap(true);
    frozen_->setAlignment(Qt::AlignCenter); frozen_->setStyleSheet("background: rgba(255, 235, 235, 230); color: #BA1A1A; border: 1.5px solid #BA1A1A; border-radius: 8px; padding: 12px; font-weight: 600;"); frozen_->hide();
    video_ = new VideoView; video_->setObjectName("video"); video_->setAlignment(Qt::AlignCenter);
    video_->setStyleSheet("background: #FFFFFF; color: #1B1C16; border: 1px solid #EAE8DE; border-radius: 12px; font-size: 14px; font-weight: 600;"); video_->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Ignored);
    video_->clearFrame("Aguardando compartilhamento"); scroll_->setWidget(video_); layout->addWidget(scroll_,1);
    auto *controls = new QHBoxLayout;
    size_ = new QComboBox; size_->setObjectName("viewerSize"); size_->addItems({"Ajustar à janela", "Tamanho real (100%)"});
    auto *fullscreen = new QPushButton("Tela cheia (F11)"); fullscreen->setObjectName("fullscreen");
    controls->addWidget(size_); controls->addWidget(fullscreen); layout->addLayout(controls);
    auto *audio = new QHBoxLayout; audio->addWidget(new QLabel("Volume recebido"));
    volume_ = new QSlider(Qt::Horizontal); volume_->setRange(0,100); volume_->setValue(100); volume_->setObjectName("receivedVolume"); volume_->setAccessibleName("Volume recebido");
    mute_ = new QCheckBox("Silenciar"); mute_->setObjectName("receivedMute");
    audio->addWidget(volume_,1); audio->addWidget(mute_); layout->addLayout(audio);
    connect(size_,&QComboBox::currentIndexChanged,this,[this]{updateSize();});
    connect(fullscreen,&QPushButton::clicked,this,&ViewerPanel::toggleFullscreen);
    connect(volume_,&QSlider::valueChanged,this,&ViewerPanel::playbackChanged);
    connect(mute_,&QCheckBox::toggled,this,&ViewerPanel::playbackChanged);
    auto *f11 = new QShortcut(QKeySequence(Qt::Key_F11),this); connect(f11,&QShortcut::activated,this,&ViewerPanel::toggleFullscreen);
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape),this); connect(escape,&QShortcut::activated,this,[this]{if(fullscreen_)toggleFullscreen();});
}
double ViewerPanel::volume() const { return volume_->value()/100.0; }
bool ViewerPanel::muted() const { return mute_->isChecked(); }
void ViewerPanel::setFrame(QImage image) {
    const bool changed=frameSize_!=image.size(); frameSize_=image.size();
    video_->setFrame(std::move(image)); if(changed)updateSize(); frozen_->hide(); state_->setText(baseState_);
}
void ViewerPanel::clearFrame(const QString &message) { lastFrame_=-1; frameSize_={}; video_->clearFrame(message); frozen_->hide(); updateSize(); }
void ViewerPanel::presentation(const QString &state,const QString &name,int avatar) {
    baseState_=state;
    const auto label=name.isEmpty()?QString("Ninguém está compartilhando"):name;
    if(state_->text()==state && transmitter_->text()==label && transmitter_->property("avatar").toInt()==avatar)return;
    state_->setTextFormat(Qt::PlainText); transmitter_->setTextFormat(Qt::PlainText);
    state_->setText(state); transmitter_->setText(label);
    avatar_->setPixmap(Theme::avatarPixmap(avatar, 20)); transmitter_->setProperty("avatar",avatar);

}
void ViewerPanel::updateFreeze(qint64 now) {
    if(lastFrame_>=0 && video_->hasFrame() && now-lastFrame_>=5000) {
        frozen_->setText(QString("Imagem congelada — sem novos quadros há %1 segundos").arg((now-lastFrame_)/1000));
        frozen_->setGeometry(0,0,scroll_->viewport()->width(),qMin(80,scroll_->viewport()->height())); frozen_->show(); frozen_->raise();
        if(state_->text()=="Ao vivo")state_->setText("Imagem congelada");
    }
}
void ViewerPanel::updateSize() {
    bool native=size_->currentIndex()==1 && frameSize_.isValid();
    scroll_->setWidgetResizable(!native);
    video_->setMinimumSize(native?frameSize_:QSize(0,0));
    if(native)video_->resize(frameSize_);
}
bool ViewerPanel::eventFilter(QObject *o,QEvent *e) {
    if(o==scroll_->viewport() && e->type()==QEvent::Resize) {
        updateSize(); if(frozen_)frozen_->setGeometry(0,0,scroll_->viewport()->width(),qMin(80,scroll_->viewport()->height()));
    }
    return QWidget::eventFilter(o,e);
}
void ViewerPanel::toggleFullscreen() {
    if(!fullscreen_) {
        originalParent_=parentWidget();
        if(!originalParent_)return;
        originalLayout_=qobject_cast<QBoxLayout *>(originalParent_->layout());
        if(!originalLayout_)return;
        originalIndex_=originalLayout_->indexOf(this); originalLayout_->removeWidget(this);
        setParent(nullptr,Qt::Window); fullscreen_=true; showFullScreen();
    } else {
        hide(); setParent(originalParent_); originalLayout_->insertWidget(originalIndex_,this,2); fullscreen_=false; show();
    }
    setFocus();
}
void ViewerPanel::closeEvent(QCloseEvent *e) { if(fullscreen_){e->ignore();toggleFullscreen();}else QWidget::closeEvent(e); }
