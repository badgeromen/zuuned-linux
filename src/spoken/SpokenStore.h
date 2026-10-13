#pragma once
#include <QString>
#include <QVariantList>
#include <QVariantMap>
struct sqlite3;

// Worker-owned database. Spoken identities never enter the music tables.
// Version 1 is additive in spoken.db; refuse unknown schemas rather than reset.
class SpokenStore {
  public:
    explicit SpokenStore(const QString &root);
    ~SpokenStore();
    SpokenStore(const SpokenStore &) = delete;
    SpokenStore &operator=(const SpokenStore &) = delete;
    bool open(QString *error);
    bool begin(QString *error);
    bool commit(QString *error);
    void rollback();
    QVariantList rows(QString *error) const;
    QVariantMap get(const QString &id, QString *error) const;
    QVariantMap find(const QString &kind, const QString &parent, const QString &identity,
                     QString *error) const;
    bool put(QVariantMap *row, QString *error);
    // Deletes only virtual collection metadata, never books, parts or files.
    bool removeAudiobookCollection(const QString &id, QString *error);
    QString root() const { return m_root; }

  private:
    bool exec(const char *, QString *) const;
    QString m_root;
    sqlite3 *m_db = nullptr;
    bool m_ready = false;
};
