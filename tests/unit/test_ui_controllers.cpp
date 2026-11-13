#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QSignalSpy>

#include "core/models/playlist.h"
#include "library/DatabaseManager.h"
#include "library/PlaylistRepository.h"
#include "library/TrackRepository.h"
#include "ui/LibraryController.h"
#include "ui/PlaylistController.h"

using namespace opendeck;

// Regression test: the counts exposed to QML must be *properties*. A
// Q_INVOKABLE method read without parentheses evaluates to a JS function in a
// binding, so `library.trackCount > 0` is always false and the track table
// never appears.
class TestUiControllers : public QObject {
    Q_OBJECT

  private:
    struct Fixture {
        QTemporaryDir dir;
        std::unique_ptr<library::DatabaseManager> db;
        std::unique_ptr<ui::LibraryController> library;
        std::unique_ptr<ui::PlaylistController> playlists;
    };

    std::unique_ptr<Fixture> makeFixture() {
        auto f = std::make_unique<Fixture>();
        if (!f->dir.isValid()) {
            return nullptr;
        }
        f->db = std::make_unique<library::DatabaseManager>(f->dir.path()
                + QStringLiteral("/library.db"));
        QString error;
        if (!f->db->open(&error)) {
            return nullptr;
        }

        library::TrackRepository tracks(*f->db);
        core::Track track;
        track.title = QStringLiteral("Song One");
        track.artist = QStringLiteral("Artist");
        // A real file is needed: the exporter copies it to /Contents.
        const QString audioPath = f->dir.path() + QStringLiteral("/a.mp3");
        QFile audio(audioPath);
        if (!audio.open(QIODevice::WriteOnly) || audio.write("ID3 fake audio") < 0) {
            return nullptr;
        }
        audio.close();
        track.filePath = audioPath;
        track.fileHash = QStringLiteral("hash-1");
        const qint64 trackId = tracks.insertTrack(track);
        if (trackId <= 0) {
            return nullptr;
        }

        library::PlaylistRepository repository(*f->db);
        core::Playlist playlist;
        playlist.name = QStringLiteral("Main Room");
        playlist.trackIds = {trackId};
        if (repository.savePlaylist(playlist) <= 0) {
            return nullptr;
        }

        f->library = std::make_unique<ui::LibraryController>(*f->db);
        f->playlists = std::make_unique<ui::PlaylistController>(*f->db);
        return f;
    }

  private slots:
    void countsAreProperties() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        QCOMPARE(f->library->property("trackCount").toInt(), 1);
        QCOMPARE(f->playlists->property("playlistCount").toInt(), 1);
        QCOMPARE(f->playlists->property("totalTrackCount").toInt(), 1);
        QCOMPARE(f->playlists->rowCount(), 1);
    }

    void qmlSeesNumbersNotFunctions() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);

        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("library"),
                f->library.get());
        engine.rootContext()->setContextProperty(QStringLiteral("playlists"),
                f->playlists.get());

        QQmlComponent component(&engine);
        component.setData(R"QML(
            import QtQml
            QtObject {
                property int tracks: library.trackCount
                property int lists: playlists.playlistCount
                property int total: playlists.totalTrackCount
                property bool tableVisible: library.trackCount > 0
                property bool emptyVisible: library.trackCount === 0
            }
        )QML", QUrl(QStringLiteral("qrc:/probe.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));

        std::unique_ptr<QObject> object(component.create());
        QVERIFY(object != nullptr);
        QCOMPARE(object->property("tracks").toInt(), 1);
        QCOMPARE(object->property("lists").toInt(), 1);
        QCOMPARE(object->property("total").toInt(), 1);
        QVERIFY(object->property("tableVisible").toBool());
        QVERIFY(!object->property("emptyVisible").toBool());
    }

    void countsUpdateWhenAPlaylistChanges() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        QSignalSpy spy(f->playlists.get(), &ui::PlaylistController::playlistsChanged);

        const qint64 id = f->playlists->createPlaylist(QStringLiteral("Second"));
        QVERIFY(id > 0);
        QCOMPARE(f->playlists->property("playlistCount").toInt(), 2);
        QCOMPARE(spy.count(), 1);

        QVERIFY(f->playlists->addTrack(id, 1));
        QCOMPARE(f->playlists->property("totalTrackCount").toInt(), 2);
    }

    void exportAcceptsBothPathAndUrl() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        const QString target = f->dir.path() + QStringLiteral("/usb");
        QVERIFY(QDir().mkpath(target));

        // A plain path works (the tests and other callers use this form).
        QVERIFY(f->playlists->validate(target).length() > 0); // nothing exported yet
        QVERIFY(f->playlists->exportTo(target).isEmpty());

        QVERIFY(QFile::exists(target + QStringLiteral("/PIONEER/rekordbox/export.pdb")));
        // A QUrl, which is what the QML FolderDialog returns, also works.
        QVERIFY(f->playlists->exportTo(QUrl::fromLocalFile(target)).isEmpty());
        QVERIFY(f->playlists->validate(QUrl::fromLocalFile(target)).isEmpty());
    }
};

QTEST_MAIN(TestUiControllers)
#include "test_ui_controllers.moc"
