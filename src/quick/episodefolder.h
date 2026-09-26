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
#include <QSet>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class TorrentEngine;

/**
 * @brief A persistent library of episode folders and imported torrent sets.
 *
 * Torrent entries are streamed on demand through TorrentEngine. Local episode
 * folders remain available as a separate library item type.
 */
class EpisodeFolder : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantMap lastSubtitleSearch READ lastSubtitleSearch NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList library READ library NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList episodes READ episodes NOTIFY episodesChanged)
    Q_PROPERTY(QVariantMap currentEntry READ currentEntry NOTIFY selectionChanged)
    Q_PROPERTY(int currentIndex READ currentIndex WRITE setCurrentIndex NOTIFY selectionChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

    /* Compatibility properties used by the Media menu. */
    Q_PROPERTY(QString folder READ folder NOTIFY selectionChanged)
    Q_PROPERTY(QString folderName READ folderName NOTIFY selectionChanged)
    Q_PROPERTY(QStringList files READ files NOTIFY episodesChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY episodesChanged)
    Q_PROPERTY(int watchedCount READ watchedCount NOTIFY watchedChanged)

public:
    explicit EpisodeFolder(QObject *parent = nullptr);

    [[nodiscard]] QVariantList library() const;
    [[nodiscard]] QVariantList episodes() const;
    [[nodiscard]] QVariantMap currentEntry() const;
    [[nodiscard]] int currentIndex() const noexcept;
    void setCurrentIndex(int index);
    [[nodiscard]] const QString &lastError() const noexcept;

    [[nodiscard]] const QString &folder() const noexcept;
    [[nodiscard]] QString folderName() const;
    [[nodiscard]] QStringList files() const;
    [[nodiscard]] int totalCount() const noexcept;
    [[nodiscard]] int watchedCount() const noexcept;

    /** Adds a recursively scanned local episode folder to the library. */
    Q_INVOKABLE int addFolder(const QUrl &folderUrl);

    /** Imports a .torrent's video file list. Returns -1 on invalid metadata. */
    Q_INVOKABLE int addTorrent(const QUrl &torrentUrl);

    /** Adds a magnet link. Episodes appear when its metadata arrives. */
    Q_INVOKABLE int addMagnet(const QString &magnet);

    /** Sets or changes the downloaded content folder for an entry. */
    Q_INVOKABLE bool setContentFolder(int entryIndex, const QUrl &folderUrl);

    /** Removes the record and retained metadata; cached media is optional. */
    Q_INVOKABLE void removeEntry(int entryIndex, bool deleteCache = false);

    /** Rescans an entry, or the selected entry when entryIndex is omitted. */
    Q_INVOKABLE QStringList rescan(int entryIndex = -1);

    /** Compatibility alias: add/select an episode folder. */
    Q_INVOKABLE QStringList open(const QUrl &folderUrl);

    /** Stable identity and original filename for a playing local/torrent episode. */
    QVariantMap playbackInfo(const QString &file) const;

    QVariantMap lastSubtitleSearch() const { return m_lastSubtitleSearch; }
    Q_INVOKABLE bool rememberSubtitleSearch(const QVariantMap &link, int episode);
    Q_INVOKABLE int defaultAudioTrackForFile(const QString &file) const;
    Q_INVOKABLE bool setDefaultAudioTrackForFile(const QString &file, int track);
    Q_INVOKABLE QVariantMap subtitleLinkForFile(const QString &file) const;
    Q_INVOKABLE bool setSubtitleLinkForFile(const QString &file, const QVariantMap &link);
    Q_INVOKABLE bool clearSubtitleLinkForFile(const QString &file);

    Q_INVOKABLE bool containsFile(const QString &file) const;
    /** True only for a currently issued, process-local torrent stream URL. */
    Q_INVOKABLE bool isTransientStream(const QString &file) const;
    Q_INVOKABLE QString fileName(const QString &file) const;
    Q_INVOKABLE bool isWatched(const QString &file) const;
    Q_INVOKABLE void setWatched(const QString &file, bool watched);
    Q_INVOKABLE void setEpisodeWatched(int episodeIndex, bool watched);
    Q_INVOKABLE void recordPlayed(const QString &file);
    Q_INVOKABLE QString episodePath(int episodeIndex);

signals:
    void libraryChanged();
    void episodesChanged();
    void selectionChanged();
    void watchedChanged();
    void lastErrorChanged();
    void errorOccurred(const QString &message);

private:
    struct Entry
    {
        QString id;
        QString title;
        QString type;
        QString source;
        QString folder;
        QStringList relativePaths;
        QStringList resolvedPaths;
        QList<qint64> episodeSizes;
        QList<int> torrentFileIndices;
        QSet<QString> watched;
        QString lastPlayed;
        QVariantMap subtitleLink;
        int audioTrack{-1};
    };

    [[nodiscard]] static QString normalizedPath(const QString &value);
    [[nodiscard]] static QString stableFolderIdentity(const QString &folder);
    [[nodiscard]] static bool isVideoPath(const QString &path);
    [[nodiscard]] static QString displayName(const QString &relativePath);
    [[nodiscard]] static QString makeFolderId(const QString &folder);
    [[nodiscard]] int findEntryForFile(const QString &file, int *episodeIndex) const;
    [[nodiscard]] int findEntryById(const QString &id) const;
    [[nodiscard]] int watchedCount(const Entry &entry) const noexcept;
    [[nodiscard]] int availableCount(const Entry &entry) const noexcept;
    [[nodiscard]] QVariantMap entryMap(const Entry &entry, int index) const;
    [[nodiscard]] QStringList playableFiles(const Entry &entry) const;
    [[nodiscard]] bool selectCurrentIndex(int index);
    [[nodiscard]] bool scanFolder(Entry &entry);
    void resolveTorrentFiles(Entry &entry);
    void loadLibrary();
    void migrateLegacyFolder();
    void loadLegacyWatched(Entry &entry) const;
    [[nodiscard]] bool writeLibrary() const;
    void reportPersistenceError();
    void setLastError(const QString &error);
    void notifyEntryChanged(bool selectionMayHaveChanged = false);
    void applyTorrentMetadata(
        const QString &id,
        const QString &title,
        const QStringList &paths,
        const QList<qint64> &sizes,
        const QList<int> &fileIndices,
        const QString &storedSource
    );
    [[nodiscard]] bool updateTorrentMetadata(
        const QString &id,
        const QString &title,
        const QStringList &paths,
        const QList<qint64> &sizes,
        const QList<int> &fileIndices,
        const QString &storedSource,
        bool selectEntry
    );

    QVariantMap m_lastSubtitleSearch;
    QVector<Entry> m_entries;
    int m_currentIndex{-1};
    QString m_lastError;
    QString m_emptyString;
    TorrentEngine *m_torrentEngine;
};
