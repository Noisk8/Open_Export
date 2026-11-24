#include <QtTest>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QSignalSpy>
#include <QQuickWindow>
#include <QSqlQuery>
#include <QTimer>
#include <QDataStream>
#include "ui/AnalysisController.h"
#include "ui/ConversionController.h"
#include "ui/CueController.h"
#include "ui/DeviceController.h"
#include "ui/ExportController.h"

#include "core/models/playlist.h"
#include "library/DatabaseManager.h"
#include "library/PlaylistRepository.h"
#include "library/TrackRepository.h"
#include "ui/LibraryController.h"
#include "ui/TrackFilterModel.h"
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
    void playlistBrowsingAndEditing() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        ui::TrackFilterModel view(*f->library, *f->playlists);
        const auto first = f->playlists->data(f->playlists->index(0), ui::PlaylistController::IdRole).toLongLong();
        const auto track = f->library->trackIdAt(0);
        const auto second = f->playlists->createPlaylist("Warm up");
        view.setPlaylistId(second);
        QCOMPARE(view.count(), 0);
        QVERIFY(f->playlists->addTrack(second, track));
        QVERIFY(f->playlists->addTrack(second, track));
        QCOMPARE(view.count(), 1);
        QCOMPARE(f->playlists->trackIds(second).size(), 1);
        view.setSearch("ARTIST");
        QCOMPARE(view.count(), 1);
        view.setSearch("absent");
        QCOMPARE(view.count(), 0);
        view.setSearch("");
        QVERIFY(f->playlists->renamePlaylist(second, "Closing"));
        QVERIFY(!f->playlists->renamePlaylist(second, "  "));
        QVERIFY(f->playlists->removeTrack(second, track));
        QCOMPARE(view.count(), 0);
        QCOMPARE(f->library->trackCount(), 1);
        QCOMPARE(f->playlists->trackIds(first).size(), 1);
        QVERIFY(QFile::exists(f->library->data(f->library->index(0), ui::LibraryController::FilePathRole).toString()));
        QVERIFY(!f->playlists->addTrack(99999, track));
        view.setPlaylistId(0);
        QCOMPARE(view.count(), 1);
    }

    void playlistEntriesSurviveDifferentSortOrder() {
        auto f = makeFixture();
        QVERIFY(f != nullptr);
        library::PlaylistRepository repo(*f->db);
        auto first = repo.allPlaylists().first();
        auto second = first;
        second.id = 0;
        second.sortOrder = -1;
        QVERIFY(repo.savePlaylist(second) > 0);
        const auto lists = repo.allPlaylists();
        QCOMPARE(lists.size(), 2);
        QCOMPARE(lists[0].trackIds, first.trackIds);
        QCOMPARE(lists[1].trackIds, first.trackIds);
    }

    void hierarchySelectionAndAtomicEdits() {
        auto f = makeFixture();
        QVERIFY(f);
        auto& lists = *f->playlists;
        const auto folder = lists.createPlaylist("Sets", 0, true);
        const auto nested = lists.createPlaylist("2026", folder, true);
        const auto a = lists.createPlaylist("Warm up", nested);
        const auto b = lists.createPlaylist("Peak", nested);
        QVERIFY(folder && nested && a && b);
        QVERIFY(!lists.movePlaylist(folder, nested));
        QVERIFY(!lists.createPlaylist("Invalid", a));
        QVERIFY(!lists.addTrack(folder, 1));
        QVERIFY(lists.addTrack(a, 1));
        QVERIFY(lists.addTrack(b, 1));
        QCOMPARE(lists.totalTrackCount(), 1);
        auto subset = lists.exportPlaylists({a}, false);
        QCOMPARE(subset.size(), 3); // ancestors included, sibling excluded
        QCOMPARE(lists.exportSummary({folder}, false)["tracks"].toInt(), 1);
        QCOMPARE(lists.exportSummary({folder}, false)["playlists"].toInt(), 2);
        QCOMPARE(lists.exportPlaylists({}, false).size(), 0);
        QVERIFY(lists.movePlaylist(b, nested, a));
        subset = lists.exportPlaylists({nested}, false);
        QCOMPARE(subset[2].id, b);
        QVERIFY(lists.movePlaylist(a, 0));
        library::PlaylistRepository repo(*f->db);
        const auto previous = repo.allPlaylists().first();
        auto invalid = previous;
        invalid.name = "Must roll back";
        invalid.trackIds = {1, 999999};
        QCOMPARE(repo.savePlaylist(invalid), 0);
        QCOMPARE(repo.allPlaylists().first().name, previous.name);
        QCOMPARE(repo.allPlaylists().first().trackIds, previous.trackIds);
        QVERIFY(!lists.editTracks(a, {1, 999999}));
        QCOMPARE(lists.trackIds(a), QList<qint64>{1});
    }

    void removesOnlyConfirmedTracksStillMissing() {
        auto f = makeFixture();
        QVERIFY(f);
        connect(f->library.get(), &ui::LibraryController::tracksRemoved,
                f->playlists.get(), &ui::PlaylistController::reload);
        library::TrackRepository repository(*f->db);
        core::Track track;
        track.filePath = f->dir.filePath("returned.wav");
        track.fileHash = "returned";
        const auto returned = repository.insertTrack(track);
        track.filePath = f->dir.filePath("missing.wav");
        track.fileHash = "missing";
        const auto missing = repository.insertTrack(track);
        QVERIFY(returned > 0 && missing > 0);
        const auto playlist = f->playlists->createPlaylist("Other set");
        QVERIFY(f->playlists->editTracks(playlist, {1, returned, missing}));
        f->library->refresh();
        QCOMPARE(f->library->missingCount(), 2);
        auto confirmed = f->library->missingTrackIds();
        confirmed.append(1);
        QFile recovered(f->dir.filePath("returned.wav"));
        QVERIFY(recovered.open(QIODevice::WriteOnly));
        recovered.close();

        QCOMPARE(f->library->removeMissing(confirmed), 1);
        QCOMPARE(f->library->trackCount(), 2);
        QCOMPARE(f->library->missingCount(), 0);
        QCOMPARE(f->playlists->trackIds(playlist), (QList<qint64>{1, returned}));
        QVERIFY(QFile::exists(recovered.fileName()));
    }

    void fullInterfaceLoadsWithRealModels() {
        auto f = makeFixture();
        QVERIFY(f);
        ui::TrackFilterModel tracks(*f->library, *f->playlists);
        ui::AnalysisController analysis(*f->db);
        ui::ConversionController converter;
        ui::CueController cues(*f->db);
        ui::DeviceController devices;
        ui::ExportController exporter(*f->playlists);
        QQmlEngine engine;
        auto context = engine.rootContext();
        context->setContextProperty("library", f->library.get());
        context->setContextProperty("playlists", f->playlists.get());
        context->setContextProperty("tracks", &tracks);
        context->setContextProperty("analysis", &analysis);
        context->setContextProperty("converter", &converter);
        context->setContextProperty("cues", &cues);
        context->setContextProperty("devices", &devices);
        context->setContextProperty("exporter", &exporter);
        QList<QQmlError> warnings;
        connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& errors) { warnings.append(errors); });
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../../src/ui/qml/Main.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root != nullptr, qPrintable(component.errorString()));
        const auto folder = f->playlists->createPlaylist("Sets", 0, true);
        const auto set = f->playlists->createPlaylist("Warm up", folder);
        QVERIFY(f->playlists->addTrack(set, 1));
        root->setProperty("selectedPlaylistId", set);
        root->setProperty("selectedPlaylistName", "Warm up");
        tracks.setPlaylistId(set);
        tracks.selectVisible();
        auto volume = root->findChild<QObject*>("playbackVolume");
        auto player = root->findChild<QObject*>("previewPlayer");
        QVERIFY(volume && player);
        volume->setProperty("value", 0.35);
        QVERIFY(QMetaObject::invokeMethod(volume, "moved"));
        QVERIFY(qAbs(player->property("volume").toDouble() - 0.35) < 0.001);
        QVERIFY(root->findChild<QObject*>("playPauseButton"));
        QVERIFY(root->findChild<QObject*>("playbackPosition"));
        QTest::qWait(100);
        QVERIFY2(warnings.isEmpty(), warnings.isEmpty() ? "" : qPrintable(warnings.first().toString()));
        if (qEnvironmentVariableIsSet("ODE_UI_SCREENSHOT")) {
            auto window = qobject_cast<QQuickWindow*>(root.get());
            QVERIFY(window);
            window->grabWindow().save(qEnvironmentVariable("ODE_UI_SCREENSHOT"));
            window->resize(1000, 600);
            QTest::qWait(50);
            window->grabWindow().save(qEnvironmentVariable("ODE_UI_SCREENSHOT") + ".small.png");
        }
        exporter.setProperty("allPlaylists", false);
        exporter.setProperty("selectedPlaylists", QVariant::fromValue(QList<qint64>{set}));
        QVERIFY(QMetaObject::invokeMethod(root.get(), "doExport", Q_ARG(QVariant, QVariant(f->dir.path()))));
        QTest::qWait(50);
        QVERIFY2(warnings.isEmpty(), warnings.isEmpty() ? "" : qPrintable(warnings.first().toString()));
        QVERIFY(!exporter.running()); // Reviewing the destination never starts a write.
    }

    void playerPausesSeeksAndControlsVolume() {
        QTemporaryDir dir;
        const QString path = dir.path() + QString::fromUtf8("/audio # prueba á.wav");
        QFile wav(path);
        QVERIFY(wav.open(QIODevice::WriteOnly));
        QDataStream out(&wav);
        out.setByteOrder(QDataStream::LittleEndian);
        const quint32 bytes = 8000 * 2 * 8;
        out.writeRawData("RIFF", 4); out << quint32(36 + bytes);
        out.writeRawData("WAVEfmt ", 8); out << quint32(16) << quint16(1) << quint16(1);
        out << quint32(8000) << quint32(16000) << quint16(2) << quint16(16);
        out.writeRawData("data", 4); out << bytes;
        wav.write(QByteArray(bytes, 0));
        wav.close();
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl::fromLocalFile(QFINDTESTDATA("../../src/ui/qml/PreviewPlayer.qml")));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        std::unique_ptr<QObject> player(component.create());
        QVERIFY2(player != nullptr, qPrintable(component.errorString()));
        QVERIFY(!player->property("hasTrack").toBool());
        player->setProperty("muted", true);
        player->setProperty("volume", 0.25);
        QVERIFY(QMetaObject::invokeMethod(player.get(), "play", Q_ARG(QVariant, QVariant(path)),
            Q_ARG(QVariant, QVariant(2.0)), Q_ARG(QVariant, QVariant("Test song"))));
        QTRY_VERIFY_WITH_TIMEOUT(player->property("duration").toDouble() >= 7.9, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(player->property("playing").toBool(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(player->property("position").toDouble() >= 1.9, 5000);
        QCOMPARE(player->property("trackName").toString(), QString("Test song"));
        QVERIFY(QMetaObject::invokeMethod(player.get(), "togglePlayback"));
        QTRY_VERIFY(player->property("paused").toBool());
        const double pausedAt = player->property("position").toDouble();
        QTest::qWait(150);
        QVERIFY(qAbs(player->property("position").toDouble() - pausedAt) < 0.1);
        QVERIFY(QMetaObject::invokeMethod(player.get(), "seek", Q_ARG(QVariant, QVariant(4.0))));
        QTRY_VERIFY(qAbs(player->property("position").toDouble() - 4) < 0.1);
        QVERIFY(player->property("paused").toBool());
        QCOMPARE(player->property("volume").toDouble(), 0.25);
        QVERIFY(player->property("muted").toBool());
        QVERIFY(QMetaObject::invokeMethod(player.get(), "togglePlayback"));
        QTRY_VERIFY(player->property("playing").toBool());
        QTRY_VERIFY(player->property("position").toDouble() > 4.1);
        QVERIFY(QMetaObject::invokeMethod(player.get(), "stop"));
        QTRY_VERIFY(!player->property("playing").toBool());
        QTRY_COMPARE(player->property("position").toDouble(), 0.0);
        QVERIFY(player->property("hasTrack").toBool());
        QVERIFY(player->property("errorText").toString().isEmpty());
        QVERIFY(QMetaObject::invokeMethod(player.get(), "play", Q_ARG(QVariant, QVariant(dir.path() + "/absent.wav")),
            Q_ARG(QVariant, QVariant(0)), Q_ARG(QVariant, QVariant("Missing"))));
        QTRY_VERIFY_WITH_TIMEOUT(!player->property("errorText").toString().isEmpty(), 5000);
    }

    void asynchronousImportKeepsUiAliveAndDeduplicates() {
        auto f = makeFixture();
        QVERIFY(f);
        const QString path = f->dir.path() + "/new.wav";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream out(&file);
        out.setByteOrder(QDataStream::LittleEndian);
        constexpr quint32 bytes = 4 * 1024 * 1024;
        out.writeRawData("RIFF", 4);
        out << quint32(36 + bytes);
        out.writeRawData("WAVEfmt ", 8);
        out << quint32(16) << quint16(1) << quint16(1) << quint32(44100)
            << quint32(88200) << quint16(2) << quint16(16);
        out.writeRawData("data", 4);
        out << bytes;
        file.write(QByteArray(bytes, 0));
        file.close();
        QSignalSpy done(f->library.get(), &ui::LibraryController::importFinished);
        bool guiTick = false;
        QTimer::singleShot(0, this, [&] { guiTick = true; });
        f->library->importAsync({QUrl::fromLocalFile(path)});
        QVERIFY(f->library->importing());
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 5000);
        QVERIFY(guiTick);
        QVERIFY(!f->library->importing());
        QCOMPARE(f->library->trackCount(), 2);
        f->library->importAsync({QUrl::fromLocalFile(path)});
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 5000);
        QCOMPARE(f->library->trackCount(), 2);
        QCOMPARE(done.last()[1].toInt(), 1);
        const auto playlistId = f->playlists->createPlaylist("Dropped tracks");
        ui::TrackFilterModel view(*f->library, *f->playlists);
        view.setPlaylistId(playlistId);
        QCOMPARE(view.count(), 0);
        connect(f->library.get(), &ui::LibraryController::playlistTracksImported,
                f->playlists.get(), [lists = f->playlists.get()](qint64 id, const QList<qint64>& tracks) {
                    QVERIFY(lists->editTracks(id, tracks, false));
                });
        const QString newPath = f->dir.path() + "/other.wav";
        QVERIFY(QFile::copy(path, newPath));
        QFile other(newPath);
        QVERIFY(other.open(QIODevice::ReadWrite));
        QVERIFY(other.seek(other.size() - 1));
        QCOMPARE(other.write("a", 1), 1);
        other.close();
        f->library->importAsync({QUrl::fromLocalFile(path), QUrl::fromLocalFile(newPath)}, playlistId);
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 3, 5000);
        QCOMPARE(f->library->trackCount(), 3);
        QCOMPARE(view.count(), 2);
        library::TrackRepository repository(*f->db);
        QCOMPARE(f->playlists->trackIds(playlistId),
                (QList<qint64>{repository.idByPath(path), repository.idByPath(newPath)}));

        f->library->importAsync({QUrl::fromLocalFile(path)});
        f->library->cancelImport();
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 4, 5000);
        QCOMPARE(f->library->trackCount(), 3);
    }

    void trackOrderAndViewSortAreIndependent() {
        auto f = makeFixture();
        QVERIFY(f);
        library::TrackRepository repo(*f->db);
        core::Track other;
        other.title = "A first title";
        other.filePath = f->dir.path() + "/other.mp3";
        other.fileHash = "another-hash";
        const auto id = repo.insertTrack(other);
        QVERIFY(id > 0);
        f->library->refresh();
        const auto list = f->playlists->data(f->playlists->index(0), ui::PlaylistController::IdRole).toLongLong();
        QVERIFY(f->playlists->addTrack(list, id));
        ui::TrackFilterModel view(*f->library, *f->playlists);
        view.setPlaylistId(list);
        view.sortBy("title");
        QCOMPARE(view.data(view.index(0, 0), ui::LibraryController::IdRole).toLongLong(), id);
        QCOMPARE(f->playlists->trackIds(list).first(), 1);
        QVERIFY(f->playlists->moveTrack(list, id, 1));
        view.sortBy("");
        QCOMPARE(view.data(view.index(0, 0), ui::LibraryController::IdRole).toLongLong(), id);
        QVERIFY(f->playlists->moveTrack(list, id, 0));
        QCOMPARE(f->playlists->trackIds(list).last(), id);
        view.selectVisible();
        QCOMPARE(view.selectedIds(), f->playlists->trackIds(list));
    }

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
        QCOMPARE(f->playlists->property("totalTrackCount").toInt(), 1);
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
