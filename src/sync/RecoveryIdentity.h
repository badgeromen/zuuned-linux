#pragma once
#include <QVariantMap>
#include <QVariantList>
#include <cmath>

// Deletion requires evidence for one exact newly-created object. A title or
// even a unique title match is never authority to delete existing media.
namespace RecoveryIdentity {
inline quint32 objectId(const QVariant &value) {
    bool ok = false;
    const double id = value.toDouble(&ok);
    return ok && std::isfinite(id) && id > 0 && id <= 4294967295.0 && std::floor(id) == id
        ? quint32(id) : 0;
}
inline QString refusal(const QVariantMap &record, const QString &serial) {
    const QString manual = QStringLiteral("Recovery cannot identify one safe object. Review the device library manually; nothing was deleted.");
    if (record.value("version").toInt() != 1
        || (record.value("type") != "music" && record.value("type") != "photo")
        || record.value("token").toString().isEmpty()) return manual;
    if (serial.isEmpty() || record.value("serial").toString().isEmpty()
        || record.value("serial").toString() != serial)
        return QStringLiteral("Connect the original Zune with a readable serial number. Nothing was deleted.");
    const quint32 id = objectId(record.value("itemId"));
    if (!id || record.value("baselineIds").metaType().id() != QMetaType::QVariantList) return manual;
    for (const auto &previous : record.value("baselineIds").toList())
        if (!objectId(previous) || objectId(previous) == id) return manual;
    return {};
}
inline bool matches(const QVariantMap &record, const QVariantMap &actual) {
    if (objectId(record.value("itemId")) != objectId(actual.value("itemId"))) return false;
    const auto expected = record.value("identity").toMap();
    auto equal = [&](const char *key) {
        const auto want = expected.value(key).toString();
        return !want.isEmpty() && want.compare(actual.value(key).toString(), Qt::CaseInsensitive) == 0;
    };
    if (record.value("type") == "photo") return equal("filename");
    if (record.value("type") != "music" || !equal("title") || !equal("artist") || !equal("album")) return false;
    for (const auto *key : {"discNumber", "trackNumber"}) {
        const int want = expected.value(key).toInt(), found = actual.value(key).toInt();
        if (want > 0 && found > 0 && want != found) return false;
    }
    return true;
}
}
