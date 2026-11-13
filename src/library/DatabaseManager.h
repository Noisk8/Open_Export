#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QString>

namespace opendeck {
namespace library {

// DatabaseManager owns the local SQLite database connection and applies
// versioned schema migrations. It intentionally does not know about the
// rekordbox/DeviceSQL export format; this database is the application's own
// library storage.
class DatabaseManager : public QObject {
    Q_OBJECT
  public:
    explicit DatabaseManager(const QString& dbPath, QObject* parent = nullptr);
    ~DatabaseManager() override;

    // Opens (and creates) the database and migrates it to the latest schema.
    // On failure, *errorMessage is populated and the database stays closed.
    bool open(QString* errorMessage = nullptr);

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString databasePath() const;

    // Returns the underlying connection. Only valid after open() succeeded.
    [[nodiscard]] QSqlDatabase database() const;

  private:
    bool migrate(QString* errorMessage);
    bool applyMigration(int version);

    QString m_dbPath;
    QString m_connectionName;
    QSqlDatabase m_db;
};

} // namespace library
} // namespace opendeck
