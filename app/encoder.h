#pragma once
#include <QString>
struct VideoEncoder {
    QString chain, format = "I420", codec = "VP8", name = "VP8 / CPU";
    QByteArray bitrateProperty = "target-bitrate";
    int bitrateMultiplier = 1000;
};
// Validate actual encoding and decoding, not merely plugin registration.
VideoEncoder selectVideoEncoder(int fps, int kbps);
