#pragma once
#include <QLabel>
class TextureView;
class VideoView : public QLabel {
public:
    explicit VideoView(QWidget *parent=nullptr);
    void setFrame(QImage image);
    void clearFrame(QString message);
    QString backend() const;
    bool hasFrame() const {return !image_.isNull();}
protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
private:
    friend class TextureView;
    QImage image_;
    TextureView *gpu_=nullptr;
    bool gpuFailed_=false,glCheckScheduled_=false;
};
