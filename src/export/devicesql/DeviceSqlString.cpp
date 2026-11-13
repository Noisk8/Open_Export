#include "export/devicesql/DeviceSqlString.h"

namespace opendeck {
namespace device {

namespace {

// Maximum payload of the short ASCII form (the length byte holds the total
// field length shifted left by one, so the field may not exceed 127 bytes).
constexpr int kMaxShortAscii = 126;

constexpr char kLongAscii = 0x40;
constexpr char kLongUtf16 = 0x90;

void appendU16LE(QByteArray& out, int value) {
    out.append(char(value & 0xff));
    out.append(char((value >> 8) & 0xff));
}

bool isPlainAscii(const QString& value) {
    for (const QChar c : value) {
        if (c.unicode() > 0x7f) {
            return false;
        }
    }
    return true;
}

// Encodes a QString as little endian UTF-16. QString stores UTF-16 code
// units, so a surrogate pair simply becomes four bytes.
QByteArray toUtf16Le(const QString& value) {
    QByteArray out;
    out.reserve(value.size() * 2);
    for (const QChar c : value) {
        const quint16 unit = c.unicode();
        out.append(char(unit & 0xff));
        out.append(char((unit >> 8) & 0xff));
    }
    return out;
}

// Builds one of the two long forms.
QByteArray encodeLong(char kind, const QByteArray& payload) {
    QByteArray out;
    out.append(kind);
    appendU16LE(out, payload.size() + 4);
    out.append(char(0)); // pad
    out.append(payload);
    return out;
}

} // namespace

DeviceSqlString DeviceSqlString::text(const QString& value) {
    DeviceSqlString result;
    if (value.contains(QChar(0))) {
        // A NUL would terminate the field early; the format has no escape.
        qWarning("DeviceSQL string contains a NUL character, truncating");
    }

    if (isPlainAscii(value)) {
        const QByteArray payload = value.toLatin1();
        if (payload.size() <= kMaxShortAscii) {
            QByteArray out;
            out.append(char(((payload.size() + 1) << 1) | 1));
            out.append(payload);
            result.m_bytes = out;
            result.m_long = false;
            return result;
        }
        result.m_bytes = encodeLong(kLongAscii, payload);
        result.m_long = true;
        return result;
    }

    result.m_bytes = encodeLong(kLongUtf16, toUtf16Le(value));
    result.m_long = true;
    return result;
}

DeviceSqlString DeviceSqlString::isrc(const QString& value) {
    DeviceSqlString result;
    const QByteArray payload = value.toLatin1();
    QByteArray body;
    body.append(char(0x03)); // marks the body as NUL terminated ASCII
    body.append(payload);
    body.append(char(0x00));
    result.m_bytes = encodeLong(kLongUtf16, body);
    result.m_long = true;
    return result;
}

} // namespace device
} // namespace opendeck
