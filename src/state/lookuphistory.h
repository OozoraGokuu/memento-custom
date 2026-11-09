#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

class EpisodeFolder;

/** Successful dictionary lookups stored locally, with explicit source context. */
class LookupHistory final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit LookupHistory(EpisodeFolder *library, QObject *parent = nullptr,
                           const QString &storagePath = {});
    int revision() const noexcept { return m_revision; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE QVariantMap contextFor(const QString &path, double position) const;
    Q_INVOKABLE void record(const QString &word, const QString &reading,
                            const QString &text, const QVariantMap &context);
    Q_INVOKABLE QVariantMap query(const QString &filter, int days,
                                 int offset, int limit) const;
    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE bool clear();

signals:
    void changed();
    void lastErrorChanged();

private:
    void load();
    bool save(QVariantList &entries);
    void setError(const QString &message);
    static QVariantMap sanitizedContext(const QVariantMap &context);

    EpisodeFolder *m_library;
    QString m_storagePath;
    QVariantList m_entries;
    QString m_lastError;
    bool m_loadFailed{false};
    int m_revision{0};
};
