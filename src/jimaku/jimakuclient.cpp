////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2026 Ripose
//
// This file is part of Memento.
//
// Memento is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2 of the License.
//
////////////////////////////////////////////////////////////////////////////////

#include "jimaku/jimakuclient.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHostAddress>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QUrlQuery>

#include <zip.h>

#include "player/mpvcontroller.h"
#include "player/mpvplayer.h"
#include "player/mpvstate.h"
#include "player/mpvtrack.h"
#include "state/context.h"
#include "util/directoryutils.h"

namespace
{

constexpr const char *DEFAULT_BASE_URL = "https://jimaku.cc";
constexpr const char *SETTINGS_GROUP = "jimaku";
constexpr const char *API_KEY_SETTING = "api-key";
constexpr const char *AUTO_FETCH_SETTING = "auto-fetch";
constexpr qint64 MAX_SUBTITLE_SIZE = 16 * 1024 * 1024;
constexpr qint64 MAX_DOWNLOAD_SIZE = 64 * 1024 * 1024;
constexpr qint64 MAX_JSON_SIZE = 4 * 1024 * 1024;
constexpr int MAX_REDIRECTS = 5;
constexpr int NETWORK_TIMEOUT_MS = 20000;
constexpr int MIN_AUTO_ENTRY_SCORE = 250;
constexpr int MAX_MANUAL_RESULTS = 100;
constexpr zip_int64_t MAX_ZIP_ENTRIES = 4096;
constexpr std::size_t MAX_ZIP_NAME_BYTES = 1024 * 1024;

QSettings makeSettings()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    return QSettings(DirectoryUtils::getCacheConfig(), QSettings::NativeFormat);
#else
    return QSettings();
#endif
}

QString normalized(QString value)
{
    value = value.normalized(QString::NormalizationForm_KC).toCaseFolded();
    value.remove(QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]")));
    return value;
}

QSet<QString> titleTokens(QString value)
{
    value = value.normalized(QString::NormalizationForm_KC).toCaseFolded();
    const QStringList parts = value.split(
        QRegularExpression(QStringLiteral("[^\\p{L}\\p{N}]+")),
        Qt::SkipEmptyParts
    );
    return QSet<QString>(parts.cbegin(), parts.cend());
}

QString extensionOf(const QString &name)
{
    return QFileInfo(name).suffix().toLower();
}

int effectivePort(const QUrl &url)
{
    const QString scheme = url.scheme().toCaseFolded();
    const int defaultPort = scheme == QStringLiteral("https") ? 443 :
        (scheme == QStringLiteral("http") ? 80 : -1);
    return url.port(defaultPort);
}

bool sameOrigin(const QUrl &left, const QUrl &right)
{
    return left.scheme().compare(right.scheme(), Qt::CaseInsensitive) == 0 &&
        left.host().compare(right.host(), Qt::CaseInsensitive) == 0 &&
        effectivePort(left) == effectivePort(right);
}

bool isLoopbackHost(const QString &host)
{
    if (host.compare(QStringLiteral("localhost"), Qt::CaseInsensitive) == 0)
    {
        return true;
    }

    QHostAddress address;
    return address.setAddress(host) && address.isLoopback();
}

bool safeApiBase(const QUrl &url)
{
    if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty())
    {
        return false;
    }

    const QString scheme = url.scheme().toCaseFolded();
    return scheme == QStringLiteral("https") ||
        (scheme == QStringLiteral("http") && isLoopbackHost(url.host()));
}

bool likelyBracketMetadata(int value)
{
    if (value >= 1900 && value <= 2199)
    {
        return true;
    }

    switch (value)
    {
        case 240:
        case 360:
        case 480:
        case 540:
        case 576:
        case 720:
        case 900:
        case 1080:
        case 1280:
        case 1440:
        case 1920:
        case 2160:
        case 2560:
        case 3840:
        case 4096:
        case 4320:
        case 7680:
            return true;

        default:
            return false;
    }
}

} // namespace

JimakuClient::JimakuClient(Context *context, QObject *parent) :
    QObject(parent),
    m_context(context),
    m_manager(this)
{
    const QString overriddenBase = qEnvironmentVariable(
        "MEMENTO_JIMAKU_API_BASE"
    ).trimmed();
    const QUrl configuredBase(overriddenBase.isEmpty() ?
        QString::fromLatin1(DEFAULT_BASE_URL) : overriddenBase,
        QUrl::StrictMode);
    m_baseUrl = safeApiBase(configuredBase) ? configuredBase :
        QUrl(QString::fromLatin1(DEFAULT_BASE_URL));
    m_manager.setTransferTimeout(NETWORK_TIMEOUT_MS);
    m_attachTimer.setSingleShot(true);
    m_attachTimer.setInterval(NETWORK_TIMEOUT_MS);
    connect(&m_attachTimer, &QTimer::timeout, this, [this] {
        if (m_pendingAttachPath.isEmpty())
        {
            return;
        }
        clearPendingAttach();
        finishError(tr(
            "Memento did not confirm the Jimaku subtitle track in time."
        ));
    });

    QSettings settings = makeSettings();
    settings.beginGroup(SETTINGS_GROUP);
    const QString environmentKey = qEnvironmentVariable(
        "MEMENTO_JIMAKU_API_KEY"
    ).trimmed();
    m_apiKey = environmentKey.isEmpty() ?
        settings.value(API_KEY_SETTING).toString().trimmed() : environmentKey;
    m_autoFetch = settings.value(AUTO_FETCH_SETTING, true).toBool();
    settings.endGroup();
}

const QString &JimakuClient::apiKey() const noexcept
{
    return m_apiKey;
}

void JimakuClient::setApiKey(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (m_apiKey == trimmed)
    {
        return;
    }
    m_apiKey = trimmed;
    QSettings settings = makeSettings();
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue(API_KEY_SETTING, m_apiKey);
    settings.endGroup();
    settings.sync();
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    QFile::setPermissions(
        DirectoryUtils::getCacheConfig(),
        QFileDevice::ReadOwner | QFileDevice::WriteOwner
    );
#endif
    emit apiKeyChanged();
}

bool JimakuClient::apiKeyConfigured() const noexcept
{
    return !m_apiKey.isEmpty();
}

bool JimakuClient::autoFetch() const noexcept
{
    return m_autoFetch;
}

void JimakuClient::setAutoFetch(bool value)
{
    if (m_autoFetch == value)
    {
        return;
    }
    m_autoFetch = value;
    QSettings settings = makeSettings();
    settings.beginGroup(SETTINGS_GROUP);
    settings.setValue(AUTO_FETCH_SETTING, m_autoFetch);
    settings.endGroup();
    settings.sync();
    emit autoFetchChanged(m_autoFetch);
}

bool JimakuClient::busy() const noexcept
{
    return m_busy;
}

const QString &JimakuClient::status() const noexcept
{
    return m_status;
}

const QVariantList &JimakuClient::searchResults() const noexcept
{
    return m_searchResultMaps;
}

const QVariantList &JimakuClient::fileResults() const noexcept
{
    return m_fileResultMaps;
}

const QString &JimakuClient::selectedEntryName() const noexcept
{
    return m_entryName;
}

int JimakuClient::selectedEpisode() const noexcept
{
    return m_selectedEpisode;
}

void JimakuClient::fetchForCurrentMedia()
{
    if (!apiKeyConfigured())
    {
        if (!m_busy)
        {
            finishError(tr("Add your Jimaku API key in Settings first."));
        }
        return;
    }

    MediaInfo media = currentMedia();
    if (media.path.isEmpty() || media.title.isEmpty())
    {
        if (!m_busy)
        {
            finishError(tr("Play an episode before fetching Jimaku subtitles."));
        }
        return;
    }
    if (media.episode < 0)
    {
        if (!m_busy)
        {
            finishError(tr(
                "Memento could not determine this episode number. Use Search "
                "Jimaku Subtitles to choose the episode and file manually."
            ));
        }
        return;
    }

    // A valid media-load fetch supersedes any request associated with the
    // previous file. Validation failures above leave that request untouched.
    cancelActive();
    const quint64 generation = ++m_generation;
    m_operation = Operation::Fetch;
    m_requestApiKey = m_apiKey;
    m_media = std::move(media);
    m_entryName.clear();
    m_file = {};
    setBusy(true);

    if (m_media.episode >= 0)
    {
        setStatus(tr("Searching Jimaku for %1 — episode %2…")
            .arg(m_media.title)
            .arg(m_media.episode));
    }
    else
    {
        setStatus(tr("Searching Jimaku for %1…").arg(m_media.title));
    }
    beginSearch(generation, m_media.title);
}

void JimakuClient::testConnection()
{
    if (m_busy)
    {
        return;
    }
    if (!apiKeyConfigured())
    {
        const QString error = tr("Enter a Jimaku API key first.");
        setStatus(error);
        emit connectionTested(false, error);
        return;
    }
    const quint64 generation = ++m_generation;
    m_operation = Operation::Test;
    m_requestApiKey = m_apiKey;
    setBusy(true);
    setStatus(tr("Testing Jimaku connection…"));
    beginSearch(generation, QStringLiteral("Frieren"));
}

void JimakuClient::search(const QString &query)
{
    if (m_busy)
    {
        return;
    }
    if (!apiKeyConfigured())
    {
        finishError(tr("Add your Jimaku API key in Settings first."));
        return;
    }
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
    {
        finishError(tr("Enter an anime title to search Jimaku."));
        return;
    }

    cancelActive();
    const quint64 generation = ++m_generation;
    m_operation = Operation::ManualSearch;
    m_requestApiKey = m_apiKey;
    m_manualQuery = trimmed;
    m_entryName.clear();
    m_selectedEntryIndex = -1;
    m_selectedEpisode = -1;
    m_fileListMedia = {};
    setSearchEntries({});
    setBrowseFiles({});
    emit selectionChanged();
    setBusy(true);
    setStatus(tr("Searching Jimaku for “%1”…").arg(trimmed));
    beginSearch(generation, trimmed);
}

void JimakuClient::selectEntry(int resultIndex, int episode)
{
    selectEntryFiles(resultIndex, episode, true);
}

void JimakuClient::selectEntryAllFiles(int resultIndex, int episode)
{
    selectEntryFiles(resultIndex, episode, false);
}

void JimakuClient::selectEntryFiles(
    int resultIndex,
    int episode,
    bool withEpisode, bool persist)
{
    if (m_busy)
    {
        return;
    }
    if (resultIndex < 0 || resultIndex >= m_searchEntries.size())
    {
        finishError(tr("The selected Jimaku title is no longer available."));
        return;
    }
    const QJsonObject entry = m_searchEntries.at(resultIndex);
    const qint64 entryId = entry.value(QStringLiteral("id")).toInteger(-1);
    if (entryId < 0)
    {
        finishError(tr("Jimaku returned an invalid title identifier."));
        return;
    }

    cancelActive();
    const quint64 generation = ++m_generation;
    m_operation = Operation::ManualFiles;
    m_requestApiKey = m_apiKey;
    m_selectedEntryIndex = resultIndex;
    m_selectedEpisode = std::max(-1, episode);
    m_fileListMedia = currentMedia();
    m_entryName = entry.value(QStringLiteral("name")).toString();
    if (m_entryName.isEmpty())
    {
        m_entryName = entry.value(QStringLiteral("english_name")).toString();
    }
    if (persist && m_context && m_context->episodeLibrary())
    {
        auto *library = m_context->episodeLibrary();
        if (library->containsFile(m_fileListMedia.path) &&
            !library->setSubtitleLinkForFile(m_fileListMedia.path,
                {{"provider", "jimaku"}, {"name", m_entryName}, {"entryId", entryId}}))
            emit failed(tr("Could not save the subtitle link. Check library storage permissions."));
    }
    setBrowseFiles({});
    emit selectionChanged();
    setBusy(true);
    setStatus(withEpisode && m_selectedEpisode >= 0 ?
        tr("Finding episode %1 subtitles in %2…")
            .arg(m_selectedEpisode).arg(m_entryName) :
        tr("Loading all subtitle files in %1…").arg(m_entryName));
    beginFileList(
        generation,
        entryId,
        withEpisode && m_selectedEpisode >= 0
    );
}

void JimakuClient::openLinkedEntry(const QVariantMap &link, int episode, bool all)
{
    if (m_busy) return;
    bool ok = false;
    const qint64 id = link.value("entryId").toLongLong(&ok);
    if (link.value("provider").toString() != "jimaku" || !ok || id <= 0)
    {
        finishError(tr("Invalid saved Jimaku link. Use Change link to select the title again."));
        return;
    }
    setSearchEntries({QJsonObject{{"id", id}, {"name", link.value("name").toString()}}});
    selectEntryFiles(0, episode, !all, false);
}

void JimakuClient::attachResult(int resultIndex)
{
    if (m_busy)
    {
        return;
    }
    if (resultIndex < 0 || resultIndex >= m_browseFiles.size())
    {
        finishError(tr("The selected Jimaku file is no longer available."));
        return;
    }
    MediaInfo media = currentMedia();
    if (media.path.isEmpty())
    {
        finishError(tr("Play an episode before adding a Jimaku subtitle."));
        return;
    }
    if (m_fileListMedia.path.isEmpty() ||
        media.path != m_fileListMedia.path ||
        media.title != m_fileListMedia.title ||
        media.season != m_fileListMedia.season ||
        media.episode != m_fileListMedia.episode)
    {
        finishError(tr(
            "The playing episode changed after these Jimaku files were "
            "loaded. Choose the title and episode again."
        ));
        return;
    }
    if (m_selectedEpisode >= 0)
    {
        media.episode = m_selectedEpisode;
    }

    cancelActive();
    const quint64 generation = ++m_generation;
    m_operation = Operation::ManualDownload;
    m_requestApiKey = m_apiKey;
    m_media = std::move(media);
    m_file = m_browseFiles.at(resultIndex);
    setBusy(true);
    useSelectedFile(generation);
}

void JimakuClient::cancel()
{
    cancelActive();
}

void JimakuClient::clearSearch()
{
    cancelActive();
    m_manualQuery.clear();
    m_entryName.clear();
    m_selectedEntryIndex = -1;
    m_selectedEpisode = -1;
    m_fileListMedia = {};
    setSearchEntries({});
    setBrowseFiles({});
    setStatus({});
    emit selectionChanged();
}

QString JimakuClient::suggestedTitle() const
{
    return currentMedia().title;
}

int JimakuClient::suggestedEpisode() const
{
    return currentMedia().episode;
}

JimakuClient::MediaInfo JimakuClient::currentMedia() const
{
    MediaInfo result;
    if (m_context == nullptr || m_context->player() == nullptr)
    {
        return result;
    }

    const MpvState *state = m_context->player()->state();
    result.path = state->path();

    QUrl url(result.path);
    QString path = url.isLocalFile() ? url.toLocalFile() : url.path();
    QString filename = QFileInfo(path).fileName();
    if (filename.isEmpty())
    {
        filename = state->title();
    }

    result.episode = episodeNumber(filename);
    result.season = seasonNumber(filename);
    if (result.episode < 0)
    {
        result.episode = episodeNumber(state->title());
    }
    if (result.season < 0)
    {
        result.season = seasonNumber(state->title());
    }
    result.title = cleanTitle(filename);
    if (result.title.isEmpty())
    {
        result.title = cleanTitle(state->title());
    }
    return result;
}

void JimakuClient::beginSearch(quint64 generation, const QString &query)
{
    QUrl url = m_baseUrl.resolved(QUrl(QStringLiteral("/api/entries/search")));
    QUrlQuery parameters;
    parameters.addQueryItem(QStringLiteral("anime"), QStringLiteral("true"));
    parameters.addQueryItem(QStringLiteral("query"), query);
    url.setQuery(parameters);

    QNetworkReply *reply = getApi(url);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        handleSearch(reply, generation);
    });
}

void JimakuClient::handleSearch(QNetworkReply *reply, quint64 generation)
{
    QJsonArray entries;
    QString error;
    const bool parsed = parseJsonArray(reply, &entries, &error);
    reply->deleteLater();
    if (generation != m_generation)
    {
        return;
    }
    m_reply = nullptr;

    if (!parsed)
    {
        if (m_operation == Operation::Test)
        {
            setBusy(false);
            setStatus(error);
            emit connectionTested(false, error);
            m_operation = Operation::None;
        }
        else
        {
            finishError(error);
        }
        return;
    }

    if (m_operation == Operation::Test)
    {
        const QString message = tr("Connected to Jimaku successfully.");
        setBusy(false);
        setStatus(message);
        emit connectionTested(true, message);
        m_operation = Operation::None;
        return;
    }

    if (m_operation == Operation::ManualSearch)
    {
        QVector<QJsonObject> results;
        results.reserve(std::min(
            static_cast<int>(entries.size()), MAX_MANUAL_RESULTS
        ));
        for (const QJsonValue &value : entries)
        {
            const QJsonObject entry = value.toObject();
            if (entry.value(QStringLiteral("id")).toInteger(-1) >= 0)
            {
                results.emplaceBack(entry);
            }
        }
        std::stable_sort(
            results.begin(),
            results.end(),
            [this](const QJsonObject &left, const QJsonObject &right) {
                return entryScore(m_manualQuery, left) >
                    entryScore(m_manualQuery, right);
            }
        );
        if (results.size() > MAX_MANUAL_RESULTS)
        {
            results.resize(MAX_MANUAL_RESULTS);
        }
        setSearchEntries(std::move(results));
        m_operation = Operation::None;
        setBusy(false);
        setStatus(m_searchEntries.isEmpty() ?
            tr("Jimaku has no anime matching “%1”.").arg(m_manualQuery) :
            tr("Choose the exact Jimaku title, then choose an episode and file."));
        return;
    }

    if (entries.isEmpty())
    {
        finishError(tr("Jimaku has no anime matching “%1”.").arg(m_media.title));
        return;
    }

    int bestScore = std::numeric_limits<int>::min();
    int secondBestScore = std::numeric_limits<int>::min();
    QJsonObject bestEntry;
    for (const QJsonValue &value : entries)
    {
        const QJsonObject entry = value.toObject();
        if (!entryMatchesSeason(m_media.season, entry))
        {
            continue;
        }
        const int score = entryScore(m_media.title, entry);
        if (bestEntry.isEmpty() || score > bestScore)
        {
            if (!bestEntry.isEmpty())
            {
                secondBestScore = bestScore;
            }
            bestScore = score;
            bestEntry = entry;
        }
        else
        {
            secondBestScore = std::max(secondBestScore, score);
        }
    }

    const qint64 entryId = bestEntry.value(QStringLiteral("id")).toInteger(-1);
    const bool ambiguous = secondBestScore != std::numeric_limits<int>::min() &&
        bestScore - secondBestScore < 100;
    if (entryId < 0 || bestScore < MIN_AUTO_ENTRY_SCORE || ambiguous)
    {
        finishError(tr(
            "Jimaku did not return a confident title match. Use Search Jimaku "
            "Subtitles to choose the title manually."
        ));
        return;
    }
    m_entryName = bestEntry.value(QStringLiteral("name")).toString();
    setStatus(tr("Finding subtitles in %1…").arg(m_entryName));
    beginFileList(generation, entryId, m_media.episode >= 0);
}

void JimakuClient::beginFileList(
    quint64 generation,
    qint64 entryId,
    bool withEpisode)
{
    QUrl url = m_baseUrl.resolved(QUrl(
        QStringLiteral("/api/entries/%1/files").arg(entryId)
    ));
    if (withEpisode)
    {
        QUrlQuery parameters;
        const int episode = m_operation == Operation::ManualFiles ?
            m_selectedEpisode : m_media.episode;
        parameters.addQueryItem(
            QStringLiteral("episode"), QString::number(episode)
        );
        url.setQuery(parameters);
    }

    QNetworkReply *reply = getApi(url);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, generation, entryId, withEpisode] {
            handleFileList(reply, generation, entryId, withEpisode);
        });
}

void JimakuClient::handleFileList(
    QNetworkReply *reply,
    quint64 generation,
    qint64 entryId,
    bool withEpisode)
{
    QJsonArray files;
    QString error;
    const bool parsed = parseJsonArray(reply, &files, &error);
    reply->deleteLater();
    if (generation != m_generation)
    {
        return;
    }
    m_reply = nullptr;

    if (!parsed)
    {
        finishError(error);
        return;
    }

    if (m_operation == Operation::ManualFiles)
    {
        setBrowseFiles(usableFiles(files));
        if (m_context && m_selectedEpisode >= 0 && !m_browseFiles.isEmpty())
            m_context->episodeLibrary()->rememberSubtitleSearch(
                {{"provider", "jimaku"}, {"name", m_entryName}, {"entryId", entryId}}, m_selectedEpisode);
        m_operation = Operation::None;
        setBusy(false);
        if (m_browseFiles.isEmpty())
        {
            setStatus(withEpisode ?
                tr("No usable file was tagged as episode %1. Try All files.")
                    .arg(m_selectedEpisode) :
                tr("This Jimaku title has no usable ASS, SSA, SRT, VTT, or ZIP files."));
        }
        else
        {
            setStatus(tr("Choose the exact subtitle file to add to Memento."));
        }
        return;
    }

    const bool requireExplicitEpisode =
        !withEpisode && m_media.episode >= 0;
    m_file = selectFile(files, requireExplicitEpisode);
    if (m_file.url.isEmpty() && withEpisode)
    {
        setStatus(tr("Trying all subtitle files for %1…").arg(m_entryName));
        beginFileList(generation, entryId, false);
        return;
    }
    if (m_file.url.isEmpty())
    {
        finishError(tr(
            "Jimaku has no directly usable ASS, SSA, SRT, VTT, or ZIP "
            "subtitle for this episode."
        ));
        return;
    }
    useSelectedFile(generation);
}

void JimakuClient::useSelectedFile(quint64 generation)
{
    if (m_file.name.isEmpty() || !safeDownloadUrl(m_file.url) ||
        m_file.size <= 0 || m_file.size > MAX_DOWNLOAD_SIZE)
    {
        finishError(tr("The selected Jimaku file is invalid or has an unsafe size."));
        return;
    }

    const QString existing = cachePath(m_file.name);
    const QFileInfo existingInfo(existing);
    if (!m_file.lastModified.isEmpty() && existingInfo.isFile() &&
        existingInfo.size() == m_file.size)
    {
        if (extensionOf(existing) == QStringLiteral("zip"))
        {
            QString extractError;
            const QString extracted = extractZip(existing, &extractError);
            if (extracted.isEmpty())
            {
                // A same-size cached archive can still be corrupt. Evict it
                // and make one fresh network request instead of permanently
                // poisoning this result across retries.
                if (!QFile::remove(existing))
                {
                    finishError(extractError);
                    return;
                }
            }
            else
            {
                attachPath(extracted, generation);
                return;
            }
        }
        else
        {
            attachPath(existing, generation);
            return;
        }
    }

    setStatus(tr("Downloading %1…").arg(m_file.name));
    beginDownload(generation, m_file.url);
}

void JimakuClient::beginDownload(
    quint64 generation,
    const QUrl &url,
    int redirects)
{
    if (!safeDownloadUrl(url))
    {
        finishError(tr("Jimaku returned an unsafe subtitle download URL."));
        return;
    }
    QNetworkReply *reply = getDownload(url);
    m_reply = reply;
    connect(reply, &QNetworkReply::downloadProgress, this,
        [reply](qint64 received, qint64) {
            if (received > MAX_DOWNLOAD_SIZE)
            {
                reply->setProperty("mementoOversized", true);
                reply->abort();
            }
        });
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, generation, redirects] {
            handleDownload(reply, generation, redirects);
        });
}

void JimakuClient::handleDownload(
    QNetworkReply *reply,
    quint64 generation,
    int redirects)
{
    const QUrl redirect = reply->attribute(
        QNetworkRequest::RedirectionTargetAttribute
    ).toUrl();
    if (generation != m_generation)
    {
        reply->deleteLater();
        return;
    }
    if (!redirect.isEmpty())
    {
        const QUrl next = reply->url().resolved(redirect);
        reply->deleteLater();
        m_reply = nullptr;
        if (redirects >= MAX_REDIRECTS || !safeDownloadUrl(next))
        {
            finishError(tr("Jimaku download redirected too many times."));
            return;
        }
        beginDownload(generation, next, redirects + 1);
        return;
    }

    if (reply->property("mementoOversized").toBool())
    {
        reply->deleteLater();
        m_reply = nullptr;
        finishError(tr("Jimaku returned an oversized subtitle file."));
        return;
    }

    if (reply->error() != QNetworkReply::NoError)
    {
        const QString error = replyError(reply);
        reply->deleteLater();
        m_reply = nullptr;
        finishError(error);
        return;
    }
    const QByteArray data = reply->readAll();
    reply->deleteLater();
    m_reply = nullptr;
    if (data.isEmpty() || data.size() > MAX_DOWNLOAD_SIZE)
    {
        finishError(tr("Jimaku returned an empty or oversized subtitle file."));
        return;
    }
    attachDownloaded(data, generation);
}

void JimakuClient::attachDownloaded(
    const QByteArray &data,
    quint64 generation)
{
    const QString path = cachePath(m_file.name);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
        !file.commit())
    {
        finishError(tr("Memento could not save the Jimaku subtitle."));
        return;
    }

    if (extensionOf(path) == QStringLiteral("zip"))
    {
        QString error;
        const QString extracted = extractZip(path, &error);
        if (extracted.isEmpty())
        {
            finishError(error);
            return;
        }
        attachPath(extracted, generation);
        return;
    }
    attachPath(path, generation);
}

void JimakuClient::attachPath(const QString &path, quint64 generation)
{
    if (generation != m_generation || m_context == nullptr ||
        m_context->player() == nullptr)
    {
        return;
    }
    if (m_context->player()->state()->path() != m_media.path)
    {
        finishError(tr("The episode changed before the subtitle finished downloading."));
        return;
    }
    clearPendingAttach();
    m_pendingAttachPath = QFileInfo(path).absoluteFilePath();
    m_pendingAttachGeneration = generation;
    m_attachTrackConnection = connect(
        m_context->player()->state(), &MpvState::subtitleTracksChanged,
        this, &JimakuClient::handleAttachedTracksChanged
    );
    m_attachTimer.start();

    if (!m_context->player()->controller()->loadSubtitle(path))
    {
        clearPendingAttach();
        finishError(tr("Memento could not attach the downloaded Jimaku subtitle."));
        return;
    }

    handleAttachedTracksChanged();
}

void JimakuClient::handleAttachedTracksChanged()
{
    if (m_pendingAttachPath.isEmpty() ||
        m_pendingAttachGeneration != m_generation ||
        m_context == nullptr || m_context->player() == nullptr)
    {
        return;
    }
    if (m_context->player()->state()->path() != m_media.path)
    {
        clearPendingAttach();
        finishError(tr(
            "The episode changed before the subtitle track was attached."
        ));
        return;
    }

    const QFileInfo expectedInfo(m_pendingAttachPath);
    QString expected = expectedInfo.canonicalFilePath();
    if (expected.isEmpty())
    {
        expected = expectedInfo.absoluteFilePath();
    }
    expected = QDir::cleanPath(expected);

    bool attached = false;
    for (const MpvTrack *track : m_context->player()->state()->subtitleTracks())
    {
        if (track == nullptr || !track->external())
        {
            continue;
        }
        const QUrl trackUrl(track->externalFilename());
        const QString trackPath = trackUrl.isLocalFile() ?
            trackUrl.toLocalFile() : track->externalFilename();
        const QFileInfo actualInfo(trackPath);
        QString actual = actualInfo.canonicalFilePath();
        if (actual.isEmpty())
        {
            actual = actualInfo.absoluteFilePath();
        }
        actual = QDir::cleanPath(actual);
#if defined(Q_OS_WIN)
        attached = actual.compare(expected, Qt::CaseInsensitive) == 0;
#else
        attached = actual == expected;
#endif
        if (attached)
        {
            break;
        }
    }
    if (!attached)
    {
        return;
    }

    const QString fileName = QFileInfo(m_pendingAttachPath).fileName();
    clearPendingAttach();
    emit subtitleAttached(fileName, m_entryName);
    finishSuccess(tr("Jimaku subtitle attached: %1").arg(fileName));
}

void JimakuClient::clearPendingAttach()
{
    m_attachTimer.stop();
    QObject::disconnect(m_attachTrackConnection);
    m_attachTrackConnection = {};
    m_pendingAttachPath.clear();
    m_pendingAttachGeneration = 0;
}

QNetworkReply *JimakuClient::getApi(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Memento/2.1 Jimaku/1");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", m_requestApiKey.toUtf8());
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::SameOriginRedirectPolicy
    );
    QNetworkReply *reply = m_manager.get(request);
    connect(reply, &QNetworkReply::downloadProgress, this,
        [reply](qint64 received, qint64) {
            if (received > MAX_JSON_SIZE)
            {
                reply->setProperty("mementoOversized", true);
                reply->abort();
            }
        });
    return reply;
}

QNetworkReply *JimakuClient::getDownload(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Memento/2.1 Jimaku/1");
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::ManualRedirectPolicy
    );

    if (sameOrigin(url, m_baseUrl))
    {
        request.setRawHeader("Authorization", m_requestApiKey.toUtf8());
    }
    return m_manager.get(request);
}

bool JimakuClient::parseJsonArray(
    QNetworkReply *reply,
    QJsonArray *array,
    QString *error) const
{
    if (reply->property("mementoOversized").toBool())
    {
        *error = tr("Jimaku returned an oversized response.");
        return false;
    }
    if (reply->error() != QNetworkReply::NoError)
    {
        *error = replyError(reply);
        return false;
    }

    const QByteArray data = reply->readAll();
    if (data.size() > MAX_JSON_SIZE)
    {
        *error = tr("Jimaku returned an oversized response.");
        return false;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray())
    {
        *error = tr("Jimaku returned an invalid response.");
        return false;
    }
    *array = document.array();
    return true;
}

QString JimakuClient::replyError(QNetworkReply *reply) const
{
    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute
    ).toInt();
    if (status == 401)
    {
        return tr("Jimaku rejected the API key.");
    }
    if (status == 429)
    {
        return tr("Jimaku's rate limit was reached. Try again shortly.");
    }

    const QJsonDocument document = QJsonDocument::fromJson(reply->peek(65536));
    const QString apiError = document.object().value(
        QStringLiteral("error")
    ).toString();
    if (!apiError.isEmpty())
    {
        return tr("Jimaku error: %1").arg(apiError);
    }
    return tr("Could not reach Jimaku: %1").arg(reply->errorString());
}

JimakuClient::FileInfo JimakuClient::selectFile(
    const QJsonArray &files,
    bool requireExplicitEpisode) const
{
    FileInfo result;
    int bestScore = std::numeric_limits<int>::min();
    for (const QJsonValue &value : files)
    {
        const QJsonObject file = value.toObject();
        const QString name = file.value(QStringLiteral("name")).toString();
        const qint64 size = file.value(QStringLiteral("size")).toInteger();
        const QUrl url(file.value(QStringLiteral("url")).toString());
        if (name.isEmpty() || !url.isValid() || size <= 0 ||
            size > MAX_DOWNLOAD_SIZE)
        {
            continue;
        }

        const int detectedEpisode = episodeNumber(name);
        if (m_media.episode >= 0 && detectedEpisode >= 0 &&
            detectedEpisode != m_media.episode)
        {
            continue;
        }
        // A generic direct subtitle is ambiguous, but a generic ZIP can be a
        // season batch whose inner filenames carry the exact episode. The ZIP
        // extractor applies the strict episode check before attaching it.
        const bool genericBatch = detectedEpisode < 0 &&
            extensionOf(name) == QStringLiteral("zip");
        if (requireExplicitEpisode &&
            detectedEpisode != m_media.episode && !genericBatch)
        {
            continue;
        }

        const int score = fileScore(name, m_media.episode);
        if (score <= -10000 || score <= bestScore)
        {
            continue;
        }
        bestScore = score;
        result = {
            name,
            url.isRelative() ? m_baseUrl.resolved(url) : url,
            size,
            file.value(QStringLiteral("last_modified")).toString(),
        };
    }
    return result;
}

QVector<JimakuClient::FileInfo> JimakuClient::usableFiles(
    const QJsonArray &files) const
{
    QVector<FileInfo> result;
    result.reserve(files.size());
    for (const QJsonValue &value : files)
    {
        const QJsonObject object = value.toObject();
        const QString name = object.value(QStringLiteral("name")).toString();
        const qint64 size = object.value(QStringLiteral("size")).toInteger();
        QUrl url(object.value(QStringLiteral("url")).toString());
        if (url.isRelative())
        {
            url = m_baseUrl.resolved(url);
        }
        const QString extension = extensionOf(name);
        if (name.isEmpty() || size <= 0 || size > MAX_DOWNLOAD_SIZE ||
            (!supportedSubtitle(name) && extension != QStringLiteral("zip")) ||
            !safeDownloadUrl(url))
        {
            continue;
        }
        result.emplaceBack(FileInfo{
            name,
            url,
            size,
            object.value(QStringLiteral("last_modified")).toString(),
        });
    }
    std::stable_sort(
        result.begin(),
        result.end(),
        [this](const FileInfo &left, const FileInfo &right) {
            const int leftScore = fileScore(left.name, m_selectedEpisode);
            const int rightScore = fileScore(right.name, m_selectedEpisode);
            return leftScore == rightScore ?
                left.name.compare(right.name, Qt::CaseInsensitive) < 0 :
                leftScore > rightScore;
        }
    );
    return result;
}

QVariantMap JimakuClient::entryMap(const QJsonObject &entry) const
{
    QString name = entry.value(QStringLiteral("name")).toString();
    const QString english = entry.value(QStringLiteral("english_name")).toString();
    const QString japanese = entry.value(QStringLiteral("japanese_name")).toString();
    if (name.isEmpty())
    {
        name = english.isEmpty() ? japanese : english;
    }
    return {
        {QStringLiteral("id"), entry.value(QStringLiteral("id")).toInteger(-1)},
        {QStringLiteral("name"), name},
        {QStringLiteral("englishName"), english},
        {QStringLiteral("japaneseName"), japanese},
        {QStringLiteral("anime"), entry.value(QStringLiteral("anime")).toBool()},
        {QStringLiteral("movie"), entry.value(QStringLiteral("movie")).toBool()},
        {QStringLiteral("adult"), entry.value(QStringLiteral("adult")).toBool()},
        {QStringLiteral("external"), entry.value(QStringLiteral("external")).toBool()},
        {QStringLiteral("unverified"), entry.value(QStringLiteral("unverified")).toBool()},
        {QStringLiteral("score"), entryScore(m_manualQuery, entry)},
    };
}

QVariantMap JimakuClient::fileMap(const FileInfo &file) const
{
    const QString extension = extensionOf(file.name);
    return {
        {QStringLiteral("name"), file.name},
        {QStringLiteral("size"), file.size},
        {QStringLiteral("url"), file.url},
        {QStringLiteral("lastModified"), file.lastModified},
        {QStringLiteral("episode"), episodeNumber(file.name)},
        {QStringLiteral("format"), extension.toUpper()},
        {QStringLiteral("archive"), extension == QStringLiteral("zip")},
    };
}

bool JimakuClient::safeDownloadUrl(const QUrl &url) const
{
    if (!safeApiBase(url))
    {
        return false;
    }
    return sameOrigin(url, m_baseUrl);
}

void JimakuClient::setSearchEntries(QVector<QJsonObject> entries)
{
    m_searchEntries = std::move(entries);
    m_searchResultMaps.clear();
    m_searchResultMaps.reserve(m_searchEntries.size());
    for (const QJsonObject &entry : std::as_const(m_searchEntries))
    {
        m_searchResultMaps.emplaceBack(entryMap(entry));
    }
    emit searchResultsChanged();
}

void JimakuClient::setBrowseFiles(QVector<FileInfo> files)
{
    m_browseFiles = std::move(files);
    m_fileResultMaps.clear();
    m_fileResultMaps.reserve(m_browseFiles.size());
    for (const FileInfo &file : std::as_const(m_browseFiles))
    {
        m_fileResultMaps.emplaceBack(fileMap(file));
    }
    emit fileResultsChanged();
}

QString JimakuClient::cachePath(const QString &name) const
{
    const QByteArray identity = (
        m_file.url.toString() + QLatin1Char('\n') +
        name + QLatin1Char('\n') +
        m_file.lastModified + QLatin1Char('\n') +
        QString::number(m_file.size)
    ).toUtf8();
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(
        identity, QCryptographicHash::Sha256
    ).toHex().left(24));
    const QString extension = extensionOf(name);
    return QDir(DirectoryUtils::getCacheDir()).filePath(
        QStringLiteral("jimaku/%1.%2").arg(digest, extension)
    );
}

QString JimakuClient::extractZip(
    const QString &archivePath,
    QString *error) const
{
    int zipError = 0;
    zip_t *archive = zip_open(
        QFile::encodeName(archivePath).constData(), ZIP_RDONLY, &zipError
    );
    if (archive == nullptr)
    {
        *error = tr("The Jimaku ZIP archive could not be opened.");
        return {};
    }

    const zip_int64_t count = zip_get_num_entries(archive, 0);
    if (count < 0 || count > MAX_ZIP_ENTRIES)
    {
        zip_close(archive);
        *error = tr("The Jimaku ZIP archive contains too many files.");
        return {};
    }

    zip_int64_t bestIndex = -1;
    QString bestName;
    int bestScore = std::numeric_limits<int>::min();
    zip_int64_t bestUntaggedIndex = -1;
    QString bestUntaggedName;
    int bestUntaggedScore = std::numeric_limits<int>::min();
    int untaggedCount = 0;
    std::size_t nameBytes = 0;
    for (zip_int64_t index = 0; index < count; ++index)
    {
        zip_stat_t stat;
        zip_stat_init(&stat);
        if (zip_stat_index(archive, index, 0, &stat) != 0 ||
            stat.name == nullptr)
        {
            continue;
        }
        const std::size_t currentNameBytes = std::strlen(stat.name);
        if (currentNameBytes > MAX_ZIP_NAME_BYTES - nameBytes)
        {
            zip_close(archive);
            *error = tr("The Jimaku ZIP archive has an oversized file list.");
            return {};
        }
        nameBytes += currentNameBytes;
        if (stat.size <= 0 ||
            stat.size > static_cast<zip_uint64_t>(MAX_SUBTITLE_SIZE))
        {
            continue;
        }
        const QString name = QString::fromUtf8(stat.name);
        if (!supportedSubtitle(name))
        {
            continue;
        }
        const int detectedEpisode = episodeNumber(name);
        if (m_media.episode >= 0)
        {
            if (detectedEpisode >= 0 && detectedEpisode != m_media.episode)
            {
                continue;
            }
            if (detectedEpisode < 0)
            {
                ++untaggedCount;
                const int score = fileScore(name, m_media.episode);
                if (score > bestUntaggedScore)
                {
                    bestUntaggedScore = score;
                    bestUntaggedIndex = index;
                    bestUntaggedName = name;
                }
                continue;
            }
        }
        const int score = fileScore(name, m_media.episode);
        if (score > bestScore)
        {
            bestScore = score;
            bestIndex = index;
            bestName = name;
        }
    }

    // An archive containing exactly one unnumbered subtitle is plausibly an
    // episode-specific download. Multiple unnumbered members are ambiguous;
    // never guess when the caller requested a particular episode.
    if (bestIndex < 0 && m_media.episode >= 0 && untaggedCount == 1)
    {
        bestIndex = bestUntaggedIndex;
        bestName = bestUntaggedName;
    }

    if (bestIndex < 0)
    {
        zip_close(archive);
        *error = tr("The Jimaku ZIP archive contains no usable subtitle file.");
        return {};
    }

    zip_file_t *source = zip_fopen_index(archive, bestIndex, 0);
    if (source == nullptr)
    {
        zip_close(archive);
        *error = tr("The selected subtitle could not be read from the ZIP archive.");
        return {};
    }

    const QByteArray identity = (archivePath + QLatin1Char('\n') + bestName)
        .toUtf8();
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(
        identity, QCryptographicHash::Sha256
    ).toHex().left(24));
    const QString outputPath = QDir(DirectoryUtils::getCacheDir()).filePath(
        QStringLiteral("jimaku/%1.%2").arg(digest, extensionOf(bestName))
    );
    QDir().mkpath(QFileInfo(outputPath).absolutePath());
    QSaveFile output(outputPath);
    bool ok = output.open(QIODevice::WriteOnly);
    char buffer[65536];
    qint64 total = 0;
    while (ok)
    {
        const zip_int64_t read = zip_fread(source, buffer, sizeof(buffer));
        if (read < 0)
        {
            ok = false;
            break;
        }
        if (read == 0)
        {
            break;
        }
        total += read;
        if (total > MAX_SUBTITLE_SIZE || output.write(buffer, read) != read)
        {
            ok = false;
            break;
        }
    }
    zip_fclose(source);
    zip_close(archive);

    if (!ok || total == 0 || !output.commit())
    {
        output.cancelWriting();
        *error = tr("Memento could not extract the Jimaku subtitle.");
        return {};
    }
    return outputPath;
}

void JimakuClient::cancelActive()
{
    ++m_generation;
    clearPendingAttach();
    if (m_reply != nullptr)
    {
        m_reply->abort();
        m_reply = nullptr;
    }
    m_operation = Operation::None;
    setBusy(false);
}

void JimakuClient::finishSuccess(const QString &message)
{
    m_operation = Operation::None;
    setBusy(false);
    setStatus(message);
}

void JimakuClient::finishError(const QString &error)
{
    m_operation = Operation::None;
    setBusy(false);
    setStatus(error);
    emit failed(error);
}

void JimakuClient::setBusy(bool value)
{
    if (!value)
    {
        m_requestApiKey.clear();
    }
    if (m_busy == value)
    {
        return;
    }
    m_busy = value;
    emit busyChanged(m_busy);
}

void JimakuClient::setStatus(const QString &value)
{
    if (m_status == value)
    {
        return;
    }
    m_status = value;
    emit statusChanged(m_status);
}

QString JimakuClient::cleanTitle(const QString &filename)
{
    QString title = QFileInfo(filename).completeBaseName();
    if (title.isEmpty())
    {
        title = filename;
    }

    title.remove(QRegularExpression(
        QStringLiteral("^\\s*\\[[^\\]]+\\]\\s*")
    ));
    title.replace(
        QRegularExpression(
            QStringLiteral(
                "(?:^|[\\s._-])S0*(\\d{1,2})E0*\\d{1,4}(?:v\\d+)?"
            ),
            QRegularExpression::CaseInsensitiveOption
        ),
        QStringLiteral(" Season \\1 ")
    );
    title.remove(QRegularExpression(
        QStringLiteral("\\s+-\\s+0*\\d{1,4}(?:v\\d+)?(?:\\s|$).*$"),
        QRegularExpression::CaseInsensitiveOption
    ));
    title.remove(QRegularExpression(
        QStringLiteral("(?:^|[\\s._-])(?:EP?|Episode)[\\s._-]*0*\\d{1,4}(?:v\\d+)?"),
        QRegularExpression::CaseInsensitiveOption
    ));
    title.remove(QRegularExpression(QStringLiteral("\\[[^\\]]*\\]")));
    title.remove(QRegularExpression(
        QStringLiteral("\\([^)]*(?:2160|1080|720|480|HEVC|AVC|x26[45]|WEB|BluRay)[^)]*\\)"),
        QRegularExpression::CaseInsensitiveOption
    ));
    title.remove(QRegularExpression(
        QStringLiteral(
            "(?:^|[\\s._-])(?:2160p|1080p|720p|480p|HEVC|AVC|x26[45]|"
            "WEB-?DL|BluRay|AAC|FLAC)(?=$|[\\s._-])"
        ),
        QRegularExpression::CaseInsensitiveOption
    ));
    title.replace(QRegularExpression(QStringLiteral("[._]+")), QStringLiteral(" "));
    title.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
    return title.trimmed();
}

int JimakuClient::seasonNumber(const QString &filename)
{
    const QList<QRegularExpression> patterns{
        QRegularExpression(
            QStringLiteral(
                "(?:^|[\\s._-])S0*(\\d{1,2})E0*\\d{1,4}(?:v\\d+)?"
            ),
            QRegularExpression::CaseInsensitiveOption
        ),
        QRegularExpression(
            QStringLiteral(
                "(?:^|[\\s._\\[-])Season[\\s._]+0*(\\d{1,2})"
                "(?=[\\]\\s._-]|$)"
            ),
            QRegularExpression::CaseInsensitiveOption
        ),
        QRegularExpression(
            QStringLiteral(
                "(?:^|[\\s._\\[-])S0*(\\d{1,2})(?=[\\]\\s._-]|$)"
            ),
            QRegularExpression::CaseInsensitiveOption
        ),
        QRegularExpression(
            QStringLiteral(
                "(?:^|[\\s._-])0*(\\d{1,2})(?:st|nd|rd|th)"
                "[\\s._-]+Season(?:\\b|$)"
            ),
            QRegularExpression::CaseInsensitiveOption
        ),
    };
    for (const QRegularExpression &pattern : patterns)
    {
        const QRegularExpressionMatch match = pattern.match(filename);
        if (!match.hasMatch())
        {
            continue;
        }

        bool ok = false;
        const int season = match.captured(1).toInt(&ok);
        if (ok && season > 0)
        {
            return season;
        }
    }
    return -1;
}

int JimakuClient::episodeNumber(const QString &filename)
{
    const QList<QRegularExpression> patterns{
        QRegularExpression(
            QStringLiteral("(?:^|[\\s._-])S\\d{1,2}E0*(\\d{1,4})(?:v\\d+)?"),
            QRegularExpression::CaseInsensitiveOption
        ),
        QRegularExpression(
            QStringLiteral("(?:^|[\\s._\\[-])(?:EP?|Episode)[\\s._-]*0*(\\d{1,4})(?:v\\d+)?"),
            QRegularExpression::CaseInsensitiveOption
        ),
        QRegularExpression(
            QStringLiteral("\\s+-\\s+0*(\\d{1,4})(?:v\\d+)?(?:\\s|\\.|$)"),
            QRegularExpression::CaseInsensitiveOption
        ),
        QRegularExpression(QStringLiteral("\\[0*(\\d{1,4})(?:v\\d+)?\\]"))
    };
    for (qsizetype index = 0; index < patterns.size(); ++index)
    {
        const QRegularExpressionMatch match = patterns[index].match(filename);
        if (match.hasMatch())
        {
            bool ok = false;
            const int episode = match.captured(1).toInt(&ok);
            const bool ambiguousBracket = index == patterns.size() - 1 &&
                likelyBracketMetadata(episode);
            if (ok && episode >= 0 && !ambiguousBracket)
            {
                return episode;
            }
        }
    }

    // Jimaku batch archives commonly use bare member names such as 01.ass.
    // Restrict this form to the complete basename so release years and video
    // resolutions embedded in longer names are not mistaken for episodes.
    static const QRegularExpression numericBase(
        QStringLiteral("^0*(\\d{1,4})(?:v\\d+)?$"),
        QRegularExpression::CaseInsensitiveOption
    );
    const QRegularExpressionMatch match = numericBase.match(
        QFileInfo(filename).completeBaseName()
    );
    if (match.hasMatch())
    {
        bool ok = false;
        const int episode = match.captured(1).toInt(&ok);
        if (ok && episode >= 0 && !likelyBracketMetadata(episode))
        {
            return episode;
        }
    }
    return -1;
}

int JimakuClient::entryScore(
    const QString &query,
    const QJsonObject &entry)
{
    const QString normalizedQuery = normalized(query);
    const QSet<QString> queryTokens = titleTokens(query);
    int best = 0;
    const QStringList fields{
        entry.value(QStringLiteral("name")).toString(),
        entry.value(QStringLiteral("english_name")).toString(),
        entry.value(QStringLiteral("japanese_name")).toString(),
    };
    for (const QString &field : fields)
    {
        if (field.isEmpty())
        {
            continue;
        }
        const QString normalizedField = normalized(field);
        int score = 0;
        if (normalizedField == normalizedQuery)
        {
            score += 1000;
        }
        else
        {
            const qsizetype shorter = std::min(
                normalizedField.size(), normalizedQuery.size()
            );
            const qsizetype longer = std::max(
                normalizedField.size(), normalizedQuery.size()
            );
            const bool contains = !normalizedQuery.isEmpty() &&
                (normalizedField.contains(normalizedQuery) ||
                 normalizedQuery.contains(normalizedField));
            if (contains && shorter >= 4 && shorter * 10 >= longer * 7)
            {
                score += 600;
            }
        }
        const QSet<QString> fieldTokens = titleTokens(field);
        for (const QString &token : queryTokens)
        {
            if (fieldTokens.contains(token))
            {
                score += 30;
            }
        }
        score -= std::abs(normalizedField.size() - normalizedQuery.size());
        best = std::max(best, score);
    }
    return best;
}

bool JimakuClient::entryMatchesSeason(
    int season,
    const QJsonObject &entry)
{
    if (season < 0)
    {
        return true;
    }

    bool hasExplicitSeason = false;
    const QStringList fields{
        entry.value(QStringLiteral("name")).toString(),
        entry.value(QStringLiteral("english_name")).toString(),
        entry.value(QStringLiteral("japanese_name")).toString(),
    };
    for (const QString &field : fields)
    {
        const int entrySeason = seasonNumber(field);
        if (entrySeason < 0)
        {
            continue;
        }
        hasExplicitSeason = true;
        if (entrySeason == season)
        {
            return true;
        }
    }

    // First-season entries commonly omit an explicit season suffix. Later
    // seasons must carry positive season evidence before automatic attachment.
    return season == 1 && !hasExplicitSeason;
}

int JimakuClient::fileScore(const QString &name, int episode)
{
    const QString extension = extensionOf(name);
    int score = -10000;
    if (extension == QStringLiteral("ass"))
    {
        score = 500;
    }
    else if (extension == QStringLiteral("srt"))
    {
        score = 480;
    }
    else if (extension == QStringLiteral("ssa"))
    {
        score = 460;
    }
    else if (extension == QStringLiteral("vtt"))
    {
        score = 440;
    }
    else if (extension == QStringLiteral("zip"))
    {
        score = 200;
    }
    else
    {
        return score;
    }

    if (episode >= 0)
    {
        const int detected = episodeNumber(name);
        if (detected == episode)
        {
            score += 150;
        }
        else if (detected >= 0)
        {
            score -= 300;
        }
    }
    if (name.contains(
        QRegularExpression(
            QStringLiteral("signs?|songs?|dub|forced|commentary|creditless"),
            QRegularExpression::CaseInsensitiveOption
        )))
    {
        score -= 120;
    }
    return score;
}

bool JimakuClient::supportedSubtitle(const QString &name)
{
    const QString extension = extensionOf(name);
    return extension == QStringLiteral("ass") ||
        extension == QStringLiteral("ssa") ||
        extension == QStringLiteral("srt") ||
        extension == QStringLiteral("vtt");
}
