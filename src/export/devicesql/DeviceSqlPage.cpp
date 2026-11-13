#include "export/devicesql/DeviceSqlPage.h"

#include <QtEndian>

namespace opendeck {
namespace device {

namespace {

void putU16(QByteArray& page, int offset, quint16 value) {
    page[offset] = static_cast<char>(value & 0xff);
    page[offset + 1] = static_cast<char>((value >> 8) & 0xff);
}

void putU32(QByteArray& page, int offset, quint32 value) {
    for (int i = 0; i < 4; ++i) {
        page[offset + i] = static_cast<char>((value >> (8 * i)) & 0xff);
    }
}

int alignUp(int value, int alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

// The rows that carry an `index shift` store it right behind their subtype.
// Rekordbox uses it to number the rows inside their page.
bool hasIndexShift(quint16 subtype) {
    switch (subtype) {
    case 0x0024: // track
    case 0x0060: // artist
    case 0x0080: // album
    case 0x0280: // history
        return true;
    default:
        return false;
    }
}

} // namespace

DeviceSqlDataPage::DeviceSqlDataPage(quint32 type) : m_type(type) {
}

int DeviceSqlDataPage::usedSize() const {
    return m_offsets.isEmpty() ? 0 : m_offsets.last() + alignUp(m_rows.last().size(), 4);
}

int DeviceSqlDataPage::groupCount() const {
    if (m_rows.isEmpty()) {
        return 0;
    }
    return (static_cast<int>(m_rows.size()) + page::kRowsPerGroup - 1) / page::kRowsPerGroup;
}

bool DeviceSqlDataPage::canFit(const QByteArray& row) const {
    const int rowSize = alignUp(row.size(), 4);
    const int newUsed = usedSize() + rowSize;
    const int slotCount = static_cast<int>(m_rows.size()) + 1;
    const int groups = (slotCount + page::kRowsPerGroup - 1) / page::kRowsPerGroup;
    // The row index blocks are reserved as whole 36 byte groups.
    return page::kHeaderSize + newUsed + groups * page::kRowGroupSize <= page::kPageSize;
}

void DeviceSqlDataPage::appendRow(const QByteArray& row) {
    QByteArray stored = row;
    if (stored.size() >= 4) {
        const quint16 subtype = static_cast<quint16>(
                static_cast<quint8>(stored.at(0)) | (static_cast<quint8>(stored.at(1)) << 8));
        if (hasIndexShift(subtype)) {
            const quint16 shift = static_cast<quint16>(m_rows.size() * 0x20);
            stored[2] = static_cast<char>(shift & 0xff);
            stored[3] = static_cast<char>((shift >> 8) & 0xff);
        }
    }
    m_offsets.append(usedSize());
    m_rows.append(stored);
}

QByteArray DeviceSqlDataPage::serialize() const {
    QByteArray out(page::kPageSize, 0);

    // --- Common page header -------------------------------------------------
    putU32(out, 0x04, m_pageIndex);
    putU32(out, 0x08, m_type);
    putU32(out, 0x0c, m_nextPage);
    putU32(out, 0x10, 1); // transaction id of the last write to this page
    putU32(out, 0x14, 0);

    // --- Row counts ---------------------------------------------------------
    // 13 bits of allocated slots, 11 bits of rows that are still present.
    const quint32 slotCount = static_cast<quint32>(m_rows.size());
    const quint32 counts = slotCount | (slotCount << 13);
    out[0x18] = static_cast<char>(counts & 0xff);
    out[0x19] = static_cast<char>((counts >> 8) & 0xff);
    out[0x1a] = static_cast<char>((counts >> 16) & 0xff);
    out[0x1b] = static_cast<char>(page::kDataPageFlags);

    const int used = usedSize();
    const int groups = groupCount();
    const int indexBytes = 4 * groups + 2 * static_cast<int>(slotCount);
    putU16(out, 0x1c, static_cast<quint16>(page::kPageSize - page::kHeaderSize - used - indexBytes));
    putU16(out, 0x1e, static_cast<quint16>(used));

    // --- Data page header ---------------------------------------------------
    putU16(out, 0x20, static_cast<quint16>(slotCount)); // rows written in this batch
    putU16(out, 0x22, 0); // first row of the batch
    putU16(out, 0x24, 0);
    putU16(out, 0x26, 0);

    // --- Heap ---------------------------------------------------------------
    int cursor = page::kHeaderSize;
    for (int i = 0; i < m_rows.size(); ++i) {
        const QByteArray& row = m_rows.at(i);
        out.replace(cursor, row.size(), row);
        cursor += alignUp(row.size(), 4);
    }

    // --- Row index ----------------------------------------------------------
    // Group 0 is the one closest to the end of the page. Inside a group the
    // slot with the highest index sits at the lowest address.
    for (int group = 0; group < groups; ++group) {
        const int blockEnd = page::kPageSize - page::kRowGroupSize * group;
        const int first = group * page::kRowsPerGroup;
        const int count = qMin(page::kRowsPerGroup, static_cast<int>(m_rows.size()) - first);

        quint16 presence = 0;
        for (int i = 0; i < count; ++i) {
            presence |= static_cast<quint16>(1u << i);
            putU16(out, blockEnd - 6 - 2 * i, static_cast<quint16>(m_offsets.at(first + i)));
        }
        putU16(out, blockEnd - 4, presence);
        putU16(out, blockEnd - 2, presence); // mask of the rows written last
    }

    return out;
}

DeviceSqlIndexPage::DeviceSqlIndexPage(quint32 type) : m_type(type) {
}

QByteArray DeviceSqlIndexPage::serialize() const {
    QByteArray out(page::kPageSize, 0);

    // --- Common page header -------------------------------------------------
    putU32(out, 0x04, m_pageIndex);
    putU32(out, 0x08, m_type);
    putU32(out, 0x0c, m_nextPage);
    putU32(out, 0x10, 1);
    putU32(out, 0x14, 0);
    out[0x1b] = static_cast<char>(page::kIndexPageFlags);
    putU16(out, 0x1c, 0);
    putU16(out, 0x1e, 0);

    // --- Index page header --------------------------------------------------
    putU16(out, 0x20, 0x1fff);
    putU16(out, 0x22, 0x1fff);
    putU16(out, 0x24, 0x03ec); // magic
    putU16(out, 0x26, 0); // offset of the next index entry
    putU32(out, 0x28, m_pageIndex);
    putU32(out, 0x2c, page::kEmptyPage);
    putU32(out, 0x30, 0x03ffffff);
    putU32(out, 0x34, 0);
    putU16(out, 0x38, 0); // number of index entries
    putU16(out, 0x3a, 0x1fff); // first empty slot

    // --- Index entries ------------------------------------------------------
    // A fresh database has no freed pages, so every slot stays empty.
    for (int i = 0; i < page::kIndexEntryCount; ++i) {
        putU32(out, 0x3c + 4 * i, page::kEmptyIndexEntry);
    }
    // The last twenty bytes of the page are always zero.

    return out;
}

} // namespace device
} // namespace opendeck
