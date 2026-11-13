#include <QtTest>

#include "core/models/playlist.h"
#include "export/devicesql/DeviceSqlReader.h"
#include "export/devicesql/DeviceSqlString.h"
#include "export/devicesql/DeviceSqlWriter.h"

using namespace opendeck;

namespace {

device::ExportTrack makeTrack(quint32 id, const QString& title, const QString& artist) {
    device::ExportTrack item;
    item.deviceId = id;
    item.track.id = id;
    item.track.title = title;
    item.track.artist = artist;
    item.track.album = QStringLiteral("An Album");
    item.track.genre = QStringLiteral("House");
    item.track.label = QStringLiteral("A Label");
    item.track.comment = QStringLiteral("A comment");
    item.track.isrc = QStringLiteral("ESABC1234567");
    item.track.mixName = QStringLiteral("Club Mix");
    item.track.year = 2024;
    item.track.duration = 321.5;
    item.track.bpm = 128.25;
    item.track.musicalKey = QStringLiteral("8A");
    item.track.sampleRate = 44100;
    item.track.sampleDepth = 16;
    item.track.bitrate = 320;
    item.track.trackNumber = 3;
    item.track.discNumber = 1;
    item.track.rating = 4;
    item.track.colorId = 2;
    item.track.filePath = QStringLiteral("/music/song.mp3");
    item.fileName = QStringLiteral("song.mp3");
    item.devicePath = QStringLiteral("/Contents/Artist/An Album/song.mp3");
    item.analyzePath = QStringLiteral("/PIONEER/USBANLZ/P001/00000001/ANLZ0000.DAT");
    item.fileSize = 1024 * 1024;
    item.fileType = 1;
    return item;
}

} // namespace

class TestDeviceSql : public QObject {
    Q_OBJECT

  private slots:
    // --- The DeviceSQL strings ---------------------------------------------

    void shortAsciiString() {
        const device::DeviceSqlString value = device::DeviceSqlString::text(QStringLiteral("abc"));
        QCOMPARE(value.bytes().size(), 4);
        QCOMPARE(static_cast<quint8>(value.bytes().at(0)), 9); // (4 << 1) | 1
        QCOMPARE(value.bytes().mid(1), QByteArray("abc"));
        QVERIFY(!value.isLong());
    }

    void emptyString() {
        const device::DeviceSqlString value = device::DeviceSqlString::text(QString());
        QCOMPARE(value.bytes().size(), 1);
        QCOMPARE(static_cast<quint8>(value.bytes().at(0)), 3);
    }

    void longAsciiString() {
        const QString text(200, QLatin1Char('x'));
        const device::DeviceSqlString value = device::DeviceSqlString::text(text);
        QVERIFY(value.isLong());
        QCOMPARE(static_cast<quint8>(value.bytes().at(0)), 0x40);
        const quint16 length = static_cast<quint8>(value.bytes().at(1))
                | (static_cast<quint16>(static_cast<quint8>(value.bytes().at(2))) << 8);
        QCOMPARE(length, static_cast<quint16>(204));
        QCOMPARE(value.bytes().size(), 204);
    }

    void utf16String() {
        const device::DeviceSqlString value =
                device::DeviceSqlString::text(QString::fromUtf8("Rekordbåks"));
        QVERIFY(value.isLong());
        QCOMPARE(static_cast<quint8>(value.bytes().at(0)), 0x90);
        const quint16 length = static_cast<quint8>(value.bytes().at(1))
                | (static_cast<quint16>(static_cast<quint8>(value.bytes().at(2))) << 8);
        QCOMPARE(length, static_cast<quint16>(4 + 2 * 10));
    }

    void isrcString() {
        const device::DeviceSqlString value =
                device::DeviceSqlString::isrc(QStringLiteral("ESABC1234567"));
        QCOMPARE(static_cast<quint8>(value.bytes().at(0)), 0x90);
        QCOMPARE(static_cast<quint8>(value.bytes().at(4)), 0x03);
        QCOMPARE(value.bytes().mid(5, 12), QByteArray("ESABC1234567"));
        QCOMPARE(static_cast<quint8>(value.bytes().at(17)), 0x00);
    }

    // --- The page layout ----------------------------------------------------

    void pageHeaderOfAnIndexPage() {
        device::DeviceSqlIndexPage page(7);
        page.setPageIndex(3);
        const QByteArray data = page.serialize();
        QCOMPARE(data.size(), 4096);
        QCOMPARE(static_cast<quint8>(data.at(0x1b)), 0x64); // index page
        QCOMPARE(static_cast<quint32>(data.at(0x04) | (data.at(0x05) << 8)
                                      | (data.at(0x06) << 16) | (data.at(0x07) << 24)),
                3u);
        const quint16 magic = static_cast<quint16>(static_cast<quint8>(data.at(0x24)))
                | static_cast<quint16>(static_cast<quint8>(data.at(0x25)) << 8);
        QCOMPARE(magic, static_cast<quint16>(0x03ec));
        // Every index slot of a fresh table is empty.
        const quint32 entry = static_cast<quint8>(data.at(0x3c))
                | (static_cast<quint32>(static_cast<quint8>(data.at(0x3d))) << 8)
                | (static_cast<quint32>(static_cast<quint8>(data.at(0x3e))) << 16)
                | (static_cast<quint32>(static_cast<quint8>(data.at(0x3f))) << 24);
        QCOMPARE(entry, 0x1ffffff8u);
    }

    void rowIndexOfADataPage() {
        device::DeviceSqlDataPage page(0);
        QByteArray row(16, char(0x41));
        QVERIFY(page.canFit(row));
        page.appendRow(row);
        page.appendRow(row);
        const QByteArray data = page.serialize();

        QCOMPARE(static_cast<quint8>(data.at(0x1b)), 0x24); // data page
        // Two slots: 13 bits of slots and 11 bits of present rows.
        const quint32 counts = static_cast<quint8>(data.at(0x18))
                | (static_cast<quint32>(static_cast<quint8>(data.at(0x19))) << 8)
                | (static_cast<quint32>(static_cast<quint8>(data.at(0x1a))) << 16);
        QCOMPARE(counts & 0x1fffU, 2u);
        QCOMPARE((counts >> 13) & 0x7ffU, 2u);

        // The presence flags of the last group sit right before the end.
        const quint16 presence = static_cast<quint8>(data.at(4092))
                | (static_cast<quint16>(static_cast<quint8>(data.at(4093))) << 8);
        QCOMPARE(presence, 3);

        // The heap offset of the first row is zero, the second one follows.
        const quint16 first = static_cast<quint8>(data.at(4090))
                | (static_cast<quint16>(static_cast<quint8>(data.at(4091))) << 8);
        const quint16 second = static_cast<quint8>(data.at(4088))
                | (static_cast<quint16>(static_cast<quint8>(data.at(4089))) << 8);
        QCOMPARE(first, 0);
        QCOMPARE(second, 16);
    }

    // --- The whole database -------------------------------------------------

    void writesTheTwentyTables() {
        device::ExportLibrary library;
        library.tracks.append(makeTrack(1, QStringLiteral("Song"), QStringLiteral("Artist")));
        device::ExportPlaylist playlist;
        playlist.id = 1;
        playlist.name = QStringLiteral("My Playlist");
        playlist.trackIds = {1};
        library.playlists.append(playlist);

        device::DeviceSqlWriter writer;
        writer.setLibrary(library);
        const QByteArray data = writer.build();

        const device::DeviceSqlReader reader(data);
        QVERIFY2(reader.isValid(), qPrintable(reader.errorString()));
        QCOMPARE(reader.tableCount(), 20);
        for (int i = 0; i < 20; ++i) {
            QCOMPARE(reader.tableType(i), static_cast<quint32>(i));
        }
        // The file header announces twenty tables and a page size of 4096.
        QCOMPARE(static_cast<quint32>(data.at(0x04) | (data.at(0x05) << 8)
                                      | (data.at(0x06) << 16) | (data.at(0x07) << 24)),
                4096u);
        QCOMPARE(data.size() % 4096, 0);
    }

    void writesTheTrackMetadata() {
        device::ExportLibrary library;
        library.tracks.append(makeTrack(1, QStringLiteral("Song"), QStringLiteral("Artist")));

        device::DeviceSqlWriter writer;
        writer.setLibrary(library);
        const device::DeviceSqlReader reader(writer.build());
        QVERIFY(reader.isValid());

        const QList<QByteArray> tracks = reader.rows(0);
        QCOMPARE(tracks.size(), 1);
        const QByteArray& row = tracks.first();

        QCOMPARE(device::DeviceSqlReader::readSlotString(row, 0x5e + 2 * 17),
                QStringLiteral("Song")); // title
        QCOMPARE(device::DeviceSqlReader::readSlotString(row, 0x5e + 2 * 20),
                QStringLiteral("/Contents/Artist/An Album/song.mp3")); // file path
        QCOMPARE(device::DeviceSqlReader::readSlotString(row, 0x5e + 2 * 19),
                QStringLiteral("song.mp3")); // file name
        QCOMPARE(device::DeviceSqlReader::readSlotString(row, 0x5e + 2 * 14),
                QStringLiteral("/PIONEER/USBANLZ/P001/00000001/ANLZ0000.DAT"));
        QCOMPARE(device::DeviceSqlReader::readSlotString(row, 0x5e + 2 * 6),
                QStringLiteral("ON")); // kuvo public
        QCOMPARE(device::DeviceSqlReader::readSlotString(row, 0x5e + 2 * 7),
                QStringLiteral("ON")); // autoload hot cues

        const quint32 id = static_cast<quint8>(row.at(0x48))
                | (static_cast<quint32>(static_cast<quint8>(row.at(0x49))) << 8)
                | (static_cast<quint32>(static_cast<quint8>(row.at(0x4a))) << 16)
                | (static_cast<quint32>(static_cast<quint8>(row.at(0x4b))) << 24);
        QCOMPARE(id, 1u);
        const quint32 tempo = static_cast<quint8>(row.at(0x38))
                | (static_cast<quint32>(static_cast<quint8>(row.at(0x39))) << 8)
                | (static_cast<quint32>(static_cast<quint8>(row.at(0x3a))) << 16)
                | (static_cast<quint32>(static_cast<quint8>(row.at(0x3b))) << 24);
        QCOMPARE(tempo, 12825u); // BPM multiplied by a hundred
        auto u16 = [](const QByteArray& data, int offset) {
            return static_cast<quint16>(static_cast<quint8>(data.at(offset)))
                    | static_cast<quint16>(static_cast<quint8>(data.at(offset + 1)) << 8);
        };
        QCOMPARE(u16(row, 0x50), static_cast<quint16>(2024)); // year
        QCOMPARE(u16(row, 0x54), static_cast<quint16>(322)); // duration
        QCOMPARE(u16(row, 0x5a), static_cast<quint16>(1)); // mp3
        QCOMPARE(static_cast<quint8>(row.at(0x59)), 4); // rating
    }

    void internsTheSmallTables() {
        device::ExportLibrary library;
        library.tracks.append(makeTrack(1, QStringLiteral("Song"), QStringLiteral("Artist")));

        device::DeviceSqlWriter writer;
        writer.setLibrary(library);
        const device::DeviceSqlReader reader(writer.build());
        QVERIFY(reader.isValid());

        const QList<QByteArray> artists = reader.rows(2);
        QCOMPARE(artists.size(), 1);
        QCOMPARE(device::DeviceSqlReader::readString(artists.first(), 10),
                QStringLiteral("Artist"));

        const QList<QByteArray> albums = reader.rows(3);
        QCOMPARE(albums.size(), 1);
        QCOMPARE(device::DeviceSqlReader::readString(albums.first(), 22),
                QStringLiteral("An Album"));

        const QList<QByteArray> genres = reader.rows(1);
        QCOMPARE(genres.size(), 1);
        QCOMPARE(device::DeviceSqlReader::readString(genres.first(), 4),
                QStringLiteral("House"));

        const QList<QByteArray> keys = reader.rows(5);
        QCOMPARE(keys.size(), 1);
        QCOMPARE(device::DeviceSqlReader::readString(keys.first(), 8), QStringLiteral("8A"));

        // The eight colour labels are always present.
        QCOMPARE(reader.rows(6).size(), 8);
    }

    void writesThePlaylistTree() {
        device::ExportLibrary library;
        library.tracks.append(makeTrack(1, QStringLiteral("A"), QStringLiteral("Artist")));
        library.tracks.append(makeTrack(2, QStringLiteral("B"), QStringLiteral("Artist")));

        // A folder with a playlist inside it, plus a playlist at the root.
        device::ExportPlaylist folder;
        folder.id = 1;
        folder.name = QStringLiteral("Folder");
        folder.isFolder = true;
        library.playlists.append(folder);

        device::ExportPlaylist nested;
        nested.id = 2;
        nested.name = QStringLiteral("Nested");
        nested.parentId = 1;
        nested.trackIds = {1, 2, 1}; // a track may be repeated
        library.playlists.append(nested);

        device::ExportPlaylist root;
        root.id = 3;
        root.name = QStringLiteral("Root");
        root.trackIds = {2};
        library.playlists.append(root);

        device::DeviceSqlWriter writer;
        writer.setLibrary(library);
        const device::DeviceSqlReader reader(writer.build());
        QVERIFY(reader.isValid());

        const QList<QByteArray> nodes = reader.rows(7);
        QCOMPARE(nodes.size(), 3);
        QCOMPARE(device::DeviceSqlReader::readString(nodes.at(0), 20),
                QStringLiteral("Folder"));
        QCOMPARE(device::DeviceSqlReader::readString(nodes.at(1), 20),
                QStringLiteral("Nested"));
        QCOMPARE(device::DeviceSqlReader::readString(nodes.at(2), 20),
                QStringLiteral("Root"));

        // The parent of the nested playlist is the folder, the root one has
        // no parent.
        const quint32 parentOfNested = static_cast<quint8>(nodes.at(1).at(0x00))
                | (static_cast<quint32>(static_cast<quint8>(nodes.at(1).at(0x01))) << 8)
                | (static_cast<quint32>(static_cast<quint8>(nodes.at(1).at(0x02))) << 16)
                | (static_cast<quint32>(static_cast<quint8>(nodes.at(1).at(0x03))) << 24);
        QCOMPARE(parentOfNested, 1u);
        const quint32 parentOfRoot = static_cast<quint8>(nodes.at(2).at(0x00));
        QCOMPARE(parentOfRoot, 0u);
        // The folder is flagged as such.
        const quint32 isFolder = static_cast<quint8>(nodes.at(0).at(0x10));
        QCOMPARE(isFolder, 1u);

        const QList<QByteArray> entries = reader.rows(8);
        QCOMPARE(entries.size(), 4);
        auto entryField = [](const QByteArray& row, int offset) {
            return static_cast<quint32>(static_cast<quint8>(row.at(offset)))
                    | (static_cast<quint32>(static_cast<quint8>(row.at(offset + 1))) << 8)
                    | (static_cast<quint32>(static_cast<quint8>(row.at(offset + 2))) << 16)
                    | (static_cast<quint32>(static_cast<quint8>(row.at(offset + 3))) << 24);
        };
        QCOMPARE(entryField(entries.at(0), 0x00), 1u); // entry index
        QCOMPARE(entryField(entries.at(0), 0x04), 1u); // track
        QCOMPARE(entryField(entries.at(0), 0x08), 2u); // playlist
        QCOMPARE(entryField(entries.at(1), 0x04), 2u);
        QCOMPARE(entryField(entries.at(2), 0x04), 1u); // the repeated track
        QCOMPARE(entryField(entries.at(3), 0x08), 3u); // the root playlist
    }

    void writesTheBrowseMenus() {
        device::DeviceSqlWriter writer;
        writer.setLibrary({});
        const device::DeviceSqlReader reader(writer.build());
        QVERIFY(reader.isValid());
        // The player needs the menus to offer the browse columns.
        QCOMPARE(reader.rows(16).size(), 27);
        QCOMPARE(reader.rows(17).size(), 22);
        QCOMPARE(reader.rows(18).size(), 17);
        QCOMPARE(reader.rows(19).size(), 1);
    }
};

QTEST_MAIN(TestDeviceSql)
#include "test_devicesql.moc"
