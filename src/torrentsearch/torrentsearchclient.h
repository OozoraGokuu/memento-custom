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

#pragma once

#include <QObject>
#include <QDateTime>

#include <QDateTime>
#include <QHash>
#include <QPointer>
#include <QString>
#include <QUrl>
#include <QVariantList>
#include <QVector>

class QNetworkReply;
class TorrentSearchClientTest;

/**
 * Searches a user-configured RSS feed and downloads user-selected torrent
 * metadata. It never starts a payload download itself.
 */
class TorrentSearchClient : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(QString sortOrder READ sortOrder WRITE setSortOrder NOTIFY sortOrderChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool cloudflareDns READ cloudflareDns WRITE setCloudflareDns NOTIFY cloudflareDnsChanged)
    Q_PROPERTY(QString networkRoute READ networkRoute NOTIFY networkRouteChanged)
    Q_PROPERTY(QUrl baseUrl READ baseUrl WRITE setBaseUrl NOTIFY baseUrlChanged)

public:
    explicit TorrentSearchClient(QObject *parent = nullptr);
    ~TorrentSearchClient() override = default;

    [[nodiscard]] const QVariantList &results() const noexcept;
    [[nodiscard]] const QString &sortOrder() const noexcept;
    Q_INVOKABLE bool configureProvider(const QUrl &url);
    void setBaseUrl(const QUrl &url) { configureProvider(url); }
    void setSortOrder(const QString &order);
    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] const QString &status() const noexcept;
    [[nodiscard]] const QUrl &baseUrl() const noexcept;

    [[nodiscard]] bool cloudflareDns() const noexcept { return m_cloudflareDns; }
    void setCloudflareDns(bool enabled);
    [[nodiscard]] const QString &networkRoute() const noexcept { return m_networkRoute; }

    /** Search the public RSS feed without downloading any torrent payload. */
    Q_INVOKABLE void search(const QString &query);

    /** Download the selected .torrent metadata to Memento's cache. */
    Q_INVOKABLE void downloadResult(int index);

    /** Cancel the active provider request without touching playback. */
    Q_INVOKABLE void cancel();

    /** Clear displayed results. Cached provider responses remain available. */
    Q_INVOKABLE void clear();

signals:
    void baseUrlChanged();
    void resultsChanged();
    void sortOrderChanged();
    void cloudflareDnsChanged();
    void networkRouteChanged();
    void busyChanged(bool value);
    void statusChanged(const QString &value);
    void torrentReady(const QUrl &fileUrl, const QString &title);
    void failed(const QString &message);

private:
    friend class TorrentSearchClientTest;

    struct Result
    {
        qint64 id{-1};
        QString title;
        QString category;
        QString size;
        QString uploader;
        QString published;
        QDateTime publishedAt;
        QString infoHash;
        QString magnet;
        QUrl detailUrl;
        QUrl torrentUrl;
        int seeders{0};
        int leechers{0};
        int downloads{0};
        bool trusted{false};
        bool remake{false};
    };

    struct CacheEntry
    {
        QDateTime fetchedAt;
        QVector<Result> items;
    };

    void requestSearch(
        const QUrl &provider,
        const QString &query,
        quint64 generation,
        const QString &key,
        bool discover = true
    );
    void handleSearch(
        QNetworkReply *reply,
        quint64 generation,
        QString key,
        QString query,
        bool discover,
        QUrl provider
    );
    void handleDownload(
        QNetworkReply *reply,
        quint64 generation,
        Result result
    );
    [[nodiscard]] QNetworkReply *get(const QUrl &url, const QByteArray &accept);
    [[nodiscard]] bool parseFeed(
        const QByteArray &data,
        QVector<Result> *items,
        QString *error
    ) const;
    [[nodiscard]] QUrl discoverFeed(const QByteArray &data, const QUrl &page) const;
    [[nodiscard]] QVariantMap resultMap(const Result &result) const;
    [[nodiscard]] QString responseError(QNetworkReply *reply) const;
    [[nodiscard]] QString torrentCachePath(const Result &result) const;
    [[nodiscard]] bool providerUrl(const QUrl &url) const;
    [[nodiscard]] static qint64 resultId(const QUrl &first, const QUrl &second);
    [[nodiscard]] static QString magnetFor(
        const QString &hash,
        const QString &title
    );
    [[nodiscard]] static bool validateTorrentData(
        const QByteArray &data,
        const QString &expectedInfoHash,
        QString *error
    );
    [[nodiscard]] static bool parseBoolean(const QString &value);
    void setResults(QVector<Result> items);
    void setBusy(bool value);
    void setStatus(const QString &value);
    void finishError(const QString &message);

    QPointer<QNetworkReply> m_reply;
    QUrl m_baseUrl;
    QUrl m_responseBaseUrl;
    QVariantList m_resultMaps;
    QVector<Result> m_results;
    QHash<QString, CacheEntry> m_cache;
    QString m_status;
    QString m_networkRoute;
    bool m_cloudflareDns{true};
    QString m_sortOrder{QStringLiteral("seeders")};
    quint64 m_generation{0};
    bool m_busy{false};
};
