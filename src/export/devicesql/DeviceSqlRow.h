#pragma once

#include <QByteArray>
#include <QString>

#include "export/devicesql/DeviceSqlString.h"

namespace opendeck {
namespace device {

// DeviceSqlRow builds one row of a DeviceSQL table.
//
// A row consists of a fixed size header, followed by a heap of variable
// length strings. The header holds a 16 bit offset per string, relative to
// the beginning of the row.
//
// Rekordbox starts every long string (the 0x40 and 0x90 forms) on a four
// byte boundary of the row; short strings are packed right after whatever
// precedes them. This class applies that rule automatically.
class DeviceSqlRow {
  public:
    explicit DeviceSqlRow(int fixedSize);

    void setU8(int offset, quint8 value);
    void setU16(int offset, quint16 value);
    void setU32(int offset, quint32 value);

    // Stores a string and writes its offset into the 16 bit slot located at
    // `slotOffset` inside the fixed header.
    void setString(int slotOffset, const DeviceSqlString& value);
    void setString(int slotOffset, const QString& text);

    // Appends a string without recording an offset; used by the rows that
    // embed their name directly (artwork, colour, genre, ...). Returns the
    // offset at which the string starts.
    int appendString(const DeviceSqlString& value);

    // Appends raw bytes to the heap of the row.
    void appendRaw(const QByteArray& data);

    // The whole row: the fixed header followed by the heap of strings.
    [[nodiscard]] const QByteArray& bytes() const;
    [[nodiscard]] int size() const { return static_cast<int>(m_fixed.size()); }

  private:
    // Appends a string to the heap and returns the offset it starts at.
    int appendHeap(const DeviceSqlString& value);

    QByteArray m_fixed;
    QByteArray m_heap;
    mutable QByteArray m_bytes;
};

} // namespace device
} // namespace opendeck
