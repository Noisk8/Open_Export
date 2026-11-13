#include <QtTest>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlQuery>

#include "core/models/playlist.h"
#include "core/models/track.h"
#include "library/DatabaseManager.h"
#include "library/PlaylistRepository.h"
#include "library/TrackRepository.h"

using namespace opendeck;

namespace {

// Builds a database with the *original* v1 schema, optionally applying the
// v2 changes but deliberately omitting `is_folder` (the bug an earlier build
// shipped). `version` is the value stored in schema_version afterwards.
bool createLegacyDatabase(QSqlDatabase& db, int version) {
    QSqlQuery q(db);
    const QStringList statements = {
            QStringLiteral("CREATE TABLE schema_version (version INTEGER NOT NULL)"),
            QStringLiteral(R"SQL(
                CREATE TABLE tracks (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    title TEXT NOT NULL DEFAULT '', artist TEXT NOT NULL DEFAULT '',
                    album TEXT NOT NULL DEFAULT '', genre TEXT NOT NULL DEFAULT '',
                    year INTEGER NOT NULL DEFAULT 0, duration REAL NOT NULL DEFAULT 0,
                    file_path TEXT NOT NULL, file_hash TEXT NOT NULL DEFAULT '',
                    sample_rate INTEGER NOT NULL DEFAULT 0,
                    bitrate INTEGER NOT NULL DEFAULT 0,
                    channels INTEGER NOT NULL DEFAULT 0, bpm REAL NOT NULL DEFAULT 0,
                    musical_key TEXT NOT NULL DEFAULT '',
                    analysis_status INTEGER NOT NULL DEFAULT 0,
                    created_at TEXT NOT NULL DEFAULT (datetime('now'))
                ))SQL"),
            QStringLiteral("CREATE UNIQUE INDEX idx_tracks_hash ON tracks(file_hash)"),
            QStringLiteral(R"SQL(
                CREATE TABLE playlists (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    name TEXT NOT NULL,
                    parent_id INTEGER REFERENCES playlists(id) ON DELETE CASCADE,
                    sort_order INTEGER NOT NULL DEFAULT 0,
                    created_at TEXT NOT NULL DEFAULT (datetime('now'))
                ))SQL"),
            QStringLiteral(R"SQL(
                CREATE TABLE playlist_entries (
                    id INTEGER PRIMARY KEY AUTOINCREMENT,
                    playlist_id INTEGER NOT NULL,
                    track_id INTEGER NOT NULL,
                    position INTEGER NOT NULL DEFAULT 0
                ))SQL"),
    };
    for (const QString& statement : statements) {
        if (!q.exec(statement)) {
            return false;
        }
    }

    if (version >= 2) {
        // Everything that migration 2 adds, except `is_folder`.
        const QStringList v2 = {
                QStringLiteral("ALTER TABLE tracks ADD COLUMN label TEXT NOT NULL DEFAULT ''"),
                QStringLiteral("ALTER TABLE tracks ADD COLUMN comment TEXT NOT NULL DEFAULT ''"),
                QStringLiteral("ALTER TABLE tracks ADD COLUMN isrc TEXT NOT NULL DEFAULT ''"),
                QStringLiteral("ALTER TABLE tracks ADD COLUMN mix_name TEXT NOT NULL DEFAULT ''"),
                QStringLiteral("ALTER TABLE tracks ADD COLUMN composer TEXT NOT NULL DEFAULT ''"),
                QStringLiteral("ALTER TABLE tracks ADD COLUMN sample_depth INTEGER NOT NULL DEFAULT 0"),
                QStringLiteral("ALTER TABLE tracks ADD COLUMN track_number INTEGER NOT NULL DEFAULT 0"),
                QStringLiteral("ALTER TABLE tracks ADD COLUMN disc_number INTEGER NOT NULL DEFAULT 0"),
                QStringLiteral("ALTER TABLE tracks ADD COLUMN rating INTEGER NOT NULL DEFAULT 0"),
                QStringLiteral("ALTER TABLE tracks ADD COLUMN color_id INTEGER NOT NULL DEFAULT 0"),
                QStringLiteral("ALTER TABLE tracks ADD COLUMN date_added TEXT NOT NULL DEFAULT ''"),
                QStringLiteral(R"SQL(
                    CREATE TABLE track_beats (
                        track_id INTEGER NOT NULL, position INTEGER NOT NULL,
                        beat_number INTEGER NOT NULL DEFAULT 1,
                        time REAL NOT NULL DEFAULT 0, tempo REAL NOT NULL DEFAULT 0
                    ))SQL"),
                QStringLiteral(R"SQL(
                    CREATE TABLE track_cues (
                        id INTEGER PRIMARY KEY AUTOINCREMENT, track_id INTEGER NOT NULL,
                        kind INTEGER NOT NULL DEFAULT 0, hot_cue INTEGER NOT NULL DEFAULT 0,
                        time REAL NOT NULL DEFAULT 0, loop_time REAL NOT NULL DEFAULT 0,
                        color_id INTEGER NOT NULL DEFAULT 0,
                        name TEXT NOT NULL DEFAULT '', comment TEXT NOT NULL DEFAULT ''
                    ))SQL"),
                QStringLiteral(R"SQL(
                    CREATE TABLE track_waveform (
                        track_id INTEGER NOT NULL, channel INTEGER NOT NULL DEFAULT 0,
                        peaks BLOB NOT NULL
                    ))SQL"),
        };
        for (const QString& statement : v2) {
            if (!q.exec(statement)) {
                return false;
            }
        }
    }
    return q.exec(QStringLiteral("INSERT INTO schema_version (version) VALUES (%1)")
                          .arg(version));
}

bool hasIsFolder(const QSqlDatabase& db) {
    QSqlQuery q(db);
    if (!q.exec(QStringLiteral("PRAGMA table_info(playlists)"))) {
        return false;
    }
    while (q.next()) {
        if (q.value(1).toString() == QStringLiteral("is_folder")) {
            return true;
        }
    }
    return false;
}

} // namespace

class TestDatabase : public QObject {
    Q_OBJECT

  private slots:
    void insertAndQueryTrack() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        library::DatabaseManager db(dir.path() + QStringLiteral("/test.db"));
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));

        library::TrackRepository repo(db);
        core::Track t;
        t.title = QStringLiteral("Track One");
        t.artist = QStringLiteral("Artist A");
        t.album = QStringLiteral("Album X");
        t.genre = QStringLiteral("House");
        t.year = 2024;
        t.duration = 301.5;
        t.filePath = QStringLiteral("/music/a.mp3");
        t.fileHash = QStringLiteral("abc123");
        t.sampleRate = 44100;
        t.bitrate = 320;
        t.channels = 2;
        t.bpm = 128.5;
        t.musicalKey = QStringLiteral("8A");

        const qint64 id = repo.insertTrack(t);
        QVERIFY(id > 0);

        const auto tracks = repo.allTracks();
        QCOMPARE(tracks.size(), 1);
        QCOMPARE(tracks[0].id, id);
        QCOMPARE(tracks[0].title, QStringLiteral("Track One"));
        QCOMPARE(tracks[0].artist, QStringLiteral("Artist A"));
        QCOMPARE(tracks[0].bpm, 128.5);
        QCOMPARE(tracks[0].musicalKey, QStringLiteral("8A"));
        QCOMPARE(tracks[0].duration, 301.5);
        QVERIFY(repo.existsByHash(QStringLiteral("abc123")));
        QVERIFY(repo.existsByPath(QStringLiteral("/music/a.mp3")));
        QVERIFY(!repo.existsByHash(QStringLiteral("missing")));
    }

    void migrationIsIdempotent() {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/test.db");
        QString error;
        {
            library::DatabaseManager db(path);
            QVERIFY2(db.open(&error), qPrintable(error));
        }
        // Reopening the same file must migrate cleanly (no duplicate table).
        library::DatabaseManager db2(path);
        QVERIFY2(db2.open(&error), qPrintable(error));
    }

    void upgradesLegacyV1Database() {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/legacy.db");
        const QString connection = QStringLiteral("legacy_v1_setup");
        {
            QSqlDatabase legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            legacy.setDatabaseName(path);
            QVERIFY(legacy.open());
            QVERIFY(createLegacyDatabase(legacy, 1));
            legacy.close();
        }
        QSqlDatabase::removeDatabase(connection);

        library::DatabaseManager db(path);
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));
        QVERIFY(hasIsFolder(db.database()));

        library::PlaylistRepository repository(db);
        core::Playlist playlist;
        playlist.name = QStringLiteral("Main Room");
        QVERIFY(repository.savePlaylist(playlist) > 0);
        QCOMPARE(repository.allPlaylists().size(), 1);
    }

    void repairsV2DatabaseMissingIsFolder() {
        // Regression: a database created by the original v1 schema that had
        // already reached schema_version 2 with the broken build. Without the
        // `is_folder` column, `SELECT ... is_folder` and the INSERT both fail,
        // so playlists never show up and cannot be created in the UI.
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/legacy.db");
        const QString connection = QStringLiteral("legacy_v2_setup");
        {
            QSqlDatabase legacy = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
            legacy.setDatabaseName(path);
            QVERIFY(legacy.open());
            QVERIFY(createLegacyDatabase(legacy, 2));
            QVERIFY(!hasIsFolder(legacy));
            legacy.close();
        }
        QSqlDatabase::removeDatabase(connection);

        library::DatabaseManager db(path);
        QString error;
        QVERIFY2(db.open(&error), qPrintable(error));
        QVERIFY(hasIsFolder(db.database()));

        library::PlaylistRepository repository(db);
        core::Playlist playlist;
        playlist.name = QStringLiteral("Main Room");
        const qint64 id = repository.savePlaylist(playlist);
        QVERIFY(id > 0);
        QCOMPARE(repository.allPlaylists().size(), 1);
        QCOMPARE(repository.allPlaylists().first().name, QStringLiteral("Main Room"));

        // A second open must not try to add the column again.
        library::DatabaseManager reopened(path);
        QVERIFY2(reopened.open(&error), qPrintable(error));
    }
};

QTEST_MAIN(TestDatabase)
#include "test_database.moc"
