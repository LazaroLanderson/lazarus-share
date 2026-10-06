#pragma once
#include <QWidget>
#include <QString>

class QLabel;

class Toast : public QWidget {
    Q_OBJECT
public:
    explicit Toast(QWidget *parent = nullptr);
    static void showComingSoon(QWidget *parent, const QString &feature = QString());

    void showMessage(const QString &title, const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
};
