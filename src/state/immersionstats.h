#pragma once

#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

class EpisodeFolder;
class MpvPlayer;

/** Local, playback-derived immersion history. No media or account data leaves the device. */
class ImmersionStats final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit ImmersionStats(EpisodeFolder *library, QObject *parent = nullptr);
    ~ImmersionStats() override;

    int revision() const noexcept { return m_revision; }
    void attachPlayer(MpvPlayer *player);
    Q_INVOKABLE QVariantMap summary(int days) const;
    /** A Monday-first, padded Jan–Dec grid; each seven entries form one week.
     * Includes available years, month columns, day details and annual totals.
     * Invalid/future years select the current year. Future days are disabled.
     */
    Q_INVOKABLE QVariantMap calendar(int year) const;

signals:
    void changed();

private:
    struct Session {
        QString title;
        QString key;
        QDateTime started;
        double seconds{0};
    };

    void sample();
    void finishSession();
    void load();
    void save() const;
    void notifyChanged();

    EpisodeFolder *m_library;
    MpvPlayer *m_player{nullptr};
    QTimer m_timer;
    QDateTime m_lastSample;
    QString m_lastPath;
    double m_lastPosition{0};
    bool m_wasWatchedBefore{false};
    Session m_current;
    QList<Session> m_sessions;
    QHash<QString, double> m_dailySeconds;
    QHash<QString, QHash<QString, double>> m_dailyTitles;
    QHash<QString, QString> m_titles;
    QHash<QString, int> m_dailyCompletions;
    QSet<QString> m_completedPaths;
    int m_revision{0};
    int m_unsavedSamples{0};
};
