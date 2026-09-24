#include "anilistclient.h"
#include "quick/episodefolder.h"
#include "player/mpvplayer.h"
#include "player/mpvstate.h"
#include "util/directoryutils.h"
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTcpSocket>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>
#include <memory>

namespace {
const QString origin = QStringLiteral("http://127.0.0.1:47832");
QString titleOf(const QJsonObject &media) {
    return media.value("title").toObject().value("userPreferred").toString();
}
}

AniListClient::AniListClient(EpisodeFolder *library, QObject *parent)
    : QObject(parent), m_library(library), m_network(this), m_login(this)
{
    m_configPath = QDir(DirectoryUtils::getConfigDir()).filePath("anilist.json");
    QFile file(m_configPath);
    if (file.open(QIODevice::ReadOnly))
        m_config = QJsonDocument::fromJson(file.read(2 * 1024 * 1024)).object();
    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &AniListClient::processQueue);
    m_loginTimeout.setSingleShot(true);
    connect(&m_loginTimeout, &QTimer::timeout, this, [this] {
        m_login.close(); m_authState.clear(); announce(tr("Sign-in expired. Connect again."));
    });
    connect(&m_login, &QTcpServer::newConnection, this, &AniListClient::receiveAuthorization);
    if (!m_config.value("access_token").toString().isEmpty())
        QTimer::singleShot(0, this, &AniListClient::testConnection);
}

bool AniListClient::save()
{
    QDir().mkpath(QFileInfo(m_configPath).absolutePath());
    QSaveFile file(m_configPath);
    if (!file.open(QIODevice::WriteOnly)) {
        announce(tr("Could not save AniList settings.")); return false;
    }
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const QByteArray data = QJsonDocument(m_config).toJson();
    if (file.write(data) != data.size() || !file.commit()) {
        announce(tr("Could not save AniList settings.")); return false;
    }
    ++m_revision;
    emit changed();
    return true;
}

void AniListClient::announce(const QString &message) { m_status = message; emit changed(); }

void AniListClient::setClientId(const QString &value)
{
    const QString id = value.trimmed();
    if (id == clientId()) return;
    if (!id.isEmpty() && !QRegularExpression("^[0-9]{1,12}$").match(id).hasMatch()) {
        announce(tr("Enter the numeric Client ID from your own AniList application.")); return;
    }
    disconnectAccount();
    m_config.insert("client_id", id);
    save();
}
void AniListClient::setEnabled(bool value)
{
    m_config.insert("enabled", value);
    if (!value) m_retry.stop();
    if (save() && value) processQueue();
}
void AniListClient::setThreshold(int value)
{
    m_config.insert("threshold", std::clamp(value, 50, 100)); save();
}
int AniListClient::pendingCount() const { return m_config.value("pending").toArray().size(); }

void AniListClient::request(const QString &query, const QJsonObject &variables,
    Callback callback, const QString &token)
{
    if (m_transport) { m_transport(query, variables, token, std::move(callback)); return; }
    QNetworkRequest request(QUrl("https://graphql.anilist.co"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("User-Agent", "Memento-AniList/1.0");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(20000);
    if (!token.isEmpty()) request.setRawHeader("Authorization", "Bearer " + token.toUtf8());
    auto *reply = m_network.post(request, QJsonDocument(QJsonObject{
        {"query", query}, {"variables", variables}}).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::readyRead, this, [reply] {
        if (reply->bytesAvailable() > 2 * 1024 * 1024) reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)] {
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const int retry = std::clamp(reply->rawHeader("Retry-After").toInt(), 30, 3600);
        QJsonParseError parse;
        const auto json = QJsonDocument::fromJson(reply->readAll(), &parse).object();
        const auto errors = json.value("errors").toArray();
        bool unauthorized = status == 401 || status == 403;
        for (const auto &error : errors) {
            const int code = error.toObject().value("status").toInt();
            unauthorized = unauthorized || code == 401 || code == 403;
        }
        QString message;
        if (unauthorized) message = tr("AniList authorization expired or was rejected. Connect again.");
        else if (status == 429) message = tr("AniList rate limit reached. Sync will retry later.");
        else if (reply->error() != QNetworkReply::NoError || status != 200 ||
                 parse.error != QJsonParseError::NoError || !errors.isEmpty() ||
                 !json.value("data").isObject())
            message = tr("AniList request failed. Check your connection and try again.");
        // Never surface server text that could contain a token or private data.
        callback(json.value("data").toObject(), message, unauthorized, retry);
        reply->deleteLater();
    });
}

void AniListClient::authenticate(const QString &token)
{
    const int generation = ++m_generation;
    m_verified = false; m_busy = true;
    announce(tr("Checking AniList account…"));
    request("query { Viewer { id name } }", {}, [this, token, generation](QJsonObject data, QString error, bool, int) {
        if (generation != m_generation) return;
        m_busy = false;
        const auto viewer = data.value("Viewer").toObject();
        if (!error.isEmpty() || viewer.value("id").toInt() <= 0 || viewer.value("name").toString().isEmpty()) {
            announce(error.isEmpty() ? tr("AniList account could not be verified.") : error); return;
        }
        const int oldId = m_config.value("user_id").toInt();
        if (oldId != viewer.value("id").toInt()) {
            m_config.remove("pending"); m_confirmed.clear(); m_rejected.clear();
        }
        m_config.insert("access_token", token);
        m_config.insert("user_id", viewer.value("id"));
        m_config.insert("username", viewer.value("name"));
        m_verified = true;
        if (!save()) { m_verified = false; emit changed(); return; }
        announce(tr("Connected as %1.").arg(username()));
        processQueue();
    }, token);
}
void AniListClient::testConnection()
{
    if (m_busy) return;
    const QString token = m_config.value("access_token").toString();
    if (token.isEmpty()) { announce(tr("Connect your own AniList application first.")); return; }
    authenticate(token);
}
void AniListClient::disconnectAccount()
{
    ++m_generation; ++m_searchGeneration;
    m_verified = false; m_busy = false;
    m_retry.stop(); m_loginTimeout.stop(); m_login.close(); m_authState.clear();
    m_config.remove("access_token"); m_config.remove("username"); m_config.remove("user_id");
    m_config.remove("pending"); m_confirmed.clear(); m_rejected.clear();
    m_config.insert("enabled", false);
    save(); announce(tr("Disconnected. Saved title links are retained."));
}
QUrl AniListClient::authorizationUrl(const QString &clientId, const QString &state)
{
    QUrl url("https://anilist.co/api/v2/oauth/authorize");
    QUrlQuery query;
    query.addQueryItem("client_id", clientId);
    query.addQueryItem("response_type", "token");
    // AniList's implicit flow uses the callback registered on the user's app.
    // Sending redirect_uri explicitly can produce unsupported_grant_type.
    query.addQueryItem("state", state);
    url.setQuery(query);
    return url;
}

void AniListClient::connectAccount()
{
    if (m_busy) return;
    if (clientId().isEmpty()) { announce(tr("Enter your AniList application's Client ID first.")); return; }
    m_login.close();
    if (!m_login.listen(QHostAddress::LocalHost, 47832)) {
        announce(tr("The sign-in port is in use. Close the old AniList setup window and try again.")); return;
    }
    m_authState.clear();
    for (int i = 0; i < 4; ++i)
        m_authState += QString::number(QRandomGenerator::system()->generate64(), 16).rightJustified(16, '0');
    m_loginTimeout.start(10 * 60 * 1000);
    const QUrl url = authorizationUrl(clientId(), m_authState);
    if (!QDesktopServices::openUrl(url)) {
        m_login.close(); m_loginTimeout.stop(); m_authState.clear();
        announce(tr("Could not open your browser.")); return;
    }
    announce(tr("Authorize your own application in the browser, then return here."));
}

void AniListClient::receiveAuthorization()
{
    while (m_login.hasPendingConnections()) {
        auto *socket = m_login.nextPendingConnection();
        auto buffer = std::make_shared<QByteArray>();
        QTimer::singleShot(15000, socket, [socket] { socket->abort(); socket->deleteLater(); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
            *buffer += socket->readAll();
            if (buffer->size() > 20000) { socket->abort(); return; }
            const int end = buffer->indexOf("\r\n\r\n");
            if (end < 0) return;
            const QList<QByteArray> lines = buffer->left(end).split('\n');
            const auto first = lines.first().trimmed().split(' ');
            QMap<QByteArray, QByteArray> headers;
            for (const auto &line : lines) {
                const int colon = line.indexOf(':');
                if (colon > 0) headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
            }
            bool ok = true;
            const int length = headers.contains("content-length") ? headers.value("content-length").toInt(&ok) : 0;
            if (!ok || length < 0 || length > 16000 || first.size() != 3 ||
                headers.value("host") != "127.0.0.1:47832" || headers.contains("transfer-encoding")) {
                socket->abort(); return;
            }
            if (buffer->size() < end + 4 + length) return;
            const auto respond = [socket](const QByteArray &body, bool success) {
                socket->write(QByteArray(success ? "HTTP/1.1 200 OK\r\n" : "HTTP/1.1 400 Bad Request\r\n") +
                    "Content-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nConnection: close\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            };
            if (m_authState.isEmpty()) { respond("Sign-in expired. Return to Memento.", false); return; }
            if (first[0] == "GET" && first[1] == "/callback") {
                respond(R"HTML(<!doctype html><meta charset="utf-8"><title>Memento AniList</title><body>Finishing sign-in…<script>
const p = new URLSearchParams(location.hash.slice(1)); history.replaceState(null, '', '/callback');
fetch('/token', {method:'POST', headers:{'Content-Type':'application/x-www-form-urlencoded'},
body:new URLSearchParams({access_token:p.get('access_token')||'', state:p.get('state')||''})})
.then(r=>r.text()).then(t=>document.body.textContent=t).catch(()=>document.body.textContent='Return to Memento and reconnect.');
</script>)HTML", true);
            } else if (first[0] == "POST" && first[1] == "/token" && headers.value("origin") == origin.toUtf8()) {
                const QUrlQuery form(QString::fromUtf8(buffer->mid(end + 4, length)));
                const QString state = form.queryItemValue("state", QUrl::FullyDecoded);
                const QString token = form.queryItemValue("access_token", QUrl::FullyDecoded);
                if (state != m_authState || token.isEmpty() || token.contains('\r') || token.contains('\n')) {
                    respond("Sign-in could not be verified. Reconnect from Memento.", false); return;
                }
                m_authState.clear(); m_login.close(); m_loginTimeout.stop();
                respond("Authorization received. Return to Memento to see your connection status.", true);
                authenticate(token);
            } else respond("Invalid sign-in request.", false);
            buffer->clear();
        });
    }
}

void AniListClient::search(const QString &query)
{
    if (query.trimmed().size() < 2) { announce(tr("Enter at least two characters.")); return; }
    const int generation = ++m_searchGeneration;
    announce(tr("Searching AniList…"));
    request("query ($search: String!) { Page(perPage: 20) { media(search: $search, type: ANIME) { id title { userPreferred } episodes seasonYear format } } }",
        {{"search", query.trimmed()}}, [this, generation](QJsonObject data, QString error, bool, int) {
            if (generation != m_searchGeneration) return;
            m_results.clear();
            if (!error.isEmpty()) { announce(error); return; }
            for (const auto &value : data.value("Page").toObject().value("media").toArray()) {
                const auto media = value.toObject();
                m_results.append(QVariantMap{{"id", media.value("id").toInt()},
                    {"title", titleOf(media)}, {"episodes", media.value("episodes").toInt()},
                    {"year", media.value("seasonYear").toInt()}, {"format", media.value("format").toString()}});
            }
            announce(tr("Choose the correct title and season to link this library entry."));
        });
}
QVariantMap AniListClient::mapping(const QString &key) const
{
    return m_config.value("mappings").toObject().value(key).toObject().toVariantMap();
}
void AniListClient::linkTitle(const QString &key, int resultIndex, int offset)
{
    if (key.isEmpty() || resultIndex < 0 || resultIndex >= m_results.size()) return;
    m_rejected.clear();
    auto item = QJsonObject::fromVariantMap(m_results.at(resultIndex).toMap());
    item.insert("offset", std::clamp(offset, -9999, 9999));
    auto mappings = m_config.value("mappings").toObject();
    mappings.insert(key, item); m_config.insert("mappings", mappings);
    // Discard unsent updates for the previous title mapping.
    QJsonArray pending;
    for (const auto &p : m_config.value("pending").toArray())
        if (p.toObject().value("key").toString() != key) pending.append(p);
    m_config.insert("pending", pending);
    if (save()) announce(tr("Linked to %1. Episode offset: %2.").arg(item.value("title").toString()).arg(offset));
}
void AniListClient::unlinkTitle(const QString &key)
{
    auto mappings = m_config.value("mappings").toObject(); mappings.remove(key);
    m_config.insert("mappings", mappings);
    QJsonArray pending;
    for (const auto &p : m_config.value("pending").toArray())
        if (p.toObject().value("key").toString() != key) pending.append(p);
    m_config.insert("pending", pending);
    if (save()) announce(tr("Title unlinked."));
}
int AniListClient::episodeFromName(const QString &name)
{
    const QString stem = QFileInfo(name).completeBaseName();
    const QStringList patterns{
        R"(\bS\d{1,2}E(\d{1,4})(?:\b|v\d))",
        R"(\b(?:EP?|Episode)[ ._-]?(\d{1,4})(?:\b|v\d))",
        R"([\[(](\d{1,3})(?:v\d)?[\])])",
        R"((?:^|\s)-\s*(\d{1,4})(?:v\d)?(?:\s|$|\[))",
        R"(^(\d{1,3})(?:\s|$))"
    };
    for (const auto &pattern : patterns) {
        const auto match = QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption).match(stem);
        if (match.hasMatch()) return match.captured(1).toInt();
    }
    return 0;
}
int AniListClient::currentEpisode() const
{
    if (m_episodeOverrides.contains(m_file)) return m_episodeOverrides.value(m_file);
    const int parsed = episodeFromName(m_current.value("filename").toString());
    if (parsed <= 0) return 0;
    return std::max(0, parsed + mapping(currentKey()).value("offset").toInt());
}
void AniListClient::setCurrentEpisode(int episode)
{
    if (m_file.isEmpty()) return;
    m_episodeOverrides.insert(m_file, std::clamp(episode, 0, 9999)); emit changed();
}
void AniListClient::refreshCurrent(const QString &path)
{
    m_file = path;
    m_current = m_library ? m_library->playbackInfo(path) : QVariantMap();
    if (m_current.isEmpty() && !path.isEmpty()) {
        QUrl url(path);
        const QString local = url.isLocalFile() ? url.toLocalFile() : url.scheme().isEmpty() ? path : QString();
        if (!local.isEmpty()) {
            const QFileInfo file(local);
            m_current = {{"key", "folder:" + file.absolutePath()},
                {"title", file.dir().dirName()}, {"filename", file.fileName()}};
        }
    }
    emit changed();
}
void AniListClient::attachPlayer(MpvPlayer *player)
{
    if (m_player) { disconnect(m_player, nullptr, this, nullptr); disconnect(m_player->state(), nullptr, this, nullptr); }
    m_player = player; m_ready = false;
    if (!player) return;
    connect(player->state(), &MpvState::pathChanged, this, [this](const QString &path) {
        m_ready = false; refreshCurrent(path);
    });
    connect(player, &MpvPlayer::fileLoaded, this, [this] {
        m_ready = true; refreshCurrent(m_player->state()->path());
    });
    connect(player->state(), &MpvState::timePositionChanged, this, [this](double position) {
        if (m_ready) observe(position, m_player->state()->duration());
    });
}
void AniListClient::observe(double position, double duration)
{
    if (!enabled() || !connected() || !std::isfinite(position) || !std::isfinite(duration) ||
        duration <= 0 || position < duration * threshold() / 100.0) return;
    // Refresh membership independently of the currently selected library item.
    const auto entry = m_library ? m_library->playbackInfo(m_file) : QVariantMap();
    if (!entry.isEmpty() && entry != m_current) { m_current = entry; emit changed(); }
    enqueueCurrent();
}
void AniListClient::syncNow()
{
    if (!connected()) { announce(tr("Connect your AniList account first.")); return; }
    if (!enabled()) { announce(tr("Enable AniList syncing first.")); return; }
    enqueueCurrent();
}
void AniListClient::enqueueCurrent()
{
    const auto link = mapping(currentKey());
    const int id = link.value("id").toInt();
    const int episode = currentEpisode();
    if (id <= 0 || episode <= 0) {
        const QString message = tr("Link the playing title and confirm its episode in AniList settings.");
        if (m_status != message) announce(message);
        return;
    }
    if (m_confirmed.value(id) >= episode || m_rejected.contains(QString::number(id) + ":" + QString::number(episode))) return;
    QJsonArray pending = m_config.value("pending").toArray();
    for (const auto &value : pending) {
        const auto p = value.toObject();
        if (p.value("id").toInt() == id && p.value("episode").toInt() >= episode) return;
    }
    if (pending.size() >= 256) { announce(tr("Pending queue is full. Retry or clear pending updates.")); return; }
    pending.append(QJsonObject{{"id", id}, {"episode", episode}, {"key", currentKey()},
        {"account", m_config.value("user_id")}});
    m_config.insert("pending", pending);
    if (save() && !m_retry.isActive()) processQueue();
}

QJsonObject AniListClient::progressUpdate(const QJsonObject &media, int requested)
{
    if (media.value("id").toInt() <= 0 || requested <= 0) return {};
    const auto entry = media.value("mediaListEntry").toObject();
    const int total = media.value("episodes").toInt();
    // An out-of-range episode usually means a wrong season/offset. Do not clamp
    // it into an accidental completion of the wrong series.
    if (total > 0 && requested > total) return {};
    if (entry.value("progress").toInt() >= requested) return {};
    QJsonObject result{{"mediaId", media.value("id")}, {"progress", requested}};
    if (total > 0 && requested == total) result.insert("status", "COMPLETED");
    else if (entry.value("status").toString() != "REPEATING" &&
             entry.value("status").toString() != "COMPLETED") result.insert("status", "CURRENT");
    return result;
}
void AniListClient::processQueue()
{
    if (m_busy || !connected() || !enabled() || m_retry.isActive()) return;
    auto pending = m_config.value("pending").toArray();
    if (pending.isEmpty()) return;
    const auto item = pending.first().toObject();
    const QString key = item.value("key").toString();
    if (item.value("account") != m_config.value("user_id") ||
        mapping(key).value("id").toInt() != item.value("id").toInt()) {
        pending.removeFirst(); m_config.insert("pending", pending); save(); processQueue(); return;
    }
    m_busy = true; const int generation = m_generation;
    announce(tr("Checking AniList progress…"));
    const auto finish = [this, generation, item](QString error, bool unauthorized, int retry, bool completed) {
        if (generation != m_generation) return;
        m_busy = false;
        if (unauthorized) { m_verified = false; announce(error); return; }
        if (!error.isEmpty()) {
            m_retrySeconds = std::min(3600, std::max(retry, m_retrySeconds * 2));
            if (enabled()) m_retry.start(m_retrySeconds * 1000);
            announce(error + tr(" Pending updates are saved.")); return;
        }
        auto queue = m_config.value("pending").toArray();
        for (qsizetype i = queue.size(); i-- > 0;)
            if (queue.at(i).toObject() == item) queue.removeAt(i);
        m_config.insert("pending", queue); m_retrySeconds = 30;
        if (completed) m_confirmed[item.value("id").toInt()] = std::max(
            m_confirmed.value(item.value("id").toInt()), item.value("episode").toInt());
        if (!completed) m_rejected.insert(QString::number(item.value("id").toInt()) + ":" + QString::number(item.value("episode").toInt()));
        if (!save()) return;
        announce(completed ? tr("AniList is up to date through episode %1.").arg(item.value("episode").toInt()) :
            tr("Episode exceeds this title's episode count. Check the title, season, and offset."));
        QTimer::singleShot(0, this, &AniListClient::processQueue);
    };
    request("query ($id: Int!) { Media(id: $id, type: ANIME) { id episodes title { userPreferred } mediaListEntry { progress status } } }",
        {{"id", item.value("id")}}, [this, generation, item, finish](QJsonObject data, QString error, bool unauthorized, int retry) {
            if (generation != m_generation) return;
            if (!error.isEmpty()) { finish(error, unauthorized, retry, false); return; }
            // A title can be relinked or syncing disabled while the read is in flight.
            if (!enabled() || !m_config.value("pending").toArray().contains(item) ||
                mapping(item.value("key").toString()).value("id").toInt() != item.value("id").toInt()) {
                m_busy = false; emit changed(); return;
            }
            const auto media = data.value("Media").toObject();
            if (media.value("id").toInt() != item.value("id").toInt()) {
                finish(tr("AniList did not return the requested title."), false, 60, false); return;
            }
            const int episode = item.value("episode").toInt();
            const auto update = progressUpdate(media, episode);
            if (update.isEmpty()) {
                finish({}, false, 0, media.value("mediaListEntry").toObject().value("progress").toInt() >= episode);
                return;
            }
            request("mutation ($mediaId: Int!, $progress: Int!, $status: MediaListStatus) { SaveMediaListEntry(mediaId: $mediaId, progress: $progress, status: $status) { progress } }",
                update, [finish, episode](QJsonObject result, QString err, bool auth, int wait) {
                    if (err.isEmpty() && result.value("SaveMediaListEntry").toObject().value("progress").toInt() < episode)
                        err = tr("AniList did not confirm the progress update.");
                    finish(err, auth, wait, err.isEmpty());
                }, m_config.value("access_token").toString());
        }, m_config.value("access_token").toString());
}
void AniListClient::retryPending()
{
    m_retry.stop();
    if (!connected()) { testConnection(); return; }
    processQueue();
}
void AniListClient::clearPending()
{
    if (m_busy) return;
    m_retry.stop(); m_config.remove("pending"); save(); announce(tr("Pending updates cleared."));
}
