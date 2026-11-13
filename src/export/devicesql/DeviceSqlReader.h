#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

namespace opendeck {
namespace device {

// DeviceSqlReader parses a DeviceSQL database, the `export.pdb` file of a
// Pioneer export.
//
// It is deliberately independent from the writer: it walks the file the way a
// player does (file header, table directory, page chains, row index) and
// therefore can be used to check what the writer produced.
//
// See <https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/>.
class DeviceSqlReader {
  public:
    explicit DeviceSqlReader(const QByteArray& data);

    [[nodiscard]] bool isValid() const { return m_error.isEmpty(); }
    [[nodiscard]] QString errorString() const { return m_error; }

    [[nodiscard]] int pageSize() const { return m_pageSize; }
    [[nodiscard]] int tableCount() const { return m_tableTypes.size(); }
    [[nodiscard]] quint32 tableType(int index) const;
    [[nodiscard]] quint32 firstPage(quint32 type) const;
    [[nodiscard]] quint32 lastPage(quint32 type) const;

    // All the rows of a table, in the order they are stored.
    [[nodiscard]] QList<QByteArray> rows(quint32 type) const;

    // Decodes a DeviceSQL string that starts at the given offset of a row.
    [[nodiscard]] static QString readString(const QByteArray& row, int offset);

    // Decodes the string that the 16 bit slot at `slotOffset` points to.
    [[nodiscard]] static QString readSlotString(const QByteArray& row, int slotOffset);

  private:
    QByteArray m_data;
    QString m_error;
    int m_pageSize = 0;
    QList<quint32> m_tableTypes;
    QList<quint32> m_firstPage;
    QList<quint32> m_lastPage;
    QHash<quint32, int> m_tableIndex;
};

} // namespace device
} // namespace opendeck
