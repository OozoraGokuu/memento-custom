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
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <memory>

class TorrentEngineTest;

/**
 * Owns Memento's BitTorrent session and exposes torrent files through a local
 * HTTP range server. mpv can therefore seek and play a file while libtorrent
 * downloads only the pieces needed for that file.
 */
class TorrentEngine : public QObject
{
    Q_OBJECT

public:
    struct AddResult
    {
        QString id;
        QString title;
        QString source;
        QStringList paths;
        QList<qint64> sizes;
        QList<int> fileIndices;
        QString error;

        [[nodiscard]] bool valid() const noexcept { return !id.isEmpty(); }
    };

    explicit TorrentEngine(QObject *parent = nullptr);
    ~TorrentEngine() override;

    [[nodiscard]] AddResult addTorrentFile(const QString &path);
    [[nodiscard]] AddResult addMagnet(const QString &magnet);
    [[nodiscard]] AddResult restore(const QString &id, const QString &source);
    void removeTorrent(const QString &id, bool deleteFiles = false);

    /** Selects the file for streaming and returns its localhost HTTP URL. */
    [[nodiscard]] QString streamUrl(
        const QString &id,
        int fileIndex,
        const QString &displayName
    );

    /** Returns a URL without changing priorities. */
    [[nodiscard]] QString urlFor(
        const QString &id,
        int fileIndex,
        const QString &displayName
    ) const;

    /** Resolves a selected stream URL previously issued by streamUrl(). */
    [[nodiscard]] bool identifyStreamUrl(
        const QString &url,
        QString *id,
        int *fileIndex
    ) const;

    [[nodiscard]] QVariantMap status(const QString &id) const;

signals:
    void metadataReady(
        const QString &id,
        const QString &title,
        const QStringList &paths,
        const QList<qint64> &sizes,
        const QList<int> &fileIndices,
        const QString &storedSource
    );
    void torrentChanged(const QString &id);
    void torrentError(const QString &id, const QString &message);

private:
    friend class TorrentEngineTest;
    [[nodiscard]] static bool safeTrackerUrl(const QString &url);

    class Private;
    std::unique_ptr<Private> d;
};
