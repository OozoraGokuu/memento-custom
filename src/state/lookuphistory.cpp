#include "state/lookuphistory.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUrl>
#include <QUuid>

#include "quick/episodefolder.h"
#include "util/directoryutils.h"

namespace {
constexpr int MaximumEntries = 10000;
constexpr qint64 MaximumFileSize = 64 * 1024 * 1024;

QString localPath(const QString &path)
{
    if (QFileInfo(path).isAbsolute()) return QDir::cleanPath(path).left(2048);
    const QUrl url(path);
    if (url.isLocalFile()) return QDir::cleanPath(url.toLocalFile()).left(2048);
    // Stream URLs may expire or contain credentials. They are never persisted.
    return {};
}
}

LookupHistory::LookupHistory(EpisodeFolder *library, QObject *parent,
                             const QString &storagePath)
    : QObject(parent), m_library(library),
      m_storagePath(storagePath.isEmpty()
          ? DirectoryUtils::getDataDir() + "/lookup-history.json" : storagePath)
{
    load();
}

QVariantMap LookupHistory::sanitizedContext(const QVariantMap &context)
{
    const double position = context.value("position", -1).toDouble();
    return {{"title", context.value("title").toString().left(512)},
            {"episode", context.value("episode").toString().left(512)},
            {"path", localPath(context.value("path").toString())},
            {"position", std::isfinite(position) && position >= 0 ? position : -1}};
}

QVariantMap LookupHistory::contextFor(const QString &path, double position) const
{
    if (path.isEmpty() || !std::isfinite(position) || position < 0) return {};
    const QVariantMap info = m_library ? m_library->playbackInfo(path) : QVariantMap{};
    const QString file = info.value("filename").toString().isEmpty()
        ? QFileInfo(localPath(path)).fileName() : info.value("filename").toString();
    const QString title = info.value("title").toString().isEmpty()
        ? QFileInfo(file).completeBaseName() : info.value("title").toString();
    return sanitizedContext({{"path", path}, {"position", position},
                             {"title", title}, {"episode", file}});
}

void LookupHistory::record(const QString &word, const QString &reading,
                           const QString &text, const QVariantMap &context)
{
    // Preserve an unreadable existing file until the user explicitly clears it.
    if (m_loadFailed) return;
    const QString normalizedWord = word.trimmed().left(256);
    if (normalizedWord.isEmpty()) return;
    QVariantMap entry = sanitizedContext(context);
    entry.insert("word", normalizedWord);
    entry.insert("reading", reading.trimmed().left(256));
    entry.insert("text", text.left(2048));
    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (!m_entries.isEmpty())
    {
        const QVariantMap previous = m_entries.first().toMap();
        const qint64 elapsed = QDateTime::fromString(
            previous.value("lookedUp").toString(), Qt::ISODateWithMs).msecsTo(now);
        const bool sameContext = previous.value("title") == entry.value("title") &&
            previous.value("episode") == entry.value("episode") &&
            previous.value("path") == entry.value("path") &&
            std::abs(previous.value("position").toDouble() -
                     entry.value("position").toDouble()) < 1;
        // Repeated hover notifications for the same result are one lookup.
        if (elapsed >= 0 && elapsed < 2000 && sameContext &&
            previous.value("word") == entry.value("word") &&
            previous.value("reading") == entry.value("reading") &&
            previous.value("text") == entry.value("text")) return;
    }
    entry.insert("id", QUuid::createUuid().toString(QUuid::WithoutBraces));
    entry.insert("lookedUp", now.toString(Qt::ISODateWithMs));
    QVariantList updated = m_entries;
    updated.prepend(entry);
    if (updated.size() > MaximumEntries) updated.removeLast();
    if (!save(updated)) return;
    m_entries = std::move(updated);
    ++m_revision;
    emit changed();
}

QVariantMap LookupHistory::query(const QString &filter, int days,
                                int offset, int limit) const
{
    const QString needle = filter.trimmed();
    const QDate first = days > 0 ? QDate::currentDate().addDays(1 - days) : QDate{};
    offset = std::max(0, offset);
    limit = std::clamp(limit, 1, 100);
    int total = 0;
    QVariantList entries;
    for (const QVariant &value : m_entries)
    {
        const QVariantMap entry = value.toMap();
        const QDate day = QDateTime::fromString(entry.value("lookedUp").toString(),
                                               Qt::ISODateWithMs).toLocalTime().date();
        if (days > 0 && (day < first || day > QDate::currentDate())) continue;
        bool matches = needle.isEmpty();
        for (const char *field : {"word", "reading", "text", "title", "episode"})
            matches |= entry.value(field).toString().contains(needle, Qt::CaseInsensitive);
        if (!matches) continue;
        if (total >= offset && entries.size() < limit) entries.append(entry);
        ++total;
    }
    return {{"entries", entries}, {"total", total}, {"retained", m_entries.size()},
            {"capacity", MaximumEntries}};
}

bool LookupHistory::remove(const QString &id)
{
    QVariantList updated = m_entries;
    for (qsizetype index = 0; index < updated.size(); ++index)
    {
        if (updated.at(index).toMap().value("id").toString() != id) continue;
        updated.removeAt(index);
        if (!save(updated)) return false;
        m_entries = std::move(updated);
        ++m_revision;
        emit changed();
        return true;
    }
    return false;
}

bool LookupHistory::clear()
{
    QVariantList empty;
    if (!save(empty)) return false;
    m_entries.clear();
    m_loadFailed = false;
    ++m_revision;
    emit changed();
    return true;
}

void LookupHistory::setError(const QString &message)
{
    if (message == m_lastError) return;
    m_lastError = message;
    emit lastErrorChanged();
}

bool LookupHistory::save(QVariantList &entries)
{
    QDir().mkpath(QFileInfo(m_storagePath).absolutePath());
    QSaveFile file(m_storagePath);
    auto serialize = [&entries] {
        return QJsonDocument(QJsonObject{{"version", 1},
            {"entries", QJsonArray::fromVariantList(entries)}}).toJson(QJsonDocument::Compact);
    };
    QByteArray data = serialize();
    // Keep the newest lookups within both the row and byte budgets. In
    // particular, escaped/UTF-8 text must never produce a file we cannot reload.
    while (data.size() > MaximumFileSize && !entries.isEmpty())
    {
        entries.removeLast();
        data = serialize();
    }
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
    {
        setError(tr("Could not save dictionary lookup history: %1").arg(file.errorString()));
        return false;
    }
    setError({});
    return true;
}

void LookupHistory::load()
{
    QFile file(m_storagePath);
    if (!file.exists()) return;
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaximumFileSize)
    {
        m_loadFailed = true;
        setError(tr("Could not read dictionary lookup history."));
        return;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() ||
        document.object().value("version").toInt() != 1 ||
        !document.object().value("entries").isArray())
    {
        m_loadFailed = true;
        setError(tr("Dictionary lookup history is not a valid history file."));
        return;
    }
    QSet<QString> ids;
    for (const QJsonValue &value : document.object().value("entries").toArray())
    {
        if (m_entries.size() >= MaximumEntries) break;
        const QVariantMap raw = value.toObject().toVariantMap();
        const QString id = raw.value("id").toString();
        const QDateTime time = QDateTime::fromString(raw.value("lookedUp").toString(), Qt::ISODateWithMs);
        const QString word = raw.value("word").toString().trimmed().left(256);
        if (id.isEmpty() || id.size() > 64 || ids.contains(id) || !time.isValid() || word.isEmpty()) continue;
        QVariantMap entry = sanitizedContext(raw);
        entry.insert("id", id);
        entry.insert("word", word);
        entry.insert("reading", raw.value("reading").toString().left(256));
        entry.insert("text", raw.value("text").toString().left(2048));
        entry.insert("lookedUp", time.toUTC().toString(Qt::ISODateWithMs));
        m_entries.append(entry);
        ids.insert(id);
    }
    std::stable_sort(m_entries.begin(), m_entries.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("lookedUp").toString() > b.toMap().value("lookedUp").toString();
    });
}
