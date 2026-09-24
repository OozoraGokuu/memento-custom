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

#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class Context;
class JimakuClientTest;
class QNetworkReply;

/**
 * Finds, downloads, and immediately attaches Japanese subtitles from Jimaku.
 */
class JimakuClient : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString apiKey READ apiKey WRITE setApiKey NOTIFY apiKeyChanged)
    Q_PROPERTY(bool apiKeyConfigured READ apiKeyConfigured NOTIFY apiKeyChanged)
    Q_PROPERTY(bool autoFetch READ autoFetch WRITE setAutoFetch NOTIFY autoFetchChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged)
    Q_PROPERTY(QVariantList fileResults READ fileResults NOTIFY fileResultsChanged)
    Q_PROPERTY(QString selectedEntryName READ selectedEntryName NOTIFY selectionChanged)
    Q_PROPERTY(int selectedEpisode READ selectedEpisode NOTIFY selectionChanged)

public:
    explicit JimakuClient(Context *context, QObject *parent = nullptr);
    virtual ~JimakuClient() = default;

    [[nodiscard]] const QString &apiKey() const noexcept;
    void setApiKey(const QString &value);
    [[nodiscard]] bool apiKeyConfigured() const noexcept;

    [[nodiscard]] bool autoFetch() const noexcept;
    void setAutoFetch(bool value);

    [[nodiscard]] bool busy() const noexcept;
    [[nodiscard]] const QString &status() const noexcept;
    [[nodiscard]] const QVariantList &searchResults() const noexcept;
    [[nodiscard]] const QVariantList &fileResults() const noexcept;
    [[nodiscard]] const QString &selectedEntryName() const noexcept;
    [[nodiscard]] int selectedEpisode() const noexcept;

    /** Fetch the best Jimaku subtitle for the currently playing episode. */
    Q_INVOKABLE void fetchForCurrentMedia();

    /** Verify the configured API key against Jimaku. */
    Q_INVOKABLE void testConnection();

    /** Search Jimaku and let the user choose the exact matching title. */
    Q_INVOKABLE void search(const QString &query);

    /** Load subtitle files for a selected title and optional episode. */
    Q_INVOKABLE void selectEntry(int resultIndex, int episode = -1);

    /**
     * Load every subtitle file for a selected title while retaining the
     * episode that the eventual file/ZIP must match.
     */
    Q_INVOKABLE void selectEntryAllFiles(int resultIndex, int episode);

    /** Download and attach the exact file selected by the user. */
    Q_INVOKABLE void attachResult(int resultIndex);

    /** Cancel the current request while preserving displayed results. */
    Q_INVOKABLE void cancel();

    /** Clear manual search results and selection. */
    Q_INVOKABLE void clearSearch();

    /** Suggested Jimaku title parsed from the currently playing media. */
    [[nodiscard]] Q_INVOKABLE QString suggestedTitle() const;

    /** Suggested episode parsed from the currently playing media. */
    [[nodiscard]] Q_INVOKABLE int suggestedEpisode() const;

signals:
    void apiKeyChanged();
    void autoFetchChanged(bool value);
    void busyChanged(bool value);
    void statusChanged(const QString &value);
    void searchResultsChanged();
    void fileResultsChanged();
    void selectionChanged();
    void subtitleAttached(const QString &fileName, const QString &entryName);
    void failed(const QString &error);
    void connectionTested(bool success, const QString &message);

private:
    friend class JimakuClientTest;
    friend class KitsunekkoClient;

    struct MediaInfo
    {
        QString path;
        QString title;
        int season{-1};
        int episode{-1};
    };

    struct FileInfo
    {
        QString name;
        QUrl url;
        qint64 size{0};
        QString lastModified;
    };

    enum class Operation
    {
        None,
        Fetch,
        Test,
        ManualSearch,
        ManualFiles,
        ManualDownload,
    };

    [[nodiscard]] MediaInfo currentMedia() const;
    void beginSearch(quint64 generation, const QString &query);
    void handleSearch(QNetworkReply *reply, quint64 generation);
    void beginFileList(quint64 generation, qint64 entryId, bool withEpisode);
    void handleFileList(
        QNetworkReply *reply,
        quint64 generation,
        qint64 entryId,
        bool withEpisode
    );
    void beginDownload(quint64 generation, const QUrl &url, int redirects = 0);
    void handleDownload(
        QNetworkReply *reply,
        quint64 generation,
        int redirects
    );
    void handleAttachedTracksChanged();
    void attachDownloaded(const QByteArray &data, quint64 generation);
    void attachPath(const QString &path, quint64 generation);
    void clearPendingAttach();

    [[nodiscard]] QNetworkReply *getApi(const QUrl &url);
    [[nodiscard]] QNetworkReply *getDownload(const QUrl &url);
    [[nodiscard]] bool parseJsonArray(
        QNetworkReply *reply,
        QJsonArray *array,
        QString *error
    ) const;
    [[nodiscard]] QString replyError(QNetworkReply *reply) const;
    [[nodiscard]] FileInfo selectFile(
        const QJsonArray &files,
        bool requireExplicitEpisode = false) const;
    [[nodiscard]] QVector<FileInfo> usableFiles(const QJsonArray &files) const;
    [[nodiscard]] QVariantMap entryMap(const QJsonObject &entry) const;
    [[nodiscard]] QVariantMap fileMap(const FileInfo &file) const;
    [[nodiscard]] QString cachePath(const QString &name) const;
    [[nodiscard]] QString extractZip(const QString &archivePath, QString *error) const;
    [[nodiscard]] bool safeDownloadUrl(const QUrl &url) const;

    void useSelectedFile(quint64 generation);
    void selectEntryFiles(int resultIndex, int episode, bool withEpisode);
    void setSearchEntries(QVector<QJsonObject> entries);
    void setBrowseFiles(QVector<FileInfo> files);

    void cancelActive();
    void finishSuccess(const QString &message);
    void finishError(const QString &error);
    void setBusy(bool value);
    void setStatus(const QString &value);

    [[nodiscard]] static QString cleanTitle(const QString &filename);
    [[nodiscard]] static int seasonNumber(const QString &filename);
    [[nodiscard]] static int episodeNumber(const QString &filename);
    [[nodiscard]] static int entryScore(const QString &query, const QJsonObject &entry);
    [[nodiscard]] static bool entryMatchesSeason(
        int season,
        const QJsonObject &entry);
    [[nodiscard]] static int fileScore(const QString &name, int episode);
    [[nodiscard]] static bool supportedSubtitle(const QString &name);

    Context *m_context;
    QNetworkAccessManager m_manager;
    QPointer<QNetworkReply> m_reply;
    QTimer m_attachTimer;
    QMetaObject::Connection m_attachTrackConnection;
    QUrl m_baseUrl;
    QString m_apiKey;
    QString m_requestApiKey;
    QString m_status;
    MediaInfo m_media;
    MediaInfo m_fileListMedia;
    FileInfo m_file;
    QVector<QJsonObject> m_searchEntries;
    QVector<FileInfo> m_browseFiles;
    QVariantList m_searchResultMaps;
    QVariantList m_fileResultMaps;
    QString m_manualQuery;
    QString m_entryName;
    QString m_pendingAttachPath;
    int m_selectedEntryIndex{-1};
    int m_selectedEpisode{-1};
    quint64 m_generation{0};
    quint64 m_pendingAttachGeneration{0};
    Operation m_operation{Operation::None};
    bool m_autoFetch{true};
    bool m_busy{false};
};
