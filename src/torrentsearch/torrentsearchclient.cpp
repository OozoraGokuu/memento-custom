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

#include "torrentsearch/torrentsearchclient.h"
#include "torrentsearch/torrentsearchnetworkreply.h"

#include <algorithm>
#include <utility>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <libtorrent/error_code.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/span.hpp>
#include <libtorrent/version.hpp>

#include "util/directoryutils.h"

namespace lt = libtorrent;

namespace
{

constexpr int NETWORK_TIMEOUT_MS = 20000;
constexpr qint64 MAX_FEED_SIZE = 4 * 1024 * 1024;
constexpr qint64 MAX_TORRENT_SIZE = 16 * 1024 * 1024;
constexpr int MAX_TORRENT_FILES = 10000;
constexpr qint64 MAX_TORRENT_PATH_BYTES = 8 * 1024 * 1024;
constexpr int MAX_RESULTS = 200;
constexpr int CACHE_SECONDS = 300;
constexpr int MAX_CACHE_ENTRIES = 12;

const lt::file_storage &torrentFiles(const lt::torrent_info &info)
{
#if LIBTORRENT_VERSION_NUM >= 20100
    return info.layout();
#else
    return info.files();
#endif
}

int integer(const QString &value)
{
    bool ok = false;
    const int parsed = value.trimmed().toInt(&ok);
    return ok ? std::max(0, parsed) : 0;
}

QString localName(const QXmlStreamReader &reader)
{
    return reader.name().toString().toCaseFolded();
}

bool isLinkOrJunction(const QFileInfo &info)
{
    return info.isSymbolicLink() || info.isJunction();
}

bool pathsEqual(const QString &first, const QString &second)
{
#if defined(Q_OS_WIN)
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    return QDir::cleanPath(first).compare(
        QDir::cleanPath(second), sensitivity
    ) == 0;
}

QString safeTorrentSearchCacheRoot()
{
    const QString path = QDir(DirectoryUtils::getCacheDir()).filePath(
        QStringLiteral("torrentsearch"));
    QFileInfo info(path);
    if (isLinkOrJunction(info))
    {
        return {};
    }
    if (!info.exists())
    {
        if (!QDir().mkpath(info.absoluteFilePath()))
        {
            return {};
        }
        info.refresh();
    }
    if (!info.isDir() || isLinkOrJunction(info))
    {
        return {};
    }
    const QString canonical = info.canonicalFilePath();
    const QString canonicalParent = info.dir().canonicalPath();
    const QString expected = canonicalParent.isEmpty() ? QString() :
        QDir(canonicalParent).filePath(info.fileName());
    return !canonical.isEmpty() && !expected.isEmpty() &&
        pathsEqual(canonical, expected) ? canonical : QString();
}

} // namespace

TorrentSearchClient::TorrentSearchClient(QObject *parent) :
    QObject(parent)
{
    #if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    QSettings settings(DirectoryUtils::getCacheConfig(), QSettings::NativeFormat);
#else
    QSettings settings;
#endif
    m_baseUrl = settings.value("torrentsearch/provider-url").toUrl();
    m_responseBaseUrl = m_baseUrl;
    m_cloudflareDns = settings.value("torrentsearch/cloudflare-dns", true).toBool();
}

const QVariantList &TorrentSearchClient::results() const noexcept
{
    return m_resultMaps;
}

bool TorrentSearchClient::busy() const noexcept
{
    return m_busy;
}

const QString &TorrentSearchClient::status() const noexcept
{
    return m_status;
}

const QUrl &TorrentSearchClient::baseUrl() const noexcept
{
    return m_baseUrl;
}

bool TorrentSearchClient::configureProvider(const QUrl &url)
{
    if (!url.isEmpty() && (!url.isValid() || url.host().isEmpty() ||
        !url.userInfo().isEmpty() || url.hasFragment() ||
        (url.scheme() != "https" && url.scheme() != "http"))) {
        finishError(tr("Enter a valid HTTP or HTTPS website or RSS feed URL."));
        return false;
    }
    if (url == m_baseUrl) return true;
    clear();
    m_cache.clear();
    m_baseUrl = url;
    m_responseBaseUrl = url;
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    QSettings settings(DirectoryUtils::getCacheConfig(), QSettings::NativeFormat);
#else
    QSettings settings;
#endif
    settings.setValue("torrentsearch/provider-url", url);
    emit baseUrlChanged();
    return true;
}

void TorrentSearchClient::search(
    const QString &query)
{
    if (m_baseUrl.isEmpty() || !m_baseUrl.isValid()) {
        finishError(tr("Add your own torrent website or RSS feed URL first."));
        return;
    }
    const QString trimmed = query.trimmed();
    if (trimmed.isEmpty())
    {
        setResults({});
        finishError(tr("Enter a title or release name to search releases."));
        return;
    }

    const QString key = trimmed.normalized(QString::NormalizationForm_KC).toCaseFolded();
    cancel();
    const auto cached = m_cache.constFind(key);
    if (cached != m_cache.cend() &&
        cached->fetchedAt.secsTo(QDateTime::currentDateTimeUtc()) < CACHE_SECONDS)
    {
        setResults(cached->items);
        m_networkRoute = tr("Cached search results");
        emit networkRouteChanged();
        setStatus(tr("Showing cached results for “%1”.").arg(trimmed));
        return;
    }

    setResults({});
    const quint64 generation = ++m_generation;
    setBusy(true);
    setStatus(tr("Searching your provider for “%1”…").arg(trimmed));

    requestSearch(
        m_baseUrl,
        trimmed,
        generation,
        key
    );
}

void TorrentSearchClient::requestSearch(
    const QUrl &provider,
    const QString &query,
    quint64 generation,
    const QString &key,
    bool discover)
{
    QUrl url = provider;
    QUrlQuery parameters(url);
    parameters.removeAllQueryItems("q");
    parameters.addQueryItem(QStringLiteral("q"), query);
    url.setQuery(parameters);

    QNetworkReply *reply = get(url, QByteArrayLiteral(
        "application/rss+xml, application/xml;q=0.9, text/xml;q=0.8"
    ));
    m_reply = reply;
    connect(reply, &QNetworkReply::downloadProgress, this,
        [reply](qint64 received, qint64) {
            if (received > MAX_FEED_SIZE)
            {
                reply->setProperty("mementoOversized", true);
                reply->abort();
            }
        });
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, generation, key, query, discover, provider] {
            handleSearch(
                reply, generation, key, query, discover, provider);
        });
}

void TorrentSearchClient::downloadResult(int index)
{
    if (index < 0 || index >= m_results.size())
    {
        finishError(tr("The selected result is no longer available."));
        return;
    }
    if (m_busy)
    {
        return;
    }

    const Result result = m_results.at(index);
    if (!providerUrl(result.torrentUrl))
    {
        finishError(tr("The provider did not provide a safe torrent URL."));
        return;
    }

    const QString cached = torrentCachePath(result);
    if (cached.isEmpty())
    {
        finishError(tr("Memento's torrent metadata cache is unsafe or unavailable."));
        return;
    }
    const QFileInfo cachedInfo(cached);
    if (!isLinkOrJunction(cachedInfo) && cachedInfo.isFile() &&
        cachedInfo.size() > 0 &&
        cachedInfo.size() <= MAX_TORRENT_SIZE)
    {
        QFile cachedFile(cached);
        const QByteArray data = cachedFile.open(QIODevice::ReadOnly) ?
            cachedFile.read(MAX_TORRENT_SIZE + 1) : QByteArray();
        QString validationError;
        if (data.size() == cachedInfo.size() &&
            validateTorrentData(data, result.infoHash, &validationError))
        {
            emit torrentReady(QUrl::fromLocalFile(cached), result.title);
            setStatus(tr("Added cached torrent metadata for “%1”.").arg(result.title));
            return;
        }
        cachedFile.close();
        QFile::remove(cached);
    }
    else if (cachedInfo.exists() && !isLinkOrJunction(cachedInfo))
    {
        QFile::remove(cached);
    }

    cancel();
    const quint64 generation = ++m_generation;
    setBusy(true);
    setStatus(tr("Downloading torrent metadata for “%1”…").arg(result.title));

    QNetworkReply *reply = get(
        result.torrentUrl,
        QByteArrayLiteral("application/x-bittorrent, application/octet-stream")
    );
    m_reply = reply;
    connect(reply, &QNetworkReply::downloadProgress, this,
        [reply](qint64 received, qint64) {
            if (received > MAX_TORRENT_SIZE)
            {
                reply->setProperty("mementoOversized", true);
                reply->abort();
            }
        });
    connect(reply, &QNetworkReply::finished, this,
        [this, reply, generation, result] {
            handleDownload(reply, generation, result);
        });
}

void TorrentSearchClient::cancel()
{
    ++m_generation;
    if (m_reply != nullptr)
    {
        disconnect(m_reply, nullptr, this, nullptr);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    setBusy(false);
}

void TorrentSearchClient::clear()
{
    cancel();
    setResults({});
    setStatus({});
}

void TorrentSearchClient::handleSearch(
    QNetworkReply *reply,
    quint64 generation,
    QString key,
    QString query,
    bool discover,
    QUrl provider)
{
    const bool oversized = reply->property("mementoOversized").toBool();
    const QByteArray data = oversized ? QByteArray() : reply->readAll();
    const QString networkError = reply->error() == QNetworkReply::NoError ?
        QString() : responseError(reply);
    reply->deleteLater();
    if (generation != m_generation)
    {
        return;
    }
    m_reply = nullptr;

    if (oversized)
    {
        finishError(tr("The provider returned an oversized search response."));
        return;
    }
    if (!networkError.isEmpty())
    {
        finishError(networkError);
        return;
    }

    QVector<Result> items;
    QString error;
    m_responseBaseUrl = provider;
    if (data.size() > MAX_FEED_SIZE || !parseFeed(data, &items, &error))
    {
        if (discover && data.size() <= MAX_FEED_SIZE) {
            const QUrl feed = discoverFeed(data, provider);
            if (!feed.isEmpty() && feed != provider) {
                setStatus(tr("Found the website’s RSS feed. Loading results…"));
                requestSearch(feed, query, generation, key, false);
                return;
            }
        }
        finishError(error.isEmpty() ?
            tr("The provider returned an invalid RSS response.") : error);
        return;
    }

    if (m_cache.size() >= MAX_CACHE_ENTRIES)
    {
        auto oldest = m_cache.begin();
        for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
        {
            if (it->fetchedAt < oldest->fetchedAt)
            {
                oldest = it;
            }
        }
        m_cache.erase(oldest);
    }
    m_cache.insert(key, {QDateTime::currentDateTimeUtc(), items});
    setResults(std::move(items));
    setBusy(false);
    setStatus(m_results.isEmpty() ? tr("No releases matched this search.") :
        tr("Found %1 releases. Choose one to inspect its episodes.").arg(m_results.size()));
}

void TorrentSearchClient::handleDownload(
    QNetworkReply *reply,
    quint64 generation,
    Result result)
{
    const bool oversized = reply->property("mementoOversized").toBool();
    const QByteArray data = oversized ? QByteArray() : reply->readAll();
    const QString networkError = reply->error() == QNetworkReply::NoError ?
        QString() : responseError(reply);
    reply->deleteLater();
    if (generation != m_generation)
    {
        return;
    }
    m_reply = nullptr;

    if (oversized || data.size() > MAX_TORRENT_SIZE)
    {
        finishError(tr("The provider returned oversized torrent metadata."));
        return;
    }
    if (!networkError.isEmpty())
    {
        finishError(networkError);
        return;
    }
    QString validationError;
    if (!validateTorrentData(data, result.infoHash, &validationError))
    {
        finishError(validationError);
        return;
    }

    const QString path = torrentCachePath(result);
    if (path.isEmpty())
    {
        finishError(tr("Memento's torrent metadata cache is unsafe or unavailable."));
        return;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() ||
        !file.commit())
    {
        finishError(tr("Memento could not save the selected torrent metadata."));
        return;
    }

    setBusy(false);
    setStatus(tr("Torrent metadata ready. Choose the episode to play."));
    emit torrentReady(QUrl::fromLocalFile(path), result.title);
}

QNetworkReply *TorrentSearchClient::get(const QUrl &url, const QByteArray &accept)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Memento-Custom/2.3 TorrentSearch/1");
    request.setRawHeader("Accept", accept);
    request.setAttribute(
        QNetworkRequest::RedirectPolicyAttribute,
        QNetworkRequest::SameOriginRedirectPolicy
    );
    request.setTransferTimeout(NETWORK_TIMEOUT_MS);
    auto *reply = new TorrentSearchNetworkReply(request, m_cloudflareDns, this);
    connect(reply, &TorrentSearchNetworkReply::routeChanged, this, [this](const QString &route) {
        m_networkRoute = route;
        emit networkRouteChanged();
    });
    return reply;
}

bool TorrentSearchClient::parseFeed(
    const QByteArray &data,
    QVector<Result> *items,
    QString *error) const
{
    if (items == nullptr || error == nullptr)
    {
        return false;
    }

    QXmlStreamReader reader(data);
    Result current;
    bool inItem = false;
    bool isRss = false;
    while (!reader.atEnd())
    {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QStringLiteral("rss")) isRss = true;
        if (reader.isStartElement() && localName(reader) == QStringLiteral("item"))
        {
            current = {};
            inItem = true;
            continue;
        }
        if (reader.isEndElement() && localName(reader) == QStringLiteral("item"))
        {
            inItem = false;
            if (!providerUrl(current.detailUrl)) current.detailUrl = m_baseUrl;
            current.id = resultId(current.detailUrl, current.torrentUrl);
            current.magnet = magnetFor(current.infoHash, current.title);
            if (!current.title.isEmpty() &&
                providerUrl(current.torrentUrl))
            {
                items->emplaceBack(std::move(current));
                if (items->size() >= MAX_RESULTS)
                {
                    break;
                }
            }
            continue;
        }
        if (!inItem || !reader.isStartElement())
        {
            continue;
        }

        const QString name = localName(reader);
        if (name == QStringLiteral("title"))
        {
            current.title = reader.readElementText().trimmed();
        }
        else if (name == QStringLiteral("link"))
        {
            const QUrl link = m_responseBaseUrl.resolved(QUrl(reader.readElementText().trimmed()));
            if (link.path().endsWith(".torrent", Qt::CaseInsensitive)) current.torrentUrl = link;
            else current.detailUrl = link;
        }
        else if (name == QStringLiteral("enclosure"))
        {
            const QUrl enclosure = m_responseBaseUrl.resolved(QUrl(reader.attributes().value("url").toString()));
            if (reader.attributes().value("type") == "application/x-bittorrent" || enclosure.path().endsWith(".torrent", Qt::CaseInsensitive))
                current.torrentUrl = enclosure;
        }
        else if (name == QStringLiteral("guid"))
        {
            const QUrl guid(reader.readElementText().trimmed());
            if (current.detailUrl.isEmpty() && guid.isValid() && !guid.isRelative()) current.detailUrl = guid;
        }
        else if (name == QStringLiteral("category"))
        {
            current.category = reader.readElementText().trimmed();
        }
        else if (name == QStringLiteral("size"))
        {
            current.size = reader.readElementText().trimmed();
        }
        else if (name == QStringLiteral("uploader"))
        {
            current.uploader = reader.readElementText().trimmed();
        }
        else if (name == QStringLiteral("pubdate"))
        {
            const QString value = reader.readElementText().trimmed();
            const QDateTime date = QDateTime::fromString(value, Qt::RFC2822Date);
            current.publishedAt = date;
            current.published = date.isValid() ?
                QLocale().toString(date.toLocalTime(), QLocale::ShortFormat) :
                value;
        }
        else if (name == QStringLiteral("infohash"))
        {
            current.infoHash = reader.readElementText().trimmed();
        }
        else if (name == QStringLiteral("seeders"))
        {
            current.seeders = integer(reader.readElementText());
        }
        else if (name == QStringLiteral("leechers"))
        {
            current.leechers = integer(reader.readElementText());
        }
        else if (name == QStringLiteral("downloads"))
        {
            current.downloads = integer(reader.readElementText());
        }
        else if (name == QStringLiteral("trusted"))
        {
            current.trusted = parseBoolean(reader.readElementText());
        }
        else if (name == QStringLiteral("remake"))
        {
            current.remake = parseBoolean(reader.readElementText());
        }
    }

    if (reader.hasError())
    {
        *error = tr("The provider returned malformed RSS: %1")
            .arg(reader.errorString());
        return false;
    }
    if (!isRss) { *error = tr("This URL did not return RSS. Enter the website’s torrent RSS feed URL."); return false; }
    return true;
}

QUrl TorrentSearchClient::discoverFeed(const QByteArray &data, const QUrl &page) const
{
    // RSS auto-discovery reads advertised feed links only; it never executes
    // page scripts or scrapes search-result HTML, and cannot leave the origin.
    const QString html = QString::fromUtf8(data);
    const QRegularExpression links(QStringLiteral("<link\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    const auto attribute = [](const QString &tag, const QString &name) {
        const QRegularExpression pattern(QStringLiteral("(?:^|\\s)") + name + QStringLiteral("\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s>]+))"),
            QRegularExpression::CaseInsensitiveOption);
        const auto match = pattern.match(tag);
        for (int i = 1; i <= 3; ++i) if (!match.captured(i).isNull())
            return match.captured(i).replace("&amp;", "&").replace("&quot;", "\"").replace("&#39;", "'");
        return QString();
    };
    auto matches = links.globalMatch(html);
    while (matches.hasNext()) {
        const QString tag = matches.next().captured();
        if (attribute(tag, "type").compare("application/rss+xml", Qt::CaseInsensitive) != 0) continue;
        const QString href = attribute(tag, "href");
        if (href.isEmpty()) continue;
        const QUrl feed = page.resolved(QUrl(href));
        if (providerUrl(feed)) return feed;
    }
    return {};
}

QVariantMap TorrentSearchClient::resultMap(const Result &result) const
{
    return {
        {QStringLiteral("id"), result.id},
        {QStringLiteral("title"), result.title},
        {QStringLiteral("category"), result.category},
        {QStringLiteral("size"), result.size},
        {QStringLiteral("uploader"), result.uploader},
        {QStringLiteral("published"), result.published},
        {QStringLiteral("infoHash"), result.infoHash},
        {QStringLiteral("magnet"), result.magnet},
        {QStringLiteral("detailUrl"), result.detailUrl},
        {QStringLiteral("torrentUrl"), result.torrentUrl},
        {QStringLiteral("seeders"), result.seeders},
        {QStringLiteral("leechers"), result.leechers},
        {QStringLiteral("downloads"), result.downloads},
        {QStringLiteral("trusted"), result.trusted},
        {QStringLiteral("remake"), result.remake},
    };
}

QString TorrentSearchClient::responseError(QNetworkReply *reply) const
{
    const int status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute
    ).toInt();
    if (status == 403 || reply->rawHeader("cf-mitigated") == "challenge")
    {
        return tr(
            "The provider requested browser verification. Open the search in your "
            "browser, or try again later; Memento will not bypass the challenge."
        );
    }
    if (status == 429)
    {
        const QString retry = QString::fromLatin1(reply->rawHeader("Retry-After"));
        return retry.isEmpty() ?
            tr("The provider is rate-limiting requests. Try again later.") :
            tr("The provider is rate-limiting requests. Try again in %1 seconds.")
                .arg(retry);
    }
    return tr("Could not reach the provider: %1").arg(reply->errorString());
}

QString TorrentSearchClient::torrentCachePath(const Result &result) const
{
    const QString cacheRoot = safeTorrentSearchCacheRoot();
    if (cacheRoot.isEmpty())
    {
        return {};
    }
    const QByteArray identity = result.torrentUrl.toEncoded();
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(
        identity, QCryptographicHash::Sha256
    ).toHex().left(20));
    const QString path = QDir(cacheRoot).filePath(
        QStringLiteral("%1-%2.torrent").arg(result.id).arg(digest));
    const QFileInfo info(path);
    if (isLinkOrJunction(info) ||
        (info.exists() && (!info.isFile() ||
         !pathsEqual(info.canonicalPath(), cacheRoot))))
    {
        return {};
    }
    return path;
}

bool TorrentSearchClient::providerUrl(const QUrl &url) const
{
    if (!url.isValid())
    {
        return false;
    }
    const auto matches = [&url] (const QUrl &provider) {
        if (!provider.isValid() || url.host().compare(
                provider.host(), Qt::CaseInsensitive) != 0)
        {
            return false;
        }
        if (provider.scheme().compare(
                QStringLiteral("https"), Qt::CaseInsensitive) == 0)
        {
            return url.scheme().compare(
                QStringLiteral("https"), Qt::CaseInsensitive) == 0;
        }
        return url.scheme().compare(
            provider.scheme(), Qt::CaseInsensitive) == 0;
    };
    return url.userInfo().isEmpty() && url.port() == m_baseUrl.port() && matches(m_baseUrl);
}

qint64 TorrentSearchClient::resultId(const QUrl &first, const QUrl &second)
{
    static const QRegularExpression expression(
        QStringLiteral("/(?:view|download)/(\\d+)(?:\\.torrent)?(?:$|[/?#])")
    );
    for (const QUrl &url : {first, second})
    {
        const QRegularExpressionMatch match = expression.match(url.path());
        if (!match.hasMatch())
        {
            continue;
        }
        bool ok = false;
        const qint64 id = match.captured(1).toLongLong(&ok);
        if (ok && id >= 0)
        {
            return id;
        }
    }
    return -1;
}

QString TorrentSearchClient::magnetFor(const QString &hash, const QString &title)
{
    static const QRegularExpression validHash(
        QStringLiteral("^(?:[0-9A-Fa-f]{40}|[A-Z2-7a-z2-7]{32})$")
    );
    if (!validHash.match(hash).hasMatch())
    {
        return {};
    }

    QUrl magnet;
    magnet.setScheme(QStringLiteral("magnet"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("xt"), QStringLiteral("urn:btih:") + hash);
    query.addQueryItem(QStringLiteral("dn"), title);
    query.addQueryItem(
        QStringLiteral("tr"),
        QStringLiteral("udp://tracker.opentrackr.org:1337/announce")
    );
    query.addQueryItem(
        QStringLiteral("tr"),
        QStringLiteral("udp://open.stealth.si:80/announce")
    );
    magnet.setQuery(query);
    return magnet.toString(QUrl::FullyEncoded);
}

bool TorrentSearchClient::validateTorrentData(
    const QByteArray &data,
    const QString &expectedInfoHash,
    QString *error)
{
    if (error == nullptr)
    {
        return false;
    }
    error->clear();
    if (data.isEmpty() || data.size() > MAX_TORRENT_SIZE)
    {
        *error = tr("The provider returned empty or oversized torrent metadata.");
        return false;
    }

    lt::load_torrent_limits limits;
    limits.max_buffer_size = static_cast<int>(MAX_TORRENT_SIZE);
    lt::add_torrent_params params;
#if LIBTORRENT_VERSION_NUM >= 20100
    lt::error_code parseError;
    params = lt::load_torrent_buffer(
        lt::span<char const>(data.constData(), data.size()),
        parseError,
        limits
    );
    const bool parseFailed = static_cast<bool>(parseError);
#else
    bool parseFailed = false;
    try
    {
        params = lt::load_torrent_buffer(
            lt::span<char const>(data.constData(), data.size()),
            limits
        );
    }
    catch (...)
    {
        parseFailed = true;
    }
#endif
    if (parseFailed || !params.ti || params.ti->piece_length() <= 0)
    {
        *error = tr(
            "The provider returned invalid torrent metadata. "
            "Use the magnet fallback or open the result in your browser."
        );
        return false;
    }

    const lt::file_storage &files = torrentFiles(*params.ti);
    if (files.num_files() <= 0 || files.num_files() > MAX_TORRENT_FILES)
    {
        *error = tr("The provider returned torrent metadata with too many files.");
        return false;
    }
    bool hasPayload = false;
    qint64 pathBytes = 0;
    for (int index = 0; index < files.num_files(); ++index)
    {
        const lt::file_index_t fileIndex{index};
        const std::string path = files.file_path(fileIndex);
        if (path.size() > static_cast<std::size_t>(
                MAX_TORRENT_PATH_BYTES - pathBytes))
        {
            *error = tr("The provider returned torrent metadata with oversized file paths.");
            return false;
        }
        pathBytes += static_cast<qint64>(path.size());
        if (!files.pad_file_at(fileIndex) && files.file_size(fileIndex) > 0)
        {
            hasPayload = true;
        }
    }
    if (!hasPayload)
    {
        *error = tr("The provider returned torrent metadata with no usable files.");
        return false;
    }

    const QString expected = expectedInfoHash.trimmed();
    if (!expected.isEmpty())
    {
        lt::error_code hashError;
        const lt::add_torrent_params expectedParams = lt::parse_magnet_uri(
            ("magnet:?xt=urn:btih:" + expected).toStdString(), hashError
        );
        const lt::info_hash_t actualHashes = params.ti->info_hashes();
        if (hashError || !expectedParams.info_hashes.has_v1() ||
            !actualHashes.has_v1() ||
            actualHashes.v1 != expectedParams.info_hashes.v1)
        {
            *error = tr(
                "The downloaded torrent does not match the provider’s advertised info hash."
            );
            return false;
        }
    }
    return true;
}

bool TorrentSearchClient::parseBoolean(const QString &value)
{
    const QString normalized = value.trimmed().toCaseFolded();
    return normalized == QStringLiteral("yes") ||
        normalized == QStringLiteral("true") ||
        normalized == QStringLiteral("1");
}

void TorrentSearchClient::setCloudflareDns(bool enabled)
{
    if (m_cloudflareDns == enabled) return;
    cancel();
    m_cloudflareDns = enabled;
    m_cache.clear();
    m_networkRoute.clear();
    #if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    QSettings settings(DirectoryUtils::getCacheConfig(), QSettings::NativeFormat);
#else
    QSettings settings;
#endif
    settings.setValue("torrentsearch/cloudflare-dns", enabled);
    emit cloudflareDnsChanged();
    emit networkRouteChanged();
}

const QString &TorrentSearchClient::sortOrder() const noexcept
{
    return m_sortOrder;
}

void TorrentSearchClient::setSortOrder(const QString &order)
{
    if ((order != QStringLiteral("seeders") && order != QStringLiteral("newest")) ||
        order == m_sortOrder)
    {
        return;
    }
    m_sortOrder = order;
    emit sortOrderChanged();
    setResults(m_results);
}

void TorrentSearchClient::setResults(QVector<Result> items)
{
    // Keep the backing records and QML rows in the same order so the selected
    // row's index always downloads that exact torrent after a sort change.
    std::stable_sort(items.begin(), items.end(), [this](const Result &a, const Result &b) {
        if (m_sortOrder == QStringLiteral("seeders") && a.seeders != b.seeders)
        {
            return a.seeders > b.seeders;
        }
        // Prefer RSS timestamps; numeric IDs provide a deterministic fallback.
        if (a.publishedAt.isValid() != b.publishedAt.isValid()) return a.publishedAt.isValid();
        if (a.publishedAt.isValid() && a.publishedAt != b.publishedAt)
            return a.publishedAt > b.publishedAt;
        return a.id > b.id;
    });
    m_results = std::move(items);
    m_resultMaps.clear();
    m_resultMaps.reserve(m_results.size());
    for (const Result &result : std::as_const(m_results))
    {
        m_resultMaps.emplaceBack(resultMap(result));
    }
    emit resultsChanged();
}

void TorrentSearchClient::setBusy(bool value)
{
    if (m_busy == value)
    {
        return;
    }
    m_busy = value;
    emit busyChanged(m_busy);
}

void TorrentSearchClient::setStatus(const QString &value)
{
    if (m_status == value)
    {
        return;
    }
    m_status = value;
    emit statusChanged(m_status);
}

void TorrentSearchClient::finishError(const QString &message)
{
    setBusy(false);
    setStatus(message);
    emit failed(message);
}
