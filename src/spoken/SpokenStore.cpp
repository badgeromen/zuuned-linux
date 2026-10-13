#include "SpokenStore.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <sqlite3.h>

namespace {
struct Statement {
    sqlite3_stmt *p = nullptr;
    ~Statement() { sqlite3_finalize(p); }
};
void bind(sqlite3_stmt *p, int n, const QString &s) {
    const auto utf8 = s.toUtf8();
    sqlite3_bind_text(p, n, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
}
QVariantMap decode(sqlite3_stmt *p, QString *error) {
    const int size = sqlite3_column_bytes(p, 0);
    if (size < 2 || size > 4 * 1024 * 1024) {
        *error = QStringLiteral("Invalid spoken-media record size");
        return {};
    }
    QJsonParseError parse;
    auto doc = QJsonDocument::fromJson(
        QByteArray(reinterpret_cast<const char *>(sqlite3_column_text(p, 0)), size), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
        *error = QStringLiteral("Invalid spoken-media record");
        return {};
    }
    return doc.object().toVariantMap();
}
} // namespace
SpokenStore::SpokenStore(const QString &root) : m_root(root) {}
SpokenStore::~SpokenStore() {
    if (m_db)
        sqlite3_close(m_db);
}
bool SpokenStore::exec(const char *sql, QString *error) const {
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) == SQLITE_OK)
        return true;
    *error = QString::fromUtf8(sqlite3_errmsg(m_db));
    return false;
}
bool SpokenStore::open(QString *error) {
    if (m_ready)
        return true;
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
    if (!QDir().mkpath(m_root)) {
        *error = QStringLiteral("Cannot create spoken-media storage");
        return false;
    }
    const QString path = m_root + QStringLiteral("/spoken.db");
    if (sqlite3_open_v2(QFile::encodeName(path).constData(), &m_db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        *error = QStringLiteral("Cannot open spoken-media database");
        return false;
    }
    sqlite3_busy_timeout(m_db, 5000);
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    Statement version;
    if (sqlite3_prepare_v2(m_db, "PRAGMA user_version", -1, &version.p, nullptr) != SQLITE_OK ||
        sqlite3_step(version.p) != SQLITE_ROW) {
        *error = QStringLiteral("Cannot inspect spoken-media schema");
        return false;
    }
    const int v = sqlite3_column_int(version.p, 0);
    sqlite3_finalize(version.p);
    version.p = nullptr;
    if (v != 0 && v != 1) {
        *error = QStringLiteral("This spoken-media database requires a newer Zuuned");
        return false;
    }
    if (!exec("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON;", error))
        return false;
    if (v == 0 &&
        !exec("BEGIN IMMEDIATE; CREATE TABLE IF NOT EXISTS items (id TEXT PRIMARY "
                    "KEY NOT NULL, kind TEXT NOT NULL, parent TEXT NOT NULL, identity TEXT "
                    "NOT NULL, payload TEXT NOT NULL, UNIQUE(kind,parent,identity)); "
                    "CREATE INDEX IF NOT EXISTS spoken_parent ON items(parent); PRAGMA "
                    "user_version=1; COMMIT;",
                    error))
        return false;
    m_ready = true;
    return true;
}
bool SpokenStore::begin(QString *error) { return exec("BEGIN IMMEDIATE", error); }
bool SpokenStore::commit(QString *error) { return exec("COMMIT", error); }
void SpokenStore::rollback() { sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr); }
QVariantList SpokenStore::rows(QString *error) const {
    Statement stmt;
    QVariantList result;
    if (sqlite3_prepare_v2(m_db, "SELECT payload FROM items ORDER BY rowid", -1, &stmt.p,
                           nullptr) != SQLITE_OK) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return {};
    }
    int rc;
    while ((rc = sqlite3_step(stmt.p)) == SQLITE_ROW) {
        if (result.size() >= 100000) {
            *error = QStringLiteral("Spoken collection exceeds the current 100,000-item limit");
            return {};
        }
        auto row = decode(stmt.p, error);
        if (!error->isEmpty())
            return {};
        result.append(row);
    }
    if (rc != SQLITE_DONE)
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
    return result;
}
QVariantMap SpokenStore::get(const QString &id, QString *error) const {
    Statement stmt;
    if (sqlite3_prepare_v2(m_db, "SELECT payload FROM items WHERE id=?", -1, &stmt.p, nullptr) !=
        SQLITE_OK) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return {};
    }
    bind(stmt.p, 1, id);
    const int rc = sqlite3_step(stmt.p);
    if (rc == SQLITE_ROW)
        return decode(stmt.p, error);
    if (rc != SQLITE_DONE)
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
    return {};
}
QVariantMap SpokenStore::find(const QString &kind, const QString &parent, const QString &identity,
                              QString *error) const {
    Statement stmt;
    if (sqlite3_prepare_v2(m_db,
                           "SELECT payload FROM items WHERE kind=? AND parent=? AND identity=?", -1,
                           &stmt.p, nullptr) != SQLITE_OK) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return {};
    }
    bind(stmt.p, 1, kind);
    bind(stmt.p, 2, parent);
    bind(stmt.p, 3, identity);
    const int rc = sqlite3_step(stmt.p);
    if (rc == SQLITE_ROW)
        return decode(stmt.p, error);
    if (rc != SQLITE_DONE)
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
    return {};
}
bool SpokenStore::put(QVariantMap *row, QString *error) {
    if (row->value("kind").toString().isEmpty() || row->value("identity").toString().isEmpty()) {
        *error = QStringLiteral("Missing spoken-media identity");
        return false;
    }
    if (row->value("id").toString().isEmpty())
        row->insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto json =
        QJsonDocument(QJsonObject::fromVariantMap(*row)).toJson(QJsonDocument::Compact);
    if (json.size() > 4 * 1024 * 1024) {
        *error = QStringLiteral("Spoken-media record too large");
        return false;
    }
    Statement stmt;
    if (sqlite3_prepare_v2(m_db,
                           "INSERT INTO items(id,kind,parent,identity,payload) "
                           "SELECT ?1,?2,?3,?4,?5 WHERE EXISTS(SELECT 1 FROM items WHERE id=?1) "
                           "OR (SELECT count(*) FROM items)<100000 "
                           "ON CONFLICT(id) DO UPDATE SET payload=excluded.payload "
                           "WHERE items.kind=excluded.kind AND items.parent=excluded.parent "
                           "AND items.identity=excluded.identity",
                           -1, &stmt.p, nullptr) != SQLITE_OK) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return false;
    }
    bind(stmt.p, 1, row->value("id").toString());
    bind(stmt.p, 2, row->value("kind").toString());
    bind(stmt.p, 3, row->value("parent").toString());
    bind(stmt.p, 4, row->value("identity").toString());
    sqlite3_bind_text(stmt.p, 5, json.constData(), json.size(), SQLITE_TRANSIENT);
    if (sqlite3_step(stmt.p) == SQLITE_DONE) {
        if (sqlite3_changes(m_db) > 0)
            return true;
        *error = QStringLiteral("Collection limit reached or immutable identity changed");
        return false;
    }
    *error = QString::fromUtf8(sqlite3_errmsg(m_db));
    return false;
}
bool SpokenStore::removeAudiobookCollection(const QString &id, QString *error) {
    Statement stmt;
    if (sqlite3_prepare_v2(m_db, "DELETE FROM items WHERE id=? AND kind='book-collection'",
                           -1, &stmt.p, nullptr) != SQLITE_OK) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return false;
    }
    bind(stmt.p, 1, id);
    if (sqlite3_step(stmt.p) != SQLITE_DONE) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return false;
    }
    if (sqlite3_changes(m_db) != 1) {
        *error = QStringLiteral("Audiobook collection no longer exists");
        return false;
    }
    return true;
}
