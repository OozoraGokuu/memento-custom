#include <QTest>

#include <QCryptographicHash>
#include <QStandardPaths>
#include <QSettings>
#include <QUuid>
#include <QTcpServer>
#include <QTcpSocket>
#include "util/directoryutils.h"

#include "torrentsearch/torrentsearchclient.h"

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

QByteArray torrentWithOversizedAggregatePaths()
{
    constexpr int EXTRA_FILE_COUNT = 9000;
    const QByteArray component(200, 'a');
    QByteArray info;
    info.reserve(10 * 1024 * 1024);
    info += "d5:filesl";
    info += "d6:lengthi1e4:pathl9:first.mkvee";
    for (int index = 0; index < EXTRA_FILE_COUNT; ++index)
    {
        const QByteArray fileName = QByteArray::number(index) + '-' +
            QByteArray(190, 'b') + ".mkv";
        info += "d6:lengthi1e4:pathl";
        for (int componentIndex = 0; componentIndex < 4; ++componentIndex)
        {
            info += QByteArray::number(component.size()) + ':' + component;
        }
        info += QByteArray::number(fileName.size()) + ':' + fileName + "ee";
    }
    info += "e4:name4:pack12:piece lengthi16384e6:pieces20:";
    info.append(QByteArray(20, '\0'));
    info += 'e';
    return QByteArrayLiteral("d4:info") + info + QByteArrayLiteral("e");
}

} // namespace

class TorrentSearchClientTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        // Native Windows QSettings needs an organization, as configured by
        // main.cpp. Use a unique test application to avoid any user profile.
        QCoreApplication::setOrganizationName("MementoTests");
        QCoreApplication::setApplicationName("torrent-search-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
    }
    void cleanupTestCase() { QSettings().clear(); }
    void init() {
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
        QSettings settings(DirectoryUtils::getCacheConfig(), QSettings::NativeFormat);
#else
        QSettings settings;
#endif
        settings.remove("torrentsearch/provider-url");
    }
    void parsesNamespacedRssAndBuildsSafeProviderUrls()
    {
        TorrentSearchClient client;
        client.setBaseUrl(QUrl("https://provider.example"));
        const QByteArray feed(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
            "<rss xmlns:torrentsearch=\"https://provider.example/xmlns/torrentsearch\"><channel><item>"
            "<title>Example Release</title>"
            "<link>https://provider.example/download/123.torrent</link>"
            "<guid>https://provider.example/view/123</guid>"
            "<torrentsearch:category>Anime - English-translated</torrentsearch:category>"
            "<torrentsearch:size>1.2 GiB</torrentsearch:size>"
            "<torrentsearch:infoHash>0123456789abcdef0123456789abcdef01234567</torrentsearch:infoHash>"
            "<torrentsearch:seeders>42</torrentsearch:seeders>"
            "<torrentsearch:leechers>7</torrentsearch:leechers>"
            "<torrentsearch:downloads>99</torrentsearch:downloads>"
            "<torrentsearch:trusted>Yes</torrentsearch:trusted>"
            "<torrentsearch:remake>No</torrentsearch:remake>"
            "</item></channel></rss>"
        );

        QVector<TorrentSearchClient::Result> results;
        QString error;
        QVERIFY2(client.parseFeed(feed, &results, &error),
            qPrintable(error));
        QCOMPARE(results.size(), 1);
        const TorrentSearchClient::Result &result = results.front();
        QCOMPARE(result.id, qint64{123});
        QCOMPARE(result.title, QStringLiteral("Example Release"));
        QCOMPARE(result.seeders, 42);
        QCOMPARE(result.leechers, 7);
        QCOMPARE(result.downloads, 99);
        QVERIFY(result.trusted);
        QVERIFY(!result.remake);
        QCOMPARE(result.detailUrl,
            QUrl(QStringLiteral("https://provider.example/view/123")));
        QCOMPARE(result.torrentUrl,
            QUrl(QStringLiteral("https://provider.example/download/123.torrent")));
        QVERIFY(result.magnet.startsWith(QStringLiteral("magnet:?")));
        QVERIFY(result.magnet.contains(QStringLiteral("urn:btih:")));
    }

    void rejectsMalformedRss()
    {
        TorrentSearchClient client;
        client.setBaseUrl(QUrl("https://provider.example"));
        QVector<TorrentSearchClient::Result> results;
        QString error;
        QVERIFY(!client.parseFeed(
            QByteArrayLiteral("<rss><channel><item>"), &results, &error));
        QVERIFY(!error.isEmpty());
    }

    void requiresUserProviderAndRejectsUnsafeUrls()
    {
        TorrentSearchClient client;
        QVERIFY(client.baseUrl().isEmpty());
        client.search("query");
        QVERIFY(!client.busy());
        QVERIFY(client.status().contains("URL"));
        QVERIFY(client.configureProvider(QUrl("https://provider.example/custom/rss?token=public")));
        QVERIFY(client.providerUrl(QUrl("https://provider.example/files/episode.torrent")));
        QVERIFY(!client.providerUrl(QUrl("https://other.example/files/episode.torrent")));
        QVERIFY(!client.providerUrl(QUrl("http://provider.example/files/episode.torrent")));
        QVERIFY(!client.providerUrl(QUrl("https://user:password@provider.example/file.torrent")));
        QVERIFY(!client.configureProvider(QUrl("file:///tmp/feed")));
        QCOMPARE(client.baseUrl().path(), QStringLiteral("/custom/rss"));
        TorrentSearchClient restored;
        QCOMPARE(restored.baseUrl(), client.baseUrl());
        QVERIFY(client.configureProvider(QUrl()));
        QVERIFY(client.baseUrl().isEmpty());
    }

    void preservesGenericRssEnclosuresAndNonNumericIds()
    {
        TorrentSearchClient client;
        client.setBaseUrl(QUrl("https://provider.example/custom/rss"));
        const QByteArray feed(
            "<rss><channel><item><title>Episode</title>"
            "<link>https://provider.example/episodes/alpha</link>"
            "<guid isPermaLink=\"false\">unique-episode</guid>"
            "<enclosure url=\"/get/alpha?format=torrent\" type=\"application/x-bittorrent\"/>"
            "</item></channel></rss>");
        QVector<TorrentSearchClient::Result> results;
        QString error;
        QVERIFY2(client.parseFeed(feed, &results, &error), qPrintable(error));
        QCOMPARE(results.size(), 1);
        QCOMPARE(results[0].torrentUrl, QUrl("https://provider.example/get/alpha?format=torrent"));
        QCOMPARE(results[0].detailUrl, QUrl("https://provider.example/episodes/alpha"));
        QVERIFY(!client.parseFeed("<html><body>Search page</body></html>", &results, &error));
        QVERIFY(error.contains("RSS"));
    }

    void searchesUserEndpointAndPreservesCustomParameters()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        QByteArray received;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                received += socket->readAll();
                if (!received.contains("\r\n\r\n")) return;
                const QByteArray body("<rss><channel><item><title>Test episode</title><enclosure url=\"/episode.torrent\" type=\"application/x-bittorrent\"/></item></channel></rss>");
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        TorrentSearchClient client;
        client.setCloudflareDns(false);
        client.setBaseUrl(QUrl(QString("http://127.0.0.1:%1/custom/feed?category=raw&q=old").arg(server.serverPort())));
        client.search("Example episode");
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 15000);
        QCOMPARE(client.results().size(), 1);
        QVERIFY(received.startsWith("GET /custom/feed?category=raw&q=Example%20episode "));
        client.setBaseUrl(QUrl("https://new.example/feed"));
        QVERIFY(client.results().isEmpty());
        QVERIFY(client.m_cache.isEmpty());
    }

    void discoversAdvertisedRssWithoutLeavingTheWebsite()
    {
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        int requests = 0;
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                const QByteArray request = socket->readAll();
                ++requests;
                const QByteArray body = request.startsWith("GET /feed?") ?
                    QByteArray("<rss><channel><item><title>Discovered episode</title><enclosure url='/episode.torrent' type='application/x-bittorrent'/></item></channel></rss>") :
                    QByteArray("<!DOCTYPE html><html><head><link rel='alternate' type='application/rss+xml' href='/feed?language=ja&amp;format=rss'></head><body>Website</body></html>");
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        TorrentSearchClient client;
        client.setCloudflareDns(false);
        const QUrl website(QString("http://127.0.0.1:%1/").arg(server.serverPort()));
        client.setBaseUrl(website);
        QCOMPARE(client.discoverFeed("<link href='/feed?one=1&amp;two=2' type='application/rss+xml'>", website),
            website.resolved(QUrl("/feed?one=1&two=2")));
        QVERIFY(client.discoverFeed("<link type='application/rss+xml' href='https://other.example/feed'>", website).isEmpty());
        QVERIFY(client.discoverFeed("<html>No feed is advertised</html>", website).isEmpty());
        client.search("Example");
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 15000);
        QCOMPARE(requests, 2);
        QCOMPARE(client.results().size(), 1);
        QCOMPARE(client.baseUrl(), website); // Keep the user's website setting.
    }

    void validatesTorrentMetadataAndAdvertisedInfoHash()
    {
        QByteArray infoHash;
        const QByteArray data = validTorrent(&infoHash);
        QString error;

        QVERIFY2(TorrentSearchClient::validateTorrentData(
            data, QString::fromLatin1(infoHash), &error), qPrintable(error));
        QVERIFY2(TorrentSearchClient::validateTorrentData(data, {}, &error), qPrintable(error));

        QVERIFY(!TorrentSearchClient::validateTorrentData(
            data, QString(40, QLatin1Char('0')), &error));
        QVERIFY(error.contains(QStringLiteral("advertised info hash")));
    }

    void rejectsMalformedTorrentMetadata()
    {
        QString error;
        QVERIFY(!TorrentSearchClient::validateTorrentData(
            QByteArrayLiteral("d4:infod3:fooi1eee"), {}, &error));
        QVERIFY(!error.isEmpty());
    }

    void ranksSeedersAndKeepsDownloadIndicesAligned()
    {
        TorrentSearchClient client;
        client.setBaseUrl(QUrl("https://provider.example"));
        TorrentSearchClient::Result low, popular, newerTie, unseeded;
        low.id = 40; low.seeders = 2; low.title = "Low";
        popular.id = 10; popular.seeders = 100; popular.title = "Popular";
        newerTie.id = 30; newerTie.seeders = 100; newerTie.title = "Newer tie";
        unseeded.id = 50; unseeded.seeders = 0; unseeded.title = "Unseeded";
        const QVector<TorrentSearchClient::Result> feed{low, popular, unseeded, newerTie};
        client.setResults(feed);
        QCOMPARE(client.sortOrder(), QStringLiteral("seeders"));
        QCOMPARE(client.m_results[0].id, 30);
        QCOMPARE(client.m_results[1].id, 10);
        QCOMPARE(client.m_results[2].id, 40);
        QCOMPARE(client.m_results[3].id, 50);
        for (int index = 0; index < client.m_results.size(); ++index)
        {
            QCOMPARE(client.results()[index].toMap().value("id").toLongLong(), client.m_results[index].id);
            QCOMPARE(client.results()[index].toMap().value("title").toString(), client.m_results[index].title);
        }
        client.setSortOrder(QStringLiteral("newest"));
        QCOMPARE(client.m_results[0].id, 50);
        QCOMPARE(client.m_results[1].id, 40);
        client.setResults(feed); // Fresh and cached responses respect the active sort.
        QCOMPARE(client.m_results[0].id, 50);
        client.setSortOrder(QStringLiteral("seeders"));
        QCOMPARE(client.m_results[0].id, 30);
        client.setSortOrder(QStringLiteral("invalid"));
        QCOMPARE(client.sortOrder(), QStringLiteral("seeders"));
        client.setResults({});
        QVERIFY(client.results().isEmpty());
    }

    void clearsStaleResultsWhenSearchStarts()
    {
        TorrentSearchClient client;
        client.setBaseUrl(QUrl("https://provider.example"));
        TorrentSearchClient::Result stale;
        stale.id = 1;
        stale.title = QStringLiteral("Stale result");
        client.setResults(QVector<TorrentSearchClient::Result>{stale});
        QCOMPARE(client.results().size(), 1);

        client.search(QStringLiteral("new query"));
        QVERIFY(client.results().isEmpty());
        client.cancel();
    }

    void rejectsTorrentWithExcessiveFileCount()
    {
        QString error;
        QVERIFY(!TorrentSearchClient::validateTorrentData(
            torrentWithFileCount(10001), {}, &error));
        QVERIFY(error.contains(QStringLiteral("too many files")));
    }

    void rejectsTorrentWhenLaterPathsExceedAggregateLimit()
    {
        QString error;
        const QByteArray data = torrentWithOversizedAggregatePaths();
        QVERIFY(data.size() < 16 * 1024 * 1024);
        QVERIFY(!TorrentSearchClient::validateTorrentData(data, {}, &error));
        QVERIFY(error.contains(QStringLiteral("oversized file paths")));
    }
};

QTEST_GUILESS_MAIN(TorrentSearchClientTest)

#include "test_torrentsearch.moc"
