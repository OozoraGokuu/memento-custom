#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantMap>

class Context;

/** Remembers explicitly selected local subtitle releases for a library series. */
class LocalSubtitleClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)

public:
    explicit LocalSubtitleClient(Context *context, QObject *parent = nullptr);
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    Q_INVOKABLE void loadFile(const QString &path);
    Q_INVOKABLE void fetchForCurrentMedia();
    Q_INVOKABLE void cancel();

signals:
    void changed();
    void failed(const QString &message);
    void subtitleAttached(const QString &fileName, const QString &entryName);

private:
    friend class LocalSubtitleClientTest;
    static QString candidateForEpisode(const QString &directory, int episode,
        const QVariantMap &rule, bool *ambiguous = nullptr);
    static QString readableSubtitlePath(const QString &path);
    void attach(const QString &path, bool automatic);
    void checkAttached();
    void error(const QString &message);

    Context *m_context;
    bool m_busy{false};
    bool m_automatic{false};
    quint64 m_generation{0};
    quint64 m_pendingGeneration{0};
    int m_episode{-1};
    QString m_status, m_mediaPath, m_pendingPath;
    QVariantMap m_link, m_savedSelection;
    QTimer m_attachTimer;
    QMetaObject::Connection m_trackConnection, m_sidConnection, m_mediaConnection;
};
