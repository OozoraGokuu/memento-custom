#include "kitsunekkoclient.h"
#include "jimakuclient.h"
#include "subtitlerelease.h"
#include "state/context.h"
#include "state/localsubtitleclient.h"
#include "player/mpvcontroller.h"
#include "player/mpvstate.h"
#include "player/mpvtrack.h"
#include "util/directoryutils.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <algorithm>

namespace {
const QString api = QStringLiteral("https://api.github.com/repos/Ajatt-Tools/kitsunekko-mirror/");
QString catalogPath() { return QDir(DirectoryUtils::getCacheDir()).filePath("kitsunekko/catalog.json"); }
bool validSha(const QString &sha) {
    return QRegularExpression("^[a-f0-9]{40}$").match(sha).hasMatch();
}
bool validLink(const QVariantMap &link) {
    const QString prefix = link.value("prefix").toString();
    return link.value("provider").toString() == "kitsunekko" &&
        validSha(link.value("sha").toString()) && prefix.startsWith("subtitles/") &&
        prefix.endsWith('/') && !prefix.contains("..") && !prefix.contains('\\');
}
bool sameEntry(const QVariantMap &left, const QVariantMap &right) {
    return left.value("provider").toString() == "kitsunekko" &&
        right.value("provider").toString() == "kitsunekko" &&
        left.value("sha") == right.value("sha") && left.value("prefix") == right.value("prefix");
}
QString cachedSubtitlePath(const QVariantMap &file) {
    return QDir(DirectoryUtils::getCacheDir()).filePath("kitsunekko/" +
        file.value("sha").toString() + "/" + QFileInfo(file.value("name").toString()).fileName());
}
bool verifiedCache(const QString &path, const QString &sha) {
    QFile file(path);
    if (!validSha(sha) || !file.open(QIODevice::ReadOnly) ||
        file.size() <= 0 || file.size() > 16 * 1024 * 1024) return false;
    const QByteArray data = file.readAll();
    const QByteArray blob = QByteArray("blob ") + QByteArray::number(data.size()) + '\0' + data;
    return QString::fromLatin1(QCryptographicHash::hash(blob, QCryptographicHash::Sha1).toHex()) == sha;
}
}

KitsunekkoClient::KitsunekkoClient(Context *context, QObject *parent)
    : QObject(parent), m_context(context)
{
    m_attachTimer.setSingleShot(true);
    m_attachTimer.setInterval(10000);
    connect(&m_attachTimer, &QTimer::timeout, this, [this] {
        error(tr("The subtitle could not be loaded. Check that the file is valid."));
    });
}

void KitsunekkoClient::get(const QUrl &url, std::function<void(const QByteArray &)> done)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "Memento-Study-Edition/2.1");
    request.setTransferTimeout(25000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    auto *reply = m_manager.get(request);
    m_reply = reply;
    const auto generation = m_generation;
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64 total) {
        if (received > 16 * 1024 * 1024 || total > 16 * 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation, done] {
        reply->deleteLater();
        if (generation != m_generation) return;
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError) {
            error(status == 403 || status == 429 ?
                tr("GitHub's request limit was reached. Wait and retry, or use Jimaku.") :
                tr("AJATT mirror request failed: %1").arg(reply->errorString()));
            return;
        }
        const QByteArray data = reply->readAll();
        if (data.size() > 16 * 1024 * 1024) { error(tr("The mirror response is too large.")); return; }
        done(data);
    });
}

void KitsunekkoClient::search(const QString &query)
{
    clearSearch();
    m_query = query.trimmed();
    if (m_query.isEmpty()) return;
    if (loadCachedCatalog()) { filterTitles(); return; }
    startCatalogLoad();
}

bool KitsunekkoClient::loadCachedCatalog()
{
    const auto fresh = [](const QDateTime &time) {
        const qint64 age = time.secsTo(QDateTime::currentDateTime());
        return time.isValid() && age >= 0 && age < 86400;
    };
    if (!m_catalog.isEmpty() && fresh(m_catalogFetched)) return true;
    m_catalog.clear();
    m_catalogFetched = {};
    QFile cache(catalogPath());
    if (!fresh(QFileInfo(cache).lastModified()) || !cache.open(QIODevice::ReadOnly) ||
        cache.size() > 16 * 1024 * 1024) return false;
    m_catalog = QJsonDocument::fromJson(cache.readAll()).array().toVariantList();
    if (m_catalog.isEmpty()) return false;
    m_catalogFetched = QFileInfo(cache).lastModified();
    return true;
}

void KitsunekkoClient::startCatalogLoad()
{
    m_busy = true;
    m_status = tr("Loading the online AJATT catalog…");
    emit changed();
    get(QUrl(api + "git/trees/main"), [this](const QByteArray &data) {
        const auto tree = QJsonDocument::fromJson(data).object().value("tree").toArray();
        QString sha;
        for (const auto &value : tree) {
            const auto object = value.toObject();
            if (object.value("path").toString() == "subtitles") sha = object.value("sha").toString();
        }
        if (!validSha(sha)) { error(tr("The AJATT subtitle catalog could not be found.")); return; }
        get(QUrl(api + "git/trees/" + sha), [this](const QByteArray &categoryData) {
            m_categories = QJsonDocument::fromJson(categoryData).object().value("tree").toArray().toVariantList();
            if (m_categories.isEmpty()) { error(tr("The AJATT catalog is empty or invalid.")); return; }
            m_catalogBuild.clear();
            loadCategory(0);
        });
    });
}

void KitsunekkoClient::loadCategory(int index)
{
    if (index >= m_categories.size()) {
        m_catalog = m_catalogBuild;
        m_catalogFetched = QDateTime::currentDateTime();
        QDir().mkpath(QFileInfo(catalogPath()).absolutePath());
        QSaveFile file(catalogPath());
        const auto data = QJsonDocument(QJsonArray::fromVariantList(m_catalog)).toJson(QJsonDocument::Compact);
        if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) file.commit();
        if (!m_catalogLink.isEmpty()) finishLinkedCatalog(true);
        else filterTitles();
        return;
    }
    const auto category = m_categories[index].toMap();
    const QString sha = category.value("sha").toString();
    if (category.value("type").toString() != "tree" || !validSha(sha)) { loadCategory(index + 1); return; }
    get(QUrl(api + "git/trees/" + sha), [this, index, category](const QByteArray &data) {
        const auto object = QJsonDocument::fromJson(data).object();
        if (!object.contains("tree") || object.value("truncated").toBool()) {
            m_catalog.clear(); error(tr("GitHub returned an incomplete AJATT catalog. Please retry.")); return;
        }
        for (const auto &value : object.value("tree").toArray()) {
            const auto entry = value.toObject();
            if (entry.value("type").toString() != "tree" || !validSha(entry.value("sha").toString())) continue;
            const QString categoryName = category.value("path").toString();
            const QString title = entry.value("path").toString();
            m_catalogBuild.append(QVariantMap{{"name", title}, {"englishName", QString(categoryName).replace('_', ' ')},
                {"japaneseName", ""}, {"prefix", "subtitles/" + categoryName + "/" + title + "/"},
                {"sha", entry.value("sha").toString()}});
        }
        loadCategory(index + 1);
    });
}

void KitsunekkoClient::resolveLinkedCatalog(const QVariantMap &link, int episode,
                                           bool force, bool automatic, bool all)
{
    m_catalogLink = link;
    m_catalogEpisode = episode;
    m_catalogAutomatic = automatic;
    m_catalogAll = all;
    watchMedia();
    if (!force && loadCachedCatalog()) {
        finishLinkedCatalog(false);
        return;
    }
    startCatalogLoad();
}

void KitsunekkoClient::finishLinkedCatalog(bool fresh)
{
    auto link = m_catalogLink;
    const int episode = m_catalogEpisode;
    const bool automatic = m_catalogAutomatic, all = m_catalogAll;
    m_catalogLink.clear();
    m_catalogEpisode = -1;
    QString sha;
    for (const auto &value : m_catalog) {
        const auto entry = value.toMap();
        if (entry.value("prefix") != link.value("prefix")) continue;
        if (!sha.isEmpty() || !validSha(entry.value("sha").toString())) {
            error(tr("The linked Kitsunekko title could not be identified uniquely. Open subtitle search to choose it again."));
            return;
        }
        sha = entry.value("sha").toString();
    }
    if (sha.isEmpty()) {
        if (!fresh) { resolveLinkedCatalog(link, episode, true, automatic, all); return; }
        error(tr("The linked Kitsunekko title is no longer in the current catalog. Open subtitle search to choose it again."));
        return;
    }
    link.insert("sha", sha);
    m_entries = {link};
    selectFiles(0, episode, all, false, automatic, fresh);
}

void KitsunekkoClient::filterTitles()
{
    const auto tokens = m_query.normalized(QString::NormalizationForm_KC).toCaseFolded()
        .split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    m_entries.clear();
    for (const auto &entry : m_catalog) {
        const auto title = entry.toMap().value("name").toString().normalized(QString::NormalizationForm_KC).toCaseFolded();
        if (std::all_of(tokens.begin(), tokens.end(), [&](const QString &token) { return title.contains(token); }))
            m_entries.append(entry);
    }
    std::sort(m_entries.begin(), m_entries.end(), [](const QVariant &a, const QVariant &b) {
        return QString::localeAwareCompare(a.toMap().value("name").toString(), b.toMap().value("name").toString()) < 0;
    });
    m_busy = false;
    m_status = tr("%1 titles found in the online AJATT mirror. Catalog cached for 24 hours.").arg(m_entries.size());
    emit changed();
}

QVariantList KitsunekkoClient::parseFiles(const QByteArray &data, const QString &prefix, int episode)
{
    QVariantList results;
    for (const auto &value : QJsonDocument::fromJson(data).object().value("tree").toArray()) {
        const auto file = value.toObject();
        const QString name = file.value("path").toString();
        if (file.value("type").toString() != "blob" || !JimakuClient::supportedSubtitle(name) ||
            !validSha(file.value("sha").toString()) || file.value("size").toInteger() > 16 * 1024 * 1024 ||
            name.split('/').contains("..") || name.startsWith('/') || name.contains('\\')) continue;
        const int detected = JimakuClient::episodeNumber(name);
        if (episode >= 0 && detected >= 0 && detected != episode) continue;
        results.append(QVariantMap{{"name", name}, {"path", prefix + name}, {"sha", file.value("sha").toString()},
            {"size", file.value("size").toInteger()}, {"episode", detected},
            {"format", QFileInfo(name).suffix().toUpper()}, {"archive", false}});
    }
    std::sort(results.begin(), results.end(), [episode](const QVariant &a, const QVariant &b) {
        const auto left = a.toMap(), right = b.toMap();
        const bool lm = episode >= 0 && left.value("episode").toInt() == episode;
        const bool rm = episode >= 0 && right.value("episode").toInt() == episode;
        if (lm != rm) return lm;
        return QString::localeAwareCompare(left.value("name").toString(), right.value("name").toString()) < 0;
    });
    return results;
}

void KitsunekkoClient::selectEntry(int index, int episode) { selectFiles(index, episode, false); }
void KitsunekkoClient::selectEntryAllFiles(int index, int episode) { selectFiles(index, episode, true); }
void KitsunekkoClient::selectFiles(int index, int episode, bool all, bool persist,
                                  bool automatic, bool catalogFresh)
{
    if (index < 0 || index >= m_entries.size()) return;
    cancel();
    m_files.clear();
    auto entry = m_entries[index].toMap();
    entry.insert("provider", "kitsunekko");
    m_entryName = entry.value("name").toString(); m_episode = episode;
    m_mediaPath = m_context && m_context->player() ? m_context->player()->state()->path() : QString();
    if (!validLink(entry)) {
        error(tr("Invalid saved Kitsunekko title. Use Change link to select it again.")); return;
    }
    m_entryLink = {{"provider", "kitsunekko"}, {"name", m_entryName},
                   {"sha", entry.value("sha")}, {"prefix", entry.value("prefix")}};
    const auto savedLink = m_context && m_context->episodeLibrary()
        ? m_context->episodeLibrary()->subtitleLinkForFile(m_mediaPath) : QVariantMap{};
    const bool sameTitle = savedLink.value("provider").toString() == "kitsunekko" &&
        entry.value("prefix") == savedLink.value("prefix");
    const auto preference = sameTitle ? savedLink : entry;
    for (const auto *key : {"releaseRule", "preferredFile", "episodeOffset", "autoLoad"})
        if (preference.contains(key)) m_entryLink.insert(key, preference.value(key));
    if (savedLink.contains("autoLoad")) m_entryLink.insert("autoLoad", savedLink.value("autoLoad"));
    if (persist && m_context && m_context->episodeLibrary()) {
        auto *library = m_context->episodeLibrary();
        const bool releaseChosen = !savedLink.value("preferredFile").toString().isEmpty() ||
            savedLink.value("releaseRule").toMap().value("valid").toBool();
        if (!releaseChosen && library->containsFile(m_mediaPath) &&
            !library->setSubtitleLinkForFile(m_mediaPath, m_entryLink))
            emit failed(tr("Could not save the subtitle link. Check library storage permissions."));
    }
    watchMedia();
    m_busy = true; m_status = tr("Loading available Japanese subtitles…"); emit changed();
    get(QUrl(api + "git/trees/" + entry.value("sha").toString() + "?recursive=1"),
        [this, entry, all, episode, automatic, catalogFresh](const QByteArray &data) {
        const auto object = QJsonDocument::fromJson(data).object();
        if (!object.contains("tree") || object.value("truncated").toBool()) {
            error(tr("GitHub returned an incomplete subtitle list.")); return;
        }
        m_files = parseFiles(data, entry.value("prefix").toString(), all || automatic ? -1 : episode);
        m_busy = false;
        if (automatic) {
            if (!m_context || !m_context->player() ||
                m_context->player()->state()->path() != m_mediaPath) {
                cancel(); return;
            }
            const int index = preferredFileIndex(episode);
            if (index < 0) {
                const bool hasReleaseMatch = std::any_of(m_files.begin(), m_files.end(),
                    [this, episode](const QVariant &value) {
                        return SubtitleRelease::matches(value.toMap().value("name").toString(),
                            episode, m_entryLink.value("releaseRule").toMap());
                    });
                if (!catalogFresh && exactSelection(episode).isEmpty() && !hasReleaseMatch) {
                    resolveLinkedCatalog(m_entryLink, episode, true);
                    return;
                }
                error(tr("Your chosen subtitle release has no unique match for episode %1. Open subtitle search to choose a file; Memento will not switch release groups automatically.").arg(episode));
                return;
            }
            attachResult(index, true);
            return;
        }
        m_status = m_files.isEmpty() ? tr("No files for this episode. Try Show all files.") :
            tr("%1 Japanese subtitle files. Only the file you select will be downloaded.").arg(m_files.size());
        emit changed();
    });
}

int KitsunekkoClient::automaticFileIndex(const QVariantList &files, int episode,
                                         const QVariantMap &rule,
                                         const QString &exactFilename)
{
    if (episode < 0) return -1;
    int selected = -1;
    for (int index = 0; index < files.size(); ++index) {
        const auto file = files.at(index).toMap();
        const QString name = file.value("name").toString();
        if (!exactFilename.isEmpty()) {
            if (name != exactFilename) continue;
        } else if (!SubtitleRelease::matches(name, episode, rule)) continue;
        if (selected >= 0) return -1;
        selected = index;
    }
    return selected;
}

QVariantMap KitsunekkoClient::exactSelection(int episode) const
{
    if (!m_context || !m_context->episodeLibrary()) return {};
    const auto selection = m_context->episodeLibrary()->subtitleSelectionForFile(m_mediaPath);
    if (!sameEntry(selection, m_entryLink) || selection.value("episode", -1).toInt() != episode)
        return {};
    return selection;
}

int KitsunekkoClient::preferredFileIndex(int episode) const
{
    const auto selection = exactSelection(episode);
    return automaticFileIndex(m_files, episode, m_entryLink.value("releaseRule").toMap(),
                              selection.value("filename").toString());
}

void KitsunekkoClient::fetchLinkedForCurrentMedia(bool manual)
{
    cancel();
    if (!m_context || !m_context->player()) return;
    m_mediaPath = m_context->player()->state()->path();
    const auto seriesLink = m_context->episodeLibrary()->subtitleLinkForFile(m_mediaPath);
    if (!manual && !seriesLink.value("autoLoad", true).toBool()) return;
    const auto selection = m_context->episodeLibrary()->subtitleSelectionForFile(m_mediaPath);
    const auto link = selection.isEmpty() ? seriesLink : selection;
    if (link.value("provider").toString() != "kitsunekko") return;
    m_context->jimakuClient()->cancel();
    m_context->localSubtitleClient()->cancel();
    if (!validLink(link)) {
        error(tr("Invalid saved Kitsunekko link. Use Change link to select it again.")); return;
    }
    m_files.clear();
    m_entries = {link};
    m_entryLink = link;
    m_entryName = link.value("name").toString();
    const int videoEpisode = m_context->jimakuClient()->suggestedEpisode();
    const qint64 requestedEpisode = selection.value("provider").toString() == "kitsunekko"
        ? selection.value("episode", -1).toLongLong()
        : videoEpisode < 0 ? -1 : qint64(videoEpisode) + link.value("episodeOffset", 0).toLongLong();
    const int episode = requestedEpisode >= 0 && requestedEpisode <= 10000 ? int(requestedEpisode) : -1;
    m_episode = episode;
    if (episode < 0) {
        error(tr("Could not determine the episode number for linked subtitles.")); return;
    }
    if ((selection.value("provider").toString() != "kitsunekko" ||
         selection.value("filename").toString().isEmpty()) &&
        !link.value("releaseRule").toMap().value("valid").toBool()) {
        error(tr("Choose a subtitle file once in subtitle search so Memento can keep using that release group for later episodes."));
        return;
    }
    if (selection.value("provider").toString() == "kitsunekko") {
        const QVariantMap file{{"name", selection.value("filename")},
            {"sha", selection.value("fileSha")}, {"episode", episode},
            {"path", link.value("prefix").toString() + selection.value("filename").toString()}};
        const QString path = cachedSubtitlePath(file);
        if (JimakuClient::supportedSubtitle(file.value("name").toString()) &&
            QFileInfo(selection.value("cachePath").toString()).canonicalFilePath() ==
                QFileInfo(path).canonicalFilePath() && verifiedCache(path, file.value("sha").toString())) {
            m_busy = true;
            m_status = tr("Loading your saved subtitle choice…");
            emit changed();
            watchMedia();
            attachPath(path, file, true);
            return;
        }
    }
    if (selection.value("provider").toString() == "kitsunekko")
        selectFiles(0, episode, false, false, true);
    else
        resolveLinkedCatalog(link, episode);
}

void KitsunekkoClient::openLinkedEntry(const QVariantMap &link, int episode, bool all)
{
    if (m_busy) return;
    if (!validLink(link)) {
        error(tr("Invalid saved Kitsunekko link. Use Change link to select the title again.")); return;
    }
    cancel();
    m_mediaPath = m_context && m_context->player() ? m_context->player()->state()->path() : QString();
    m_entries = {link};
    m_entryLink = link;
    m_entryName = link.value("name").toString();
    m_episode = episode;
    m_files.clear();
    resolveLinkedCatalog(link, episode, false, false, all);
}

void KitsunekkoClient::attachResult(int index)
{
    attachResult(index, false);
}

void KitsunekkoClient::attachResult(int index, bool automatic)
{
    if (m_busy || index < 0 || index >= m_files.size()) return;
    if (!m_context || !m_context->player() || m_mediaPath.isEmpty() || m_context->player()->state()->path() != m_mediaPath) {
        error(tr("The video changed. Choose the title again before attaching subtitles.")); return;
    }
    m_context->jimakuClient()->cancel();
    m_context->localSubtitleClient()->cancel();
    const auto file = m_files[index].toMap();
    if (!validLink(m_entryLink) || !validSha(file.value("sha").toString()) ||
        !JimakuClient::supportedSubtitle(file.value("name").toString()) ||
        file.value("path").toString() != m_entryLink.value("prefix").toString() + file.value("name").toString()) {
        error(tr("The selected subtitle is invalid. Refresh the file list and choose it again.")); return;
    }
    if (!automatic && file.value("episode", -1).toInt() >= 0)
        m_episode = file.value("episode").toInt();
    watchMedia();
    m_busy = true; m_status = tr("Downloading Japanese subtitle…"); emit changed();
    const QString cached = cachedSubtitlePath(file);
    if (verifiedCache(cached, file.value("sha").toString())) {
        attachPath(cached, file, automatic);
        return;
    }
    const auto encoded = QUrl::toPercentEncoding(file.value("path").toString(), "/");
    get(QUrl("https://raw.githubusercontent.com/Ajatt-Tools/kitsunekko-mirror/main/" + QString::fromLatin1(encoded)),
        [this, file, automatic](const QByteArray &data) {
        if (m_context->player()->state()->path() != m_mediaPath) {
            error(tr("The video changed before the download finished.")); return;
        }
        const QByteArray blob = QByteArray("blob ") + QByteArray::number(data.size()) + '\0' + data;
        if (QString::fromLatin1(QCryptographicHash::hash(blob, QCryptographicHash::Sha1).toHex()) != file.value("sha").toString()) {
            error(tr("The subtitle changed on GitHub. Refresh the catalog and select it again.")); return;
        }
        const QString path = cachedSubtitlePath(file);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile output(path);
        if (data.isEmpty() || !output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit()) {
            error(tr("Could not save the downloaded subtitle.")); return;
        }
        attachPath(path, file, automatic);
    });
}

void KitsunekkoClient::attachPath(const QString &path, const QVariantMap &file, bool automatic)
{
    if (!m_context || !m_context->player() || m_context->player()->state()->path() != m_mediaPath) {
        error(tr("The video changed before the subtitle was attached.")); return;
    }
    m_pendingPath = QFileInfo(path).canonicalFilePath();
    if (m_pendingPath.isEmpty()) { error(tr("The downloaded subtitle is no longer available.")); return; }
    m_pendingFile = file;
    m_pendingAutomatic = automatic;
    m_pendingSelection = automatic ? exactSelection(m_episode) : QVariantMap{};
    disconnect(m_trackConnection);
    disconnect(m_sidConnection);
    m_trackConnection = connect(m_context->player()->state(), &MpvState::subtitleTracksChanged,
                                this, &KitsunekkoClient::checkAttached);
    m_sidConnection = connect(m_context->player()->state(), &MpvState::sidChanged,
                              this, &KitsunekkoClient::checkAttached);
    m_attachTimer.start();
    if (!m_context->player()->controller()->loadSubtitle(path)) error(tr("Memento could not load the selected subtitle."));
    else checkAttached();
}

void KitsunekkoClient::checkAttached()
{
    if (m_pendingPath.isEmpty()) return;
    if (m_context->player()->state()->path() != m_mediaPath) { error(tr("The video changed before the subtitle was attached.")); return; }
    for (const auto *track : m_context->player()->state()->subtitleTracks()) {
        if (!track || !track->external() || track->id() != m_context->player()->state()->sid()) continue;
        const QUrl url(track->externalFilename());
        const QString path = url.isLocalFile() ? url.toLocalFile() : track->externalFilename();
        if (QFileInfo(path).canonicalFilePath() != m_pendingPath) continue;
        const QString name = m_pendingFile.value("name").toString();
        auto *library = m_context->episodeLibrary();
        QVariantMap link = m_pendingAutomatic ? library->subtitleLinkForFile(m_mediaPath) : m_entryLink;
        const auto rule = m_pendingAutomatic
            ? m_pendingSelection.value("releaseRule", m_entryLink.value("releaseRule")).toMap()
            : SubtitleRelease::fromFile(name, m_episode);
        if (!m_pendingAutomatic) {
            link.insert("releaseRule", rule);
            link.insert("preferredFile", name);
            const int videoEpisode = m_context->jimakuClient()->suggestedEpisode();
            if (videoEpisode >= 0 && m_episode >= 0)
                link.insert("episodeOffset", m_episode - videoEpisode);
        } else if (m_pendingSelection.isEmpty() &&
                   link.value("provider").toString() == "kitsunekko" &&
                   link.value("prefix") == m_entryLink.value("prefix") &&
                   link.value("releaseRule").toMap() == rule) {
            // Advance the directory snapshot without changing the chosen release.
            link.insert("sha", m_entryLink.value("sha"));
        }
        const double delay = m_pendingAutomatic
            ? m_pendingSelection.value("delay", 0.0).toDouble()
            : m_context->player()->state()->subtitle()->delay();
        QVariantMap selection{{"provider", "kitsunekko"}, {"name", m_entryName},
            {"sha", m_entryLink.value("sha")}, {"prefix", m_entryLink.value("prefix")},
            {"filename", name}, {"fileSha", m_pendingFile.value("sha")},
            {"cachePath", m_pendingPath}, {"episode", m_episode},
            {"releaseRule", rule}, {"delay", delay}};
        const bool saved = !library->containsFile(m_mediaPath) || link.isEmpty() ||
            library->rememberSubtitleSelection(m_mediaPath, link, selection);
        if (saved && library->containsFile(m_mediaPath))
            library->rememberSubtitleSearch(link, m_episode);
        if (m_pendingAutomatic)
            m_context->player()->controller()->setSubtitleDelay(delay);
        if (!m_pendingAutomatic) m_entryLink = link;
        cancel();
        m_status = saved ? tr("Japanese subtitle loaded.") :
            tr("Japanese subtitle loaded, but its choice could not be saved. Check library storage permissions.");
        emit changed();
        if (!saved) emit failed(m_status);
        emit subtitleAttached(name, m_entryName); return;
    }
}

void KitsunekkoClient::watchMedia()
{
    disconnect(m_mediaConnection);
    m_mediaConnection = {};
    if (!m_context || !m_context->player()) return;
    m_mediaConnection = connect(m_context->player()->state(), &MpvState::pathChanged,
        this, [this](const QString &path) {
            if (path == m_mediaPath) return;
            cancel();
            m_mediaPath.clear();
            m_status = tr("The video changed. Open subtitle search for the current episode.");
            emit changed();
        });
}

void KitsunekkoClient::cancel()
{
    ++m_generation;
    if (m_reply) { m_reply->abort(); m_reply = nullptr; }
    m_attachTimer.stop(); disconnect(m_trackConnection); m_trackConnection = {};
    disconnect(m_sidConnection); m_sidConnection = {};
    disconnect(m_mediaConnection); m_mediaConnection = {};
    m_pendingPath.clear(); m_pendingFile.clear(); m_pendingSelection.clear();
    m_catalogLink.clear(); m_catalogEpisode = -1;
    m_catalogAutomatic = true; m_catalogAll = false;
    m_pendingAutomatic = false; m_busy = false; emit changed();
}
void KitsunekkoClient::clearSearch()
{
    cancel(); m_entries.clear(); m_files.clear(); m_entryLink.clear(); m_entryName.clear(); m_status.clear();
    m_episode = -1; emit changed();
}
void KitsunekkoClient::refreshCatalog(const QString &query)
{
    cancel(); m_catalog.clear(); m_catalogFetched = {};
    QFile::remove(catalogPath()); search(query);
}
void KitsunekkoClient::error(const QString &message)
{
    // Never keep an incomplete catalog after a failed category request.
    if (m_busy && m_entries.isEmpty()) m_catalog.clear();
    cancel(); m_status = message; emit changed(); emit failed(message);
}
