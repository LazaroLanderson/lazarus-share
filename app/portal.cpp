#include "portal.h"
#include <QDBusArgument>
PortalSelection readPortalSelection(const QVariantMap &result) {
    if (result.value("streams").metaType() != QMetaType::fromType<QDBusArgument>()) return {};
    // QDBusArgument has separate read (const) and write (mutable) overloads.
    const auto streams = qvariant_cast<QDBusArgument>(result.value("streams"));
    if (streams.currentSignature() != "a(ua{sv})") return {};
    PortalSelection selected; QVariantMap properties;
    streams.beginArray();
    if (!streams.atEnd()) { streams.beginStructure(); streams >> selected.node >> properties; streams.endStructure(); }
    streams.endArray();
    selected.serial = properties.value("pipewire-serial").toULongLong();
    if (properties.value("size").metaType() == QMetaType::fromType<QDBusArgument>()) {
        const auto size = qvariant_cast<QDBusArgument>(properties.value("size")); int width = 0, height = 0;
        if (size.currentSignature() != "(ii)") return selected;
        size.beginStructure(); size >> width >> height; size.endStructure();
        if (width > 0 && height > 0) selected.size = QSize(width, height);
    }
    return selected;
}
