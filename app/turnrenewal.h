#pragma once
#include <QtGlobal>

// Credential refresh scheduling is independent of the active media generation.
struct TurnRenewal {
    bool pending = false;
    qint64 deadline = 0, nextAttempt = 0;
    unsigned failures = 0;
    bool due(qint64 now, qint64 expiry) const {
        return !pending && expiry > 0 && now >= expiry - 300000 && now >= nextAttempt;
    }
    void requested(qint64 now) { pending = true; deadline = now + 15000; }
    bool timedOut(qint64 now) const { return pending && now >= deadline; }
    void failed(qint64 now) {
        static constexpr qint64 delays[] = {30000, 60000, 120000, 300000};
        nextAttempt = now + delays[qMin(failures, 3u)];
        ++failures; pending = false; deadline = 0;
    }
    void completed() { *this = {}; }
    void cancel() { *this = {}; }
};
