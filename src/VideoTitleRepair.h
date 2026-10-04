#pragma once

#include "library/LibraryTypes.h"

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

// A reviewable, conservative Name-only repair. DeviceService owns all USB
// work; this controller queues one rename and waits for its verdict.
class VideoTitleRepair : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject *library READ library WRITE setLibrary NOTIFY inputsChanged)
    Q_PROPERTY(QObject *device READ device WRITE setDevice NOTIFY inputsChanged)
    Q_PROPERTY(QObject *sync READ sync WRITE setSync NOTIFY inputsChanged)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(int candidateCount READ candidateCount NOTIFY changed)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY changed)
    Q_PROPERTY(int unchangedCount READ unchangedCount NOTIFY changed)
    Q_PROPERTY(int unmatchedCount READ unmatchedCount NOTIFY changed)
    Q_PROPERTY(int ambiguousCount READ ambiguousCount NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool canApply READ canApply NOTIFY changed)
    Q_PROPERTY(QString blockedReason READ blockedReason NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit VideoTitleRepair(QObject *parent = nullptr);

    QObject *library() const { return m_library; }
    QObject *device() const { return m_device; }
    QObject *sync() const { return m_sync; }
    void setLibrary(QObject *value);
    void setDevice(QObject *value);
    void setSync(QObject *value);
    QVariantList rows() const { return m_plan.rows; }
    int candidateCount() const { return int(m_plan.rows.size()); }
    int selectedCount() const;
    int unchangedCount() const { return m_plan.unchanged; }
    int unmatchedCount() const { return m_plan.unmatched; }
    int ambiguousCount() const { return m_plan.ambiguous; }
    bool busy() const { return m_busy; }
    bool canApply() const;
    QString blockedReason() const;
    QString error() const { return m_error; }

    Q_INVOKABLE void rebuildPreview();
    Q_INVOKABLE void setSelected(quint32 itemId, bool selected);
    Q_INVOKABLE void selectAll(bool selected);
    Q_INVOKABLE void applySelected();

    struct Plan {
        QVariantList rows;
        int unchanged = 0;
        int unmatched = 0;
        int ambiguous = 0;
    };
    // Pure planning boundary: no disk access, mutation, network or USB.
    static Plan buildPlan(const QVariantList &deviceVideos, const QVector<LibVideo> &libraryVideos);

signals:
    void inputsChanged();
    void changed();
    void finished(int updated, int failed, int skipped);

private slots:
    void onStateChanged();
    void onRenameFinished(quint32 itemId, const QString &newName, bool ok);

private:
    void reconnectInputs();
    void sendNext();
    void finish();
    void stopRemaining(const QString &reason);
    void setRowStatus(int row, const QString &status, const QString &error = {});
    QPointer<QObject> m_library, m_device, m_sync;
    QList<QMetaObject::Connection> m_connections;
    Plan m_plan;
    QVector<int> m_queue;
    int m_cursor = 0;
    int m_current = -1;
    int m_updated = 0, m_failed = 0, m_skipped = 0;
    quint64 m_generation = 1, m_previewGeneration = 0;
    bool m_wasConnected = false;
    bool m_busy = false;
    QString m_error;
};
