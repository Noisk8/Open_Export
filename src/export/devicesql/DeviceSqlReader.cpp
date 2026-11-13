#include "export/devicesql/DeviceSqlReader.h"

namespace opendeck {
namespace device {

namespace {

quint32 readU32(const QByteArray& data, int offset) {
    if (offset < 0 || offset + 4 > data.size()) {
        return 0;
    }
    return static_cast<quint32>(static_cast<quint8>(data.at(offset)))
            | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 8)
            | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 16)
            | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 3))) << 24);
}

quint16 readU16(const QByteArray& data, int offset) {
    if (offset < 0 || offset + 2 > data.size()) {
        return 0;
    }
    return static_cast<quint16>(static_cast<quint8>(data.at(offset)))
            | static_cast<quint16>(static_cast<quint8>(data.at(offset + 1)) << 8);
}

} // namespace

DeviceSqlReader::DeviceSqlReader(const QByteArray& data) : m_data(data) {
    if (m_data.size() < 0x28) {
        m_error = QStringLiteral("El fichero es demasiado pequeño");
        return;
    }

    m_pageSize = static_cast<int>(readU32(m_data, 0x04));
    if (m_pageSize <= 0 || m_pageSize > 65536 || (m_pageSize % 4) != 0) {
        m_error = QStringLiteral("Tamaño de página inválido");
        return;
    }
    if (m_data.size() % m_pageSize != 0) {
        m_error = QStringLiteral("El fichero no es múltiplo del tamaño de página");
        return;
    }

    const quint32 tableCount = readU32(m_data, 0x08);
    if (tableCount > 64) {
        m_error = QStringLiteral("Número de tablas inválido");
        return;
    }

    const int pageCount = m_data.size() / m_pageSize;
    for (quint32 i = 0; i < tableCount; ++i) {
        const int pointer = 0x1c + static_cast<int>(i) * 16;
        const quint32 type = readU32(m_data, pointer);
        const quint32 first = readU32(m_data, pointer + 8);
        const quint32 last = readU32(m_data, pointer + 12);
        if (first >= static_cast<quint32>(pageCount) || last >= static_cast<quint32>(pageCount)) {
            m_error = QStringLiteral("La tabla %1 apunta a una página inexistente").arg(type);
            return;
        }
        m_tableTypes.append(type);
        m_firstPage.append(first);
        m_lastPage.append(last);
        m_tableIndex.insert(type, static_cast<int>(i));
    }
}

quint32 DeviceSqlReader::tableType(int index) const {
    if (index < 0 || index >= m_tableTypes.size()) {
        return 0xffffffffU;
    }
    return m_tableTypes.at(index);
}

quint32 DeviceSqlReader::firstPage(quint32 type) const {
    const auto it = m_tableIndex.constFind(type);
    return it == m_tableIndex.constEnd() ? 0xffffffffU : m_firstPage.at(it.value());
}

quint32 DeviceSqlReader::lastPage(quint32 type) const {
    const auto it = m_tableIndex.constFind(type);
    return it == m_tableIndex.constEnd() ? 0xffffffffU : m_lastPage.at(it.value());
}

QList<QByteArray> DeviceSqlReader::rows(quint32 type) const {
    QList<QByteArray> result;
    const auto it = m_tableIndex.constFind(type);
    if (it == m_tableIndex.constEnd()) {
        return result;
    }
    const int index = it.value();
    const quint32 first = m_firstPage.at(index);
    const quint32 last = m_lastPage.at(index);

    quint32 page = first;
    int guard = 0;
    while (page <= last && page < static_cast<quint32>(m_data.size() / m_pageSize)) {
        if (++guard > 100000) {
            break;
        }
        const int base = static_cast<int>(page) * m_pageSize;
        const QByteArray pageData = m_data.mid(base, m_pageSize);
        const quint8 flags = static_cast<quint8>(pageData.at(0x1b));
        if ((flags & 0x40) != 0) {
            // A sentinel page holds no rows; follow its link to the data.
            page = readU32(pageData, 0x0c);
            continue;
        }

        // The number of slotCount tells how many row groups the page has.
        const quint32 counts = static_cast<quint32>(static_cast<quint8>(pageData.at(0x18)))
                | (static_cast<quint32>(static_cast<quint8>(pageData.at(0x19))) << 8)
                | (static_cast<quint32>(static_cast<quint8>(pageData.at(0x1a))) << 16);
        const quint32 slotCount = counts & 0x1fffU;
        const quint32 present = (counts >> 13) & 0x7ffU;
        const int groups = static_cast<int>((slotCount + 15) / 16);

        for (int group = 0; group < groups; ++group) {
            const int blockEnd = m_pageSize - 36 * group;
            const quint16 mask = readU16(pageData, blockEnd - 4);
            for (int slot = 0; slot < 16; ++slot) {
                if ((mask & (1u << slot)) == 0) {
                    continue;
                }
                const quint16 heapOffset = readU16(pageData, blockEnd - 6 - 2 * slot);
                const int rowStart = 0x28 + heapOffset;
                if (rowStart < 0x28 || rowStart >= m_pageSize) {
                    continue;
                }
                // The row ends where the next one starts; without a better
                // hint the rest of the heap is a safe upper bound.
                const int rowEnd = m_pageSize - 36 * groups;
                const int length = qMax(0, rowEnd - rowStart);
                result.append(pageData.mid(rowStart, length));
            }
        }
        Q_UNUSED(present)
        page = readU32(pageData, 0x0c);
    }
    return result;
}

QString DeviceSqlReader::readString(const QByteArray& row, int offset) {
    if (offset < 0 || offset >= row.size()) {
        return {};
    }
    const quint8 kind = static_cast<quint8>(row.at(offset));
    if ((kind & 0x01) != 0) {
        // Short ASCII: the length of the whole field is the kind shifted once.
        const int length = (kind >> 1) - 1;
        if (length <= 0 || offset + 1 + length > row.size()) {
            return {};
        }
        return QString::fromLatin1(row.constData() + offset + 1, length);
    }
    if (kind == 0x40 || kind == 0x90) {
        const int length = readU16(row, offset + 1);
        if (length < 4 || offset + length > row.size()) {
            return {};
        }
        const int payload = length - 4;
        if (kind == 0x40) {
            return QString::fromLatin1(row.constData() + offset + 4, payload);
        }
        // UTF-16LE, unless the body is the NUL terminated ASCII of an ISRC.
        if (payload > 0 && static_cast<quint8>(row.at(offset + 4)) == 0x03) {
            return QString::fromLatin1(row.constData() + offset + 5, qMax(0, payload - 1));
        }
        QString out;
        for (int i = 0; i + 1 < payload; i += 2) {
            out.append(QChar(static_cast<quint16>(
                    static_cast<quint8>(row.at(offset + 4 + i))
                    | (static_cast<quint16>(static_cast<quint8>(row.at(offset + 5 + i))) << 8))));
        }
        return out;
    }
    return {};
}

QString DeviceSqlReader::readSlotString(const QByteArray& row, int slotOffset) {
    if (slotOffset < 0 || slotOffset + 2 > row.size()) {
        return {};
    }
    return readString(row, readU16(row, slotOffset));
}

} // namespace device
} // namespace opendeck
