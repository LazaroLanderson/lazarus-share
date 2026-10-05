#include "profile.h"
#include "diagnostic.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <iostream>
int main(int argc,char **argv) {
    QCoreApplication app(argc,argv); QTemporaryDir temp;
    qputenv("XDG_DATA_HOME",temp.path().toUtf8()); qputenv("LOCALAPPDATA",temp.path().toUtf8());
    QCoreApplication::setOrganizationName("Tests"); QCoreApplication::setApplicationName("Profile");
    QSettings::setDefaultFormat(QSettings::IniFormat); QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,temp.path());
    if(normalizedNickname("abcdefghijk").size() || normalizedNickname("x\ny").size() || normalizedNickname("   ").size())return 1;
    if(normalizedNickname(QString::fromUtf8("Jose\xcc\x81"))!=QString::fromUtf8("José"))return 2;
    Profile p{"José",9,true};p.save();auto copy=Profile::load();if(!copy.valid() || copy.nickname!=p.nickname || !copy.relay || copy.avatar!=9)return 3;
    DiagnosticLog logs(temp.path()+"/logs");logs.append("test",{{"nickname","José"},{"component","video"},{"factory","openh264dec"},{"decoder_mode","software"},{"error_domain","gst-stream-error-quark"},{"error_number",7},{"attempt",1},{"invite","https://share.app.lazaruslabs.com.br/join#SECRET"},{"link","lazarus-share://join#SECRET"},{"token","SECRET"},{"sdp","SECRET"},{"ip","SECRET"},{"message","SECRET"},{"debug","SECRET"},{"metrics",QJsonObject{{"kbps",0},{"password","SECRET"}}}});
    auto text=QJsonDocument(logs.events()).toJson();if(text.contains("SECRET") || !text.contains("kbps"))return 4;
    auto event=logs.events().first().toObject();if(event["version"]!=LAZARUS_VERSION || event["component"]!="video" || event["factory"]!="openh264dec" || event["decoder_mode"]!="software" || event["error_number"].toInt()!=7 || event["attempt"].toInt()!=1)return 9;
    logs.clear();if(!logs.events().isEmpty())return 5;
    QFile expired(temp.path()+"/logs/events-0.jsonl"); if(!expired.open(QIODevice::WriteOnly))return 6;
    expired.write(QJsonDocument(QJsonObject{{"utc",QDateTime::currentDateTimeUtc().addDays(-8).toString(Qt::ISODateWithMs)},{"event","expired"}}).toJson(QJsonDocument::Compact)+"\n"); expired.close();
    DiagnosticLog reloaded(temp.path()+"/logs"); if(!reloaded.events().isEmpty())return 7;
    reloaded.append("after_close",{{"nickname","José"}}); DiagnosticLog reopened(temp.path()+"/logs"); if(reopened.events().size()!=1)return 8;
    std::cout<<"Unicode nickname, local profile and redacted diagnostic persistence passed\n";return 0;
}
