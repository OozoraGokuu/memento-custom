#pragma once

#include <QNetworkReply>
#include <atomic>
#include <functional>
#include <memory>

class TorrentSearchNetworkReplyTest;

// GET transport with verified Cloudflare DoH, then system-DNS fallback.
// The original hostname remains in the URL, preserving TLS SNI and verification.
class TorrentSearchNetworkReply final : public QNetworkReply
{
    Q_OBJECT
public:
    TorrentSearchNetworkReply(const QNetworkRequest &request, bool cloudflare, QObject *parent = nullptr);
    ~TorrentSearchNetworkReply() override;
    void abort() override;
    qint64 bytesAvailable() const override;
    bool isSequential() const override { return true; }

signals:
    void routeChanged(const QString &route);

protected:
    qint64 readData(char *data, qint64 maximum) override;

private:
    friend class TorrentSearchNetworkReplyTest;
    struct Response {
        QByteArray body;
        QList<QPair<QByteArray, QByteArray>> headers;
        int status{0};
        NetworkError error{NoError};
        QString message;
        bool retryable{false};
        bool oversized{false};
    };
    using CancelFlag = std::shared_ptr<std::atomic_bool>;
    using Runner = std::function<Response(const QNetworkRequest &, bool, const CancelFlag &)>;
    static Response transfer(const QNetworkRequest &request, bool cloudflare,
        const CancelFlag &cancel, const QString &dohUrl = QStringLiteral("https://cloudflare-dns.com/dns-query"));
    void startAttempt(bool cloudflare);
    void complete(Response response);
    CancelFlag m_cancel{std::make_shared<std::atomic_bool>(false)};
    Runner m_runner;
    QByteArray m_data;
    qint64 m_offset{0};
};
