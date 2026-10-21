#pragma once
#include <QHash>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
struct sqlite3;
struct sqlite3_stmt;

// Worker-owned database. Spoken identities never enter the music tables.
// Version 1 is additive in spoken.db; refuse unknown schemas rather than reset.
// Podcast episodes are not loaded as a whole: a show can list 50,000 of them.
// They are read a page at a time, by the worker or through openReader().
class SpokenStore {
  public:
    // Every row together, episodes included (500,000 at most).
    static constexpr int MaxItems = 500000;
    explicit SpokenStore(const QString &root);
    ~SpokenStore();
    SpokenStore(const SpokenStore &) = delete;
    SpokenStore &operator=(const SpokenStore &) = delete;
    bool open(QString *error);
    // A read-only connection for the UI thread. In WAL mode a reader sees the
    // last commit and never waits for the worker's write transaction. It
    // creates nothing, so it fails until the worker has opened the database.
    bool openReader(QString *error);
    bool begin(QString *error);
    bool commit(QString *error);
    void rollback();
    QVariantList rows(QString *error) const;
    // Every row except podcast episodes: shows, books, parts, collections.
    QVariantList collectionRows(QString *error) const;
    // Children of one parent, in insertion order.
    QVariantList children(const QString &parent, const QString &kind, QString *error) const;
    QVariantMap get(const QString &id, QString *error) const;
    QVariantMap find(const QString &kind, const QString &parent, const QString &identity,
                     QString *error) const;
    bool put(QVariantMap *row, QString *error);
    // Deletes only virtual collection metadata, never books, parts or files.
    bool removeAudiobookCollection(const QString &id, QString *error);
    QString root() const { return m_root; }

    struct EpisodeQuery {
        QString show;              // empty: every show
        QString filter;            // all, unplayed, downloaded or starred
        QString search;            // matched against the title, case-folded
        QStringList playedSources; // audio URLs the Zune reports played
        QStringList sources;       // when set: only these audio URLs
        bool starredOnly = false;  // starred episodes, with any filter
        bool localUnplayed = false; // not marked played here (Zune state aside)
        bool sentOnly = false;      // sent to a Zune: they keep deviceSourceUrl
        // Lists: newest first, equal dates in the order the feed gave them;
        // oldest first is that order reversed, undated episodes still last.
        // Selections: equal dates by identity, then ID; newest or oldest
        // first, undated episodes last either way.
        bool identityTies = false;
        bool newestFirst = true;
        bool skipKept = false;     // leave out episodes kept on the Zune
        int offset = 0, limit = 100;
    };
    QVariantList episodes(const EpisodeQuery &query, QString *error) const;
    int countEpisodes(const EpisodeQuery &query, QString *error) const;
    // Show id -> {episodes, downloaded}; every show when shows is empty.
    QHash<QString, QPair<int, int>> episodeStats(const QSet<QString> &shows, QString *error) const;
    // Other episodes of the show that use this device audio URL.
    int sharingSource(const QString &show, const QString &episode, const QString &source,
                      QString *error) const;
    // Episodes of the show whose device audio URL is missing or shared.
    int ambiguousSources(const QString &show, QString *error) const;
    // Shows whose episode rows were written since the last call.
    QSet<QString> takeTouchedShows();

  private:
    enum Cached { Get, Find, Exists, Upsert, Count, CachedCount };
    bool exec(const char *, QString *) const;
    bool configure(QString *error);
    sqlite3_stmt *statement(Cached which, const char *sql, QString *error) const;
    QVariantList select(const QString &sql, const QVariantList &binds, QString *error) const;
    QString m_root;
    sqlite3 *m_db = nullptr;
    bool m_ready = false, m_reader = false;
    mutable sqlite3_stmt *m_statements[CachedCount] = {};
    qint64 m_itemCount = -1; // known within a transaction; -1 means count again
    QSet<QString> m_touchedShows;
};
