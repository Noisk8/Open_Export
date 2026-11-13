#include "library/DatabaseManager.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace opendeck {
namespace library {

namespace {
constexpr int kSchemaVersion = 3;

// Runs a list of statements one by one: the SQLite driver does not reliably
// execute several statements in a single exec().
bool execAll(QSqlDatabase& db, const QStringList& statements) {
    for (const QString& statement : statements) {
        QSqlQuery q(db);
        if (!q.exec(statement)) {
            return false;
        }
    }
    return true;
}

// Adds a column only when it is missing. Databases created by an older build
// may already have part of a migration applied; a plain ALTER TABLE would make
// the whole migration fail with "duplicate column name".
bool ensureColumn(QSqlDatabase& db, const QString& table, const QString& column,
                  const QString& definition) {
    QSqlQuery pragma(db);
    if (!pragma.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        return false;
    }
    while (pragma.next()) {
        if (pragma.value(1).toString().compare(column, Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    QSqlQuery alter(db);
    return alter.exec(QStringLiteral("ALTER TABLE %1 ADD COLUMN %2 %3")
                              .arg(table, column, definition));
}
} // namespace

DatabaseManager::DatabaseManager(const QString& dbPath, QObject* parent)
        : QObject(parent),
          m_dbPath(dbPath),
          m_connectionName(QStringLiteral("ode_%1").arg(QUuid::createUuid().toString(QUuid::Id128))) {
}

DatabaseManager::~DatabaseManager() {
    if (m_db.isOpen()) {
        m_db.close();
    }
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connectionName);
}

bool DatabaseManager::open(QString* errorMessage) {
    if (m_db.isOpen()) {
        return true;
    }
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(m_dbPath);

    if (!m_db.open()) {
        if (errorMessage) {
            *errorMessage = m_db.lastError().text();
        }
        return false;
    }

    if (!migrate(errorMessage)) {
        if (errorMessage && errorMessage->isEmpty()) {
            *errorMessage = m_db.lastError().text();
        }
        m_db.close();
        return false;
    }
    return true;
}

bool DatabaseManager::isOpen() const {
    return m_db.isOpen();
}

QString DatabaseManager::databasePath() const {
    return m_dbPath;
}

QSqlDatabase DatabaseManager::database() const {
    return m_db;
}

bool DatabaseManager::migrate(QString* errorMessage) {
    // Enable foreign keys and WAL for reliability/concurrency.
    QSqlQuery pragma(m_db);
    pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"));
    pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"));

    // Versioned schema. Only append new migrations; never edit an existing one.
    int version = 0;
    if (!m_db.tables().isEmpty()) {
        QSqlQuery q(m_db);
        if (q.exec(QStringLiteral("SELECT MAX(version) FROM schema_version")) && q.next()) {
            version = q.value(0).toInt();
        }
    }

    while (version < kSchemaVersion) {
        ++version;
        if (!applyMigration(version)) {
            if (errorMessage) {
                *errorMessage = m_db.lastError().text();
            }
            return false;
        }
        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral("INSERT INTO schema_version (version) VALUES (:v)"));
        ins.bindValue(QStringLiteral(":v"), version);
        if (!ins.exec()) {
            if (errorMessage) {
                *errorMessage = ins.lastError().text();
            }
            return false;
        }
    }
    return true;
}

bool DatabaseManager::applyMigration(int version) {
    switch (version) {
    case 1:
        return execAll(m_db, {
                QStringLiteral("CREATE TABLE schema_version (version INTEGER NOT NULL)"),
                QStringLiteral(R"SQL(
                    CREATE TABLE tracks (
                        id              INTEGER PRIMARY KEY AUTOINCREMENT,
                        title           TEXT NOT NULL DEFAULT '',
                        artist          TEXT NOT NULL DEFAULT '',
                        album           TEXT NOT NULL DEFAULT '',
                        genre           TEXT NOT NULL DEFAULT '',
                        year            INTEGER NOT NULL DEFAULT 0,
                        duration        REAL NOT NULL DEFAULT 0,
                        file_path       TEXT NOT NULL,
                        file_hash       TEXT NOT NULL DEFAULT '',
                        sample_rate     INTEGER NOT NULL DEFAULT 0,
                        bitrate         INTEGER NOT NULL DEFAULT 0,
                        channels        INTEGER NOT NULL DEFAULT 0,
                        bpm             REAL NOT NULL DEFAULT 0,
                        musical_key     TEXT NOT NULL DEFAULT '',
                        analysis_status INTEGER NOT NULL DEFAULT 0,
                        created_at      TEXT NOT NULL DEFAULT (datetime('now'))
                    )
                )SQL"),
                QStringLiteral("CREATE UNIQUE INDEX idx_tracks_hash ON tracks(file_hash)"),
                QStringLiteral(R"SQL(
                    CREATE TABLE playlists (
                        id         INTEGER PRIMARY KEY AUTOINCREMENT,
                        name       TEXT NOT NULL,
                        parent_id  INTEGER REFERENCES playlists(id) ON DELETE CASCADE,
                        sort_order INTEGER NOT NULL DEFAULT 0,
                        created_at TEXT NOT NULL DEFAULT (datetime('now'))
                    )
                )SQL"),
                QStringLiteral(R"SQL(
                    CREATE TABLE playlist_entries (
                        id          INTEGER PRIMARY KEY AUTOINCREMENT,
                        playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
                        track_id    INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
                        position    INTEGER NOT NULL DEFAULT 0
                    )
                )SQL"),
                QStringLiteral(
                        "CREATE INDEX idx_playlist_entries_playlist "
                        "ON playlist_entries(playlist_id, position)"),
        });

    case 2:
        // Extra metadata plus the tables that hold the musical analysis
        // needed to build the ANLZ files of a Pioneer export.
        if (!execAll(m_db, {
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
                        track_id   INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
                        position   INTEGER NOT NULL,
                        beat_number INTEGER NOT NULL DEFAULT 1,
                        time       REAL NOT NULL DEFAULT 0,
                        tempo      REAL NOT NULL DEFAULT 0
                    )
                )SQL"),
                QStringLiteral("CREATE INDEX idx_track_beats_track ON track_beats(track_id, position)"),
                QStringLiteral(R"SQL(
                    CREATE TABLE track_cues (
                        id          INTEGER PRIMARY KEY AUTOINCREMENT,
                        track_id    INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
                        kind        INTEGER NOT NULL DEFAULT 0,
                        hot_cue     INTEGER NOT NULL DEFAULT 0,
                        time        REAL NOT NULL DEFAULT 0,
                        loop_time   REAL NOT NULL DEFAULT 0,
                        color_id    INTEGER NOT NULL DEFAULT 0,
                        name        TEXT NOT NULL DEFAULT '',
                        comment     TEXT NOT NULL DEFAULT ''
                    )
                )SQL"),
                QStringLiteral("CREATE INDEX idx_track_cues_track ON track_cues(track_id)"),
                QStringLiteral(R"SQL(
                    CREATE TABLE track_waveform (
                        track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
                        channel  INTEGER NOT NULL DEFAULT 0,
                        peaks    BLOB NOT NULL
                    )
                )SQL"),
                QStringLiteral(
                        "CREATE UNIQUE INDEX idx_track_waveform_channel "
                        "ON track_waveform(track_id, channel)"),
        })) {
            return false;
        }
        return ensureColumn(m_db, QStringLiteral("playlists"), QStringLiteral("is_folder"),
                QStringLiteral("INTEGER NOT NULL DEFAULT 0"));

    case 3:
        // Repair migration. An earlier build of migration 2 forgot to add
        // `is_folder` to databases that were created by the original v1
        // schema, so `playlists` could neither be read nor written. The check
        // is idempotent: a database that already has the column is untouched.
        return ensureColumn(m_db, QStringLiteral("playlists"), QStringLiteral("is_folder"),
                QStringLiteral("INTEGER NOT NULL DEFAULT 0"));

    default:
        return false;
    }
}

} // namespace library
} // namespace opendeck
