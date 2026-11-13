#pragma once

#include <QByteArray>
#include <QString>

namespace opendeck {
namespace device {

// DeviceSqlString is a variable length string as stored in a DeviceSQL page
// heap.
//
// The format has three flavours, all identified by the first byte:
//
//   * short ASCII - the byte is odd, and holds the total length of the
//     field (including the byte itself) shifted left by one. The payload is
//     plain ASCII and may be at most 126 bytes long. The empty string is the
//     single byte 0x03.
//   * long ASCII - the byte is 0x40, followed by a 16 bit length (the total
//     size of the field, including the four byte header), a pad byte and the
//     ASCII payload.
//   * long UTF-16LE - the byte is 0x90, followed by the same 16 bit length
//     and pad byte, then the payload encoded as little endian UTF-16.
//
// Rekordbox writes the long flavours on a four byte boundary of the row, so
// `isLong()` tells the caller that padding has to be inserted before the
// string is appended to a row.
class DeviceSqlString {
  public:
    // Encodes arbitrary text. Pure ASCII text of up to 126 characters uses
    // the short form, longer or non-ASCII text uses one of the long forms.
    [[nodiscard]] static DeviceSqlString text(const QString& value);

    // Encodes an ISRC. Rekordbox stores it in the long UTF-16 slot but with
    // a NUL terminated ASCII body, which the players expect.
    [[nodiscard]] static DeviceSqlString isrc(const QString& value);

    [[nodiscard]] const QByteArray& bytes() const { return m_bytes; }
    [[nodiscard]] bool isLong() const { return m_long; }
    [[nodiscard]] int size() const { return static_cast<int>(m_bytes.size()); }

  private:
    QByteArray m_bytes;
    bool m_long = false;
};

} // namespace device
} // namespace opendeck
