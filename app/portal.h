#pragma once
#include <QSize>
#include <QVariantMap>
struct PortalSelection {
    uint node = 0;
    quint64 serial = 0;
    QSize size;
};
PortalSelection readPortalSelection(const QVariantMap &result);
