#include "diagnostic.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QDateTime>
#include <QSaveFile>
#include <QSet>
static QJsonObject sanitized(const QJsonObject &input) {
    // Only structured fields produced by the app; never raw messages/SDP/GStreamer errors.
    static const QSet<QString> keys = {"nickname", "own_nickname", "build", "avatar", "peer", "session", "generation", "attempt", "role", "sharing", "relay_local", "relay_remote", "route", "last_route", "stage", "ice_state", "gathering_state", "dtls_state", "local_description", "remote_description", "local_candidates", "remote_candidates", "candidate_counts", "local", "remote", "host", "srflx", "prflx", "relay", "udp", "tcp", "metrics", "encoder", "decoder", "encoder_kbps", "width", "height", "video_fps", "decoder_fps", "kbps", "loss_percent", "rtt_ms", "packets", "bytes", "error_code", "preset", "failed", "selected_pair", "capture_fps", "target_fps", "target_kbps", "transport", "bitrate_changed", "feedback_valid", "frames_discarded", "frames_prepare_discarded", "prepare_us", "selection_ms", "frames_pending_estimate", "render_backend"};
    QJsonObject out;
    for (auto it = input.begin(); it != input.end(); ++it) if (keys.contains(it.key())) {
        if (it.value().isObject()) out[it.key()] = sanitized(it.value().toObject());
        else if (!it.value().isArray()) out[it.key()] = it.value();
    }
    return out;
}
DiagnosticLog::DiagnosticLog(QString directory) : directory_(directory.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/diagnostics" : directory) { QDir().mkpath(directory_); prune(); }
QJsonArray DiagnosticLog::events() const {
    QJsonArray result; const auto cutoff = QDateTime::currentDateTimeUtc().addDays(-7);
    for (int i = 4; i >= 0; --i) {
        QFile f(directory_ + QString("/events-%1.jsonl").arg(i)); if (!f.open(QIODevice::ReadOnly)) continue;
        while (!f.atEnd()) { auto item = QJsonDocument::fromJson(f.readLine()).object();
            if (QDateTime::fromString(item["utc"].toString(), Qt::ISODateWithMs) >= cutoff) result.append(item);
        }
    } return result;
}
void DiagnosticLog::prune() {
    const auto cutoff = QDateTime::currentDateTimeUtc().addDays(-7);
    for (int i = 0; i < 5; ++i) {
        QString path = directory_ + QString("/events-%1.jsonl").arg(i); QFile file(path); if (!file.open(QIODevice::ReadOnly)) continue;
        QByteArray kept; bool changed = false;
        while (!file.atEnd()) { auto line = file.readLine(); auto value = QJsonDocument::fromJson(line).object();
            if (QDateTime::fromString(value["utc"].toString(), Qt::ISODateWithMs) >= cutoff) kept += line; else changed = true;
        }
        file.close(); if (changed) { QSaveFile output(path); if (output.open(QIODevice::WriteOnly)) { output.write(kept); output.commit(); } }
    }
}
void DiagnosticLog::append(QString event, QJsonObject fields) {
    auto now = QDateTime::currentMSecsSinceEpoch(); if (now-lastPrune_ >= 60000) { prune(); lastPrune_=now; } const QString base = directory_ + "/events-0.jsonl";
    auto value = sanitized(fields); value["event"] = event; value["utc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs); value["version"] = "0.2.1";
#ifdef LAZARUS_BUILD_ID
    value["build"] = LAZARUS_BUILD_ID;
#endif
    const auto line = QJsonDocument(value).toJson(QJsonDocument::Compact) + '\n';
    if (QFileInfo(base).size() + line.size() > 2 * 1024 * 1024) {
        QFile::remove(directory_ + "/events-4.jsonl");
        for (int i = 3; i >= 0; --i) QFile::rename(directory_ + QString("/events-%1.jsonl").arg(i), directory_ + QString("/events-%1.jsonl").arg(i+1));
    }
    QFile file(base); if (file.open(QIODevice::WriteOnly | QIODevice::Append)) { file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner); file.write(line); }
}
void DiagnosticLog::clear() { for (int i = 0; i < 5; ++i) QFile::remove(directory_ + QString("/events-%1.jsonl").arg(i)); }
