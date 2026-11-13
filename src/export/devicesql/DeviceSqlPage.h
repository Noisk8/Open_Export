#pragma once

#include <QByteArray>
#include <QList>

namespace opendeck {
namespace device {

// Page geometry of a DeviceSQL database. All of it is fixed by the format.
namespace page {

constexpr int kPageSize = 4096;
constexpr int kHeaderSize = 0x28; // common header + data page header
constexpr int kRowGroupSize = 36; // one group of sixteen row slots
constexpr int kRowsPerGroup = 16;
constexpr int kIndexHeaderSize = 0x1c; // index page header, at 0x20
constexpr int kIndexEntryCount = 1004; // index slots that fit in a page
constexpr int kIndexTrailingZeroBytes = 20;

constexpr quint32 kEmptyPage = 0x03ffffff; // "no page" marker
constexpr quint32 kEmptyIndexEntry = 0x1ffffff8;

constexpr quint8 kDataPageFlags = 0x24;
constexpr quint8 kIndexPageFlags = 0x64;

} // namespace page

// DeviceSqlDataPage collects the rows of one table and serialises them into a
// single 4096 byte page.
//
// Rows are stored in a heap that starts right after the 40 byte page header.
// The index that locates them is built backwards from the end of the page in
// groups of sixteen slots; each group occupies 36 bytes and holds the heap
// offset of every slot plus a bit mask of the slots that are still present.
class DeviceSqlDataPage {
  public:
    explicit DeviceSqlDataPage(quint32 type);

    // True when the row still fits in this page.
    [[nodiscard]] bool canFit(const QByteArray& row) const;

    // Appends a row. The caller must have checked canFit() first.
    void appendRow(const QByteArray& row);

    void setPageIndex(quint32 index) { m_pageIndex = index; }
    void setNextPage(quint32 index) { m_nextPage = index; }

    [[nodiscard]] quint32 pageIndex() const { return m_pageIndex; }
    [[nodiscard]] int rowCount() const { return static_cast<int>(m_rows.size()); }
    [[nodiscard]] bool isEmpty() const { return m_rows.isEmpty(); }

    // Renders the page.
    [[nodiscard]] QByteArray serialize() const;

  private:
    [[nodiscard]] int usedSize() const;
    [[nodiscard]] int groupCount() const;

    quint32 m_type;
    quint32 m_pageIndex = 0;
    quint32 m_nextPage = page::kEmptyPage;
    QList<QByteArray> m_rows;
    QList<int> m_offsets; // heap offset of every row
};

// DeviceSqlIndexPage is the sentinel page that every table starts with. It
// carries no rows; it is the entry point of the table and the place where the
// garbage collector would find freed pages.
class DeviceSqlIndexPage {
  public:
    explicit DeviceSqlIndexPage(quint32 type);

    void setPageIndex(quint32 index) { m_pageIndex = index; }
    void setNextPage(quint32 index) { m_nextPage = index; }

    [[nodiscard]] quint32 pageIndex() const { return m_pageIndex; }
    [[nodiscard]] QByteArray serialize() const;

  private:
    quint32 m_type;
    quint32 m_pageIndex = 0;
    quint32 m_nextPage = page::kEmptyPage;
};

} // namespace device
} // namespace opendeck
