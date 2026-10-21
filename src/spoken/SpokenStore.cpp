#include "SpokenStore.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <sqlite3.h>

namespace {
struct Statement {
    sqlite3_stmt *p = nullptr;
    ~Statement() { sqlite3_finalize(p); }
};
// Cached statements are reset, never finalized, after each use.
struct Reset {
    sqlite3_stmt *p;
    ~Reset() {
        if (p) {
            sqlite3_reset(p);
            sqlite3_clear_bindings(p);
        }
    }
};
void bind(sqlite3_stmt *p, int n, const QString &s) {
    const auto utf8 = s.toUtf8();
    sqlite3_bind_text(p, n, utf8.constData(), utf8.size(), SQLITE_TRANSIENT);
}
void bind(sqlite3_stmt *p, int n, const QVariant &value) {
    if (value.typeId() == QMetaType::Int || value.typeId() == QMetaType::LongLong)
        sqlite3_bind_int64(p, n, value.toLongLong());
    else
        bind(p, n, value.toString());
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
// Title search folds case the way QString does; SQLite's lower() is ASCII only.
void fold(sqlite3_context *context, int argc, sqlite3_value **argv) {
    if (argc != 1 || sqlite3_value_type(argv[0]) == SQLITE_NULL) {
        sqlite3_result_null(context);
        return;
    }
    const auto *text = reinterpret_cast<const char *>(sqlite3_value_text(argv[0]));
    const auto folded = QString::fromUtf8(text, sqlite3_value_bytes(argv[0])).toCaseFolded().toUtf8();
    sqlite3_result_text(context, folded.constData(), folded.size(), SQLITE_TRANSIENT);
}
QString jsonList(const QStringList &values) {
    return QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(values)).toJson(QJsonDocument::Compact));
}
// The audio URL the Zune knows an episode by: pinned once sent, else the feed's.
// These expressions must match the indexes in open() character for character,
// or SQLite cannot use them.
const QString Source =
    QStringLiteral("coalesce(json_extract(payload,'$.deviceSourceUrl'),json_extract(payload,'$.sourceUrl'))");
// Publication instant: SQLite parses the ISO text, offsets included, and
// gives NULL for a missing or invalid date, which sorts last.
const QString Published = QStringLiteral("julianday(json_extract(payload,'$.published'))");
const QString Downloaded = QStringLiteral("coalesce(json_extract(payload,'$.path'),'')<>''");
const QString Unplayed = QStringLiteral("coalesce(json_extract(payload,'$.played'),0)=0");
const QString Starred = QStringLiteral("coalesce(json_extract(payload,'$.favorite'),0)=1");
const QString Title = QStringLiteral("json_extract(payload,'$.title')");
const QString Sent = QStringLiteral("json_extract(payload,'$.deviceSourceUrl') IS NOT NULL");
// SQLite's planner prefers the unique (kind,parent,identity) index and scans
// every episode of the show; naming the matching partial index keeps a page
// of a 50,000-episode show at a few milliseconds.
QString episodeFrom(const SpokenStore::EpisodeQuery &q) {
    if (!q.sources.isEmpty())
        return QStringLiteral("items INDEXED BY spoken_episode_sources");
    if (q.sentOnly)
        return QStringLiteral("items INDEXED BY spoken_episode_sent");
    if (q.starredOnly || q.filter == u"starred")
        return QStringLiteral("items INDEXED BY spoken_episode_starred");
    if (q.filter == u"unplayed")
        return QStringLiteral("items INDEXED BY spoken_episodes_unplayed");
    if (q.filter == u"downloaded")
        return QStringLiteral("items INDEXED BY spoken_episode_downloaded");
    if (!q.search.trimmed().isEmpty())
        return QStringLiteral("items INDEXED BY spoken_episode_titles");
    return QStringLiteral("items INDEXED BY spoken_episode_dates");
}
QString episodeWhere(const SpokenStore::EpisodeQuery &q, QVariantList *binds) {
    QString sql = QStringLiteral("kind='episode'");
    if (!q.show.isEmpty()) {
        sql += QStringLiteral(" AND parent=?");
        binds->append(q.show);
    }
    if (q.starredOnly || q.filter == u"starred")
        sql += QStringLiteral(" AND ") + Starred;
    if (q.filter == u"downloaded")
        sql += QStringLiteral(" AND ") + Downloaded;
    else if (q.filter == u"unplayed") {
        sql += QStringLiteral(" AND ") + Unplayed;
        // Episodes the Zune reports played, found by the sources index: the
        // outer scan compares row IDs and reads no rows, so a deep page stays
        // fast and keeps the list's order.
        if (!q.playedSources.isEmpty()) {
            sql += QStringLiteral(" AND rowid NOT IN (SELECT rowid FROM items INDEXED BY spoken_episode_sources "
                                  "WHERE kind='episode' AND ") + Source +
                   QStringLiteral(" IN (SELECT value FROM json_each(?)))");
            binds->append(jsonList(q.playedSources));
        }
    }
    if (q.localUnplayed && q.filter != u"unplayed")
        sql += QStringLiteral(" AND ") + Unplayed;
    if (q.sentOnly)
        sql += QStringLiteral(" AND ") + Sent;
    if (q.skipKept)
        sql += QStringLiteral(" AND coalesce(json_extract(payload,'$.keepOnDevice'),0)=0");
    if (!q.sources.isEmpty()) {
        sql += QStringLiteral(" AND ") + Source + QStringLiteral(" IN (SELECT value FROM json_each(?))");
        binds->append(jsonList(q.sources));
    }
    const auto needle = q.search.trimmed().toCaseFolded();
    if (!needle.isEmpty()) {
        sql += QStringLiteral(" AND instr(zuuned_fold(") + Title + QStringLiteral("),?)>0");
        binds->append(needle);
    }
    return sql;
}
} // namespace
SpokenStore::SpokenStore(const QString &root) : m_root(root) {}
SpokenStore::~SpokenStore() {
    for (auto *statement : m_statements)
        sqlite3_finalize(statement);
    if (m_db)
        sqlite3_close(m_db);
}
bool SpokenStore::exec(const char *sql, QString *error) const {
    if (sqlite3_exec(m_db, sql, nullptr, nullptr, nullptr) == SQLITE_OK)
        return true;
    *error = QString::fromUtf8(sqlite3_errmsg(m_db));
    return false;
}
sqlite3_stmt *SpokenStore::statement(Cached which, const char *sql, QString *error) const {
    if (!m_statements[which] &&
        sqlite3_prepare_v2(m_db, sql, -1, &m_statements[which], nullptr) != SQLITE_OK) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        m_statements[which] = nullptr;
    }
    return m_statements[which];
}
bool SpokenStore::configure(QString *error) {
    if (sqlite3_create_function_v2(m_db, "zuuned_fold", 1, SQLITE_UTF8 | SQLITE_DETERMINISTIC,
                                   nullptr, fold, nullptr, nullptr, nullptr) != SQLITE_OK) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return false;
    }
    return true;
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
    // The UI reader keeps the WAL file open, so it never shrinks on its own:
    // a reset after a 20,000-episode save truncates it to 8 MiB.
    if (!configure(error) ||
        !exec("PRAGMA journal_mode=WAL; PRAGMA foreign_keys=ON; PRAGMA journal_size_limit=8388608;", error))
        return false;
    if (v == 0 &&
        !exec("BEGIN IMMEDIATE; CREATE TABLE IF NOT EXISTS items (id TEXT PRIMARY "
                    "KEY NOT NULL, kind TEXT NOT NULL, parent TEXT NOT NULL, identity TEXT "
                    "NOT NULL, payload TEXT NOT NULL, UNIQUE(kind,parent,identity)); "
                    "CREATE INDEX IF NOT EXISTS spoken_parent ON items(parent); PRAGMA "
                    "user_version=1; COMMIT;",
                    error))
        return false;
    // Indexes only: earlier builds still open version 1, up to their own
    // 100,000-row load limit (past it they show an empty spoken library and
    // refuse new rows; nothing is lost). Episodes page by date within their
    // show, one index per list filter; the titles index lets a search read no
    // rows. Everything else loads by kind.
    const QString indexes =
        QStringLiteral("CREATE INDEX IF NOT EXISTS spoken_kind ON items(kind); "
                       "CREATE INDEX IF NOT EXISTS spoken_episode_dates ON items(parent, ") +
        Published + QStringLiteral(" DESC) WHERE kind='episode'; "
                                   "CREATE INDEX IF NOT EXISTS spoken_episode_titles ON items(parent, ") +
        Published + QStringLiteral(" DESC, ") + Title + QStringLiteral(") WHERE kind='episode'; "
                                   // Development builds made this one with the
                                   // audio URL as a column, which broke its order.
                                   "DROP INDEX IF EXISTS spoken_episode_unplayed; "
                                   "CREATE INDEX IF NOT EXISTS spoken_episodes_unplayed ON items(parent, ") +
        Published + QStringLiteral(" DESC) WHERE kind='episode' AND ") +
        Unplayed + QStringLiteral("; CREATE INDEX IF NOT EXISTS spoken_episode_downloaded ON items(parent, ") +
        Published + QStringLiteral(" DESC) WHERE kind='episode' AND ") + Downloaded +
        QStringLiteral("; CREATE INDEX IF NOT EXISTS spoken_episode_starred ON items(parent, ") + Published +
        QStringLiteral(" DESC) WHERE kind='episode' AND ") + Starred +
        QStringLiteral("; CREATE INDEX IF NOT EXISTS spoken_episode_sources ON items(") + Source +
        QStringLiteral(", parent) WHERE kind='episode'; CREATE INDEX IF NOT EXISTS spoken_episode_sent ON "
                       "items(parent) WHERE kind='episode' AND ") + Sent + QStringLiteral(";");
    if (!exec(indexes.toUtf8().constData(), error))
        return false;
    m_ready = true;
    return true;
}
bool SpokenStore::openReader(QString *error) {
    if (m_ready)
        return true;
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
    const QString path = m_root + QStringLiteral("/spoken.db");
    if (!QFileInfo::exists(path) ||
        sqlite3_open_v2(QFile::encodeName(path).constData(), &m_db, SQLITE_OPEN_READONLY,
                        nullptr) != SQLITE_OK) {
        *error = QStringLiteral("The spoken-media library is not open yet");
        return false;
    }
    // Brief: the UI thread must never wait long for a lock.
    sqlite3_busy_timeout(m_db, 250);
    Statement version;
    if (sqlite3_prepare_v2(m_db, "PRAGMA user_version", -1, &version.p, nullptr) != SQLITE_OK ||
        sqlite3_step(version.p) != SQLITE_ROW || sqlite3_column_int(version.p, 0) != 1) {
        *error = QStringLiteral("The spoken-media library is not open yet");
        return false;
    }
    if (!configure(error) || !exec("PRAGMA query_only=1;", error))
        return false;
    m_reader = true;
    m_ready = true;
    return true;
}
bool SpokenStore::begin(QString *error) {
    m_itemCount = -1;
    return exec("BEGIN IMMEDIATE", error);
}
bool SpokenStore::commit(QString *error) { return exec("COMMIT", error); }
void SpokenStore::rollback() {
    m_itemCount = -1;
    sqlite3_exec(m_db, "ROLLBACK", nullptr, nullptr, nullptr);
}
QVariantList SpokenStore::select(const QString &sql, const QVariantList &binds,
                                 QString *error) const {
    Statement stmt;
    QVariantList result;
    if (!m_db || sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &stmt.p, nullptr) != SQLITE_OK) {
        *error = m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QStringLiteral("Spoken-media library is closed");
        return {};
    }
    for (int i = 0; i < binds.size(); ++i)
        bind(stmt.p, i + 1, binds[i]);
    int rc;
    while ((rc = sqlite3_step(stmt.p)) == SQLITE_ROW) {
        if (result.size() >= MaxItems) {
            *error = QStringLiteral("Spoken collection exceeds the current 500,000-item limit");
            return {};
        }
        auto row = decode(stmt.p, error);
        if (!error->isEmpty())
            return {};
        result.append(row);
    }
    if (rc != SQLITE_DONE)
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
    return error->isEmpty() ? result : QVariantList();
}
QVariantList SpokenStore::rows(QString *error) const {
    return select(QStringLiteral("SELECT payload FROM items ORDER BY rowid"), {}, error);
}
QVariantList SpokenStore::collectionRows(QString *error) const {
    // The kind index skips the episodes without reading them. List every
    // non-episode kind here.
    return select(QStringLiteral("SELECT payload FROM items WHERE kind IN "
                                 "('show','book','part','book-collection') ORDER BY rowid"),
                  {}, error);
}
QVariantList SpokenStore::children(const QString &parent, const QString &kind,
                                   QString *error) const {
    return select(QStringLiteral("SELECT payload FROM items WHERE parent=? AND kind=? ORDER BY rowid"),
                  {parent, kind}, error);
}
QVariantList SpokenStore::episodes(const EpisodeQuery &q, QString *error) const {
    QVariantList binds;
    const QString where = episodeWhere(q, &binds);
    const QString from = QStringLiteral("SELECT payload FROM ") + episodeFrom(q) + QStringLiteral(" WHERE ") + where;
    const int limit = qMax(0, q.limit), offset = qMax(0, q.offset);
    if (!q.identityTies && !q.newestFirst && !q.show.isEmpty()) {
        // A list, oldest first: the date index read backwards, then the
        // undated episodes, both newest-saved first, so the order is the
        // newest-first list reversed.
        auto dated = binds;
        dated << limit << offset;
        auto rows = select(from + QStringLiteral(" AND ") + Published + QStringLiteral(" IS NOT NULL ORDER BY ") +
                               Published + QStringLiteral(", rowid DESC LIMIT ? OFFSET ?"),
                           dated, error);
        if (!error->isEmpty() || rows.size() >= limit)
            return rows;
        Statement count;
        const auto countSql = QStringLiteral("SELECT count(*) FROM ") + episodeFrom(q) + QStringLiteral(" WHERE ") +
                              where + QStringLiteral(" AND ") + Published + QStringLiteral(" IS NOT NULL");
        if (sqlite3_prepare_v2(m_db, countSql.toUtf8().constData(), -1, &count.p, nullptr) != SQLITE_OK) {
            *error = QString::fromUtf8(sqlite3_errmsg(m_db));
            return {};
        }
        for (int i = 0; i < binds.size(); ++i)
            bind(count.p, i + 1, binds[i]);
        const int datedTotal = sqlite3_step(count.p) == SQLITE_ROW ? sqlite3_column_int(count.p, 0) : 0;
        auto undated = binds;
        undated << limit - int(rows.size()) << qMax(0, offset - datedTotal);
        rows += select(from + QStringLiteral(" AND ") + Published + QStringLiteral(" IS NULL ORDER BY rowid DESC LIMIT ? OFFSET ?"),
                       undated, error);
        return error->isEmpty() ? rows : QVariantList();
    }
    QString sql = from + QStringLiteral(" ORDER BY");
    if (q.show.isEmpty())
        sql += QStringLiteral(" (SELECT rowid FROM items AS s WHERE s.id=items.parent),");
    if (!q.identityTies && q.newestFirst)
        // The descending index yields this order directly: no sort, and OFFSET
        // skips rows without reading them.
        sql += QStringLiteral(" ") + Published + QStringLiteral(" DESC, rowid");
    else if (!q.identityTies)
        sql += QStringLiteral(" ") + Published + QStringLiteral(" IS NULL, ") + Published + QStringLiteral(", rowid DESC");
    else if (q.newestFirst)
        sql += QStringLiteral(" ") + Published + QStringLiteral(" DESC, identity, id");
    else
        sql += QStringLiteral(" ") + Published + QStringLiteral(" IS NULL, ") + Published +
               QStringLiteral(", identity, id");
    sql += QStringLiteral(" LIMIT ? OFFSET ?");
    binds << limit << offset;
    return select(sql, binds, error);
}
int SpokenStore::countEpisodes(const EpisodeQuery &q, QString *error) const {
    QVariantList binds;
    const auto sql = QStringLiteral("SELECT count(*) FROM ") + episodeFrom(q) + QStringLiteral(" WHERE ") +
                     episodeWhere(q, &binds);
    Statement stmt;
    if (!m_db || sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &stmt.p, nullptr) != SQLITE_OK) {
        *error = m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QStringLiteral("Spoken-media library is closed");
        return 0;
    }
    for (int i = 0; i < binds.size(); ++i)
        bind(stmt.p, i + 1, binds[i]);
    if (sqlite3_step(stmt.p) != SQLITE_ROW) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return 0;
    }
    return sqlite3_column_int(stmt.p, 0);
}
QHash<QString, QPair<int, int>> SpokenStore::episodeStats(const QSet<QString> &shows,
                                                          QString *error) const {
    // Two index-only counts: every episode, then the downloaded ones.
    QHash<QString, QPair<int, int>> stats;
    const auto filter = shows.isEmpty() ? QString()
                                        : QStringLiteral(" AND parent IN (SELECT value FROM json_each(?))");
    const auto list = jsonList(QStringList(shows.begin(), shows.end()));
    const QString queries[] = {
        QStringLiteral("SELECT parent, count(*) FROM items INDEXED BY spoken_episode_dates "
                       "WHERE kind='episode'") + filter + QStringLiteral(" GROUP BY parent"),
        QStringLiteral("SELECT parent, count(*) FROM items INDEXED BY spoken_episode_downloaded "
                       "WHERE kind='episode' AND ") + Downloaded + filter + QStringLiteral(" GROUP BY parent")};
    for (int pass = 0; pass < 2; ++pass) {
        Statement stmt;
        if (!m_db || sqlite3_prepare_v2(m_db, queries[pass].toUtf8().constData(), -1, &stmt.p, nullptr) != SQLITE_OK) {
            *error = m_db ? QString::fromUtf8(sqlite3_errmsg(m_db)) : QStringLiteral("Spoken-media library is closed");
            return {};
        }
        if (!shows.isEmpty())
            bind(stmt.p, 1, list);
        int rc;
        while ((rc = sqlite3_step(stmt.p)) == SQLITE_ROW) {
            const auto show = QString::fromUtf8(reinterpret_cast<const char *>(sqlite3_column_text(stmt.p, 0)));
            if (pass == 0)
                stats.insert(show, {sqlite3_column_int(stmt.p, 1), 0});
            else
                stats[show].second = sqlite3_column_int(stmt.p, 1);
        }
        if (rc != SQLITE_DONE) {
            *error = QString::fromUtf8(sqlite3_errmsg(m_db));
            return {};
        }
    }
    return stats;
}
int SpokenStore::sharingSource(const QString &show, const QString &episode, const QString &source,
                               QString *error) const {
    Statement stmt;
    const auto sql = QStringLiteral("SELECT count(*) FROM items INDEXED BY spoken_episode_sources "
                                    "WHERE kind='episode' AND parent=? AND id<>? AND ") +
                     Source + QStringLiteral("=?");
    if (sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &stmt.p, nullptr) != SQLITE_OK) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return 0;
    }
    bind(stmt.p, 1, show);
    bind(stmt.p, 2, episode);
    bind(stmt.p, 3, source);
    if (sqlite3_step(stmt.p) != SQLITE_ROW) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return 0;
    }
    return sqlite3_column_int(stmt.p, 0);
}
int SpokenStore::ambiguousSources(const QString &show, QString *error) const {
    Statement stmt;
    const auto sql = QStringLiteral("SELECT coalesce(sum(n),0) FROM (SELECT count(*) AS n FROM items "
                                    "INDEXED BY spoken_episode_sources WHERE kind='episode' AND parent=? "
                                    "GROUP BY ") + Source + QStringLiteral(" HAVING count(*)>1 OR coalesce(") +
                     Source + QStringLiteral(",'')='')");
    if (sqlite3_prepare_v2(m_db, sql.toUtf8().constData(), -1, &stmt.p, nullptr) != SQLITE_OK) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return 0;
    }
    bind(stmt.p, 1, show);
    if (sqlite3_step(stmt.p) != SQLITE_ROW) {
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
        return 0;
    }
    return sqlite3_column_int(stmt.p, 0);
}
QSet<QString> SpokenStore::takeTouchedShows() {
    QSet<QString> shows;
    shows.swap(m_touchedShows);
    return shows;
}
QVariantMap SpokenStore::get(const QString &id, QString *error) const {
    auto *stmt = statement(Get, "SELECT payload FROM items WHERE id=?", error);
    if (!stmt)
        return {};
    Reset reset{stmt};
    bind(stmt, 1, id);
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
        return decode(stmt, error);
    if (rc != SQLITE_DONE)
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
    return {};
}
QVariantMap SpokenStore::find(const QString &kind, const QString &parent, const QString &identity,
                              QString *error) const {
    auto *stmt = statement(Find, "SELECT payload FROM items WHERE kind=? AND parent=? AND identity=?",
                           error);
    if (!stmt)
        return {};
    Reset reset{stmt};
    bind(stmt, 1, kind);
    bind(stmt, 2, parent);
    bind(stmt, 3, identity);
    const int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW)
        return decode(stmt, error);
    if (rc != SQLITE_DONE)
        *error = QString::fromUtf8(sqlite3_errmsg(m_db));
    return {};
}
bool SpokenStore::put(QVariantMap *row, QString *error) {
    if (m_reader) {
        *error = QStringLiteral("The spoken-media reader cannot write");
        return false;
    }
    if (row->value("kind").toString().isEmpty() || row->value("identity").toString().isEmpty()) {
        *error = QStringLiteral("Missing spoken-media identity");
        return false;
    }
    const bool fresh = row->value("id").toString().isEmpty();
    if (fresh)
        row->insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
    const auto json =
        QJsonDocument(QJsonObject::fromVariantMap(*row)).toJson(QJsonDocument::Compact);
    if (json.size() > 4 * 1024 * 1024) {
        *error = QStringLiteral("Spoken-media record too large");
        return false;
    }
    // An update keeps the total. A new row must fit under MaxItems; counting
    // every row on every insert would make saving a long feed quadratic, so
    // count once per transaction and keep the total current.
    bool exists = false;
    if (!fresh) {
        auto *probe = statement(Exists, "SELECT 1 FROM items WHERE id=?", error);
        if (!probe)
            return false;
        Reset reset{probe};
        bind(probe, 1, row->value("id").toString());
        const int rc = sqlite3_step(probe);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) {
            *error = QString::fromUtf8(sqlite3_errmsg(m_db));
            return false;
        }
        exists = rc == SQLITE_ROW;
    }
    if (!exists) {
        if (m_itemCount < 0) {
            auto *count = statement(Count, "SELECT count(*) FROM items", error);
            if (!count)
                return false;
            Reset reset{count};
            if (sqlite3_step(count) != SQLITE_ROW) {
                *error = QString::fromUtf8(sqlite3_errmsg(m_db));
                return false;
            }
            m_itemCount = sqlite3_column_int64(count, 0);
        }
        if (m_itemCount >= MaxItems) {
            *error = QStringLiteral("The spoken-media library is full (500,000 items)");
            return false;
        }
    }
    auto *stmt = statement(Upsert,
                           "INSERT INTO items(id,kind,parent,identity,payload) VALUES(?1,?2,?3,?4,?5) "
                           "ON CONFLICT(id) DO UPDATE SET payload=excluded.payload "
                           "WHERE items.kind=excluded.kind AND items.parent=excluded.parent "
                           "AND items.identity=excluded.identity",
                           error);
    if (!stmt)
        return false;
    Reset reset{stmt};
    bind(stmt, 1, row->value("id").toString());
    bind(stmt, 2, row->value("kind").toString());
    bind(stmt, 3, row->value("parent").toString());
    bind(stmt, 4, row->value("identity").toString());
    sqlite3_bind_text(stmt, 5, json.constData(), json.size(), SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_DONE) {
        if (sqlite3_changes(m_db) > 0) {
            if (!exists)
                ++m_itemCount;
            if (row->value("kind") == "episode")
                m_touchedShows.insert(row->value("parent").toString());
            return true;
        }
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
    m_itemCount = -1;
    return true;
}
