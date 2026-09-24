#include "kitsunekkoclient.h"
#include "jimakuclient.h"
#include "state/context.h"
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
    if (m_catalog.isEmpty()) {
        QFile cache(catalogPath());
        if (QFileInfo(cache).lastModified().secsTo(QDateTime::currentDateTime()) < 86400 &&
            cache.open(QIODevice::ReadOnly))
            m_catalog = QJsonDocument::fromJson(cache.readAll()).array().toVariantList();
    }
    if (!m_catalog.isEmpty()) { filterTitles(); return; }
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
        QDir().mkpath(QFileInfo(catalogPath()).absolutePath());
        QSaveFile file(catalogPath());
        const auto data = QJsonDocument(QJsonArray::fromVariantList(m_catalog)).toJson(QJsonDocument::Compact);
        if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size()) file.commit();
        filterTitles();
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
            name.split('/').contains("..") || name.startsWith('/')) continue;
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
void KitsunekkoClient::selectFiles(int index, int episode, bool all)
{
    if (index < 0 || index >= m_entries.size()) return;
    cancel();
    m_files.clear();
    const auto entry = m_entries[index].toMap();
    m_entryName = entry.value("name").toString(); m_episode = episode;
    m_mediaPath = m_context && m_context->player() ? m_context->player()->state()->path() : QString();
    m_busy = true; m_status = tr("Loading available Japanese subtitles…"); emit changed();
    get(QUrl(api + "git/trees/" + entry.value("sha").toString() + "?recursive=1"),
        [this, entry, all, episode](const QByteArray &data) {
        const auto object = QJsonDocument::fromJson(data).object();
        if (!object.contains("tree") || object.value("truncated").toBool()) {
            error(tr("GitHub returned an incomplete subtitle list.")); return;
        }
        m_files = parseFiles(data, entry.value("prefix").toString(), all ? -1 : episode);
        m_busy = false;
        m_status = m_files.isEmpty() ? tr("No files for this episode. Try Show all files.") :
            tr("%1 Japanese subtitle files. Only the file you select will be downloaded.").arg(m_files.size());
        emit changed();
    });
}

void KitsunekkoClient::attachResult(int index)
{
    if (m_busy || index < 0 || index >= m_files.size()) return;
    if (!m_context || !m_context->player() || m_mediaPath.isEmpty() || m_context->player()->state()->path() != m_mediaPath) {
        error(tr("The video changed. Choose the title again before attaching subtitles.")); return;
    }
    const auto file = m_files[index].toMap();
    m_busy = true; m_status = tr("Downloading Japanese subtitle…"); emit changed();
    const auto encoded = QUrl::toPercentEncoding(file.value("path").toString(), "/");
    get(QUrl("https://raw.githubusercontent.com/Ajatt-Tools/kitsunekko-mirror/main/" + QString::fromLatin1(encoded)),
        [this, file](const QByteArray &data) {
        if (m_context->player()->state()->path() != m_mediaPath) {
            error(tr("The video changed before the download finished.")); return;
        }
        const QByteArray blob = QByteArray("blob ") + QByteArray::number(data.size()) + '\0' + data;
        if (QString::fromLatin1(QCryptographicHash::hash(blob, QCryptographicHash::Sha1).toHex()) != file.value("sha").toString()) {
            error(tr("The subtitle changed on GitHub. Refresh the catalog and select it again.")); return;
        }
        const QString path = QDir(DirectoryUtils::getCacheDir()).filePath("kitsunekko/" + file.value("sha").toString() + "/" + QFileInfo(file.value("name").toString()).fileName());
        QDir().mkpath(QFileInfo(path).absolutePath());
        QSaveFile output(path);
        if (data.isEmpty() || !output.open(QIODevice::WriteOnly) || output.write(data) != data.size() || !output.commit()) {
            error(tr("Could not save the downloaded subtitle.")); return;
        }
        m_pendingPath = QFileInfo(path).canonicalFilePath();
        m_trackConnection = connect(m_context->player()->state(), &MpvState::subtitleTracksChanged, this, &KitsunekkoClient::checkAttached);
        m_attachTimer.start();
        if (!m_context->player()->controller()->loadSubtitle(path)) error(tr("Memento could not load the selected subtitle."));
        else checkAttached();
    });
}

void KitsunekkoClient::checkAttached()
{
    if (m_pendingPath.isEmpty()) return;
    if (m_context->player()->state()->path() != m_mediaPath) { error(tr("The video changed before the subtitle was attached.")); return; }
    for (const auto *track : m_context->player()->state()->subtitleTracks()) {
        if (!track || !track->external()) continue;
        const QUrl url(track->externalFilename());
        const QString path = url.isLocalFile() ? url.toLocalFile() : track->externalFilename();
        if (QFileInfo(path).canonicalFilePath() != m_pendingPath) continue;
        const QString name = QFileInfo(m_pendingPath).fileName();
        cancel(); m_status = tr("Japanese subtitle loaded."); emit changed();
        emit subtitleAttached(name, m_entryName); return;
    }
}
void KitsunekkoClient::cancel()
{
    ++m_generation;
    if (m_reply) { m_reply->abort(); m_reply = nullptr; }
    m_attachTimer.stop(); disconnect(m_trackConnection); m_trackConnection = {};
    m_pendingPath.clear(); m_busy = false; emit changed();
}
void KitsunekkoClient::clearSearch()
{
    cancel(); m_entries.clear(); m_files.clear(); m_entryName.clear(); m_status.clear();
    m_episode = -1; emit changed();
}
void KitsunekkoClient::refreshCatalog(const QString &query)
{
    cancel(); m_catalog.clear(); QFile::remove(catalogPath()); search(query);
}
void KitsunekkoClient::error(const QString &message)
{
    // Never keep an incomplete catalog after a failed category request.
    if (m_busy && m_entries.isEmpty()) m_catalog.clear();
    cancel(); m_status = message; emit changed(); emit failed(message);
}
