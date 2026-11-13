#include "export/devicesql/DeviceSqlRow.h"

namespace opendeck {
namespace device {

DeviceSqlRow::DeviceSqlRow(int fixedSize) : m_fixed(fixedSize, 0) {
}

const QByteArray& DeviceSqlRow::bytes() const {
    if (m_bytes.size() != m_fixed.size() + m_heap.size()) {
        m_bytes = m_fixed;
        m_bytes.append(m_heap);
    }
    return m_bytes;
}

void DeviceSqlRow::setU8(int offset, quint8 value) {
    m_fixed[offset] = static_cast<char>(value);
}

void DeviceSqlRow::setU16(int offset, quint16 value) {
    m_fixed[offset] = static_cast<char>(value & 0xff);
    m_fixed[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

void DeviceSqlRow::setU32(int offset, quint32 value) {
    for (int i = 0; i < 4; ++i) {
        m_fixed[offset + i] = static_cast<char>((value >> (8 * i)) & 0xff);
    }
}

void DeviceSqlRow::setString(int slotOffset, const DeviceSqlString& value) {
    const int offset = appendHeap(value);
    setU16(slotOffset, static_cast<quint16>(offset));
}

void DeviceSqlRow::setString(int slotOffset, const QString& text) {
    setString(slotOffset, DeviceSqlString::text(text));
}

int DeviceSqlRow::appendString(const DeviceSqlString& value) {
    return appendHeap(value);
}

void DeviceSqlRow::appendRaw(const QByteArray& data) {
    m_heap.append(data);
}

int DeviceSqlRow::appendHeap(const DeviceSqlString& value) {
    if (value.isLong()) {
        // Long strings start on a four byte boundary of the row.
        while (((m_fixed.size() + m_heap.size()) % 4) != 0) {
            m_heap.append(char(0));
        }
    }
    const int offset = static_cast<int>(m_fixed.size() + m_heap.size());
    m_heap.append(value.bytes());
    return offset;
}

} // namespace device
} // namespace opendeck
