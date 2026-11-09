#include <QUrlQuery>
#include "state/context.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QUuid>
#include <clocale>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkReply>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <zip.h>

#include "jimaku/jimakuclient.h"
#include "jimaku/subtitlerelease.h"

namespace
{

QJsonObject subtitleFile(const QString &name, const QString &url)
{
    return {{"name", name}, {"size", 1024}, {"url", url}};
}

QVariantMap releaseLink(const QString &name, int episode)
{
    return {{"provider", "jimaku"}, {"name", "Chosen Show"}, {"entryId", 123},
        {"preferredFile", name}, {"releaseRule", SubtitleRelease::fromFile(name, episode)}};
}

bool createArchive(
    const QString &path,
    const QList<QPair<QByteArray, QByteArray>> &members)
{
    int error = 0;
    zip_t *archive = zip_open(
        QFile::encodeName(path).constData(), ZIP_CREATE | ZIP_TRUNCATE, &error
    );
    if (archive == nullptr)
    {
        return false;
    }
    for (const auto &[name, contents] : members)
    {
        zip_source_t *source = zip_source_buffer(
            archive, contents.constData(), static_cast<zip_uint64_t>(contents.size()), 0
        );
        if (source == nullptr ||
            zip_file_add(archive, name.constData(), source, ZIP_FL_ENC_UTF_8) < 0)
        {
            if (source != nullptr)
            {
                zip_source_free(source);
            }
            zip_discard(archive);
            return false;
        }
    }
    return zip_close(archive) == 0;
}

} // namespace

class JimakuClientTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {
        std::setlocale(LC_NUMERIC, "C");
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("subtitle-provider-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(QDir().mkpath(DirectoryUtils::getDataDir()));
        QVERIFY(QDir().mkpath(DirectoryUtils::getConfigDir()));
        Dictionary::createDatabaseInstance();
    }
    void explicitSelectionPersistsForPlayingLibrary() {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        QFile file(folder.filePath("Show - 23.mkv"));
        QVERIFY(file.open(QIODevice::WriteOnly)); file.write("test"); file.close();
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        player.state()->setPath(file.fileName());
        JimakuClient client(&context);
        client.m_apiKey = "test-key"; client.m_baseUrl = QUrl("http://127.0.0.1:9");
        client.setSearchEntries({QJsonObject{{"id", 123}, {"name", "Show"}}});
        client.selectEntry(0, 23);
        const auto link = context.episodeLibrary()->subtitleLinkForFile(file.fileName());
        QCOMPARE(link.value("name").toString(), QString("Show"));
        QCOMPARE(link.value("provider").toString(), QString("jimaku")); QCOMPARE(link.value("entryId").toInt(), 123);
        client.cancel();
        player.state()->setPath("/outside.mkv");
        client.selectEntry(0, 23); client.cancel();
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(file.fileName()), link);
        context.setPlayer(nullptr);
    }

    void nextEpisodeAutomaticallyUsesSavedSeriesLink()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        for (const auto &name : {"Show - 23.mkv", "Show - 24.mkv"}) {
            QFile file(folder.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write("test");
        }
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        const QVariantMap link = releaseLink("[Selected] Show - 23.ass", 23);
        QVERIFY(context.episodeLibrary()->setSubtitleLinkForFile(folder.filePath("Show - 23.mkv"), link));
        const QString next = folder.filePath("Show - 24.mkv");
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(next), link);
        player.state()->setPath(next);
        JimakuClient client(&context);
        client.m_apiKey = "test-key";
        client.m_baseUrl = QUrl("http://127.0.0.1:1");
        client.fetchForCurrentMedia();
        QVERIFY(client.m_reply);
        QCOMPARE(client.m_reply->url().path(), QString("/api/entries/123/files"));
        QCOMPARE(QUrlQuery(client.m_reply->url()).queryItemValue("episode"), QString("24"));
        QVERIFY(client.m_operation == JimakuClient::Operation::Fetch);
        QCOMPARE(client.m_media.path, next);
        QCOMPARE(client.selectedEntryName(), QString("Chosen Show"));
        QCOMPARE(client.m_activeLink, link);
        client.cancel();
        context.setPlayer(nullptr);
    }

    void titleLinkWithoutReleaseDoesNotGuessAGroup()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        const QString path = folder.filePath("Show - 24.mkv");
        QFile media(path);
        QVERIFY(media.open(QIODevice::WriteOnly)); media.write("test"); media.close();
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        const QVariantMap link{{"provider", "jimaku"}, {"name", "Chosen Show"}, {"entryId", 123}};
        QVERIFY(context.episodeLibrary()->setSubtitleLinkForFile(path, link));
        player.state()->setPath(path);

        JimakuClient client(&context);
        client.m_apiKey = "test-key";
        client.m_baseUrl = QUrl("http://127.0.0.1:1");
        QSignalSpy failed(&client, &JimakuClient::failed);
        client.fetchForCurrentMedia();

        QCOMPARE(failed.count(), 1);
        QVERIFY(!client.busy());
        QVERIFY(!client.m_reply);
        QVERIFY(client.status().contains("Choose a subtitle file"));
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(path), link);
        context.setPlayer(nullptr);
    }

    void browsingSameTitlePreservesReleaseChoice()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        const QString path = folder.filePath("Show - 24.mkv");
        QFile media(path);
        QVERIFY(media.open(QIODevice::WriteOnly)); media.write("test"); media.close();
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        auto link = releaseLink("[Selected] Show - 23.ass", 23);
        link.insert("episodeOffset", 12);
        QVERIFY(context.episodeLibrary()->setSubtitleLinkForFile(path, link));
        player.state()->setPath(path);

        JimakuClient client(&context);
        client.m_apiKey = "test-key";
        client.m_baseUrl = QUrl("http://127.0.0.1:1");
        client.setSearchEntries({QJsonObject{{"id", 123}, {"name", "Chosen Show"}}});
        client.selectEntry(0, 24);
        QCOMPARE(client.m_selectedLink, link);
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(path), link);
        client.cancel();
        client.selectEntryAllFiles(0, 24);
        QCOMPARE(client.m_selectedLink, link);
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(path), link);
        client.cancel();
        // Merely inspecting another title must not replace the working group.
        // Its new identity becomes active only after a file is attached.
        client.setSearchEntries({QJsonObject{{"id", 456}, {"name", "Another Show"}}});
        client.selectEntry(0, 24);
        QCOMPARE(client.m_selectedLink.value("entryId").toLongLong(), qint64(456));
        QVERIFY(client.m_selectedLink.value("releaseRule").toMap().isEmpty());
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(path), link);
        client.cancel();
        context.setPlayer(nullptr);
    }

    void savedEpisodeChoiceTakesPrecedenceOverSeriesRelease()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        const QString path = folder.filePath("Show - 24.mkv");
        QFile media(path);
        QVERIFY(media.open(QIODevice::WriteOnly)); media.write("test"); media.close();
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        const auto series = releaseLink("[Usual] Show - 23.ass", 23);
        auto selected = releaseLink("[Exception] Show - 24 [1234ABCD].ass", 24);
        selected.insert("filename", "[Exception] Show - 24 [1234ABCD].ass");
        selected.insert("episode", 24);
        selected.insert("delay", 1.75);
        selected.insert("cachePath", folder.filePath("missing-cached-subtitle.ass"));
        QVERIFY(context.episodeLibrary()->rememberSubtitleSelection(path, series, selected));
        player.state()->setPath(path);

        JimakuClient client(&context);
        client.m_apiKey = "test-key";
        client.m_baseUrl = QUrl("http://127.0.0.1:1");
        client.fetchForCurrentMedia();
        QCOMPARE(client.m_savedSelection, selected);
        QCOMPARE(client.m_activeLink, selected);
        const QJsonArray files{
            subtitleFile("[Usual] Show - 24.ass", "/usual.ass"),
            subtitleFile("[Exception] Show - 24 [DEADBEEF].ass", "/other-version.ass"),
            subtitleFile("[Exception] Show - 24 [1234ABCD].ass", "/exact.ass")
        };
        QCOMPARE(client.selectFile(files).name, selected.value("filename").toString());
        // A changed CRC is another file, even if it belongs to this release.
        // Losing the exact saved choice must not silently substitute it.
        QJsonArray missing = files;
        missing.removeLast();
        QVERIFY(client.selectFile(missing).name.isEmpty());
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(path), series);
        client.cancel();
        context.setPlayer(nullptr);
    }

    void missingOrAmbiguousReleaseStopsWithoutDownload_data()
    {
        QTest::addColumn<QJsonArray>("files");
        QTest::newRow("missing") << QJsonArray{
            subtitleFile("[Other] Show - 24.ass", "/other.ass"),
            subtitleFile("[Selected] Show - 23.ass", "/old.ass")
        };
        QTest::newRow("ambiguous") << QJsonArray{
            subtitleFile("[Other] Show - 24.ass", "/other.ass"),
            subtitleFile("[Selected] Show - 24 [1234ABCD].ass", "/selected-a.ass"),
            subtitleFile("[Selected] Show - 24 [DEADBEEF].ass", "/selected-b.ass")
        };
    }

    void missingOrAmbiguousReleaseStopsWithoutDownload()
    {
        QFETCH(QJsonArray, files);
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        const QString path = folder.filePath("Show - 24.mkv");
        QFile media(path);
        QVERIFY(media.open(QIODevice::WriteOnly)); media.write("test"); media.close();
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        const auto link = releaseLink("[Selected] Show - 23.ass", 23);
        QVERIFY(context.episodeLibrary()->setSubtitleLinkForFile(path, link));
        player.state()->setPath(path);

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        const QByteArray body = QJsonDocument(files).toJson(QJsonDocument::Compact);
        QStringList requests;
        connect(&server, &QTcpServer::newConnection, &server, [&server, &requests, body] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, &requests, body] {
                QByteArray request = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", request);
                if (!request.contains("\r\n\r\n") || socket->property("answered").toBool()) return;
                socket->setProperty("answered", true);
                requests.append(QString::fromUtf8(request.split('\n').first()).trimmed());
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        JimakuClient client(&context);
        client.m_apiKey = "test-key";
        client.m_baseUrl = QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        QSignalSpy failed(&client, &JimakuClient::failed);
        client.fetchForCurrentMedia();
        QTRY_COMPARE(failed.count(), 1);

        QVERIFY(!client.busy());
        QVERIFY(client.m_file.name.isEmpty());
        QVERIFY(client.status().contains("missing or ambiguous"));
        QCOMPARE(requests, QStringList({"GET /api/entries/123/files?episode=24 HTTP/1.1",
                                       "GET /api/entries/123/files HTTP/1.1"}));
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(path), link);
        QVERIFY(context.episodeLibrary()->subtitleSelectionForFile(path).isEmpty());
        context.setPlayer(nullptr);
    }

    void linkedTitleOpensDirectlyAndFailsSafely()
    {
        JimakuClient client(nullptr);
        client.m_apiKey = "test-key";
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&server] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                socket->readAll();
                socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
            });
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        });
        client.m_baseUrl = QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
        QSignalSpy failed(&client, &JimakuClient::failed);
        client.openLinkedEntry({{"provider", "jimaku"}, {"name", "Show"}, {"entryId", 123}}, 23);
        QVERIFY(client.m_operation == JimakuClient::Operation::ManualFiles);
        QVERIFY(client.m_reply);
        QCOMPARE(client.m_reply->url().path(), QString("/api/entries/123/files"));
        QCOMPARE(client.selectedEpisode(), 23);
        QCOMPARE(client.selectedEntryName(), QString("Show"));
        QTRY_VERIFY(!client.busy());
        QCOMPARE(failed.size(), 1);
        client.openLinkedEntry({{"provider", "jimaku"}}, 23);
        QCOMPARE(failed.size(), 2);
        QVERIFY(!client.busy());
    }

    void extractsEpisodeAndCleansReleaseNames()
    {
        QCOMPARE(
            JimakuClient::episodeNumber(
                QStringLiteral("[Group] Frieren - 03 [1080p].mkv")),
            3
        );
        QCOMPARE(
            JimakuClient::cleanTitle(
                QStringLiteral("[Group] Frieren - 03 [1080p].mkv")),
            QStringLiteral("Frieren")
        );
        QCOMPARE(
            JimakuClient::seasonNumber(
                QStringLiteral("[Group] Example_Show.S02E03.1080p.mkv")),
            2
        );
        QCOMPARE(
            JimakuClient::cleanTitle(
                QStringLiteral("[Group] Example_Show.S02E03.1080p.mkv")),
            QStringLiteral("Example Show Season 2")
        );
        QCOMPARE(
            JimakuClient::seasonNumber(
                QStringLiteral("Example Show 3rd Season - 04.mkv")),
            3
        );
        QCOMPARE(
            JimakuClient::seasonNumber(
                QStringLiteral("Example Show [Season 2] - 03.mkv")),
            2
        );
        QCOMPARE(
            JimakuClient::seasonNumber(
                QStringLiteral("Example Show [S2] - 03.mkv")),
            2
        );
        QCOMPARE(
            JimakuClient::episodeNumber(
                QStringLiteral("Example Show [1080].mkv")),
            -1
        );
        QCOMPARE(
            JimakuClient::episodeNumber(
                QStringLiteral("Example Show [2024].mkv")),
            -1
        );
        QCOMPARE(
            JimakuClient::episodeNumber(
                QStringLiteral("Example Show [03].mkv")),
            3
        );
        QCOMPARE(JimakuClient::episodeNumber(QStringLiteral("03.ass")), 3);
        QCOMPARE(JimakuClient::episodeNumber(QStringLiteral("003v2.srt")), 3);
        QCOMPARE(JimakuClient::episodeNumber(QStringLiteral("1080.ass")), -1);
        QCOMPARE(JimakuClient::episodeNumber(QStringLiteral("2024.ass")), -1);
    }

    void extractsExactEpisodeFromNumericBatchArchive()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString archivePath = directory.filePath(QStringLiteral("batch.zip"));
        QVERIFY(createArchive(archivePath, {
            {QByteArrayLiteral("01.ass"), QByteArrayLiteral("episode one")},
            {QByteArrayLiteral("02.ass"), QByteArrayLiteral("episode two")},
            {QByteArrayLiteral("03.ass"), QByteArrayLiteral("episode three")},
        }));

        JimakuClient client(nullptr);
        client.m_media.episode = 3;
        QString error;
        const QString extracted = client.extractZip(archivePath, &error);
        QVERIFY2(!extracted.isEmpty(), qPrintable(error));

        QFile subtitle(extracted);
        QVERIFY(subtitle.open(QIODevice::ReadOnly));
        QCOMPARE(subtitle.readAll(), QByteArrayLiteral("episode three"));
        subtitle.close();
        QVERIFY(QFile::remove(extracted));
    }

    void rejectsAmbiguousUnnumberedBatchArchive()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString archivePath = directory.filePath(QStringLiteral("ambiguous.zip"));
        QVERIFY(createArchive(archivePath, {
            {QByteArrayLiteral("dialogue.ass"), QByteArrayLiteral("dialogue")},
            {QByteArrayLiteral("signs.ass"), QByteArrayLiteral("signs")},
        }));

        JimakuClient client(nullptr);
        client.m_media.episode = 3;
        QString error;
        QVERIFY(client.extractZip(archivePath, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void rejectsMultipleNumberedReleasesInsideArchive()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString archivePath = directory.filePath("multiple-releases.zip");
        QVERIFY(createArchive(archivePath, {
            {QByteArrayLiteral("[Other] Show - 03.ass"), QByteArrayLiteral("other group")},
            {QByteArrayLiteral("[Selected] Show - 03.ass"), QByteArrayLiteral("selected group")},
            {QByteArrayLiteral("[Selected] Show - 02.ass"), QByteArrayLiteral("wrong episode")},
        }));

        JimakuClient client(nullptr);
        client.m_media.episode = 3;
        QString error;
        QVERIFY(client.extractZip(archivePath, &error).isEmpty());
        QVERIFY(error.contains("multiple subtitle releases"));

        // A previously selected member identifies its group for the next
        // episode, even when a batch also contains other groups.
        client.m_activeLink.insert("memberRule",
            SubtitleRelease::fromFile("[Selected] Show - 02.ass", 2));
        error.clear();
        const QString extracted = client.extractZip(archivePath, &error);
        QVERIFY2(!extracted.isEmpty(), qPrintable(error));
        QFile subtitle(extracted);
        QVERIFY(subtitle.open(QIODevice::ReadOnly));
        QCOMPARE(subtitle.readAll(), QByteArrayLiteral("selected group"));
        subtitle.close();
        QVERIFY(QFile::remove(extracted));
    }

    void rejectsBatchArchiveWithoutTheRequestedEpisode()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString archivePath = directory.filePath(QStringLiteral("wrong.zip"));
        QVERIFY(createArchive(archivePath, {
            {QByteArrayLiteral("01.ass"), QByteArrayLiteral("episode one")},
            {QByteArrayLiteral("02.ass"), QByteArrayLiteral("episode two")},
        }));

        JimakuClient client(nullptr);
        client.m_media.episode = 3;
        QString error;
        QVERIFY(client.extractZip(archivePath, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void rejectsCumulativeNamesFromIgnoredArchiveMembers()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString archivePath = directory.filePath(QStringLiteral("oversized-names.zip"));

        QList<QPair<QByteArray, QByteArray>> members;
        members.reserve(3501);
        members.append({QByteArrayLiteral("03.ass"), QByteArrayLiteral("episode three")});
        for (int index = 0; index < 3500; ++index)
        {
            members.append({
                QByteArray::number(index) + '-' + QByteArray(300, 'x') + ".txt",
                {},
            });
        }
        QVERIFY(createArchive(archivePath, members));

        JimakuClient client(nullptr);
        client.m_media.episode = 3;
        QString error;
        QVERIFY(client.extractZip(archivePath, &error).isEmpty());
        QVERIFY(error.contains(QStringLiteral("oversized file list")));
    }

    void scoresExactTitlesAndEpisodeFiles()
    {
        const QJsonObject exact{
            {QStringLiteral("name"), QStringLiteral("Sousou no Frieren")},
            {QStringLiteral("english_name"), QStringLiteral("Frieren")},
        };
        const QJsonObject partial{
            {QStringLiteral("name"), QStringLiteral("Frieren: Beyond Journey's End Specials")},
        };
        QVERIFY(JimakuClient::entryScore(
            QStringLiteral("Frieren"), exact) >
            JimakuClient::entryScore(QStringLiteral("Frieren"), partial));
        const QJsonObject overbroad{
            {QStringLiteral("name"), QStringLiteral("One Piece")},
        };
        QVERIFY(JimakuClient::entryScore(
            QStringLiteral("One"), overbroad) < 250);
        QVERIFY(JimakuClient::fileScore(
            QStringLiteral("Frieren - 03.ass"), 3) >
            JimakuClient::fileScore(QStringLiteral("Frieren - 04.ass"), 3));
        QVERIFY(JimakuClient::supportedSubtitle(
            QStringLiteral("legacy-script.ssa")));
        QVERIFY(!JimakuClient::supportedSubtitle(
            QStringLiteral("readme.txt")));

        const QJsonObject unnumberedSeason{
            {QStringLiteral("name"), QStringLiteral("Example Show")},
        };
        const QJsonObject secondSeason{
            {QStringLiteral("name"), QStringLiteral("Example Show Season 2")},
        };
        const QJsonObject thirdSeason{
            {QStringLiteral("name"), QStringLiteral("Example Show 3rd Season")},
        };
        QVERIFY(JimakuClient::entryMatchesSeason(1, unnumberedSeason));
        QVERIFY(!JimakuClient::entryMatchesSeason(2, unnumberedSeason));
        QVERIFY(JimakuClient::entryMatchesSeason(2, secondSeason));
        QVERIFY(!JimakuClient::entryMatchesSeason(2, thirdSeason));
    }

    void acceptsOnlySecureOrLoopbackApiBases()
    {
        const QByteArray previous = qgetenv("MEMENTO_JIMAKU_API_BASE");
        const bool wasSet = qEnvironmentVariableIsSet(
            "MEMENTO_JIMAKU_API_BASE");
        [[maybe_unused]] auto restoreEnvironment = qScopeGuard(
            [previous, wasSet] {
                if (wasSet)
                {
                    qputenv("MEMENTO_JIMAKU_API_BASE", previous);
                }
                else
                {
                    qunsetenv("MEMENTO_JIMAKU_API_BASE");
                }
            }
        );

        QVERIFY(qputenv(
            "MEMENTO_JIMAKU_API_BASE",
            QByteArrayLiteral("http://example.com")
        ));
        JimakuClient insecure(nullptr);
        QCOMPARE(
            insecure.m_baseUrl,
            QUrl(QStringLiteral("https://jimaku.cc"))
        );

        QVERIFY(qputenv(
            "MEMENTO_JIMAKU_API_BASE",
            QByteArrayLiteral("http://127.0.0.1:8123")
        ));
        JimakuClient loopback(nullptr);
        QCOMPARE(
            loopback.m_baseUrl,
            QUrl(QStringLiteral("http://127.0.0.1:8123"))
        );
    }

    void restrictsDownloadsToTheConfiguredJimakuOrigin()
    {
        JimakuClient client(nullptr);
        client.m_baseUrl = QUrl(QStringLiteral("https://jimaku.cc"));

        QVERIFY(client.safeDownloadUrl(
            QUrl(QStringLiteral("https://jimaku.cc/entry/1/download/a.ass"))));
        QVERIFY(client.safeDownloadUrl(
            QUrl(QStringLiteral("https://JIMAKU.CC:443/entry/1/download/a.ass"))));
        QVERIFY(!client.safeDownloadUrl(
            QUrl(QStringLiteral("https://evil.example/a.ass"))));
        QVERIFY(!client.safeDownloadUrl(
            QUrl(QStringLiteral("https://jimaku.cc:444/a.ass"))));
        QVERIFY(!client.safeDownloadUrl(
            QUrl(QStringLiteral("http://jimaku.cc/a.ass"))));
        QVERIFY(!client.safeDownloadUrl(
            QUrl(QStringLiteral("https://jimaku.cc@evil.example/a.ass"))));
    }

    void invalidInvocationsDoNotMutateAnActiveOperation()
    {
        JimakuClient client(nullptr);
        client.m_apiKey.clear();
        client.m_busy = true;
        client.m_operation = JimakuClient::Operation::ManualSearch;
        client.m_generation = 42;
        client.m_status = QStringLiteral("active");

        client.fetchForCurrentMedia();
        client.testConnection();
        client.search({});
        client.selectEntry(-1);
        client.attachResult(-1);

        QVERIFY(client.m_busy);
        QVERIFY(client.m_operation == JimakuClient::Operation::ManualSearch);
        QCOMPARE(client.m_generation, quint64{42});
        QCOMPARE(client.m_status, QStringLiteral("active"));
    }

    void validSearchClearsStaleResultsImmediately()
    {
        JimakuClient client(nullptr);
        client.m_apiKey = QStringLiteral("test-key");
        client.m_baseUrl = QUrl(QStringLiteral("http://127.0.0.1:9"));
        client.setSearchEntries({QJsonObject{
            {QStringLiteral("id"), 1},
            {QStringLiteral("name"), QStringLiteral("Old result")},
        }});
        QCOMPARE(client.searchResults().size(), 1);

        client.search(QStringLiteral("New query"));
        QVERIFY(client.searchResults().isEmpty());
        client.cancel();
    }

    void browsingAllFilesRetainsExactEpisodeContext()
    {
        JimakuClient client(nullptr);
        client.m_apiKey = QStringLiteral("test-key");
        client.m_baseUrl = QUrl(QStringLiteral("http://127.0.0.1:9"));
        client.setSearchEntries({QJsonObject{
            {QStringLiteral("id"), 123},
            {QStringLiteral("name"), QStringLiteral("Example Show")},
        }});

        client.selectEntryAllFiles(0, 3);

        QVERIFY(client.m_operation == JimakuClient::Operation::ManualFiles);
        QCOMPARE(client.selectedEpisode(), 3);
        QVERIFY(client.status().contains(QStringLiteral("all subtitle files")));
        QVERIFY(client.m_reply != nullptr);
        client.cancel();
    }

    void activeOperationKeepsItsCredentialSnapshot()
    {
        JimakuClient client(nullptr);
        client.m_apiKey = QStringLiteral("key-a");
        client.m_baseUrl = QUrl(QStringLiteral("http://127.0.0.1:9"));
        client.search(QStringLiteral("Example"));
        QCOMPARE(client.m_requestApiKey, QStringLiteral("key-a"));

        client.m_apiKey = QStringLiteral("key-b");
        QCOMPARE(client.apiKey(), QStringLiteral("key-b"));
        QCOMPARE(client.m_requestApiKey, QStringLiteral("key-a"));
        client.cancel();
        QVERIFY(client.m_requestApiKey.isEmpty());
    }

    void cacheIdentityChangesWhenProviderFileChanges()
    {
        JimakuClient client(nullptr);
        client.m_file.name = QStringLiteral("episode.ass");
        client.m_file.url = QUrl(
            QStringLiteral("https://jimaku.cc/entry/1/download/episode.ass")
        );
        client.m_file.size = 1024;
        client.m_file.lastModified = QStringLiteral("2026-01-01T00:00:00Z");
        const QString original = client.cachePath(client.m_file.name);

        client.m_file.lastModified = QStringLiteral("2026-01-02T00:00:00Z");
        const QString corrected = client.cachePath(client.m_file.name);
        QVERIFY(original != corrected);

        client.m_file.lastModified.clear();
        client.m_file.size = 2048;
        const QString resized = client.cachePath(client.m_file.name);
        QVERIFY(original != resized);
        QVERIFY(corrected != resized);
    }

    void evictsCorruptCachedArchiveBeforeRetrying()
    {
        JimakuClient client(nullptr);
        client.m_baseUrl = QUrl(QStringLiteral("http://127.0.0.1:9"));
        client.m_file.name = QStringLiteral("broken.zip");
        client.m_file.url = QUrl(QStringLiteral("http://127.0.0.1:9/broken.zip"));
        client.m_file.lastModified = QStringLiteral("2026-01-01T00:00:00Z");
        const QByteArray corrupt = QByteArrayLiteral("not-a-zip");
        client.m_file.size = corrupt.size();

        const QString cached = client.cachePath(client.m_file.name);
        QVERIFY(QDir().mkpath(QFileInfo(cached).absolutePath()));
        QFile file(cached);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(corrupt), corrupt.size());
        file.close();

        client.useSelectedFile(client.m_generation);
        QVERIFY(!QFileInfo::exists(cached));
        QVERIFY(client.m_reply != nullptr);
        client.cancel();
    }

    void automaticSelectionRejectsKnownWrongEpisodes()
    {
        JimakuClient client(nullptr);
        client.m_media.episode = 3;
        const QJsonArray files{
            QJsonObject{
                {QStringLiteral("name"), QStringLiteral("Series - 04.ass")},
                {QStringLiteral("size"), 1024},
                {QStringLiteral("url"), QStringLiteral("/wrong.ass")},
            },
        };

        QVERIFY(client.selectFile(files).name.isEmpty());

        QJsonArray untagged{
            QJsonObject{
                {QStringLiteral("name"), QStringLiteral("subtitle.ass")},
                {QStringLiteral("size"), 1024},
                {QStringLiteral("url"), QStringLiteral("/untagged.ass")},
            },
        };
        QCOMPARE(
            client.selectFile(untagged, false).name,
            QStringLiteral("subtitle.ass")
        );
        QVERIFY(client.selectFile(untagged, true).name.isEmpty());

        QJsonArray batch{
            QJsonObject{
                {QStringLiteral("name"), QStringLiteral("batch.zip")},
                {QStringLiteral("size"), 1024},
                {QStringLiteral("url"), QStringLiteral("/batch.zip")},
            },
        };
        QCOMPARE(
            client.selectFile(batch, true).name,
            QStringLiteral("batch.zip")
        );

        QJsonArray withCorrect = files;
        withCorrect.append(QJsonObject{
            {QStringLiteral("name"), QStringLiteral("Series - 03.srt")},
            {QStringLiteral("size"), 1024},
            {QStringLiteral("url"), QStringLiteral("/correct.srt")},
        });
        QCOMPARE(
            client.selectFile(withCorrect, true).name,
            QStringLiteral("Series - 03.srt")
        );
    }

    void automaticSelectionUsesOnlyUniqueChosenRelease()
    {
        JimakuClient client(nullptr);
        client.m_media.episode = 24;
        client.m_activeLink = releaseLink("[Selected] Show - 23.ja.srt", 23);
        const QJsonArray files{
            subtitleFile("[Other] Show - 24.ja.ass", "/other.ass"),
            subtitleFile("[Selected] Show - 24.ja.signs.srt", "/signs.srt"),
            subtitleFile("[Selected] Show - 24.en.srt", "/english.srt"),
            subtitleFile("[Selected] Show - 23.ja.srt", "/old.srt"),
            subtitleFile("[Selected] Show - 24.ja.srt", "/chosen.srt")
        };
        QCOMPARE(client.selectFile(files).name, QString("[Selected] Show - 24.ja.srt"));
        QCOMPARE(client.selectFile(files, true).name, QString("[Selected] Show - 24.ja.srt"));

        client.m_selectedLink = client.m_activeLink;
        client.setBrowseFiles(client.usableFiles(files));
        const int index = client.preferredFileIndex(24);
        QVERIFY(index >= 0);
        QCOMPARE(client.m_browseFiles.at(index).name, QString("[Selected] Show - 24.ja.srt"));

        QJsonArray ambiguous = files;
        ambiguous.append(subtitleFile("[Selected] Show - 24 [1234ABCD].ja.srt", "/duplicate.srt"));
        QVERIFY(client.selectFile(ambiguous).name.isEmpty());
        client.setBrowseFiles(client.usableFiles(ambiguous));
        QCOMPARE(client.preferredFileIndex(24), -1);
    }
};

QTEST_MAIN(JimakuClientTest)

#include "test_jimaku.moc"
