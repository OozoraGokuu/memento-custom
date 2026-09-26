#include <QTest>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "quick/episodefolder.h"
#include "quick/torrentengine.h"
#include "util/directoryutils.h"

namespace
{

QByteArray validTorrent(QByteArray *infoHash)
{
    QByteArray info(
        "d6:lengthi1e4:name8:test.mkv12:piece lengthi16384e6:pieces20:"
    );
    info.append(QByteArray(20, '\0'));
    info.append('e');
    if (infoHash != nullptr)
    {
        *infoHash = QCryptographicHash::hash(info, QCryptographicHash::Sha1)
            .toHex();
    }
    return QByteArrayLiteral("d4:info") + info + QByteArrayLiteral("e");
}

QByteArray torrentNamed(const QByteArray &name, QByteArray *infoHash)
{
    QByteArray info = "d6:lengthi1e4:name" + QByteArray::number(name.size()) +
        ':' + name + "12:piece lengthi16384e6:pieces20:";
    info.append(QByteArray(20, '\0'));
    info.append('e');
    if (infoHash != nullptr)
    {
        *infoHash = QCryptographicHash::hash(info, QCryptographicHash::Sha1)
            .toHex();
    }
    return QByteArrayLiteral("d4:info") + info + QByteArrayLiteral("e");
}

QByteArray torrentWithFileCount(int count)
{
    QByteArray info("d5:filesl");
    for (int index = 0; index < count; ++index)
    {
        const QByteArray name = QByteArray::number(index) + ".mkv";
        info += "d6:lengthi1e4:pathl" + QByteArray::number(name.size()) + ':' +
            name + "ee";
    }
    info += "e4:name4:pack12:piece lengthi16384e6:pieces20:";
    info.append(QByteArray(20, '\0'));
    info += 'e';
    return QByteArrayLiteral("d4:info") + info + QByteArrayLiteral("e");
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size() &&
        file.flush();
}

bool createDirectoryRedirect(const QString &target, const QString &link)
{
#if defined(Q_OS_WIN)
    QProcess process;
    process.setProgram(QStringLiteral("cmd.exe"));
    // cmd.exe does not follow CommandLineToArgvW quoting rules.
    process.setNativeArguments(QStringLiteral("/D /C mklink /J \"%1\" \"%2\"")
        .arg(QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)));
    process.start();
    return process.waitForFinished(10'000) && process.exitCode() == 0 &&
        QFileInfo(link).isJunction();
#elif defined(Q_OS_UNIX)
    return QFile::link(target, link) && QFileInfo(link).isSymbolicLink();
#else
    Q_UNUSED(target)
    Q_UNUSED(link)
    return false;
#endif
}

bool removeDirectoryRedirect(const QString &link)
{
#if defined(Q_OS_WIN)
    return QDir().rmdir(link);
#elif defined(Q_OS_UNIX)
    return QFile::remove(link);
#else
    Q_UNUSED(link)
    return false;
#endif
}

} // namespace

class TorrentEngineTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_stateDirectory.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("memento-tests"));
        QCoreApplication::setApplicationName(QStringLiteral("torrent-engine-") +
            QUuid::createUuid().toString(QUuid::WithoutBraces));
        const QString config = m_stateDirectory.filePath(QStringLiteral("config"));
        const QString cache = m_stateDirectory.filePath(QStringLiteral("cache"));
        QVERIFY(QDir().mkpath(config));
        QVERIFY(QDir().mkpath(cache));
        QVERIFY(qputenv("XDG_CONFIG_HOME", config.toUtf8()));
        QVERIFY(qputenv("XDG_CACHE_HOME", cache.toUtf8()));
        // Main creates this before the library; Windows QSettings uses the
        // registry and does not implicitly create the disk configuration path.
        QVERIFY(QDir().mkpath(DirectoryUtils::getConfigDir()));
    }

    void canonicalizesLegacyRestoreAndRecognizesOnlyIssuedUrls()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QByteArray infoHash;
        const QString source = directory.filePath(QStringLiteral("source.torrent"));
        QVERIFY(writeFile(source, validTorrent(&infoHash)));

        TorrentEngine engine;
        const QString legacyId = QStringLiteral("torrent-") +
            QString::fromLatin1(infoHash);
        const TorrentEngine::AddResult result = engine.restore(legacyId, source);
        QVERIFY2(result.valid(), qPrintable(result.error));
        QCOMPARE(result.id, QStringLiteral("torrent-v1-") +
            QString::fromLatin1(infoHash));
        QCOMPARE(result.fileIndices, QList<int>{0});
        QVERIFY(!engine.status(legacyId).value(QStringLiteral("ready")).toBool());
        QVERIFY(engine.status(result.id).value(QStringLiteral("ready")).toBool());
        QVERIFY(!engine.status(legacyId).value(QStringLiteral("loadingMetadata")).toBool());
        QVERIFY(!engine.status(result.id).value(QStringLiteral("loadingMetadata")).toBool());

        QString identifiedId;
        int identifiedIndex = -1;
        const QString preview = engine.urlFor(result.id, 0, QStringLiteral("test.mkv"));
        QVERIFY(!preview.isEmpty());
        QVERIFY(!engine.identifyStreamUrl(
            preview, &identifiedId, &identifiedIndex));

        const QString issued = engine.streamUrl(
            result.id, 0, QStringLiteral("test.mkv")
        );
        QVERIFY(!issued.isEmpty());
        QVERIFY(engine.identifyStreamUrl(issued, &identifiedId, &identifiedIndex));
        QCOMPARE(identifiedId, result.id);
        QCOMPARE(identifiedIndex, 0);

        QUrl wrongOrigin(issued);
        wrongOrigin.setHost(QStringLiteral("localhost"));
        QVERIFY(!engine.identifyStreamUrl(
            wrongOrigin.toString(), &identifiedId, &identifiedIndex));

        QUrl wrongPort(issued);
        const int port = wrongPort.port();
        wrongPort.setPort(port == 65535 ? port - 1 : port + 1);
        QVERIFY(!engine.identifyStreamUrl(
            wrongPort.toString(), &identifiedId, &identifiedIndex));

        QUrl withQuery(issued);
        withQuery.setQuery(QUrlQuery(QStringLiteral("unexpected=1")));
        QVERIFY(!engine.identifyStreamUrl(
            withQuery.toString(), &identifiedId, &identifiedIndex));

        const QString unissued = engine.urlFor(
            result.id, 0, QStringLiteral("different-name.mkv")
        );
        QVERIFY(!engine.identifyStreamUrl(
            unissued, &identifiedId, &identifiedIndex));

        engine.removeTorrent(result.id, true);
        QVERIFY(!engine.identifyStreamUrl(
            issued, &identifiedId, &identifiedIndex));
    }

    void servesCachedTorrentRangesAndDeletesOwnedFiles()
    {
        QTemporaryDir metadata;
        QVERIFY(metadata.isValid());
        const QByteArray payload("synthetic-video-data-for-range-serving");
        QByteArray info = "d6:lengthi" + QByteArray::number(payload.size()) +
            "e4:name8:test.mkv12:piece lengthi16384e6:pieces20:";
        info += QCryptographicHash::hash(payload, QCryptographicHash::Sha1);
        info += 'e';
        const QString id = "torrent-v1-" + QString::fromLatin1(
            QCryptographicHash::hash(info, QCryptographicHash::Sha1).toHex());
        const auto source = metadata.filePath("range.torrent");
        QVERIFY(writeFile(source, "d4:info" + info + "e"));
        TorrentEngine engine;
        const QString cache = QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
            .filePath("torrents/" + id);
        QVERIFY(QDir().mkpath(cache));
        QVERIFY(writeFile(QDir(cache).filePath("test.mkv"), payload));
        const auto added = engine.addTorrentFile(source);
        QVERIFY2(added.valid(), qPrintable(added.error));
        const auto url = engine.streamUrl(added.id, 0, "test.mkv");
        QVERIFY(!url.isEmpty());
        QNetworkAccessManager manager;
        QNetworkRequest request{QUrl(url)};
        request.setRawHeader("Range", "bytes=3-12");
        auto *reply = manager.get(request);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 15000);
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 206);
        QCOMPARE(reply->readAll(), payload.mid(3, 10));
        reply->deleteLater();
        request.setRawHeader("Range", "bytes=999-");
        reply = manager.get(request);
        QTRY_VERIFY_WITH_TIMEOUT(reply->isFinished(), 5000);
        QCOMPARE(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 416);
        reply->deleteLater();
        // Exercise normal cleanup as well as the link-rejection safety tests.
        QVERIFY(QDir().mkpath(QDir(cache).filePath("nested")));
        QVERIFY(writeFile(QDir(cache).filePath("nested/owned.partial"), "partial"));
        engine.removeTorrent(added.id, true);
        QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(cache), 5000);
        QVERIFY(QFileInfo::exists(source));
    }

    void rejectsOversizedMetadataBeforeParsing()
    {
        constexpr qint64 metadataLimit = 64 * 1024 * 1024;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile oversized(directory.filePath(QStringLiteral("oversized.torrent")));
        QVERIFY(oversized.open(QIODevice::WriteOnly));
        QVERIFY(oversized.resize(metadataLimit + 1));
        oversized.close();

        TorrentEngine engine;
        const TorrentEngine::AddResult result = engine.addTorrentFile(
            oversized.fileName()
        );
        QVERIFY(!result.valid());
        QVERIFY(result.error.contains(QStringLiteral("64 MiB")));
    }

    void rejectsPrivateTrackerLiterals()
    {
        for (const QString &url : {
                QStringLiteral("http://127.0.0.1/announce"),
                QStringLiteral("http://0.0.0.0/announce"),
                QStringLiteral("udp://10.0.0.1:80/announce"),
                QStringLiteral("udp://100.64.0.1:80/announce"),
                QStringLiteral("https://172.16.2.3/announce"),
                QStringLiteral("http://192.168.1.1/announce"),
                QStringLiteral("http://192.0.2.1/announce"),
                QStringLiteral("http://198.18.0.1/announce"),
                QStringLiteral("http://224.0.0.1/announce"),
                QStringLiteral("http://255.255.255.255/announce"),
                QStringLiteral("http://169.254.1.1/announce"),
                QStringLiteral("http://[::]/announce"),
                QStringLiteral("http://[::1]/announce"),
                QStringLiteral("http://[::7f00:1]/announce"),
                QStringLiteral("http://[::ffff:127.0.0.1]/announce"),
                QStringLiteral("http://[::ffff:1.1.1.1]/announce"),
                QStringLiteral("http://[100::1]/announce"),
                QStringLiteral("http://[2001:db8::1]/announce"),
                QStringLiteral("http://[fc00::1]/announce"),
                QStringLiteral("http://[fe80::1]/announce"),
                QStringLiteral("http://[fec0::1]/announce"),
                QStringLiteral("http://[ff02::1]/announce"),
                QStringLiteral("http://localhost/announce"),
                QStringLiteral("http://localhost./announce")})
        {
            QVERIFY2(!TorrentEngine::safeTrackerUrl(url), qPrintable(url));
        }
        QVERIFY(TorrentEngine::safeTrackerUrl(
            QStringLiteral("udp://tracker.opentrackr.org:1337/announce")));
        QVERIFY(TorrentEngine::safeTrackerUrl(
            QStringLiteral("https://1.1.1.1/announce")));
        QVERIFY(TorrentEngine::safeTrackerUrl(
            QStringLiteral("https://[2606:4700:4700::1111]/announce")));
    }

    void rejectsTorrentWithExcessiveFileCount()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath(QStringLiteral("many.torrent"));
        QVERIFY(writeFile(source, torrentWithFileCount(10001)));

        TorrentEngine engine;
        const TorrentEngine::AddResult result = engine.addTorrentFile(source);
        QVERIFY(!result.valid());
        QVERIFY(result.error.contains(QStringLiteral("file-list limits")));
    }

    void rejectsTorrentWithoutPlayableEpisodes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath(QStringLiteral("text.torrent"));
        QVERIFY(writeFile(source, torrentNamed("readme.txt", nullptr)));

        EpisodeFolder library;
        QCOMPARE(library.addTorrent(QUrl::fromLocalFile(source)), -1);
        QVERIFY(library.library().isEmpty());
        QVERIFY(library.lastError().contains(
            QStringLiteral("supported video episodes")));
    }

    void internalTorrentStreamsAreTransient()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath(QStringLiteral("episode.torrent"));
        QVERIFY(writeFile(source, validTorrent(nullptr)));

        EpisodeFolder library;
        const int index = library.addTorrent(QUrl::fromLocalFile(source));
        QVERIFY2(index >= 0, qPrintable(library.lastError()));

        const QString stream = library.episodePath(0);
        QVERIFY(!stream.isEmpty());
        QVERIFY(library.isTransientStream(stream));
        const QVariantMap link{{"provider", "jimaku"}, {"name", "Torrent show"}, {"entryId", 42}};
        QVERIFY(library.setSubtitleLinkForFile(stream, link));
        QCOMPARE(library.subtitleLinkForFile(stream), link);
        QVERIFY(library.clearSubtitleLinkForFile(stream));
        QVERIFY(!library.isTransientStream(source));

        library.removeEntry(index, true);
        QVERIFY(library.library().isEmpty());
    }

    void torrentCacheDoesNotFollowDirectorySymlinks()
    {
#if defined(Q_OS_UNIX) || defined(Q_OS_WIN)
        TorrentEngine engine;
        QTemporaryDir outside;
        QTemporaryDir metadata;
        QVERIFY(outside.isValid());
        QVERIFY(metadata.isValid());

        const QString sentinel = outside.filePath(QStringLiteral("keep.txt"));
        QVERIFY(writeFile(sentinel, QByteArrayLiteral("do not delete")));

        QByteArray infoHash;
        const QString source = metadata.filePath(QStringLiteral("source.torrent"));
        QVERIFY(writeFile(source, torrentNamed("symlink.mkv", &infoHash)));
        const QString id = QStringLiteral("torrent-v1-") +
            QString::fromLatin1(infoHash);
        const QString cacheRoot = QDir(
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        ).filePath(QStringLiteral("torrents"));
        const QString cachePath = QDir(cacheRoot).filePath(id);

        if (!createDirectoryRedirect(outside.path(), cachePath))
        {
            QSKIP("This host could not create a directory link for the regression test.");
        }
        const auto removeLink = qScopeGuard([&cachePath]() {
            removeDirectoryRedirect(cachePath);
        });

        const TorrentEngine::AddResult result = engine.addTorrentFile(source);
        QVERIFY(!result.valid());
        QVERIFY(result.error.contains(QStringLiteral("safe torrent cache")));
        QVERIFY(QFileInfo::exists(sentinel));

        QSignalSpy errors(&engine, &TorrentEngine::torrentError);
        engine.removeTorrent(id, true);
        QVERIFY(!errors.isEmpty());
        QVERIFY(QFileInfo::exists(sentinel));
        QVERIFY(QFileInfo(cachePath).isSymLink() ||
                QFileInfo(cachePath).isJunction());
#else
        QSKIP("Directory link regression is not supported on this host.");
#endif
    }

    void torrentCacheRejectsLinkedStorageRoot()
    {
#if defined(Q_OS_UNIX) || defined(Q_OS_WIN)
        QTemporaryDir outside;
        QTemporaryDir metadata;
        QVERIFY(outside.isValid());
        QVERIFY(metadata.isValid());

        const QString sentinel = outside.filePath(QStringLiteral("keep.txt"));
        QVERIFY(writeFile(sentinel, QByteArrayLiteral("do not delete")));
        QByteArray infoHash;
        const QString source = metadata.filePath(QStringLiteral("source.torrent"));
        QVERIFY(writeFile(source, torrentNamed("root-link.mkv", &infoHash)));
        const QString id = QStringLiteral("torrent-v1-") +
            QString::fromLatin1(infoHash);

        const QString cacheRoot = QDir(
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        ).filePath(QStringLiteral("torrents"));
        const QFileInfo rootInfo(cacheRoot);
        QVERIFY(!rootInfo.isSymLink());
        QVERIFY(!rootInfo.isJunction());
        QVERIFY(!rootInfo.exists() || QDir(cacheRoot).removeRecursively());
        if (!createDirectoryRedirect(outside.path(), cacheRoot))
        {
            QVERIFY(QDir().mkpath(cacheRoot));
            QSKIP("This host could not create a directory link for the regression test.");
        }
        const auto restoreRoot = qScopeGuard([&cacheRoot]() {
            removeDirectoryRedirect(cacheRoot);
            QDir().mkpath(cacheRoot);
        });

        TorrentEngine engine;
        const TorrentEngine::AddResult result = engine.addTorrentFile(source);
        QVERIFY(!result.valid());
        QVERIFY(result.error.contains(QStringLiteral("unsafe or unavailable")));
        QVERIFY(QFileInfo::exists(sentinel));

        QSignalSpy errors(&engine, &TorrentEngine::torrentError);
        engine.removeTorrent(id, true);
        QVERIFY(!errors.isEmpty());
        QVERIFY(QFileInfo::exists(sentinel));
        QVERIFY(QFileInfo(cacheRoot).isSymLink() ||
                QFileInfo(cacheRoot).isJunction());
#else
        QSKIP("Directory link regression is not supported on this host.");
#endif
    }

    void torrentDeletionRejectsCacheEntrySwappedForLink()
    {
#if defined(Q_OS_UNIX) || defined(Q_OS_WIN)
        QTemporaryDir outside;
        QTemporaryDir metadata;
        QVERIFY(outside.isValid());
        QVERIFY(metadata.isValid());

        const QString sentinel = outside.filePath(QStringLiteral("keep.txt"));
        QVERIFY(writeFile(sentinel, QByteArrayLiteral("do not delete")));
        const QString source = metadata.filePath(QStringLiteral("source.torrent"));
        QVERIFY(writeFile(source, torrentNamed("swap-link.mkv", nullptr)));

        TorrentEngine engine;
        const TorrentEngine::AddResult result = engine.addTorrentFile(source);
        QVERIFY2(result.valid(), qPrintable(result.error));

        const QString cacheRoot = QDir(
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        ).filePath(QStringLiteral("torrents"));
        const QString cachePath = QDir(cacheRoot).filePath(result.id);
        QVERIFY(QFileInfo(cachePath).isDir());
        QVERIFY(QDir(cachePath).removeRecursively());
        if (!createDirectoryRedirect(outside.path(), cachePath))
        {
            QSKIP("This host could not create a directory link for the regression test.");
        }
        const auto removeLink = qScopeGuard([&cachePath]() {
            removeDirectoryRedirect(cachePath);
        });

        QSignalSpy errors(&engine, &TorrentEngine::torrentError);
        engine.removeTorrent(result.id, true);
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 10'000);
        QVERIFY(QFileInfo::exists(sentinel));
        QVERIFY(QFileInfo(cachePath).isSymLink() ||
                QFileInfo(cachePath).isJunction());
#else
        QSKIP("Directory link regression is not supported on this host.");
#endif
    }

    void torrentDeletionRejectsNestedDirectoryLink()
    {
#if defined(Q_OS_UNIX) || defined(Q_OS_WIN)
        QTemporaryDir outside;
        QTemporaryDir metadata;
        QVERIFY(outside.isValid());
        QVERIFY(metadata.isValid());

        const QString sentinel = outside.filePath(QStringLiteral("keep.txt"));
        QVERIFY(writeFile(sentinel, QByteArrayLiteral("do not delete")));
        const QString source = metadata.filePath(QStringLiteral("source.torrent"));
        QVERIFY(writeFile(source, torrentNamed("nested-link.mkv", nullptr)));

        TorrentEngine engine;
        const TorrentEngine::AddResult result = engine.addTorrentFile(source);
        QVERIFY2(result.valid(), qPrintable(result.error));

        const QString cacheRoot = QDir(
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        ).filePath(QStringLiteral("torrents"));
        const QString cachePath = QDir(cacheRoot).filePath(result.id);
        const QString redirect = QDir(cachePath).filePath(QStringLiteral("redirect"));
        if (!createDirectoryRedirect(outside.path(), redirect))
        {
            QSKIP("This host could not create a directory link for the regression test.");
        }
        const auto removeLink = qScopeGuard([&redirect, &cachePath]() {
            removeDirectoryRedirect(redirect);
            QDir(cachePath).removeRecursively();
        });

        QSignalSpy errors(&engine, &TorrentEngine::torrentError);
        engine.removeTorrent(result.id, true);
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 10'000);
        QVERIFY(QFileInfo::exists(sentinel));
        QVERIFY(QFileInfo(cachePath).isDir());
        QVERIFY(QFileInfo(redirect).isSymLink() ||
                QFileInfo(redirect).isJunction());
#else
        QSKIP("Directory link regression is not supported on this host.");
#endif
    }

    void setContentFolderDoesNotCreateDuplicateIdentity()
    {
        QTemporaryDir first;
        QTemporaryDir second;
        QVERIFY(first.isValid());
        QVERIFY(second.isValid());
        QVERIFY(writeFile(first.filePath(QStringLiteral("01.mkv")), "video"));
        QVERIFY(writeFile(second.filePath(QStringLiteral("02.mkv")), "video"));

        EpisodeFolder library;
        const int firstIndex = library.addFolder(QUrl::fromLocalFile(first.path()));
        const int secondIndex = library.addFolder(QUrl::fromLocalFile(second.path()));
        QVERIFY(firstIndex >= 0);
        QVERIFY(secondIndex >= 0);
        QVERIFY(!library.setContentFolder(
            secondIndex, QUrl::fromLocalFile(first.path())));
        QCOMPARE(library.currentIndex(), firstIndex);
        QCOMPARE(library.library().size(), 2);

        library.removeEntry(1);
        library.removeEntry(0);
    }

    void failedLibraryCommitRollsBackBeforeTorrentDeletion()
    {
#if defined(Q_OS_UNIX)
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString source = directory.filePath(QStringLiteral("episode.torrent"));
        QVERIFY(writeFile(source, validTorrent(nullptr)));

        EpisodeFolder library;
        const int index = library.addTorrent(QUrl::fromLocalFile(source));
        QVERIFY2(index >= 0, qPrintable(library.lastError()));
        QCOMPARE(library.library().size(), 1);

        const QVariantMap entry = library.library().at(index).toMap();
        const QString id = entry.value(QStringLiteral("id")).toString();
        const QString storedSource =
            entry.value(QStringLiteral("source")).toString();
        QVERIFY(!id.isEmpty());
        QVERIFY(QFileInfo::exists(storedSource));

        const QString cachePath = QDir(
            QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        ).filePath(QStringLiteral("torrents/") + id);
        QVERIFY(QDir().mkpath(cachePath));
        const QString sentinel = QDir(cachePath).filePath(
            QStringLiteral("keep.partial"));
        QVERIFY(writeFile(sentinel, QByteArrayLiteral("partial payload")));

        const QString configDirectory = DirectoryUtils::getConfigDir();
        const QFile::Permissions originalPermissions =
            QFileInfo(configDirectory).permissions();
        QVERIFY(QFile::setPermissions(
            configDirectory,
            QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        auto permissionsGuard = qScopeGuard([
            configDirectory,
            originalPermissions
        ] {
            QFile::setPermissions(configDirectory, originalPermissions);
        });

        QSignalSpy errorSpy(&library, &EpisodeFolder::errorOccurred);
        library.setEpisodeWatched(0, true);
        QCOMPARE(errorSpy.count(), 1);
        QVERIFY(!library.episodes().at(0).toMap()
            .value(QStringLiteral("watched")).toBool());

        errorSpy.clear();
        library.removeEntry(index, true);

        QCOMPARE(errorSpy.count(), 1);
        QCOMPARE(library.library().size(), 1);
        QCOMPARE(library.currentIndex(), index);
        QVERIFY(library.lastError().contains(QStringLiteral("could not save")));
        QVERIFY(QFileInfo::exists(storedSource));
        QVERIFY(QFileInfo::exists(sentinel));

        QFile persisted(QDir(configDirectory).filePath(
            QStringLiteral("episode-library.json")));
        QVERIFY(persisted.open(QIODevice::ReadOnly));
        const QJsonArray persistedEntries = QJsonDocument::fromJson(
            persisted.readAll()).object().value(QStringLiteral("entries")).toArray();
        QCOMPARE(persistedEntries.size(), 1);
        QCOMPARE(
            persistedEntries.at(0).toObject()
                .value(QStringLiteral("id")).toString(),
            id);

        permissionsGuard.dismiss();
        QVERIFY(QFile::setPermissions(configDirectory, originalPermissions));
        library.removeEntry(index, true);
        QVERIFY(library.library().isEmpty());
#else
        QSKIP("Read-only directory regression is exercised on Unix hosts.");
#endif
    }

private:
    QTemporaryDir m_stateDirectory;
};

QTEST_GUILESS_MAIN(TorrentEngineTest)

#include "test_torrentengine.moc"
