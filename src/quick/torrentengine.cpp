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

#include "quick/torrentengine.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <sstream>
#include <utility>
#include <vector>

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QHostAddress>
#include <QMimeDatabase>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <libtorrent/add_torrent_params.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/error_code.hpp>
#include <libtorrent/file_storage.hpp>
#include <libtorrent/ip_filter.hpp>
#include <libtorrent/load_torrent.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/settings_pack.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/torrent_handle.hpp>
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/version.hpp>

#include "util/directoryutils.h"

namespace lt = libtorrent;

namespace
{

constexpr int ALERT_INTERVAL_MS = 100;
constexpr int STATUS_INTERVAL_TICKS = 20;
constexpr int STREAM_LOOKAHEAD_PIECES = 8;
constexpr qint64 STREAM_CHUNK_SIZE = 256 * 1024;
constexpr qint64 MAX_PENDING_BYTES = 2 * 1024 * 1024;
constexpr qint64 MAX_TORRENT_METADATA_SIZE = 64 * 1024 * 1024;
constexpr qint64 COPY_CHUNK_SIZE = 256 * 1024;
constexpr int MAX_STREAM_CLIENTS = 32;
constexpr qint64 HEADER_TIMEOUT_MS = 10 * 1000;
constexpr qint64 STREAM_IDLE_TIMEOUT_MS = 120 * 1000;
constexpr qint64 STREAM_HARD_STALL_TIMEOUT_MS = 10 * 60 * 1000;
constexpr int MAX_FILE_IO_FAILURES = 20;
constexpr int MAX_TORRENT_FILES = 10000;
constexpr qint64 MAX_TORRENT_PATH_BYTES = 8 * 1024 * 1024;

const lt::file_storage &torrentFiles(const lt::torrent_info &info)
{
#if LIBTORRENT_VERSION_NUM >= 20100
    return info.layout();
#else
    return info.files();
#endif
}

bool isValidTorrentId(const QString &id)
{
    static const QRegularExpression expression(
        "^torrent-(?:v[12]-)?(?:[0-9a-f]{40}|[0-9a-f]{64})$",
        QRegularExpression::CaseInsensitiveOption
    );
    return expression.match(id).hasMatch();
}

lt::settings_pack makeSettings()
{
    lt::settings_pack settings;
    settings.set_str(
        lt::settings_pack::user_agent,
        "Memento/2.1.0 libtorrent/" LIBTORRENT_VERSION
    );
    settings.set_str(
        lt::settings_pack::listen_interfaces,
        "0.0.0.0:0,[::]:0"
    );
    settings.set_int(
        lt::settings_pack::alert_mask,
        lt::alert_category::error |
            lt::alert_category::storage |
            lt::alert_category::status
    );
    settings.set_bool(lt::settings_pack::enable_dht, true);
    settings.set_bool(lt::settings_pack::enable_lsd, true);
    settings.set_bool(lt::settings_pack::enable_upnp, true);
    settings.set_bool(lt::settings_pack::enable_natpmp, true);
    settings.set_bool(lt::settings_pack::ssrf_mitigation, true);
    settings.set_bool(lt::settings_pack::allow_idna, false);
    return settings;
}

lt::ip_filter makeIpFilter()
{
    lt::ip_filter filter;
    const auto block = [&filter](const char *first, const char *last) {
        filter.add_rule(
            lt::make_address(first), lt::make_address(last), lt::ip_filter::blocked
        );
    };
    // Reject every IPv4 special-use range that cannot be a public tracker.
    // This also prevents a hostname that resolves to one of these ranges from
    // bypassing the literal-address validation in safeTrackerUrl().
    block("0.0.0.0", "0.255.255.255");
    block("10.0.0.0", "10.255.255.255");
    block("100.64.0.0", "100.127.255.255");
    block("127.0.0.0", "127.255.255.255");
    block("169.254.0.0", "169.254.255.255");
    block("172.16.0.0", "172.31.255.255");
    block("192.0.0.0", "192.0.0.255");
    block("192.0.2.0", "192.0.2.255");
    block("192.168.0.0", "192.168.255.255");
    block("192.88.99.0", "192.88.99.255");
    block("198.18.0.0", "198.19.255.255");
    block("198.51.100.0", "198.51.100.255");
    block("203.0.113.0", "203.0.113.255");
    block("224.0.0.0", "255.255.255.255");

    // Reject unspecified, loopback, translated/special-use, private,
    // link-local, documentation, 6to4 and multicast IPv6 destinations.
    block("::", "::ffff:ffff");
    block("::ffff:0:0", "::ffff:ffff:ffff");
    block("64:ff9b::", "64:ff9b::ffff:ffff");
    block("64:ff9b:1::", "64:ff9b:1:ffff:ffff:ffff:ffff:ffff");
    block("100::", "100::ffff:ffff:ffff:ffff");
    block("2001::", "2001:1ff:ffff:ffff:ffff:ffff:ffff:ffff");
    block("2001:db8::", "2001:db8:ffff:ffff:ffff:ffff:ffff:ffff");
    block("2002::", "2002:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    block("3fff::", "3fff:0fff:ffff:ffff:ffff:ffff:ffff:ffff");
    block("5f00::", "5f00:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    block("fc00::", "fdff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    block("fe80::", "febf:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    block("fec0::", "feff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    block("ff00::", "ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff");
    return filter;
}

bool isLocalAddress(const QHostAddress &address)
{
    bool isV4 = false;
    const quint32 ipv4 = address.toIPv4Address(&isV4);
    if (isV4)
    {
        return address.protocol() == QAbstractSocket::IPv6Protocol ||
            (ipv4 & 0xff000000U) == 0x00000000U || // 0.0.0.0/8
            (ipv4 & 0xff000000U) == 0x0a000000U ||   // 10.0.0.0/8
            (ipv4 & 0xffc00000U) == 0x64400000U ||   // 100.64.0.0/10
            (ipv4 & 0xff000000U) == 0x7f000000U ||   // 127.0.0.0/8
            (ipv4 & 0xffff0000U) == 0xa9fe0000U ||   // 169.254.0.0/16
            (ipv4 & 0xfff00000U) == 0xac100000U ||   // 172.16.0.0/12
            (ipv4 & 0xffffff00U) == 0xc0000000U ||   // 192.0.0.0/24
            (ipv4 & 0xffffff00U) == 0xc0000200U ||   // 192.0.2.0/24
            (ipv4 & 0xffff0000U) == 0xc0a80000U ||   // 192.168.0.0/16
            (ipv4 & 0xffffff00U) == 0xc0586300U ||   // 192.88.99.0/24
            (ipv4 & 0xfffe0000U) == 0xc6120000U ||   // 198.18.0.0/15
            (ipv4 & 0xffffff00U) == 0xc6336400U ||   // 198.51.100.0/24
            (ipv4 & 0xffffff00U) == 0xcb007100U ||   // 203.0.113.0/24
            ipv4 >= 0xe0000000U;                     // multicast/reserved
    }

    if (address.protocol() != QAbstractSocket::IPv6Protocol)
    {
        return true;
    }
    return address.isInSubnet(QHostAddress(QStringLiteral("::")), 96) ||
        address.isInSubnet(QHostAddress(QStringLiteral("::ffff:0:0")), 96) ||
        address.isInSubnet(QHostAddress(QStringLiteral("64:ff9b::")), 96) ||
        address.isInSubnet(QHostAddress(QStringLiteral("64:ff9b:1::")), 48) ||
        address.isInSubnet(QHostAddress(QStringLiteral("100::")), 64) ||
        address.isInSubnet(QHostAddress(QStringLiteral("2001::")), 23) ||
        address.isInSubnet(QHostAddress(QStringLiteral("2001:db8::")), 32) ||
        address.isInSubnet(QHostAddress(QStringLiteral("2002::")), 16) ||
        address.isInSubnet(QHostAddress(QStringLiteral("3fff::")), 20) ||
        address.isInSubnet(QHostAddress(QStringLiteral("5f00::")), 16) ||
        address.isInSubnet(QHostAddress(QStringLiteral("fc00::")), 7) ||
        address.isInSubnet(QHostAddress(QStringLiteral("fe80::")), 10) ||
        address.isInSubnet(QHostAddress(QStringLiteral("fec0::")), 10) ||
        address.isInSubnet(QHostAddress(QStringLiteral("ff00::")), 8);
}

QString idFromHashes(const lt::info_hash_t &hashes)
{
    std::ostringstream stream;
    if (hashes.has_v1())
    {
        stream << hashes.v1;
        return "torrent-v1-" + QString::fromStdString(stream.str());
    }
    if (hashes.has_v2())
    {
        stream << hashes.v2;
        return "torrent-v2-" + QString::fromStdString(stream.str());
    }
    return {};
}

QString stateName(lt::torrent_status::state_t state)
{
    switch (state)
    {
    case lt::torrent_status::checking_files:
        return QObject::tr("Checking downloaded data");
    case lt::torrent_status::downloading_metadata:
        return QObject::tr("Finding torrent metadata");
    case lt::torrent_status::downloading:
        return QObject::tr("Ready to stream");
    case lt::torrent_status::finished:
        return QObject::tr("Selected files downloaded");
    case lt::torrent_status::seeding:
        return QObject::tr("Downloaded");
    case lt::torrent_status::checking_resume_data:
        return QObject::tr("Checking resume data");
    default:
        return QObject::tr("Starting torrent");
    }
}

lt::load_torrent_limits torrentLoadLimits()
{
    lt::load_torrent_limits limits;
    limits.max_buffer_size = static_cast<int>(MAX_TORRENT_METADATA_SIZE);
    return limits;
}

bool loadTorrentFile(
    const QString &path,
    lt::add_torrent_params *params,
    QString *error)
{
    if (params == nullptr || error == nullptr)
    {
        return false;
    }
    error->clear();
#if LIBTORRENT_VERSION_NUM >= 20100
    lt::error_code parseError;
    *params = lt::load_torrent_file(
        path.toStdString(), parseError, torrentLoadLimits()
    );
    if (parseError)
    {
        *error = QString::fromStdString(parseError.message());
        return false;
    }
#else
    try
    {
        *params = lt::load_torrent_file(
            path.toStdString(), torrentLoadLimits()
        );
    }
    catch (const std::exception &exception)
    {
        *error = QString::fromUtf8(exception.what());
        return false;
    }
    catch (...)
    {
        *error = QObject::tr("Unknown torrent parsing error");
        return false;
    }
#endif
    return params->ti != nullptr;
}

bool validMetadataFile(const QString &path)
{
    const QFileInfo info(path);
    return info.isFile() && info.size() > 0 &&
        info.size() <= MAX_TORRENT_METADATA_SIZE;
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

QString safeStorageRoot(const QString &path, bool create)
{
    if (path.isEmpty())
    {
        return {};
    }

    QFileInfo info(path);
    // QFileInfo::exists() is false for dangling links, so test the link type
    // first. A storage root must always be a real directory owned by Memento.
    if (isLinkOrJunction(info))
    {
        return {};
    }
    if (!info.exists())
    {
        if (!create || !QDir().mkpath(info.absoluteFilePath()))
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
    if (canonical.isEmpty() || expected.isEmpty() ||
        !pathsEqual(canonical, expected))
    {
        return {};
    }
    return canonical;
}

QString safeDirectChild(
    const QString &root,
    const QString &name,
    bool createDirectory,
    bool requireDirectory)
{
    if (name.isEmpty() || name == QStringLiteral(".") ||
        name == QStringLiteral("..") || name.contains('/') ||
        name.contains('\\'))
    {
        return {};
    }

    const QString canonicalRoot = safeStorageRoot(root, false);
    if (canonicalRoot.isEmpty())
    {
        return {};
    }
    const QString path = QDir(root).filePath(name);
    QFileInfo info(path);
    if (isLinkOrJunction(info))
    {
        return {};
    }
    if (!info.exists() && createDirectory)
    {
        if (!QDir(root).mkdir(name))
        {
            return {};
        }
        info.refresh();
    }

    const QString refreshedRoot = safeStorageRoot(root, false);
    if (refreshedRoot.isEmpty() || !pathsEqual(canonicalRoot, refreshedRoot) ||
        isLinkOrJunction(info))
    {
        return {};
    }
    if (!info.exists())
    {
        return createDirectory ? QString() : path;
    }
    if ((requireDirectory && !info.isDir()) ||
        (!requireDirectory && !info.isFile()))
    {
        return {};
    }

    const QString canonicalChild = info.canonicalFilePath();
    const QString expected = QDir(canonicalRoot).filePath(name);
    return !canonicalChild.isEmpty() && pathsEqual(canonicalChild, expected) ?
        path : QString();
}

bool pathInside(const QString &root, const QString &path)
{
    if (pathsEqual(root, path))
    {
        return true;
    }
#if defined(Q_OS_WIN)
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseInsensitive;
#else
    constexpr Qt::CaseSensitivity sensitivity = Qt::CaseSensitive;
#endif
    QString prefix = QDir::cleanPath(root);
    // QDir::cleanPath uses forward slashes on Windows as well.
    if (!prefix.endsWith('/'))
    {
        prefix += '/';
    }
    return QDir::cleanPath(path).startsWith(prefix, sensitivity);
}

bool safeDirectoryTree(const QString &root)
{
    const QFileInfo rootInfo(root);
    if (!rootInfo.isDir() || isLinkOrJunction(rootInfo))
    {
        return false;
    }
    const QString canonicalRoot = rootInfo.canonicalFilePath();
    if (canonicalRoot.isEmpty())
    {
        return false;
    }

    QList<QString> pending{root};
    while (!pending.isEmpty())
    {
        const QString directoryPath = pending.takeLast();
        const QFileInfo directoryInfo(directoryPath);
        if (!directoryInfo.isDir() || isLinkOrJunction(directoryInfo) ||
            !pathInside(canonicalRoot, directoryInfo.canonicalFilePath()))
        {
            return false;
        }
        const QFileInfoList entries = QDir(directoryPath).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::System |
                QDir::NoDotAndDotDot,
            QDir::NoSort
        );
        for (const QFileInfo &entry : entries)
        {
            if (isLinkOrJunction(entry) ||
                !pathInside(canonicalRoot, entry.canonicalFilePath()))
            {
                return false;
            }
            if (entry.isDir())
            {
                pending.append(entry.absoluteFilePath());
            }
            else if (!entry.isFile())
            {
                return false;
            }
        }
    }
    return true;
}

bool removeDirectoryTreeNoFollow(const QString &path)
{
    const QFileInfo directoryInfo(path);
    if (!directoryInfo.isDir() || isLinkOrJunction(directoryInfo))
    {
        return false;
    }
    const QFileInfoList entries = QDir(path).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDir::NoSort
    );
    for (const QFileInfo &entry : entries)
    {
        const QFileInfo refreshed(entry.absoluteFilePath());
        if (isLinkOrJunction(refreshed))
        {
            return false;
        }
        if (refreshed.isDir())
        {
            if (!removeDirectoryTreeNoFollow(refreshed.absoluteFilePath()))
            {
                return false;
            }
        }
        else if (!refreshed.isFile() || !QFile::remove(refreshed.absoluteFilePath()))
        {
            return false;
        }
    }
    const QFileInfo finalInfo(path);
    return finalInfo.isDir() && !isLinkOrJunction(finalInfo) &&
        QDir().rmdir(path);
}

bool copyAtomically(
    const QString &sourcePath,
    const QString &targetPath,
    const QString &targetRoot)
{
    const QFileInfo requestedTarget(targetPath);
    const QString safeTarget = safeDirectChild(
        targetRoot, requestedTarget.fileName(), false, false
    );
    if (safeTarget.isEmpty() ||
        !pathsEqual(requestedTarget.absoluteFilePath(),
                    QFileInfo(safeTarget).absoluteFilePath()))
    {
        return false;
    }
    if (QFileInfo(sourcePath).absoluteFilePath() ==
        QFileInfo(safeTarget).absoluteFilePath())
    {
        return validMetadataFile(sourcePath);
    }

    const qint64 expectedSize = QFileInfo(sourcePath).size();
    if (!validMetadataFile(sourcePath))
    {
        return false;
    }
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly))
    {
        return false;
    }

    QSaveFile target(safeTarget);
    if (!target.open(QIODevice::WriteOnly))
    {
        return false;
    }

    QByteArray buffer(COPY_CHUNK_SIZE, Qt::Uninitialized);
    qint64 total = 0;
    while (true)
    {
        const qint64 count = source.read(buffer.data(), buffer.size());
        if (count < 0 || total > MAX_TORRENT_METADATA_SIZE - count)
        {
            target.cancelWriting();
            return false;
        }
        if (count == 0)
        {
            break;
        }
        if (target.write(buffer.constData(), count) != count)
        {
            target.cancelWriting();
            return false;
        }
        total += count;
    }
    if (source.error() != QFileDevice::NoError || total != expectedSize || total <= 0)
    {
        target.cancelWriting();
        return false;
    }
    return target.commit();
}

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
void clearWebSeeds(lt::add_torrent_params &params)
{
    params.url_seeds.clear();
#if TORRENT_ABI_VERSION < 4
    params.http_seeds.clear();
#endif
}
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace

class TorrentEngine::Private
{
public:
    struct StreamClient
    {
        QPointer<QTcpSocket> socket;
        QByteArray request;
        QString id;
        int fileIndex{-1};
        qint64 position{0};
        qint64 end{-1};
        QFile file;
        bool parsed{false};
        bool bodyRequest{false};
        int prioritizedPiece{-1};
        int fileIoFailures{0};
        qint64 connectedAt{0};
        qint64 lastProgressAt{0};
        qint64 lastOutputAt{0};
        qint64 lastWantedDone{-1};
    };

    explicit Private(TorrentEngine *owner) : q(owner), session(makeSettings())
    {
        session.set_ip_filter(makeIpFilter());
        clock.start();
        cacheRoot = QDir(
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        ).filePath("torrents");
        metadataRoot = QDir(DirectoryUtils::getConfigDir()).filePath("torrents");
        if (safeStorageRoot(cacheRoot, true).isEmpty() ||
            safeStorageRoot(metadataRoot, true).isEmpty())
        {
            storageError = QObject::tr(
                "Memento's torrent storage folders are unsafe or unavailable."
            );
        }

        QObject::connect(
            &server, &QTcpServer::newConnection, q,
            [this]() { acceptConnections(); }
        );
        server.setMaxPendingConnections(MAX_STREAM_CLIENTS);
        if (!server.listen(QHostAddress::LocalHost, 0))
        {
            serverError = QObject::tr("Could not start the local torrent stream server.");
        }

        timer.setInterval(ALERT_INTERVAL_MS);
        QObject::connect(&timer, &QTimer::timeout, q, [this]() { tick(); });
        timer.start();
    }

    ~Private()
    {
        const QList<StreamClient *> values = clients.values();
        for (StreamClient *client : values)
        {
            if (client != nullptr && client->socket != nullptr)
            {
                QObject::disconnect(client->socket, nullptr, q, nullptr);
                client->socket->abort();
            }
            delete client;
        }
        clients.clear();
    }

    TorrentEngine::AddResult addTorrentFile(const QString &path)
    {
        TorrentEngine::AddResult result;
        if (!serverError.isEmpty())
        {
            result.error = serverError;
            return result;
        }
        if (!storageReady())
        {
            result.error = storageError;
            return result;
        }

        if (!validMetadataFile(path))
        {
            result.error = QObject::tr(
                "The selected torrent metadata is empty, inaccessible, or larger than 64 MiB."
            );
            return result;
        }

        lt::error_code error;
        lt::add_torrent_params params;
        QString loadError;
        if (!loadTorrentFile(path, &params, &loadError))
        {
            result.error = QObject::tr("The selected torrent file is invalid: %1")
                .arg(loadError);
            return result;
        }

        result.id = idFromHashes(params.ti->info_hashes());
        result.title = QString::fromUtf8(params.ti->name().c_str());
        if (result.id.isEmpty())
        {
            result.error = QObject::tr("The torrent does not contain a usable info hash.");
            return result;
        }
        if (pendingFileDeletions.contains(result.id))
        {
            result.error = QObject::tr("This torrent is still being removed.");
            result.id.clear();
            return result;
        }
        if (!fillFiles(*params.ti, result))
        {
            result.error = QObject::tr(
                "The torrent contains no usable files or exceeds Memento's file-list limits."
            );
            result.id.clear();
            return result;
        }

        const QString stored = QDir(metadataRoot).filePath(result.id + ".torrent");
        if (!copyAtomically(path, stored, metadataRoot))
        {
            result.error = QObject::tr("Memento could not retain the torrent metadata.");
            result.id.clear();
            return result;
        }
        result.source = stored;

        if (QFileInfo(path).absoluteFilePath() != QFileInfo(stored).absoluteFilePath())
        {
            if (!loadTorrentFile(stored, &params, &loadError) ||
                !params.ti || idFromHashes(params.ti->info_hashes()) != result.id)
            {
                removeStoredMetadata(result.id, stored);
                result.error = QObject::tr(
                    "The torrent metadata changed while Memento was importing it."
                );
                result.id.clear();
                return result;
            }
        }
        if (!handles.contains(result.id) || !handles.value(result.id).is_valid())
        {
            const QString savePath = torrentSavePath(result.id);
            if (savePath.isEmpty())
            {
                removeStoredMetadata(result.id, stored);
                result.error = QObject::tr(
                    "Memento could not create a safe torrent cache directory."
                );
                result.id.clear();
                return result;
            }
            params.save_path = savePath.toStdString();
            params.storage_mode = lt::storage_mode_sparse;
            params.file_priorities.assign(
                static_cast<std::size_t>(params.ti->num_files()),
                lt::dont_download
            );
            params.flags |= lt::torrent_flags::paused |
                lt::torrent_flags::default_dont_download;
            params.flags &= ~lt::torrent_flags::auto_managed;
            sanitizeNetworkSources(params);
            const lt::torrent_handle handle = session.add_torrent(params, error);
            if (error || !handle.is_valid())
            {
                removeStoredMetadata(result.id, stored);
                removeCachedData(result.id);
                result.error = error ?
                    QObject::tr("Could not start the torrent: %1")
                        .arg(QString::fromStdString(error.message())) :
                    QObject::tr("Could not start the torrent session.");
                result.id.clear();
                return result;
            }
            handles.insert(result.id, handle);
        }
        sources.insert(result.id, result.source);
        return result;
    }

    TorrentEngine::AddResult addMagnet(const QString &magnet)
    {
        TorrentEngine::AddResult result;
        if (!serverError.isEmpty())
        {
            result.error = serverError;
            return result;
        }
        if (!storageReady())
        {
            result.error = storageError;
            return result;
        }

        lt::error_code error;
        lt::add_torrent_params params = lt::parse_magnet_uri(
            magnet.toStdString(), error
        );
        if (error)
        {
            result.error = QObject::tr("The magnet link is invalid: %1")
                .arg(QString::fromStdString(error.message()));
            return result;
        }
        result.id = idFromHashes(params.info_hashes);
        result.title = QString::fromUtf8(params.name.c_str());
        if (result.title.isEmpty())
        {
            result.title = QObject::tr("Loading torrent metadata…");
        }
        result.source = magnet;
        if (result.id.isEmpty())
        {
            result.error = QObject::tr("The magnet link has no supported info hash.");
            return result;
        }
        if (pendingFileDeletions.contains(result.id))
        {
            result.error = QObject::tr("This torrent is still being removed.");
            result.id.clear();
            return result;
        }

        const QString savePath = torrentSavePath(result.id);
        if (savePath.isEmpty())
        {
            result.error = QObject::tr(
                "Memento could not create a safe torrent cache directory."
            );
            result.id.clear();
            return result;
        }
        params.save_path = savePath.toStdString();
        params.storage_mode = lt::storage_mode_sparse;
        // A magnet must contact peers once to obtain its file list, but no
        // payload files are eligible for download until an episode is chosen.
        params.flags |= lt::torrent_flags::default_dont_download;
        sanitizeNetworkSources(params);
        if (!handles.contains(result.id) || !handles.value(result.id).is_valid())
        {
            const lt::torrent_handle handle = session.add_torrent(params, error);
            if (error || !handle.is_valid())
            {
                removeCachedData(result.id);
                result.error = error ?
                    QObject::tr("Could not start the magnet: %1")
                        .arg(QString::fromStdString(error.message())) :
                    QObject::tr("Could not start the magnet session.");
                result.id.clear();
                return result;
            }
            handles.insert(result.id, handle);
        }
        sources.insert(result.id, magnet);
        return result;
    }

    bool fillFiles(
        const lt::torrent_info &info,
        TorrentEngine::AddResult &result) const
    {
        result.paths.clear();
        result.sizes.clear();
        result.fileIndices.clear();
        const lt::file_storage &files = torrentFiles(info);
        if (files.num_files() <= 0 || files.num_files() > MAX_TORRENT_FILES)
        {
            return false;
        }
        qint64 pathBytes = 0;
        for (int index = 0; index < files.num_files(); ++index)
        {
            const lt::file_index_t fileIndex{index};
            const std::string path = files.file_path(fileIndex);
            if (path.size() > static_cast<std::size_t>(
                    MAX_TORRENT_PATH_BYTES - pathBytes))
            {
                result.paths.clear();
                result.sizes.clear();
                result.fileIndices.clear();
                return false;
            }
            pathBytes += static_cast<qint64>(path.size());
            if (files.pad_file_at(fileIndex) || files.file_size(fileIndex) <= 0)
            {
                continue;
            }
            result.paths.emplaceBack(QString::fromUtf8(
                path.data(), static_cast<qsizetype>(path.size())
            ));
            result.sizes.emplaceBack(files.file_size(fileIndex));
            result.fileIndices.emplaceBack(index);
        }
        return !result.paths.isEmpty();
    }

    void sanitizeNetworkSources(lt::add_torrent_params &params) const
    {
        std::vector<std::string> trackers;
        std::vector<int> tiers;
        QSet<QString> seenTrackers;
        const auto addTracker = [&](const std::string &tracker, int tier) {
            const QString url = QString::fromStdString(tracker);
            if (!TorrentEngine::safeTrackerUrl(url) || seenTrackers.contains(url))
            {
                return;
            }
            seenTrackers.insert(url);
            trackers.emplace_back(tracker);
            tiers.emplace_back(tier);
        };
        for (std::size_t index = 0; index < params.trackers.size(); ++index)
        {
            addTracker(
                params.trackers[index],
                index < params.tracker_tiers.size() ? params.tracker_tiers[index] : 0
            );
        }
        if (params.ti)
        {
#if LIBTORRENT_VERSION_NUM >= 20100
            for (const lt::announce_entry &tracker :
                 params.ti->internal_trackers())
#else
            for (const lt::announce_entry &tracker : params.ti->trackers())
#endif
            {
                addTracker(tracker.url, tracker.tier);
            }
        }
        params.trackers = std::move(trackers);
        params.tracker_tiers = std::move(tiers);
        clearWebSeeds(params);
        params.dht_nodes.clear();
        params.flags |= lt::torrent_flags::apply_ip_filter |
#if LIBTORRENT_VERSION_NUM >= 20100
            lt::torrent_flags::deprecated_override_trackers |
            lt::torrent_flags::deprecated_override_web_seeds;
#else
            lt::torrent_flags::override_trackers |
            lt::torrent_flags::override_web_seeds;
#endif
    }

    void emitMetadata(const QString &id, const lt::torrent_handle &handle)
    {
        const std::shared_ptr<const lt::torrent_info> info = handle.torrent_file();
        if (!info)
        {
            return;
        }
        TorrentEngine::AddResult result;
        result.id = id;
        result.title = QString::fromUtf8(info->name().c_str());
        result.source = sources.value(id);
        const bool validFiles = fillFiles(*info, result);

        if (validFiles)
        {
            std::vector<lt::download_priority_t> priorities(
                static_cast<std::size_t>(info->num_files()), lt::dont_download
            );
            handle.prioritize_files(priorities);
        }
        handle.clear_piece_deadlines();
        handle.unset_flags(lt::torrent_flags::auto_managed);
        handle.pause();
        activeFiles.remove(id);
        selectedFiles.remove(id);
        issuedStreamUrls.remove(id);
        emit q->metadataReady(
            id, result.title, result.paths, result.sizes,
            result.fileIndices, result.source
        );
    }

    QString torrentSavePath(const QString &id) const
    {
        if (!isValidTorrentId(id) || safeStorageRoot(cacheRoot, false).isEmpty())
        {
            return {};
        }
        return safeDirectChild(cacheRoot, id, true, true);
    }

    [[nodiscard]] QString torrentCachePath(const QString &id) const
    {
        return isValidTorrentId(id) ? QDir(cacheRoot).filePath(id) : QString();
    }

    QString urlFor(
        const QString &id,
        int fileIndex,
        const QString &displayName) const
    {
        const lt::torrent_handle handle = handles.value(id);
        const std::shared_ptr<const lt::torrent_info> info =
            handle.is_valid() ? handle.torrent_file() : nullptr;
        if (!server.isListening() || !isValidTorrentId(id) || !info ||
            !isPlayableFile(*info, fileIndex))
        {
            return {};
        }
        const QString safeName = QString::fromLatin1(
            QUrl::toPercentEncoding(displayName)
        );
        return QString("http://127.0.0.1:%1/stream/%2/%3/%4")
            .arg(server.serverPort())
            .arg(id)
            .arg(fileIndex)
            .arg(safeName);
    }

    QString streamUrl(
        const QString &id,
        int fileIndex,
        const QString &displayName)
    {
        const lt::torrent_handle handle = handles.value(id);
        const std::shared_ptr<const lt::torrent_info> info =
            handle.is_valid() ? handle.torrent_file() : nullptr;
        if (!isValidTorrentId(id) || !info ||
            !isPlayableFile(*info, fileIndex))
        {
            return {};
        }

        const int previousFile = selectedFiles.value(id, -1);
        if (previousFile != fileIndex)
        {
            disconnectOtherFiles(id, fileIndex);
            deactivateTorrent(id);
            selectedFiles.insert(id, fileIndex);
            issuedStreamUrls.remove(id);
        }
        emit q->torrentChanged(id);
        const QString url = urlFor(id, fileIndex, displayName);
        if (!url.isEmpty())
        {
            issuedStreamUrls.insert(id, url);
        }
        return url;
    }

    bool identifyStreamUrl(
        const QString &value,
        QString *id,
        int *fileIndex) const
    {
        if (id == nullptr || fileIndex == nullptr)
        {
            return false;
        }

        const QUrl candidate(value);
        if (!candidate.isValid() || candidate.scheme() != QStringLiteral("http") ||
            candidate.host() != QStringLiteral("127.0.0.1") ||
            candidate.port(-1) != static_cast<int>(server.serverPort()) ||
            !candidate.userInfo().isEmpty() || candidate.hasQuery() ||
            candidate.hasFragment())
        {
            return false;
        }

        const QStringList parts = candidate.path(QUrl::FullyEncoded)
            .split('/', Qt::SkipEmptyParts);
        bool indexOk = false;
        if (parts.size() < 3 || parts.at(0) != QStringLiteral("stream"))
        {
            return false;
        }
        const QString candidateId = parts.at(1);
        const int candidateIndex = parts.at(2).toInt(&indexOk);
        const QString issued = issuedStreamUrls.value(candidateId);
        const lt::torrent_handle handle = handles.value(candidateId);
        const std::shared_ptr<const lt::torrent_info> info =
            handle.is_valid() ? handle.torrent_file() : nullptr;
        if (!indexOk || issued.isEmpty() || QUrl(issued) != candidate || !info ||
            selectedFiles.value(candidateId, -1) != candidateIndex ||
            !isPlayableFile(*info, candidateIndex))
        {
            return false;
        }

        *id = candidateId;
        *fileIndex = candidateIndex;
        return true;
    }

    [[nodiscard]] bool isPlayableFile(
        const lt::torrent_info &info,
        int fileIndex) const
    {
        if (fileIndex < 0 || fileIndex >= info.num_files() ||
            info.piece_length() <= 0)
        {
            return false;
        }
        const lt::file_index_t index{fileIndex};
        return !torrentFiles(info).pad_file_at(index) &&
            torrentFiles(info).file_size(index) > 0;
    }

    [[nodiscard]] bool activateSelectedFile(
        const QString &id,
        const lt::torrent_handle &handle,
        const lt::torrent_info &info,
        int fileIndex,
        qint64 fileOffset)
    {
        if (selectedFiles.value(id, -1) != fileIndex ||
            !isPlayableFile(info, fileIndex))
        {
            return false;
        }

        std::vector<lt::download_priority_t> priorities(
            static_cast<std::size_t>(info.num_files()), lt::dont_download
        );
        priorities[static_cast<std::size_t>(fileIndex)] = lt::top_priority;
        handle.clear_piece_deadlines();
        handle.prioritize_files(priorities);
        handle.unset_flags(lt::torrent_flags::auto_managed);
        handle.resume();
        activeFiles.insert(id, fileIndex);
        prioritizeRange(handle, info, fileIndex, fileOffset);
        emit q->torrentChanged(id);
        return true;
    }

    void disconnectOtherFiles(const QString &id, int selectedFile)
    {
        const QList<StreamClient *> values = clients.values();
        for (StreamClient *client : values)
        {
            if (client != nullptr && client->socket != nullptr &&
                client->id == id && client->fileIndex != selectedFile)
            {
                client->socket->abort();
            }
        }
    }

    void prioritizeRange(
        const lt::torrent_handle &handle,
        const lt::torrent_info &info,
        int fileIndex,
        qint64 fileOffset) const
    {
        if (!isPlayableFile(info, fileIndex))
        {
            return;
        }
        const lt::file_storage &files = torrentFiles(info);
        const lt::file_index_t index{fileIndex};
        const qint64 fileSize = files.file_size(index);
        fileOffset = std::clamp<qint64>(fileOffset, 0, fileSize - 1);
        const qint64 absolute = files.file_offset(index) + fileOffset;
        const qint64 fileEnd = files.file_offset(index) + files.file_size(index);
        const int pieceLength = info.piece_length();
        const int first = static_cast<int>(absolute / pieceLength);
        const int final = static_cast<int>((std::max<qint64>(absolute, fileEnd - 1)) /
                                           pieceLength);
        for (int offset = 0;
             offset < STREAM_LOOKAHEAD_PIECES && first + offset <= final;
             ++offset)
        {
            handle.set_piece_deadline(
                lt::piece_index_t{first + offset}, offset * 350
            );
        }
    }

    void acceptConnections()
    {
        while (server.hasPendingConnections())
        {
            QTcpSocket *socket = server.nextPendingConnection();
            if (socket == nullptr)
            {
                continue;
            }
            if (clients.size() >= MAX_STREAM_CLIENTS)
            {
                QObject::connect(
                    socket, &QTcpSocket::disconnected,
                    socket, &QObject::deleteLater
                );
                sendError(socket, 503, "Service Unavailable");
                continue;
            }
            auto *client = new StreamClient;
            client->socket = socket;
            client->connectedAt = clock.elapsed();
            client->lastProgressAt = client->connectedAt;
            client->lastOutputAt = client->connectedAt;
            clients.insert(socket, client);
            QObject::connect(socket, &QTcpSocket::readyRead, q, [this, socket]() {
                readRequest(socket);
            });
            QObject::connect(
                socket, &QTcpSocket::bytesWritten, q,
                [this, socket](qint64 count) {
                    StreamClient *client = clients.value(socket);
                    if (client != nullptr && count > 0)
                    {
                        client->lastProgressAt = clock.elapsed();
                        client->lastOutputAt = client->lastProgressAt;
                    }
                }
            );
            QObject::connect(socket, &QTcpSocket::disconnected, q, [this, socket]() {
                StreamClient *client = clients.take(socket);
                const QString id = client != nullptr ? client->id : QString();
                const int fileIndex = client != nullptr ? client->fileIndex : -1;
                const bool bodyRequest = client != nullptr && client->bodyRequest;
                delete client;
                socket->deleteLater();
                if (!id.isEmpty() && bodyRequest &&
                    activeFiles.value(id, -1) == fileIndex)
                {
                    bool stillStreaming = false;
                    for (const StreamClient *other : std::as_const(clients))
                    {
                        if (other != nullptr && other->bodyRequest &&
                            other->id == id && other->fileIndex == fileIndex)
                        {
                            stillStreaming = true;
                            break;
                        }
                    }
                    if (!stillStreaming)
                    {
                        deactivateTorrent(id);
                    }
                }
            });
        }
    }

    void deactivateTorrent(const QString &id)
    {
        const lt::torrent_handle handle = handles.value(id);
        const std::shared_ptr<const lt::torrent_info> info =
            handle.is_valid() ? handle.torrent_file() : nullptr;
        if (!info)
        {
            return;
        }
        handle.clear_piece_deadlines();
        handle.prioritize_files(std::vector<lt::download_priority_t>(
            static_cast<std::size_t>(info->num_files()), lt::dont_download
        ));
        handle.unset_flags(lt::torrent_flags::auto_managed);
        handle.pause();
        activeFiles.remove(id);
        emit q->torrentChanged(id);
    }

    void disconnectClients(const QString &id)
    {
        const QList<StreamClient *> values = clients.values();
        for (StreamClient *client : values)
        {
            if (client != nullptr && client->socket != nullptr && client->id == id)
            {
                client->socket->abort();
            }
        }
    }

    void removeStoredMetadata(const QString &id, const QString &source)
    {
        if (safeStorageRoot(metadataRoot, false).isEmpty())
        {
            emit q->torrentError(
                id,
                QObject::tr(
                    "Memento refused to access an unsafe torrent metadata folder."
                )
            );
            return;
        }

        QSet<QString> names;
        const QFileInfo sourceInfo(source);
        if (!source.isEmpty() && pathsEqual(
                sourceInfo.absolutePath(), QFileInfo(metadataRoot).absoluteFilePath()
            ))
        {
            names.insert(sourceInfo.fileName());
        }
        if (isValidTorrentId(id))
        {
            names.insert(id + ".torrent");
        }
        for (const QString &name : std::as_const(names))
        {
            const QString expected = QDir(metadataRoot).filePath(name);
            const QFileInfo info(expected);
            if (!info.exists() && !isLinkOrJunction(info))
            {
                continue;
            }
            const QString path = safeDirectChild(
                metadataRoot, name, false, false
            );
            if (path.isEmpty())
            {
                emit q->torrentError(
                    id,
                    QObject::tr("Memento refused to remove unsafe torrent metadata.")
                );
                continue;
            }
            if (!QFile::remove(path))
            {
                emit q->torrentError(
                    id,
                    QObject::tr("Memento could not remove retained torrent metadata.")
                );
            }
        }
    }

    void sendError(QTcpSocket *socket, int code, const QByteArray &message)
    {
        if (StreamClient *client = clients.value(socket); client != nullptr)
        {
            client->parsed = true;
        }
        socket->write(
            "HTTP/1.1 " + QByteArray::number(code) + " " + message + "\r\n"
            "Content-Length: 0\r\nConnection: close\r\n\r\n"
        );
        socket->disconnectFromHost();
    }

    void readRequest(QTcpSocket *socket)
    {
        StreamClient *client = clients.value(socket);
        if (client == nullptr || client->parsed)
        {
            return;
        }
        client->request += socket->readAll();
        if (client->request.size() > 64 * 1024)
        {
            sendError(socket, 431, "Request Header Fields Too Large");
            return;
        }
        if (!client->request.contains("\r\n\r\n"))
        {
            return;
        }

        const QList<QByteArray> lines = client->request.split('\n');
        if (lines.isEmpty())
        {
            sendError(socket, 400, "Bad Request");
            return;
        }
        const QList<QByteArray> requestParts = lines.constFirst().trimmed().split(' ');
        if (requestParts.size() < 3 ||
            (requestParts.at(0) != "GET" && requestParts.at(0) != "HEAD"))
        {
            sendError(socket, 405, "Method Not Allowed");
            return;
        }

        const QUrl requestUrl = QUrl::fromEncoded(requestParts.at(1));
        const QStringList parts = requestUrl.path().split('/', Qt::SkipEmptyParts);
        bool indexOk = false;
        if (parts.size() < 3 || parts.at(0) != "stream")
        {
            sendError(socket, 404, "Not Found");
            return;
        }
        client->id = parts.at(1);
        client->fileIndex = parts.at(2).toInt(&indexOk);
        const lt::torrent_handle handle = handles.value(client->id);
        const std::shared_ptr<const lt::torrent_info> info =
            handle.is_valid() ? handle.torrent_file() : nullptr;
        if (!indexOk || !info || client->fileIndex < 0 ||
            selectedFiles.value(client->id, -1) != client->fileIndex ||
            !isPlayableFile(*info, client->fileIndex) ||
            QUrl(issuedStreamUrls.value(client->id)).path(QUrl::FullyEncoded) !=
                requestUrl.path(QUrl::FullyEncoded) ||
            requestUrl.hasQuery() || requestUrl.hasFragment())
        {
            sendError(socket, 404, "Not Found");
            return;
        }

        const lt::file_index_t fileIndex{client->fileIndex};
        const qint64 fileSize = torrentFiles(*info).file_size(fileIndex);
        qint64 start = 0;
        qint64 end = fileSize - 1;
        bool partial = false;
        static const QRegularExpression rangeExpression(
            "^bytes=(?:(\\d+)-(\\d*)|-(\\d+))$",
            QRegularExpression::CaseInsensitiveOption
        );
        for (const QByteArray &rawLine : lines)
        {
            const QByteArray line = rawLine.trimmed();
            if (!line.toLower().startsWith("range:"))
            {
                continue;
            }
            const QString range = QString::fromLatin1(line.mid(6).trimmed());
            const QRegularExpressionMatch match = rangeExpression.match(range);
            bool startOk = false;
            bool endOk = true;
            if (!match.hasMatch())
            {
                sendError(socket, 416, "Range Not Satisfiable");
                return;
            }
            if (!match.captured(3).isEmpty())
            {
                const qint64 suffixLength = match.captured(3).toLongLong(&startOk);
                if (!startOk || suffixLength <= 0)
                {
                    sendError(socket, 416, "Range Not Satisfiable");
                    return;
                }
                else
                {
                    start = std::max<qint64>(0, fileSize - suffixLength);
                }
            }
            else
            {
                start = match.captured(1).toLongLong(&startOk);
                if (!match.captured(2).isEmpty())
                {
                    end = match.captured(2).toLongLong(&endOk);
                }
            }
            if (!startOk || !endOk || start < 0 || start >= fileSize || end < start)
            {
                sendError(socket, 416, "Range Not Satisfiable");
                return;
            }
            end = std::min(end, fileSize - 1);
            partial = true;
            break;
        }

        const QString relativePath = QString::fromUtf8(
            torrentFiles(*info).file_path(fileIndex).c_str()
        );
#if LIBTORRENT_VERSION_NUM >= 20100
        const lt::renamed_files renamedFiles = handle.get_renamed_files();
        const lt::filenames currentFiles(torrentFiles(*info), renamedFiles);
        const std::string currentPath = currentFiles.file_path(
            fileIndex,
            torrentSavePath(client->id).toStdString()
        );
#else
        const std::string currentPath = torrentFiles(*info).file_path(
            fileIndex,
            torrentSavePath(client->id).toStdString()
        );
#endif
        client->file.setFileName(QString::fromUtf8(currentPath.c_str()));
        client->position = start;
        client->end = end;
        client->parsed = true;

        const bool bodyRequest = requestParts.at(0) == "GET";
        if (bodyRequest && !activateSelectedFile(
                client->id, handle, *info, client->fileIndex, start))
        {
            sendError(socket, 409, "Conflict");
            return;
        }
        client->bodyRequest = bodyRequest;
        client->lastProgressAt = clock.elapsed();
        client->lastOutputAt = client->lastProgressAt;

        QByteArray headers = partial ? "HTTP/1.1 206 Partial Content\r\n" :
                                       "HTTP/1.1 200 OK\r\n";
        headers += "Accept-Ranges: bytes\r\n";
        headers += "Content-Type: " + QMimeDatabase().mimeTypeForFile(relativePath)
            .name().toLatin1() + "\r\n";
        headers += "Content-Length: " + QByteArray::number(end - start + 1) + "\r\n";
        if (partial)
        {
            headers += "Content-Range: bytes " + QByteArray::number(start) + "-" +
                QByteArray::number(end) + "/" + QByteArray::number(fileSize) + "\r\n";
        }
        headers += "Connection: close\r\n\r\n";
        socket->write(headers);
        if (!bodyRequest)
        {
            socket->disconnectFromHost();
            return;
        }
        client->prioritizedPiece = static_cast<int>(
            (torrentFiles(*info).file_offset(fileIndex) + start) /
            info->piece_length()
        );
    }

    void pumpClient(StreamClient *client)
    {
        QTcpSocket *socket = client->socket;
        if (socket == nullptr || !client->parsed ||
            socket->state() != QAbstractSocket::ConnectedState ||
            socket->bytesToWrite() > MAX_PENDING_BYTES)
        {
            return;
        }
        if (client->position > client->end)
        {
            socket->disconnectFromHost();
            return;
        }

        const lt::torrent_handle handle = handles.value(client->id);
        const std::shared_ptr<const lt::torrent_info> info =
            handle.is_valid() ? handle.torrent_file() : nullptr;
        if (!info || !isPlayableFile(*info, client->fileIndex) ||
            selectedFiles.value(client->id, -1) != client->fileIndex)
        {
            socket->disconnectFromHost();
            return;
        }
        const lt::file_index_t fileIndex{client->fileIndex};
        const qint64 absolute =
            torrentFiles(*info).file_offset(fileIndex) + client->position;
        const int pieceLength = info->piece_length();
        const int pieceNumber = static_cast<int>(absolute / pieceLength);
        if (client->prioritizedPiece != pieceNumber)
        {
            prioritizeRange(handle, *info, client->fileIndex, client->position);
            client->prioritizedPiece = pieceNumber;
        }
        if (!handle.have_piece(lt::piece_index_t{pieceNumber}))
        {
            return;
        }

        if (!client->file.isOpen() && !client->file.open(QIODevice::ReadOnly))
        {
            if (++client->fileIoFailures >= MAX_FILE_IO_FAILURES)
            {
                socket->disconnectFromHost();
            }
            return;
        }
        if (!client->file.seek(client->position))
        {
            socket->disconnectFromHost();
            return;
        }

        const qint64 bytesUntilPieceEnd =
            (static_cast<qint64>(pieceNumber) + 1) * pieceLength - absolute;
        const qint64 length = std::min({
            STREAM_CHUNK_SIZE,
            client->end - client->position + 1,
            bytesUntilPieceEnd,
        });
        const QByteArray data = client->file.read(length);
        if (data.isEmpty())
        {
            if (++client->fileIoFailures >= MAX_FILE_IO_FAILURES)
            {
                socket->disconnectFromHost();
            }
            return;
        }
        const qint64 accepted = socket->write(data);
        if (accepted <= 0)
        {
            socket->disconnectFromHost();
            return;
        }
        client->fileIoFailures = 0;
        client->position += accepted;
        client->lastProgressAt = clock.elapsed();
        client->lastOutputAt = client->lastProgressAt;
    }

    void tick()
    {
        std::vector<lt::alert *> alerts;
        session.pop_alerts(&alerts);
        for (const lt::alert *alert : alerts)
        {
            if (const auto *removed = lt::alert_cast<lt::torrent_removed_alert>(alert))
            {
                const QString id = idFromHashes(removed->info_hashes);
                if (pendingFileDeletions.remove(id))
                {
                    // Libtorrent is no longer using the save path. Perform
                    // deletion ourselves so the path can be revalidated and
                    // directory links are never followed.
                    removeCachedData(id);
                }
            }
            else if (const auto *deleted =
                     lt::alert_cast<lt::torrent_deleted_alert>(alert))
            {
                const QString id = idFromHashes(deleted->info_hashes);
                if (pendingFileDeletions.remove(id))
                {
                    removeCachedData(id);
                }
            }
            else if (const auto *failed =
                     lt::alert_cast<lt::torrent_delete_failed_alert>(alert))
            {
                const QString id = idFromHashes(failed->info_hashes);
                if (pendingFileDeletions.remove(id))
                {
                    emit q->torrentError(
                        id,
                        QObject::tr("Could not delete cached torrent data: %1")
                            .arg(QString::fromStdString(failed->error.message()))
                    );
                }
            }
            else if (const auto *metadata =
                     lt::alert_cast<lt::metadata_received_alert>(alert))
            {
                const QString id = idFromHashes(metadata->handle.info_hashes());
                if (!id.isEmpty() && sources.contains(id) &&
                    !pendingFileDeletions.contains(id))
                {
                    handles.insert(id, metadata->handle);
                    emitMetadata(id, metadata->handle);
                }
            }
            else if (const auto *error = lt::alert_cast<lt::torrent_error_alert>(alert))
            {
                const QString id = idFromHashes(error->handle.info_hashes());
                if (sources.contains(id))
                {
                    emit q->torrentError(
                        id, QString::fromStdString(error->error.message())
                    );
                }
            }
        }

        const QList<StreamClient *> values = clients.values();
        for (StreamClient *client : values)
        {
            if (client == nullptr || client->socket == nullptr)
            {
                continue;
            }
            const qint64 now = clock.elapsed();
            if (!client->parsed && now - client->connectedAt > HEADER_TIMEOUT_MS)
            {
                sendError(client->socket, 408, "Request Timeout");
                continue;
            }
            if (client->parsed && client->bodyRequest)
            {
                const lt::torrent_handle handle = handles.value(client->id);
                if (handle.is_valid())
                {
                    const lt::torrent_status torrentStatus = handle.status();
                    if (torrentStatus.download_payload_rate > 0 ||
                        torrentStatus.total_wanted_done > client->lastWantedDone)
                    {
                        client->lastProgressAt = now;
                    }
                    client->lastWantedDone = std::max(
                        client->lastWantedDone,
                        static_cast<qint64>(torrentStatus.total_wanted_done)
                    );
                }
            }
            if (client->parsed &&
                (now - client->lastProgressAt > STREAM_IDLE_TIMEOUT_MS ||
                 (client->bodyRequest &&
                  now - client->lastOutputAt > STREAM_HARD_STALL_TIMEOUT_MS)))
            {
                client->socket->disconnectFromHost();
                continue;
            }
            pumpClient(client);
        }

        if (++statusTick >= STATUS_INTERVAL_TICKS)
        {
            statusTick = 0;
            for (auto it = handles.cbegin(); it != handles.cend(); ++it)
            {
                emit q->torrentChanged(it.key());
            }
        }
    }

    void removeCachedData(const QString &id)
    {
        if (!isValidTorrentId(id))
        {
            return;
        }
        if (safeStorageRoot(cacheRoot, false).isEmpty())
        {
            emit q->torrentError(
                id,
                QObject::tr("Memento refused to access an unsafe torrent cache folder.")
            );
            return;
        }

        const QString expected = torrentCachePath(id);
        const QFileInfo info(expected);
        if (!info.exists() && !isLinkOrJunction(info))
        {
            return;
        }
        const QString cachePath = safeDirectChild(cacheRoot, id, false, true);
        if (cachePath.isEmpty())
        {
            emit q->torrentError(
                id,
                QObject::tr("Memento refused to remove an unsafe torrent cache entry.")
            );
            return;
        }
        if (!safeDirectoryTree(cachePath) ||
            !removeDirectoryTreeNoFollow(cachePath))
        {
            emit q->torrentError(
                id, QObject::tr("Memento could not remove cached torrent data.")
            );
        }
    }

    QVariantMap status(const QString &id) const
    {
        const lt::torrent_handle handle = handles.value(id);
        if (!handle.is_valid())
        {
            return {
                {"ready", false},
                {"loadingMetadata", false},
                {"state", QObject::tr("Torrent unavailable")},
                {"downloadRate", 0},
                {"peers", 0},
                {"progress", 0.0},
            };
        }
        const lt::torrent_status torrentStatus = handle.status();
        double progress = std::isfinite(torrentStatus.progress) ?
            torrentStatus.progress : 0.0;
        const int selectedFile = selectedFiles.value(id, -1);
        const std::shared_ptr<const lt::torrent_info> info = handle.torrent_file();
        if (info && isPlayableFile(*info, selectedFile))
        {
            const std::vector<std::int64_t> fileProgress = handle.file_progress(
                lt::torrent_handle::piece_granularity
            );
            const qint64 size = torrentFiles(*info).file_size(
                lt::file_index_t{selectedFile}
            );
            if (selectedFile < static_cast<int>(fileProgress.size()))
            {
                progress = static_cast<double>(fileProgress[static_cast<std::size_t>(selectedFile)]) /
                    static_cast<double>(size);
            }
        }
        return {
            {"ready", info != nullptr},
            {"loadingMetadata", !info && !torrentStatus.errc && !(torrentStatus.flags & lt::torrent_flags::paused)},
            {"state", info && selectedFile < 0 ?
                QObject::tr("Ready — choose an episode") :
                stateName(torrentStatus.state)},
            {"downloadRate", torrentStatus.download_payload_rate},
            {"peers", torrentStatus.num_peers},
            {"progress", std::clamp(progress, 0.0, 1.0)},
        };
    }

    TorrentEngine *q;
    lt::session session;
    QTcpServer server;
    QTimer timer;
    QHash<QString, lt::torrent_handle> handles;
    QHash<QString, QString> sources;
    QHash<QString, int> selectedFiles;
    QHash<QString, int> activeFiles;
    QHash<QString, QString> issuedStreamUrls;
    QHash<QTcpSocket *, StreamClient *> clients;
    QSet<QString> pendingFileDeletions;
    QString cacheRoot;
    QString metadataRoot;
    QString serverError;
    QString storageError;
    QElapsedTimer clock;
    int statusTick{0};

    bool storageReady()
    {
        if (!storageError.isEmpty())
        {
            return false;
        }
        if (safeStorageRoot(cacheRoot, false).isEmpty() ||
            safeStorageRoot(metadataRoot, false).isEmpty())
        {
            storageError = QObject::tr(
                "Memento's torrent storage folders became unsafe or unavailable."
            );
            return false;
        }
        return true;
    }
};

TorrentEngine::TorrentEngine(QObject *parent) :
    QObject(parent), d(std::make_unique<Private>(this))
{
}

TorrentEngine::~TorrentEngine() = default;

bool TorrentEngine::safeTrackerUrl(const QString &value)
{
    const QUrl url(value);
    const QString scheme = url.scheme().toCaseFolded();
    const QString host = url.host().toCaseFolded();
    if (!url.isValid() || host.isEmpty() ||
        (scheme != QStringLiteral("http") &&
         scheme != QStringLiteral("https") &&
         scheme != QStringLiteral("udp") &&
         scheme != QStringLiteral("ws") &&
         scheme != QStringLiteral("wss")) ||
        host == QStringLiteral("localhost") ||
        host == QStringLiteral("localhost.") ||
        host.endsWith(QStringLiteral(".localhost")) ||
        host.endsWith(QStringLiteral(".localhost.")))
    {
        return false;
    }

    QHostAddress address;
    return !address.setAddress(host) || !isLocalAddress(address);
}

TorrentEngine::AddResult TorrentEngine::addTorrentFile(const QString &path)
{
    return d->addTorrentFile(path);
}

TorrentEngine::AddResult TorrentEngine::addMagnet(const QString &magnet)
{
    return d->addMagnet(magnet);
}

TorrentEngine::AddResult TorrentEngine::restore(
    const QString &id,
    const QString &source)
{
    AddResult result = source.startsWith("magnet:", Qt::CaseInsensitive) ?
        d->addMagnet(source) : d->addTorrentFile(source);
    if (!result.valid())
    {
        emit torrentError(id, result.error);
        return result;
    }
    if (!result.paths.isEmpty())
    {
        QTimer::singleShot(0, this, [this, result]() {
            emit metadataReady(
                result.id, result.title, result.paths, result.sizes,
                result.fileIndices, result.source
            );
        });
    }
    return result;
}

void TorrentEngine::removeTorrent(const QString &id, bool deleteFiles)
{
    const QString source = d->sources.take(id);
    d->disconnectClients(id);
    const lt::torrent_handle handle = d->handles.take(id);
    if (handle.is_valid())
    {
        if (deleteFiles)
        {
            d->pendingFileDeletions.insert(id);
        }
        // Never delegate recursive deletion to libtorrent: a cache directory
        // could be replaced by a link between addition and removal. The
        // torrent_removed_alert path performs a revalidated, no-follow delete.
        d->session.remove_torrent(handle, lt::remove_flags_t{});
    }
    d->selectedFiles.remove(id);
    d->activeFiles.remove(id);
    d->issuedStreamUrls.remove(id);
    d->removeStoredMetadata(id, source);
    if (deleteFiles && !handle.is_valid())
    {
        d->removeCachedData(id);
    }
}

QString TorrentEngine::streamUrl(
    const QString &id,
    int fileIndex,
    const QString &displayName)
{
    return d->streamUrl(id, fileIndex, displayName);
}

QString TorrentEngine::urlFor(
    const QString &id,
    int fileIndex,
    const QString &displayName) const
{
    return d->urlFor(id, fileIndex, displayName);
}

bool TorrentEngine::identifyStreamUrl(
    const QString &url,
    QString *id,
    int *fileIndex) const
{
    return d->identifyStreamUrl(url, id, fileIndex);
}

QVariantMap TorrentEngine::status(const QString &id) const
{
    return d->status(id);
}
