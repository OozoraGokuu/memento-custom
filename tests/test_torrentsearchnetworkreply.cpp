#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include "torrentsearch/torrentsearchnetworkreply.h"

class TorrentSearchNetworkReplyTest : public QObject
{
    Q_OBJECT
private slots:
    void fallbackOnResolverFailure()
    {
        TorrentSearchNetworkReply reply(QNetworkRequest(QUrl("https://provider.example/")), true);
        QSignalSpy routes(&reply, &TorrentSearchNetworkReply::routeChanged);
        reply.m_runner = [](const auto &, bool doh, const auto &) {
            TorrentSearchNetworkReply::Response r;
            if (doh) { r.error = QNetworkReply::HostNotFoundError; r.retryable = true; }
            else { r.status = 200; r.body = "rss"; }
            return r;
        };
        QTRY_VERIFY(reply.isFinished());
        QCOMPARE(routes.count(), 2);
        QCOMPARE(reply.error(), QNetworkReply::NoError);
        QCOMPARE(reply.readAll(), QByteArray("rss"));
        QVERIFY(reply.property("mementoSystemDns").toBool());
    }
    void httpErrorsDoNotRetry()
    {
        TorrentSearchNetworkReply reply(QNetworkRequest(QUrl("https://provider.example/")), true);
        QSignalSpy routes(&reply, &TorrentSearchNetworkReply::routeChanged);
        reply.m_runner = [](const auto &, bool, const auto &) {
            TorrentSearchNetworkReply::Response r;
            r.status = 403; r.error = QNetworkReply::ContentAccessDenied;
            return r;
        };
        QTRY_VERIFY(reply.isFinished());
        QCOMPARE(routes.count(), 1);
        QCOMPARE(reply.error(), QNetworkReply::ContentAccessDenied);
    }
    void cancelBeforeStart()
    {
        TorrentSearchNetworkReply reply(QNetworkRequest(QUrl("https://provider.example/")), true);
        QSignalSpy routes(&reply, &TorrentSearchNetworkReply::routeChanged);
        QSignalSpy finished(&reply, &QNetworkReply::finished);
        reply.abort();
        QCoreApplication::processEvents();
        QCOMPARE(routes.count(), 0);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(reply.error(), QNetworkReply::OperationCanceledError);
    }
    void nativeHttp_data()
    {
        QTest::addColumn<QByteArray>("response");
        QTest::addColumn<int>("error");
        QTest::newRow("success") << QByteArray("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nrss") << int(QNetworkReply::NoError);
        QTest::newRow("not found") << QByteArray("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n") << int(QNetworkReply::ContentNotFoundError);
        QTest::newRow("cross-origin redirect") << QByteArray("HTTP/1.1 302 Found\r\nLocation: https://example.com/\r\nContent-Length: 0\r\n\r\n") << int(QNetworkReply::ProtocolInvalidOperationError);
    }
    void nativeHttp()
    {
        QFETCH(QByteArray, response);
        QFETCH(int, error);
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, socket, [socket, response] {
                socket->readAll(); socket->write(response); socket->disconnectFromHost();
            });
        });
        TorrentSearchNetworkReply reply(QNetworkRequest(QUrl(QString("http://127.0.0.1:%1/").arg(server.serverPort()))), false);
        QTRY_VERIFY_WITH_TIMEOUT(reply.isFinished(), 5000);
        QCOMPARE(int(reply.error()), error);
        QVERIFY(reply.property("mementoSystemDns").toBool());
        if (error == 0) QCOMPARE(reply.readAll(), QByteArray("rss"));
    }
    void liveCloudflareAndFallback_data()
    {
        QTest::addColumn<bool>("breakResolver");
        QTest::newRow("cloudflare") << false;
        QTest::newRow("broken resolver falls back") << true;
    }
    void liveCloudflareAndFallback()
    {
        if (!qEnvironmentVariableIsSet("MEMENTO_TEST_ONLINE")) QSKIP("Opt-in live network test");
        QFETCH(bool, breakResolver);
        TorrentSearchNetworkReply reply(QNetworkRequest(QUrl("https://provider.example/?page=rss&q=Example&c=1_2&f=0")), true);
        if (breakResolver) reply.m_runner = [](const auto &req, bool doh, const auto &cancel) {
            return TorrentSearchNetworkReply::transfer(req, doh, cancel, "https://127.0.0.1:9/dns-query");
        };
        QTRY_VERIFY_WITH_TIMEOUT(reply.isFinished(), 60000);
        QCOMPARE(reply.property("mementoSystemDns").toBool(), breakResolver);
        if (breakResolver) {
            // System DNS may itself be blocked. Verify the fallback matches a
            // direct system-DNS request, including its actual network error.
            TorrentSearchNetworkReply system(reply.request(), false);
            QTRY_VERIFY_WITH_TIMEOUT(system.isFinished(), 30000);
            QCOMPARE(reply.error(), system.error());
            QCOMPARE(reply.attribute(QNetworkRequest::HttpStatusCodeAttribute),
                system.attribute(QNetworkRequest::HttpStatusCodeAttribute));
        } else {
            QVERIFY2(reply.error() == QNetworkReply::NoError, qPrintable(reply.errorString()));
            QCOMPARE(reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
            QVERIFY(reply.readAll().contains("<rss"));
        }
    }
};
QTEST_GUILESS_MAIN(TorrentSearchNetworkReplyTest)
#include "test_torrentsearchnetworkreply.moc"
