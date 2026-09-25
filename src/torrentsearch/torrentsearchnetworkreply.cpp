#include "torrentsearchnetworkreply.h"
#include <QFutureWatcher>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QtConcurrentRun>
#include <curl/curl.h>
#include <algorithm>
#include <cstring>
#include <QCoreApplication>
#include <QFile>
#include <mutex>

namespace {
constexpr qsizetype MAX_BODY = 16 * 1024 * 1024;
constexpr qsizetype MAX_HEADERS = 64 * 1024;

bool sameOrigin(const QUrl &a, const QUrl &b)
{
    const auto port = [](const QUrl &url) { return url.port(url.scheme() == "https" ? 443 : 80); };
    return a.scheme() == b.scheme() && a.host() == b.host() && port(a) == port(b) &&
        b.userInfo().isEmpty();
}
}

TorrentSearchNetworkReply::TorrentSearchNetworkReply(const QNetworkRequest &request, bool cloudflare, QObject *parent)
    : QNetworkReply(parent), m_runner([](const auto &req, bool doh, const auto &cancel) {
        return transfer(req, doh, cancel);
    })
{
    setRequest(request);
    setUrl(request.url());
    setOperation(QNetworkAccessManager::GetOperation);
    open(QIODevice::ReadOnly | QIODevice::Unbuffered);
    QTimer::singleShot(0, this, [this, cloudflare] { startAttempt(cloudflare); });
}

TorrentSearchNetworkReply::~TorrentSearchNetworkReply() { m_cancel->store(true); }

void TorrentSearchNetworkReply::startAttempt(bool cloudflare)
{
    if (isFinished()) return;
    setProperty("mementoSystemDns", !cloudflare);
    emit routeChanged(cloudflare ? tr("%1 · Cloudflare DNS").arg(url().host()) :
        tr("%1 · System DNS").arg(url().host()));
    auto *watcher = new QFutureWatcher<Response>(this);
    connect(watcher, &QFutureWatcher<Response>::finished, this, [this, watcher, cloudflare] {
        const Response result = watcher->result();
        watcher->deleteLater();
        if (isFinished()) return;
        if (cloudflare && result.retryable && !m_cancel->load()) {
            startAttempt(false);
        } else {
            complete(result);
        }
    });
    watcher->setFuture(QtConcurrent::run(m_runner, request(), cloudflare, m_cancel));
}

void TorrentSearchNetworkReply::abort()
{
    if (isFinished()) return;
    m_cancel->store(true);
    Response response;
    response.error = OperationCanceledError;
    response.message = tr("Request canceled.");
    complete(response);
}

void TorrentSearchNetworkReply::complete(Response response)
{
    if (isFinished()) return;
    m_data = std::move(response.body);
    setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.status);
    for (const auto &[name, value] : response.headers) setRawHeader(name, value);
    setProperty("mementoOversized", response.oversized);
    if (response.error != NoError) setError(response.error, response.message);
    setFinished(true);
    emit metaDataChanged();
    if (response.error != NoError) emit errorOccurred(response.error);
    emit downloadProgress(m_data.size(), m_data.size());
    if (!m_data.isEmpty()) emit readyRead();
    emit finished();
}

qint64 TorrentSearchNetworkReply::bytesAvailable() const
{
    return m_data.size() - m_offset + QNetworkReply::bytesAvailable();
}

qint64 TorrentSearchNetworkReply::readData(char *data, qint64 maximum)
{
    const qint64 count = std::min(maximum, static_cast<qint64>(m_data.size()) - m_offset);
    if (count <= 0) return isFinished() ? -1 : 0;
    std::memcpy(data, m_data.constData() + m_offset, static_cast<size_t>(count));
    m_offset += count;
    return count;
}

TorrentSearchNetworkReply::Response TorrentSearchNetworkReply::transfer(
    const QNetworkRequest &request, bool cloudflare, const CancelFlag &cancel, const QString &dohUrl)
{
    static std::once_flag initialized;
    static CURLcode initialization = CURLE_FAILED_INIT;
    std::call_once(initialized, [] { initialization = curl_global_init(CURL_GLOBAL_DEFAULT); });
    Response result;
    if (initialization != CURLE_OK) {
        result.error = UnknownNetworkError; result.message = tr("Could not initialize the network transport.");
        return result;
    }
    QUrl current = request.url();
    for (int redirects = 0; redirects <= 5; ++redirects) {
        result = {};
        CURL *curl = curl_easy_init();
        if (!curl) { result.error = UnknownNetworkError; result.message = tr("Could not create a network request."); return result; }
        struct State {
            Response *response;
            const std::atomic_bool *cancel;
            qsizetype headerBytes{0};
        } state{&result, cancel.get()};
        const QByteArray encoded = current.toEncoded();
        const QByteArray doh = dohUrl.toUtf8();
        char errorBuffer[CURL_ERROR_SIZE]{};
        curl_slist *headers = nullptr;
        for (const auto &name : request.rawHeaderList()) {
            const QByteArray line = name + ": " + request.rawHeader(name);
            headers = curl_slist_append(headers, line.constData());
        }
        // Bootstrap the resolver itself without depending on system DNS.
        curl_slist *bootstrap = nullptr;
        if (cloudflare) bootstrap = curl_slist_append(bootstrap, "cloudflare-dns.com:443:1.1.1.1,1.0.0.1");
        curl_easy_setopt(curl, CURLOPT_URL, encoded.constData());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, cloudflare ? 6000L : 10000L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 20000L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
        // Use native trust roots and a bundled CA set on standalone desktop builds.
        curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_NATIVE_CA));
        // A bundled CA set also covers curl's separate DNS-over-HTTPS handles.
        // Read with Qt so non-ASCII install paths work with every curl backend.
        QFile caFile(QCoreApplication::applicationDirPath() + "/cacert.pem");
        QByteArray caData;
        if (caFile.open(QIODevice::ReadOnly)) caData = caFile.readAll();
        if (!caData.isEmpty()) {
            curl_blob ca{caData.data(), static_cast<size_t>(caData.size()), CURL_BLOB_COPY};
            curl_easy_setopt(curl, CURLOPT_CAINFO_BLOB, &ca);
        }
#endif
        curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);
        if (cloudflare) {
            curl_easy_setopt(curl, CURLOPT_DOH_URL, doh.constData());
            curl_easy_setopt(curl, CURLOPT_RESOLVE, bootstrap);
            curl_easy_setopt(curl, CURLOPT_DOH_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(curl, CURLOPT_DOH_SSL_VERIFYHOST, 2L);
        }
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &state);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char *data, size_t size, size_t count, void *opaque) -> size_t {
            auto &s = *static_cast<State *>(opaque);
            const size_t length = size * count;
            if (s.cancel->load()) return 0;
            if (length > static_cast<size_t>(MAX_BODY - s.response->body.size())) {
                s.response->oversized = true; return 0;
            }
            s.response->body.append(data, static_cast<qsizetype>(length));
            return length;
        });
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &state);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, +[](char *data, size_t size, size_t count, void *opaque) -> size_t {
            auto &s = *static_cast<State *>(opaque);
            const size_t length = size * count;
            if (length > static_cast<size_t>(MAX_HEADERS - s.headerBytes)) {
                s.response->oversized = true; return 0;
            }
            s.headerBytes += static_cast<qsizetype>(length);
            const QByteArray line(data, static_cast<qsizetype>(length));
            if (line.startsWith("HTTP/")) s.response->headers.clear();
            const auto colon = line.indexOf(':');
            if (colon > 0) s.response->headers.append({line.left(colon).trimmed(), line.mid(colon + 1).trimmed()});
            return length;
        });
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, cancel.get());
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, +[](void *opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
            return static_cast<std::atomic_bool *>(opaque)->load() ? 1 : 0;
        });
        const CURLcode code = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        result.status = static_cast<int>(status);
        curl_slist_free_all(headers);
        curl_slist_free_all(bootstrap);
        curl_easy_cleanup(curl);
        if (cancel->load()) { result.error = OperationCanceledError; result.message = tr("Request canceled."); return result; }
        if (result.oversized) { result.error = UnknownContentError; result.message = tr("The server response exceeds the size limit."); return result; }
        if (code != CURLE_OK) {
            result.message = QString::fromUtf8(errorBuffer[0] ? errorBuffer : curl_easy_strerror(code));
            result.error = code == CURLE_COULDNT_RESOLVE_HOST ? HostNotFoundError :
                code == CURLE_OPERATION_TIMEDOUT ? TimeoutError :
                code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CONNECT_ERROR ? SslHandshakeFailedError :
                code == CURLE_COULDNT_CONNECT ? ConnectionRefusedError : UnknownNetworkError;
            result.retryable = code == CURLE_COULDNT_RESOLVE_HOST || code == CURLE_COULDNT_RESOLVE_PROXY ||
                code == CURLE_COULDNT_CONNECT || code == CURLE_OPERATION_TIMEDOUT ||
                code == CURLE_PEER_FAILED_VERIFICATION || code == CURLE_SSL_CONNECT_ERROR ||
                code == CURLE_RECV_ERROR || code == CURLE_SEND_ERROR || code == CURLE_GOT_NOTHING;
            return result;
        }
        if (status >= 300 && status < 400) {
            QByteArray location;
            for (const auto &[name, value] : result.headers)
                if (name.compare("location", Qt::CaseInsensitive) == 0) location = value;
            const QUrl next = current.resolved(QUrl::fromEncoded(location));
            if (location.isEmpty() || redirects == 5 || !sameOrigin(request.url(), next)) {
                result.error = ProtocolInvalidOperationError;
                result.message = tr("The server returned an unsupported redirect."); return result;
            }
            current = next;
            continue;
        }
        if (status >= 400) {
            result.error = status == 404 ? ContentNotFoundError : status == 403 ? ContentAccessDenied : UnknownContentError;
            result.message = tr("HTTP %1 from %2").arg(status).arg(current.host());
        }
        return result;
    }
    return result;
}
