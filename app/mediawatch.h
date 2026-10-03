#pragma once
#include <QtGlobal>
inline bool encoderStalled(qint64 now,qint64 lastInput,qint64 lastOutput,qint64 connectedAt,bool connected){
    return connected && connectedAt>0 && now-connectedAt>=5000 && lastInput>0 && now-lastInput<1000 && lastOutput>0 && now-lastOutput>=5000;
}
