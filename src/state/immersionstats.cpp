#include "state/immersionstats.h"

#include <algorithm>
#include <cmath>

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>

#include "player/mpvplayer.h"
#include "quick/episodefolder.h"
#include "util/utils.h"

namespace {
QString statsPath()
{
    return DirectoryUtils::getDataDir() + "/immersion-stats.json";
}

QJsonObject numberMap(const QHash<QString, double> &values)
{
    QJsonObject result;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        result.insert(it.key(), it.value());
    return result;
}
}

ImmersionStats::ImmersionStats(EpisodeFolder *library, QObject *parent)
    : QObject(parent), m_library(library)
{
    load();
    m_timer.setInterval(1000);
    connect(&m_timer, &QTimer::timeout, this, &ImmersionStats::sample);
}

ImmersionStats::~ImmersionStats()
{
    finishSession();
    save();
}

void ImmersionStats::attachPlayer(MpvPlayer *player)
{
    m_player = player;
    m_lastSample = QDateTime::currentDateTime();
    m_lastPath.clear();
    m_lastPosition = 0;
    if (player)
    {
        connect(player, &QObject::destroyed, this, [this] {
            finishSession();
            save();
            m_player = nullptr;
            m_timer.stop();
        });
        m_timer.start();
    }
}

void ImmersionStats::sample()
{
    if (!m_player || !m_player->state()) return;
    const QDateTime now = QDateTime::currentDateTime();
    const QString path = m_player->state()->path();
    const double position = m_player->state()->timePosition();
    const double duration = m_player->state()->duration();
    const double elapsed = m_lastSample.msecsTo(now) / 1000.0;

    if (path != m_lastPath)
    {
        finishSession();
        m_lastPath = path;
        m_lastPosition = position;
        m_wasWatchedBefore = m_library->isWatched(path);
        m_lastSample = now;
        return;
    }

    const double advance = position - m_lastPosition;
    m_lastPosition = position;
    m_lastSample = now;
    if (path.isEmpty() || m_player->loading() || m_player->buffering() ||
        m_player->state()->pause() || !std::isfinite(advance) ||
        elapsed <= 0 || elapsed > 3 || advance <= 0 ||
        advance > elapsed * 1.5 + 0.5)
        return;

    const double credited = std::min(advance, elapsed);
    const QString day = now.date().toString(Qt::ISODate);
    const QVariantMap info = m_library->playbackInfo(path);
    const QString key = info.value("key").toString().isEmpty() ? path : info.value("key").toString();
    const QString title = info.value("title").toString().isEmpty() ?
        QFileInfo(path).completeBaseName() : info.value("title").toString();
    if (m_current.key != key || m_current.title != title)
    {
        finishSession();
        m_current = {title, key, now, 0};
    }
    m_current.seconds += credited;
    m_dailySeconds[day] += credited;
    m_dailyTitles[day][key] += credited;
    m_titles[key] = title;

    // Count a completion only after normal playback reaches the library's 90% mark.
    if (!info.isEmpty() && duration > 0 && position >= duration * 0.9 &&
        position - advance < duration * 0.9 &&
        !m_completedPaths.contains(path) && !m_wasWatchedBefore)
    {
        m_completedPaths.insert(path);
        ++m_dailyCompletions[day];
    }

    notifyChanged();
    if (++m_unsavedSamples >= 15)
    {
        save();
        m_unsavedSamples = 0;
    }
}

void ImmersionStats::finishSession()
{
    if (m_current.seconds >= 1)
    {
        m_sessions.prepend(m_current);
        while (m_sessions.size() > 500) m_sessions.removeLast();
        notifyChanged();
    }
    m_current = {};
}

void ImmersionStats::notifyChanged()
{
    ++m_revision;
    emit changed();
}

QVariantMap ImmersionStats::summary(int days) const
{
    const QDate today = QDate::currentDate();
    const QDate first = days <= 0 ? QDate() : today.addDays(1 - days);
    double total = 0;
    int activeDays = 0;
    int completions = 0;
    for (auto it = m_dailySeconds.cbegin(); it != m_dailySeconds.cend(); ++it)
    {
        const QDate day = QDate::fromString(it.key(), Qt::ISODate);
        if (day.isValid() && (days <= 0 || day >= first) && day <= today)
        {
            total += it.value();
            if (it.value() >= 60) ++activeDays;
        }
    }
    for (auto it = m_dailyCompletions.cbegin(); it != m_dailyCompletions.cend(); ++it)
    {
        const QDate day = QDate::fromString(it.key(), Qt::ISODate);
        if (day.isValid() && (days <= 0 || day >= first) && day <= today)
            completions += it.value();
    }

    const int chartDays = days <= 0 ? 30 : days;
    QVariantList daily;
    for (int offset = chartDays - 1; offset >= 0; --offset)
    {
        const QDate date = today.addDays(-offset);
        const QString iso = date.toString(Qt::ISODate);
        daily.append(QVariantMap{{"date", iso},
            {"label", QLocale().dayName(date.dayOfWeek(), QLocale::ShortFormat)},
            {"seconds", m_dailySeconds.value(iso)}});
    }

    QHash<QString, double> byTitle;
    for (auto it = m_dailyTitles.cbegin(); it != m_dailyTitles.cend(); ++it)
    {
        const QDate day = QDate::fromString(it.key(), Qt::ISODate);
        if (!day.isValid() || day > today || (days > 0 && day < first)) continue;
        for (auto title = it.value().cbegin(); title != it.value().cend(); ++title)
            byTitle[title.key()] += title.value();
    }
    QList<QString> titleKeys = byTitle.keys();
    std::sort(titleKeys.begin(), titleKeys.end(), [&byTitle](const QString &a, const QString &b) {
        return byTitle.value(a) > byTitle.value(b);
    });
    QVariantList titles;
    for (const QString &key : titleKeys.mid(0, 5))
        titles.append(QVariantMap{{"title", m_titles.value(key, key)},
            {"seconds", byTitle.value(key)},
            {"fraction", total > 0 ? byTitle.value(key) / total : 0}});

    QVariantList sessions;
    auto appendSession = [&](const Session &session) {
        if (session.seconds < 30 ||
            (days > 0 && session.started.date() < first) ||
            session.started.date() > today || sessions.size() >= 6) return;
        sessions.append(QVariantMap{{"title", session.title},
            {"started", QLocale().toString(session.started, QLocale::ShortFormat)},
            {"seconds", session.seconds}});
    };
    int sessionCount = 0;
    if (m_current.seconds >= 30 && (days <= 0 || m_current.started.date() >= first))
    {
        ++sessionCount;
        appendSession(m_current);
    }
    for (const Session &session : m_sessions)
    {
        if (session.seconds >= 30 && (days <= 0 || session.started.date() >= first))
            ++sessionCount;
        appendSession(session);
    }
    return {{"activeSeconds", total}, {"activeDays", activeDays},
        {"sessionCount", sessionCount}, {"completedCount", completions},
        {"daily", daily}, {"titles", titles}, {"sessions", sessions}};
}

void ImmersionStats::load()
{
    QFile file(statsPath());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    if (!document.isObject()) return;
    const QJsonObject root = document.object();
    const QJsonObject daily = root.value("dailySeconds").toObject();
    for (auto it = daily.begin(); it != daily.end(); ++it)
        m_dailySeconds[it.key()] = it.value().toDouble();
    const QJsonObject dailyTitles = root.value("dailyTitles").toObject();
    for (auto it = dailyTitles.begin(); it != dailyTitles.end(); ++it)
    {
        const QJsonObject values = it.value().toObject();
        for (auto value = values.begin(); value != values.end(); ++value)
            m_dailyTitles[it.key()][value.key()] = value.value().toDouble();
    }
    const QJsonObject titles = root.value("titles").toObject();
    for (auto it = titles.begin(); it != titles.end(); ++it)
        m_titles[it.key()] = it.value().toString();
    const QJsonObject completed = root.value("dailyCompletions").toObject();
    for (auto it = completed.begin(); it != completed.end(); ++it)
        m_dailyCompletions[it.key()] = it.value().toInt();
    for (const QJsonValue &value : root.value("completedPaths").toArray())
        m_completedPaths.insert(value.toString());
    for (const QJsonValue &value : root.value("sessions").toArray())
    {
        const QJsonObject item = value.toObject();
        const QDateTime started = QDateTime::fromString(item.value("started").toString(), Qt::ISODate);
        if (started.isValid())
            m_sessions.append({item.value("title").toString(), item.value("key").toString(),
                started, item.value("seconds").toDouble()});
    }
}

void ImmersionStats::save() const
{
    QJsonObject root{{"version", 1}, {"dailySeconds", numberMap(m_dailySeconds)}};
    QJsonObject dailyTitles;
    for (auto it = m_dailyTitles.cbegin(); it != m_dailyTitles.cend(); ++it)
        dailyTitles.insert(it.key(), numberMap(it.value()));
    root.insert("dailyTitles", dailyTitles);
    QJsonObject titles;
    for (auto it = m_titles.cbegin(); it != m_titles.cend(); ++it)
        titles.insert(it.key(), it.value());
    root.insert("titles", titles);
    QJsonObject completed;
    for (auto it = m_dailyCompletions.cbegin(); it != m_dailyCompletions.cend(); ++it)
        completed.insert(it.key(), it.value());
    root.insert("dailyCompletions", completed);
    QJsonArray completedPaths;
    for (const QString &path : m_completedPaths) completedPaths.append(path);
    root.insert("completedPaths", completedPaths);
    QJsonArray sessions;
    auto append = [&sessions](const Session &session) {
        if (session.seconds < 1) return;
        sessions.append(QJsonObject{{"title", session.title}, {"key", session.key},
            {"started", session.started.toString(Qt::ISODate)},
            {"seconds", session.seconds}});
    };
    append(m_current);
    for (const Session &session : m_sessions) append(session);
    root.insert("sessions", sessions);
    QSaveFile file(statsPath());
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
        file.commit();
    }
}
