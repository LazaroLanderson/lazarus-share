#pragma once
#include <QJsonObject>
#include <QString>
#include <QColor>
struct Profile {
    QString nickname;
    int avatar = 0;
    bool relay = false;
    bool valid() const;
    QJsonObject json() const;
    static Profile load();
    void save() const;
};
QString normalizedNickname(QString value);
QColor avatarColor(int index);
bool editProfile(Profile &profile, class QWidget *parent);
