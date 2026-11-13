#include <QtTest>
#include <QDir>
#include <QFile>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QTemporaryDir>

#include "export/ExportModel.h"
#include "export/onelibrary/DeviceLibraryWriter.h"
#include "export/onelibrary/SqlCipherWriter.h"

using namespace opendeck;
using namespace opendeck::device;
using namespace opendeck::device::onelibrary;

namespace {

ExportLibrary makeLibrary() {
    ExportLibrary library;
    for (int i = 1; i <= 3; ++i) {
        ExportTrack item;
        item.deviceId = i;
        item.track.title = QStringLiteral("Track %1").arg(i);
        item.track.artist = QStringLiteral("Artist");
        item.track.album = QStringLiteral("Album");
        item.track.genre = QStringLiteral("House");
        item.track.musicalKey = QStringLiteral("8A");
        item.track.bpm = 120.0 + i;
        item.track.duration = 300.0;
        item.track.filePath = QStringLiteral("/music/t%1.mp3").arg(i);
        item.devicePath = QStringLiteral("/Contents/Artist/Album/t%1.mp3").arg(i);
        item.analyzePath =
                QStringLiteral("/PIONEER/USBANLZ/P001/0000000%1/ANLZ0000.DAT").arg(i);
        item.fileName = QStringLiteral("t%1.mp3").arg(i);
        item.fileSize = 1000;
        item.fileType = 1;

        core::Cue cue;
        cue.kind = core::CueKind::HotCue;
        cue.hotCueNumber = 1;
        cue.time = 10.0 + i;
        cue.comment = QStringLiteral("Drop");
        item.cues.append(cue);
        library.tracks.append(item);
    }
    ExportPlaylist playlist;
    playlist.id = 1;
    playlist.name = QStringLiteral("Main Room");
    playlist.trackIds = {1, 2, 3};
    library.playlists.append(playlist);
    return library;
}

} // namespace

class TestOneLibrary : public QObject {
    Q_OBJECT

  private slots:
    void sqlCipherRoundTrip() {
        // A synthetic plain SQLite file with the right page geometry.
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString plainPath = dir.path() + QStringLiteral("/plain.db");
        const QString connection = QStringLiteral("test_sqlcipher_plain");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(plainPath);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral("PRAGMA page_size=4096")));
            QVERIFY(query.exec(QStringLiteral("PRAGMA journal_mode=DELETE")));
            QVERIFY(query.exec(QStringLiteral("CREATE TABLE t(a INTEGER, b TEXT)")));
            for (int i = 0; i < 40; ++i) {
                query.prepare(QStringLiteral("INSERT INTO t VALUES(?, ?)"));
                query.addBindValue(i);
                query.addBindValue(QStringLiteral("row %1 with some padding text").arg(i));
                QVERIFY(query.exec());
            }
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);

        QFile file(plainPath);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QByteArray plain = file.readAll();
        QCOMPARE(plain.size() % 4096, 0);
        plain[20] = char(SqlCipherWriter::kReservedBytes);
        QVERIFY(file.seek(0));
        QCOMPARE(file.write(plain), plain.size());
        file.close();
        // VACUUM rewrites the pages so the reserved area is really free.
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(plainPath);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral("VACUUM")));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);

        QFile patched(plainPath);
        QVERIFY(patched.open(QIODevice::ReadOnly));
        plain = patched.readAll();
        patched.close();

        const QByteArray salt("0123456789abcdef");
        QString error;
        const QByteArray encrypted =
                SqlCipherWriter::encrypt(plain, salt, &error);
        QVERIFY2(!encrypted.isEmpty(), qPrintable(error));
        QCOMPARE(encrypted.size(), plain.size());
        QVERIFY(encrypted.left(16) != QByteArray("SQLite format 3\0", 16));
        QCOMPARE(encrypted.left(16), salt);

        const QByteArray decrypted =
                SqlCipherWriter::decrypt(encrypted, &error);
        QVERIFY2(!decrypted.isEmpty(), qPrintable(error));
        QCOMPARE(decrypted, plain);

        // A tampered page must be rejected by the HMAC.
        QByteArray tampered = encrypted;
        tampered[100] = char(static_cast<quint8>(tampered[100]) ^ 0x40);
        QVERIFY(SqlCipherWriter::decrypt(tampered, &error).isEmpty());
    }

    void writesAnEncryptedDeviceLibrary() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString root = dir.path() + QStringLiteral("/usb");
        QVERIFY(QDir().mkpath(root + QStringLiteral("/PIONEER/rekordbox")));
        const QString databasePath = root + QStringLiteral("/PIONEER/rekordbox/exportLibrary.db");

        QString error;
        QVERIFY2(DeviceLibraryWriter::write(
                         databasePath, makeLibrary(), QStringLiteral("2026-11-13"), &error),
                qPrintable(error));
        QVERIFY(QFile::exists(databasePath));

        QFile encrypted(databasePath);
        QVERIFY(encrypted.open(QIODevice::ReadOnly));
        const QByteArray bytes = encrypted.readAll();
        encrypted.close();
        QCOMPARE(bytes.size() % 4096, 0);
        QVERIFY(bytes.left(16) != QByteArray("SQLite format 3\0", 16));
        QVERIFY(!QFile::exists(databasePath + QStringLiteral(".plain")));

        // Decrypt and open it as a normal SQLite database.
        const QString decryptedPath = dir.path() + QStringLiteral("/decrypted.db");
        QVERIFY2(SqlCipherWriter::decryptFile(
                         databasePath, decryptedPath, &error),
                qPrintable(error));

        const QString connection = QStringLiteral("test_dlp_decrypted");
        {
            QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            db.setDatabaseName(decryptedPath);
            QVERIFY2(db.open(), qPrintable(db.lastError().text()));
            QSqlQuery query(db);
            QVERIFY(query.exec(QStringLiteral("SELECT count(*) FROM content")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 3);
            QVERIFY(query.exec(QStringLiteral("SELECT title, bpmx100 FROM content "
                                              "WHERE content_id = 1")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), QStringLiteral("Track 1"));
            QCOMPARE(query.value(1).toInt(), 12100);

            QVERIFY(query.exec(QStringLiteral("SELECT count(*) FROM playlist")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 1);
            QVERIFY(query.exec(QStringLiteral("SELECT count(*) FROM playlist_content")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 3);

            QVERIFY(query.exec(QStringLiteral("SELECT dbVersion, numberOfContents "
                                              "FROM property")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), QStringLiteral("1000"));
            QCOMPARE(query.value(1).toInt(), 3);

            QVERIFY(query.exec(QStringLiteral("SELECT count(*) FROM menuItem")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 27);
            QVERIFY(query.exec(QStringLiteral("SELECT count(*) FROM category")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 22);
            QVERIFY(query.exec(QStringLiteral("SELECT count(*) FROM sort")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 17);

            QVERIFY(query.exec(QStringLiteral("SELECT count(*) FROM cue")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toInt(), 3);

            // The full set of OneLibrary tables is present.
            const QStringList expected = {
                    "content", "artist", "album", "genre", "label", "key", "color",
                    "image", "cue", "playlist", "playlist_content", "history",
                    "history_content", "hotCueBankList", "hotCueBankList_cue",
                    "myTag", "myTag_content", "menuItem", "category", "sort",
                    "property", "recommendedLike"};
            for (const QString& table : expected) {
                QVERIFY2(query.exec(QStringLiteral("SELECT count(*) FROM %1").arg(table)),
                        qPrintable(table));
            }
            QVERIFY(query.exec(QStringLiteral("PRAGMA integrity_check")));
            QVERIFY(query.next());
            QCOMPARE(query.value(0).toString(), QStringLiteral("ok"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
};

QTEST_MAIN(TestOneLibrary)
#include "test_onelibrary.moc"
