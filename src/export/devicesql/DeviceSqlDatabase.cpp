#include "export/devicesql/DeviceSqlDatabase.h"

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

} // namespace

DeviceSqlDatabase::DeviceSqlDatabase() {
    // Page zero is the file header; it is filled in when the file is written.
    m_pages.append(QByteArray(page::kPageSize, 0));
}

void DeviceSqlDatabase::createTable(quint32 type) {
    Table entry;
    entry.type = type;

    // Rekordbox pre-allocates the sentinel pages at the odd page numbers
    // (1, 3, 5, ...) and appends the data pages afterwards.
    entry.sentinelPage = m_nextPage;
    m_nextPage += 2;
    entry.lastPage = entry.sentinelPage;

    DeviceSqlIndexPage sentinel(type);
    sentinel.setPageIndex(entry.sentinelPage);
    sentinel.setNextPage(page::kEmptyPage);

    QList<QByteArray> pages;
    pages.append(sentinel.serialize());
    while (static_cast<quint32>(m_pages.size()) <= entry.sentinelPage) {
        m_pages.append(QByteArray());
    }
    m_pages[entry.sentinelPage] = pages.first();

    m_tableIndex.insert(type, m_tables.size());
    m_tables.append(entry);
}

DeviceSqlDatabase::Table* DeviceSqlDatabase::tableFor(quint32 type) {
    const auto it = m_tableIndex.constFind(type);
    if (it == m_tableIndex.constEnd()) {
        return nullptr;
    }
    return &m_tables[it.value()];
}

const DeviceSqlDatabase::Table* DeviceSqlDatabase::tableFor(quint32 type) const {
    const auto it = m_tableIndex.constFind(type);
    if (it == m_tableIndex.constEnd()) {
        return nullptr;
    }
    return &m_tables.at(it.value());
}

void DeviceSqlDatabase::appendRow(quint32 type, const QByteArray& row) {
    Table* entry = tableFor(type);
    if (entry == nullptr) {
        return;
    }
    entry->rows.append(row);
}

int DeviceSqlDatabase::rowCount(quint32 type) const {
    const Table* entry = tableFor(type);
    return entry == nullptr ? 0 : static_cast<int>(entry->rows.size());
}

QByteArray DeviceSqlDatabase::serialize() const {
    // --- Lay the data pages out --------------------------------------------
    // The sentinel pages were already reserved; the data pages are appended
    // in table order, starting right after the last sentinel.
    QList<QByteArray> pages = m_pages;
    quint32 nextPage = m_nextPage;

    QList<quint32> firstPage(table::kCount, 0);
    QList<quint32> lastPage(table::kCount, 0);

    for (const Table& entry : m_tables) {
        const int slot = static_cast<int>(entry.type);
        firstPage[slot] = entry.sentinelPage;
        lastPage[slot] = entry.sentinelPage;
        if (entry.rows.isEmpty()) {
            continue;
        }

        // Split the rows into as many data pages as needed.
        QList<QList<QByteArray>> chunks;
        QList<QByteArray> chunk;
        DeviceSqlDataPage probe(entry.type);
        for (const QByteArray& row : entry.rows) {
            if (!probe.canFit(row) && !chunk.isEmpty()) {
                chunks.append(chunk);
                chunk.clear();
                // A fresh page starts with an empty heap again.
                probe = DeviceSqlDataPage(entry.type);
            }
            chunk.append(row);
            probe.appendRow(row);
        }
        if (!chunk.isEmpty()) {
            chunks.append(chunk);
        }

        // Reserve the page numbers so that the chain can be linked.
        QList<quint32> pageNumbers;
        for (int i = 0; i < chunks.size(); ++i) {
            pageNumbers.append(nextPage++);
        }

        for (int i = 0; i < chunks.size(); ++i) {
            DeviceSqlDataPage dataPage(entry.type);
            dataPage.setPageIndex(pageNumbers.at(i));
            dataPage.setNextPage(i + 1 < pageNumbers.size() ? pageNumbers.at(i + 1)
                                                            : page::kEmptyPage);
            for (const QByteArray& row : chunks.at(i)) {
                if (!dataPage.canFit(row)) {
                    // Defensive: the chunk was built with the same geometry,
                    // so this should never happen, but never overflow a page.
                    break;
                }
                dataPage.appendRow(row);
            }
            const quint32 pageNumber = pageNumbers.at(i);
            while (static_cast<quint32>(pages.size()) <= pageNumber) {
                pages.append(QByteArray());
            }
            pages[pageNumber] = dataPage.serialize();
        }

        // The sentinel page links to the first data page.
        lastPage[slot] = pageNumbers.last();

        DeviceSqlIndexPage sentinel(entry.type);
        sentinel.setPageIndex(entry.sentinelPage);
        sentinel.setNextPage(pageNumbers.first());
        pages[entry.sentinelPage] = sentinel.serialize();
    }

    // --- File header --------------------------------------------------------
    QByteArray header(page::kPageSize, 0);
    putU32(header, 0x04, static_cast<quint32>(page::kPageSize));
    putU32(header, 0x08, static_cast<quint32>(m_tables.size()));
    putU32(header, 0x0c, nextPage);
    putU32(header, 0x10, 5); // unknown, players accept any value here
    putU32(header, 0x14, 5); // global sequence number
    putU32(header, 0x18, 0);

    for (const Table& entry : m_tables) {
        const int slot = static_cast<int>(entry.type);
        const int pointer = 0x1c + slot * 16;
        putU32(header, pointer + 0, entry.type);
        putU32(header, pointer + 4, lastPage[slot] + 1); // empty candidate
        putU32(header, pointer + 8, firstPage[slot]);
        putU32(header, pointer + 12, lastPage[slot]);
    }

    pages[0] = header;

    QByteArray out;
    out.reserve(pages.size() * page::kPageSize);
    for (const QByteArray& p : pages) {
        if (p.size() != page::kPageSize) {
            // A page that was reserved but never written must still exist.
            out.append(QByteArray(page::kPageSize, 0));
        } else {
            out.append(p);
        }
    }
    return out;
}

} // namespace device
} // namespace opendeck
