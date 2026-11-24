#pragma once
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QUuid>
namespace opendeck::library {
// Savepoints compose with caller transactions; every failure rolls back the full edit.
class Savepoint {
public:
    explicit Savepoint(QSqlDatabase db) : m_db(db), m_name("ode_" + QUuid::createUuid().toString(QUuid::Id128)) {
        QSqlQuery q(m_db);
        m_active = q.exec("SAVEPOINT " + m_name);
    }
    ~Savepoint() {
        if (m_active) {
            QSqlQuery q(m_db);
            q.exec("ROLLBACK TO " + m_name);
            q.exec("RELEASE " + m_name);
        }
    }
    bool valid() const { return m_active; }
    bool commit() {
        QSqlQuery q(m_db);
        if (!m_active || !q.exec("RELEASE " + m_name)) return false;
        m_active = false;
        return true;
    }
private:
    QSqlDatabase m_db;
    QString m_name;
    bool m_active = false;
};
}
