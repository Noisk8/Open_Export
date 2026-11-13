#include <QtTest>

#include "export/anlz/AnlzWriter.h"

using namespace opendeck;

namespace {

quint32 readU32BE(const QByteArray& data, int offset) {
    return (static_cast<quint32>(static_cast<quint8>(data.at(offset))) << 24)
            | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 1))) << 16)
            | (static_cast<quint32>(static_cast<quint8>(data.at(offset + 2))) << 8)
            | static_cast<quint32>(static_cast<quint8>(data.at(offset + 3)));
}

quint16 readU16BE(const QByteArray& data, int offset) {
    return static_cast<quint16>((static_cast<quint8>(data.at(offset)) << 8)
            | static_cast<quint8>(data.at(offset + 1)));
}

struct Section {
    QString fourcc;
    quint32 headerSize = 0;
    quint32 totalSize = 0;
    QByteArray body;
};

QList<Section> sections(const QByteArray& data) {
    QList<Section> result;
    const quint32 headerSize = readU32BE(data, 4);
    const quint32 fileSize = readU32BE(data, 8);
    int offset = static_cast<int>(headerSize);
    while (offset + 12 <= static_cast<int>(fileSize)) {
        Section section;
        section.fourcc = QString::fromLatin1(data.mid(offset, 4));
        section.headerSize = readU32BE(data, offset + 4);
        section.totalSize = readU32BE(data, offset + 8);
        section.body = data.mid(offset + 12, static_cast<int>(section.totalSize) - 12);
        result.append(section);
        offset += static_cast<int>(section.totalSize);
    }
    return result;
}

device::AnlzData makeData() {
    device::AnlzData data;
    data.devicePath = QStringLiteral("/Contents/Artist/Album/song.mp3");
    data.duration = 300.0;

    core::Beat beat;
    beat.number = 1;
    beat.time = 0.5;
    beat.tempo = 128.0;
    data.beats.append(beat);
    beat.number = 2;
    beat.time = 0.968;
    data.beats.append(beat);

    core::Cue hotCue;
    hotCue.kind = core::CueKind::HotCue;
    hotCue.hotCueNumber = 1;
    hotCue.time = 10.0;
    data.cues.append(hotCue);

    core::Cue memoryCue;
    memoryCue.kind = core::CueKind::MemoryCue;
    memoryCue.time = 20.0;
    data.cues.append(memoryCue);

    QByteArray peaks;
    peaks.resize(45000); // 150 columns per second for five minutes
    for (int i = 0; i < peaks.size(); ++i) {
        peaks[i] = static_cast<char>(i % 256);
    }
    data.peaks = peaks;
    return data;
}

} // namespace

class TestAnlz : public QObject {
    Q_OBJECT

  private slots:
    void fileHeader() {
        const QByteArray data = device::AnlzWriter::buildDat(makeData());
        QCOMPARE(data.size() > 28, true);
        QCOMPARE(data.mid(0, 4), QByteArray("PMAI"));
        QCOMPARE(readU32BE(data, 4), 28u); // length of the header
        QCOMPARE(readU32BE(data, 8), static_cast<quint32>(data.size()));
    }

    void datSections() {
        const QList<Section> list = sections(device::AnlzWriter::buildDat(makeData()));
        QStringList names;
        for (const Section& section : list) {
            names.append(section.fourcc);
        }
        QCOMPARE(names, QStringList({"PPTH", "PVBR", "PQTZ", "PWAV", "PWV2", "PCOB", "PCOB"}));
    }

    void extSections() {
        const QList<Section> list = sections(device::AnlzWriter::buildExt(makeData()));
        QStringList names;
        for (const Section& section : list) {
            names.append(section.fourcc);
        }
        QCOMPARE(names,
                QStringList({"PPTH", "PWV3", "PCOB", "PCOB", "PCO2", "PCO2", "PWV5", "PWV4"}));
    }

    void twoExSections() {
        const QList<Section> list = sections(device::AnlzWriter::build2Ex(makeData()));
        QStringList names;
        for (const Section& section : list) {
            names.append(section.fourcc);
        }
        QCOMPARE(names, QStringList({"PPTH", "PWV7", "PWV6"}));
    }

    void pathSection() {
        const QList<Section> list = sections(device::AnlzWriter::buildDat(makeData()));
        const Section& path = list.first();
        QCOMPARE(path.fourcc, QStringLiteral("PPTH"));
        const quint32 length = readU32BE(path.body, 0);
        const QString expected = QStringLiteral("/Contents/Artist/Album/song.mp3");
        // The path is UTF-16 big endian with a trailing NUL.
        QCOMPARE(length, static_cast<quint32>(2 * expected.size() + 2));
        QByteArray expectedBytes;
        for (const QChar c : expected) {
            expectedBytes.append(char((c.unicode() >> 8) & 0xff));
            expectedBytes.append(char(c.unicode() & 0xff));
        }
        expectedBytes.append(char(0));
        expectedBytes.append(char(0));
        QCOMPARE(path.body.mid(4), expectedBytes);
    }

    void beatGridSection() {
        const QList<Section> list = sections(device::AnlzWriter::buildDat(makeData()));
        const Section& grid = list.at(2);
        QCOMPARE(grid.fourcc, QStringLiteral("PQTZ"));
        QCOMPARE(readU32BE(grid.body, 0), 0u);
        QCOMPARE(readU32BE(grid.body, 4), 0x00080000u);
        QCOMPARE(readU32BE(grid.body, 8), 2u); // two beats
        QCOMPARE(readU16BE(grid.body, 12), 1); // beat number
        QCOMPARE(readU16BE(grid.body, 14), 12800); // tempo multiplied by a hundred
        QCOMPARE(readU32BE(grid.body, 16), 500u); // time in milliseconds
    }

    void cueSections() {
        const QList<Section> list = sections(device::AnlzWriter::buildDat(makeData()));
        const Section& hot = list.at(5);
        const Section& memory = list.at(6);
        QCOMPARE(readU32BE(hot.body, 0), 1u); // hot cues
        QCOMPARE(readU32BE(memory.body, 0), 0u); // memory cues
        QCOMPARE(readU16BE(hot.body, 6), 1); // one cue
        QCOMPARE(readU16BE(memory.body, 6), 1);
        QCOMPARE(hot.body.mid(12, 4), QByteArray("PCPT"));
        QCOMPARE(readU32BE(hot.body, 44), 10000u); // the cue time in milliseconds
    }

    void previewIsNotEmpty() {
        const QList<Section> list = sections(device::AnlzWriter::buildDat(makeData()));
        const Section& preview = list.at(3);
        QCOMPARE(preview.fourcc, QStringLiteral("PWAV"));
        QCOMPARE(readU32BE(preview.body, 0), 400u);
        // A player reads a zero column as an unfinished analysis.
        bool allZero = true;
        for (int i = 8; i < preview.body.size(); ++i) {
            if (preview.body.at(i) != 0) {
                allZero = false;
                break;
            }
        }
        QVERIFY(!allZero);
    }

    void analysisDirectoryIsStable() {
        const QString path = QStringLiteral("/Contents/Artist/Album/song.mp3");
        const QString directory = device::AnlzWriter::analysisDirectory(path);
        QVERIFY(directory.startsWith(QStringLiteral("/PIONEER/USBANLZ/P")));
        QCOMPARE(directory, device::AnlzWriter::analysisDirectory(path));
        QVERIFY(directory.endsWith(QStringLiteral("/ANLZ0000.DAT")) == false);
        QCOMPARE(device::AnlzWriter::analysisPath(path),
                directory + QStringLiteral("/ANLZ0000.DAT"));
    }
};

QTEST_MAIN(TestAnlz)
#include "test_anlz.moc"
