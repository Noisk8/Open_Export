#include "export/anlz/AnlzWriter.h"

#include <QtEndian>

#include <algorithm>
#include <cmath>

namespace opendeck {
namespace device {

namespace {

// --- File and section geometry ---------------------------------------------

constexpr int kFileHeaderSize = 0x1c; // 28 bytes
constexpr int kSectionHeaderSize = 12; // fourcc + len_header + len_tag

constexpr int kPreviewColumns = 400; // PWAV
constexpr int kTinyPreviewColumns = 100; // PWV2
constexpr int kColorPreviewColumns = 1200; // PWV4
constexpr int kThreeBandPreviewColumns = 1200; // PWV6
constexpr int kDetailColumnsPerSecond = 150; // PWV3, PWV5 and PWV7

// --- Cue list ---------------------------------------------------------------

constexpr quint32 kHotCues = 1;
constexpr quint32 kMemoryCues = 0;
constexpr quint8 kCuePoint = 1;
constexpr quint8 kCueLoop = 2;

void putU8(QByteArray& out, quint8 value) {
    out.append(char(value));
}

void putU16(QByteArray& out, quint16 value) {
    out.append(char((value >> 8) & 0xff));
    out.append(char(value & 0xff));
}

void putU32(QByteArray& out, quint32 value) {
    out.append(char((value >> 24) & 0xff));
    out.append(char((value >> 16) & 0xff));
    out.append(char((value >> 8) & 0xff));
    out.append(char(value & 0xff));
}

// Encodes a string as UTF-16 big endian, the encoding the analysis files use.
QByteArray utf16Be(const QString& value) {
    QByteArray out;
    out.reserve(value.size() * 2);
    for (const QChar c : value) {
        const quint16 unit = c.unicode();
        out.append(char((unit >> 8) & 0xff));
        out.append(char(unit & 0xff));
    }
    return out;
}

// --- Section builder --------------------------------------------------------

// A tagged section: a four character code, the size of its header and its
// body.
class Section {
  public:
    Section(const char* fourcc, quint32 headerSize) : m_fourcc(fourcc),
                                                      m_headerSize(headerSize) {
    }

    void writeBody(QByteArray& out) const { out.append(m_body); }

    void setBody(const QByteArray& body) { m_body = body; }

    [[nodiscard]] QByteArray serialize() const {
        QByteArray out;
        out.append(m_fourcc, 4);
        putU32(out, m_headerSize);
        putU32(out, static_cast<quint32>(kSectionHeaderSize + m_body.size()));
        out.append(m_body);
        return out;
    }

  private:
    const char* m_fourcc;
    quint32 m_headerSize;
    QByteArray m_body;
};

// Builds a section with its body already set.
Section makeSection(const char* fourcc, quint32 headerSize, const QByteArray& body) {
    Section section(fourcc, headerSize);
    section.setBody(body);
    return section;
}

// Builds a whole ANLZ file out of a list of sections.
QByteArray buildFile(const QList<Section>& sections) {
    QByteArray body;
    for (const Section& section : sections) {
        body.append(section.serialize());
    }

    QByteArray out;
    out.append("PMAI", 4);
    putU32(out, kFileHeaderSize);
    putU32(out, static_cast<quint32>(kFileHeaderSize + body.size()));
    // The remaining header words are constants in every file that rekordbox
    // writes.
    putU32(out, 0x00010000);
    putU32(out, 0x00010000);
    putU32(out, 0x00010000);
    putU32(out, 0x00000000);
    out.append(body);
    return out;
}

// --- Sections ----------------------------------------------------------------

// PPTH: the path of the audio file the analysis belongs to.
Section makePathSection(const QString& devicePath) {
    const QByteArray text = utf16Be(devicePath);
    QByteArray body;
    putU32(body, static_cast<quint32>(text.size() + 2)); // includes the NUL
    body.append(text);
    body.append(char(0));
    body.append(char(0));
    return makeSection("PPTH", 0x10, body);
}

// PVBR: an index that lets the player seek quickly inside a variable bitrate
// file. The format is not documented, so it is written as zeros.
Section makeVbrSection() {
    QByteArray body;
    putU32(body, 0);
    for (int i = 0; i < 400; ++i) {
        putU32(body, 0);
    }
    return makeSection("PVBR", 0x10, body);
}

// PQTZ: the beat grid.
Section makeBeatGridSection(const QList<core::Beat>& beats) {
    QByteArray body;
    putU32(body, 0);
    putU32(body, 0x00080000);
    putU32(body, static_cast<quint32>(beats.size()));
    for (const core::Beat& beat : beats) {
        putU16(body, static_cast<quint16>(qBound(1, beat.number, 4)));
        putU16(body, static_cast<quint16>(qBound(0, qRound(beat.tempo * 100.0), 65535)));
        putU32(body, static_cast<quint32>(qMax(0, qRound(beat.time * 1000.0))));
    }
    return makeSection("PQTZ", 0x18, body);
}

// The waveform sections share the same layout: a count of entries, a count of
// the entries themselves and the data.
QByteArray resample(const QByteArray& peaks, int columns) {
    QByteArray out(qMax(0, columns), char(0));
    if (peaks.isEmpty() || columns <= 0) {
        return out;
    }
    for (int i = 0; i < columns; ++i) {
        const int from = static_cast<int>((static_cast<qint64>(i) * peaks.size()) / columns);
        int to = static_cast<int>((static_cast<qint64>(i + 1) * peaks.size()) / columns);
        if (to <= from) {
            to = from + 1;
        }
        quint8 maximum = 0;
        for (int j = from; j < to && j < peaks.size(); ++j) {
            maximum = std::max(maximum, static_cast<quint8>(peaks.at(j)));
        }
        out[i] = static_cast<char>(maximum);
    }
    return out;
}

// The largest peak of the envelope, used to normalise the heights.
int peakMaximum(const QByteArray& peaks) {
    int maximum = 0;
    for (const char value : peaks) {
        maximum = std::max(maximum, static_cast<int>(static_cast<quint8>(value)));
    }
    return maximum;
}

// PWAV: the monochrome preview that the player shows above the touch strip.
Section makePreviewSection(const QByteArray& peaks) {
    const QByteArray columns = resample(peaks, kPreviewColumns);
    const int maximum = peakMaximum(columns);

    QByteArray body;
    putU32(body, kPreviewColumns);
    putU32(body, 0x00010000);
    for (int i = 0; i < columns.size(); ++i) {
        const int value = static_cast<quint8>(columns.at(i));
        const int relative = maximum > 0 ? (value * 31) / maximum : 0;
        // The low five bits hold the height, the high three the whiteness.
        body.append(char(relative & 0x1f));
    }
    return makeSection("PWAV", 0x14, body);
}

// PWV2: the tiny preview of the older players.
Section makeTinyPreviewSection(const QByteArray& peaks) {
    const QByteArray columns = resample(peaks, kTinyPreviewColumns);
    const int maximum = peakMaximum(columns);

    QByteArray body;
    putU32(body, kTinyPreviewColumns);
    putU32(body, 0x00010000);
    for (int i = 0; i < columns.size(); ++i) {
        const int value = static_cast<quint8>(columns.at(i));
        int height = maximum > 0 ? (value * 15) / maximum : 0;
        // A player reads a zero column as an unfinished analysis.
        height = qBound(1, height, 15);
        body.append(char(height & 0x0f));
    }
    return makeSection("PWV2", 0x14, body);
}

// PWV3: the scrolling monochrome waveform.
Section makeDetailSection(const QByteArray& peaks) {
    const int maximum = peakMaximum(peaks);

    QByteArray body;
    putU32(body, 1); // one byte per column
    putU32(body, static_cast<quint32>(peaks.size()));
    putU32(body, 0x00960000);
    for (const char value : peaks) {
        const int amplitude = static_cast<quint8>(value);
        const int relative = maximum > 0 ? amplitude : 0;
        // Rekordbox scales the height with the square of the relative peak.
        const int height = qBound(0, (31 * relative * relative) / (maximum * maximum), 31);
        body.append(char(height & 0x1f));
    }
    return makeSection("PWV3", 0x18, body);
}

// PWV4: the colour preview. The six bytes of every column are not documented,
// so the same envelope is rendered as a blue gradient.
Section makeColorPreviewSection(const QByteArray& peaks) {
    const QByteArray columns = resample(peaks, kColorPreviewColumns);
    const int maximum = peakMaximum(columns);

    QByteArray body;
    putU32(body, 6); // six bytes per column
    putU32(body, kColorPreviewColumns);
    putU32(body, 0);
    for (int i = 0; i < columns.size(); ++i) {
        const int value = static_cast<quint8>(columns.at(i));
        const int height = maximum > 0 ? (value * 31) / maximum : 0;
        body.append(char(0x20)); // red
        body.append(char(0x40)); // green
        body.append(char(0xc0)); // blue
        body.append(char(height & 0x1f));
        body.append(char(0));
        body.append(char(0));
    }
    return makeSection("PWV4", 0x18, body);
}

// PWV5: the scrolling colour waveform. Every column is a 16 bit big endian
// value: three bits of red, three of green, three of blue and five of height.
Section makeColorDetailSection(const QByteArray& peaks) {
    const int maximum = peakMaximum(peaks);

    QByteArray body;
    putU32(body, 2);
    putU32(body, static_cast<quint32>(peaks.size()));
    putU32(body, 0x00960305);
    for (const char value : peaks) {
        const int amplitude = static_cast<quint8>(value);
        const int relative = maximum > 0 ? amplitude : 0;
        const int height = qBound(0, (31 * relative * relative) / (maximum * maximum), 31);
        const quint16 packed = static_cast<quint16>((4 << 13) | (2 << 10) | (6 << 7)
                | (height << 2));
        putU16(body, packed);
    }
    return makeSection("PWV5", 0x18, body);
}

// PWV6: the three band preview of the CDJ-3000.
Section makeThreeBandPreviewSection(const QByteArray& peaks) {
    const QByteArray columns = resample(peaks, kThreeBandPreviewColumns);
    const int maximum = peakMaximum(columns);

    QByteArray body;
    putU32(body, 3);
    putU32(body, kThreeBandPreviewColumns);
    for (int i = 0; i < columns.size(); ++i) {
        const int value = static_cast<quint8>(columns.at(i));
        const int height = maximum > 0 ? (value * 31) / maximum : 0;
        body.append(char(height)); // mid
        body.append(char(height / 2)); // high
        body.append(char(height)); // low
    }
    return makeSection("PWV6", 0x14, body);
}

// PWV7: the scrolling three band waveform of the CDJ-3000.
Section makeThreeBandDetailSection(const QByteArray& peaks) {
    const int maximum = peakMaximum(peaks);

    QByteArray body;
    putU32(body, 3);
    putU32(body, static_cast<quint32>(peaks.size()));
    putU32(body, 0x00960000);
    for (const char value : peaks) {
        const int amplitude = static_cast<quint8>(value);
        const int relative = maximum > 0 ? amplitude : 0;
        const int height = qBound(0, (31 * relative * relative) / (maximum * maximum), 31);
        body.append(char(height));
        body.append(char(height / 2));
        body.append(char(height));
    }
    return makeSection("PWV7", 0x18, body);
}

// --- Cue lists ---------------------------------------------------------------

// Splits the cues of a track into the hot cue list and the memory cue list.
void splitCues(const QList<core::Cue>& cues, QList<core::Cue>& hot,
               QList<core::Cue>& memory) {
    for (const core::Cue& cue : cues) {
        if (cue.kind == core::CueKind::HotCue) {
            hot.append(cue);
        } else {
            memory.append(cue);
        }
    }
}

// PCOB: the classic cue list, with the PCPT entries.
Section makeCueListSection(quint32 type, QList<core::Cue> cues) {
    // The cues are sorted by time; the players do the same.
    std::sort(cues.begin(), cues.end(),
            [](const core::Cue& a, const core::Cue& b) { return a.time < b.time; });

    QByteArray body;
    putU32(body, type);
    putU16(body, 0);
    putU16(body, static_cast<quint16>(cues.size()));
    putU32(body, type == kMemoryCues ? static_cast<quint32>(cues.size()) : 0xffffffffU);

    for (int i = 0; i < cues.size(); ++i) {
        const core::Cue& cue = cues.at(i);
        body.append("PCPT", 4);
        putU32(body, 28); // len_header
        putU32(body, 56); // len_entry
        putU32(body, static_cast<quint32>(cue.hotCueNumber));
        putU32(body, 0); // status
        putU32(body, 0x00100000);
        putU16(body, static_cast<quint16>(i == 0 ? 0xffff : i - 1));
        putU16(body, static_cast<quint16>(i == cues.size() - 1 ? 0xffff : i + 1));
        putU8(body, cue.kind == core::CueKind::Loop ? kCueLoop : kCuePoint);
        putU8(body, 0x00);
        putU16(body, 1000);
        putU32(body, static_cast<quint32>(qMax(0, qRound(cue.time * 1000.0))));
        quint32 loopTime = static_cast<quint32>(qMax(0, qRound(cue.loopTime * 1000.0)));
        if (cue.kind != core::CueKind::Loop) {
            loopTime = 0xffffffffU;
        }
        putU32(body, loopTime);
        for (int j = 0; j < 16; ++j) {
            putU8(body, 0);
        }
    }
    return makeSection("PCOB", 0x18, body);
}

// PCO2: the extended cue list, with the PCP2 entries. It adds the colour of
// the memory cues, the comments and the colour of the hot cues.
Section makeExtendedCueListSection(quint32 type, QList<core::Cue> cues) {
    std::sort(cues.begin(), cues.end(),
            [](const core::Cue& a, const core::Cue& b) { return a.time < b.time; });

    QByteArray body;
    putU32(body, type);
    putU16(body, static_cast<quint16>(cues.size()));
    putU16(body, 0);

    for (const core::Cue& cue : cues) {
        const QByteArray comment = cue.comment.isEmpty() ? QByteArray() : utf16Be(cue.comment);

        QByteArray entry;
        entry.append("PCP2", 4);
        putU32(entry, 16); // len_header
        putU32(entry, 0); // len_entry, patched below
        putU32(entry, static_cast<quint32>(cue.hotCueNumber));
        putU8(entry, cue.kind == core::CueKind::Loop ? kCueLoop : kCuePoint);
        putU8(entry, 0x00);
        putU16(entry, 1000);
        putU32(entry, static_cast<quint32>(qMax(0, qRound(cue.time * 1000.0))));
        putU32(entry, static_cast<quint32>(qMax(0, qRound(cue.loopTime * 1000.0))));
        putU8(entry, static_cast<quint8>(qBound(0, cue.colorId, 255)));
        for (int i = 0; i < 7; ++i) {
            putU8(entry, i == 0 ? 0x01 : 0x00);
        }
        putU16(entry, 0); // loop numerator
        putU16(entry, 0); // loop denominator
        putU32(entry, static_cast<quint32>(comment.size() + (comment.isEmpty() ? 0 : 2)));
        entry.append(comment);
        if (!comment.isEmpty()) {
            putU16(entry, 0); // the trailing NUL of the comment
        }
        putU8(entry, 0); // hot cue colour code
        putU8(entry, 255); // red
        putU8(entry, 0); // green
        putU8(entry, 23); // blue
        for (int i = 0; i < 40; ++i) {
            putU8(entry, 0);
        }

        const quint32 length = static_cast<quint32>(entry.size());
        entry[8] = static_cast<char>((length >> 24) & 0xff);
        entry[9] = static_cast<char>((length >> 16) & 0xff);
        entry[10] = static_cast<char>((length >> 8) & 0xff);
        entry[11] = static_cast<char>(length & 0xff);
        body.append(entry);
    }
    return makeSection("PCO2", 0x14, body);
}

} // namespace

QByteArray AnlzWriter::buildDat(const AnlzData& data) {
    QList<core::Cue> hot;
    QList<core::Cue> memory;
    splitCues(data.cues, hot, memory);

    QList<Section> sections;
    sections.append(makePathSection(data.devicePath));
    sections.append(makeVbrSection());
    if (!data.beats.isEmpty()) {
        sections.append(makeBeatGridSection(data.beats));
    }
    sections.append(makePreviewSection(data.peaks));
    sections.append(makeTinyPreviewSection(data.peaks));
    sections.append(makeCueListSection(kHotCues, hot));
    sections.append(makeCueListSection(kMemoryCues, memory));
    return buildFile(sections);
}

QByteArray AnlzWriter::buildExt(const AnlzData& data) {
    QList<core::Cue> hot;
    QList<core::Cue> memory;
    splitCues(data.cues, hot, memory);

    QList<Section> sections;
    sections.append(makePathSection(data.devicePath));
    if (!data.peaks.isEmpty()) {
        sections.append(makeDetailSection(data.peaks));
    }
    sections.append(makeCueListSection(kHotCues, hot));
    sections.append(makeCueListSection(kMemoryCues, memory));
    sections.append(makeExtendedCueListSection(kHotCues, hot));
    sections.append(makeExtendedCueListSection(kMemoryCues, memory));
    if (!data.peaks.isEmpty()) {
        sections.append(makeColorDetailSection(data.peaks));
        sections.append(makeColorPreviewSection(data.peaks));
    }
    return buildFile(sections);
}

QByteArray AnlzWriter::build2Ex(const AnlzData& data) {
    QList<Section> sections;
    sections.append(makePathSection(data.devicePath));
    if (!data.peaks.isEmpty()) {
        sections.append(makeThreeBandDetailSection(data.peaks));
        sections.append(makeThreeBandPreviewSection(data.peaks));
    }
    return buildFile(sections);
}

QString AnlzWriter::analysisDirectory(const QString& devicePath) {
    // The directory name is a hash of the path of the audio file, computed
    // over its UTF-16 code units.
    quint32 hash = 0;
    for (const QChar c : devicePath) {
        hash = hash * 0x34f5501dU + static_cast<quint32>(c.unicode()) * 0x93b6U;
    }
    hash %= 0x30d43U;

    // Seven bits of the hash select the shard directory.
    quint32 shard = 0;
    shard |= (hash >> 0) & 0x01U;
    shard |= ((hash >> 2) & 0x01U) << 1;
    shard |= ((hash >> 6) & 0x01U) << 2;
    shard |= ((hash >> 7) & 0x01U) << 3;
    shard |= ((hash >> 9) & 0x01U) << 4;
    shard |= ((hash >> 13) & 0x01U) << 5;
    shard |= ((hash >> 16) & 0x01U) << 6;

    return QStringLiteral("/PIONEER/USBANLZ/P%1/%2")
            .arg(shard, 3, 16, QChar('0'))
            .arg(hash, 8, 16, QChar('0'));
}

QString AnlzWriter::analysisPath(const QString& devicePath) {
    return analysisDirectory(devicePath) + QStringLiteral("/ANLZ0000.DAT");
}

} // namespace device
} // namespace opendeck
