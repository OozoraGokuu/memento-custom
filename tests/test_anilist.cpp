#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QFile>
#include <QUuid>
#include "anilist/anilistclient.h"
#include "quick/episodefolder.h"
#include "util/directoryutils.h"

class AniListClientTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    void prepare(AniListClient &client) {
        client.m_configPath = m_dir.filePath("anilist.json");
        client.m_config = {{"client_id", "123"}, {"access_token", "test-only"},
            {"user_id", 7}, {"username", "Test"}, {"enabled", true}, {"threshold", 80},
            {"mappings", QJsonObject{{"show", QJsonObject{{"id", 101}, {"title", "Show"}, {"offset", 0}}}}}};
        client.m_verified = true;
        client.m_file = "/test/Show - 02.mkv";
        client.m_current = {{"key", "show"}, {"title", "Show"}, {"filename", "Show - 02.mkv"}};
    }
    static QJsonObject media(int progress, int total = 12, QString status = "CURRENT") {
        return {{"id", 101}, {"episodes", total},
            {"mediaListEntry", QJsonObject{{"progress", progress}, {"status", status}}}};
    }
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("anilist-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(m_dir.isValid());
        QVERIFY(QDir().mkpath(DirectoryUtils::getConfigDir()));
    }
    void authorizationUrlUsesRegisteredCallback() {
        const QUrl url = AniListClient::authorizationUrl("12345", "test-nonce");
        QCOMPARE(url.scheme(), QString("https"));
        QCOMPARE(url.host(), QString("anilist.co"));
        QCOMPARE(url.path(), QString("/api/v2/oauth/authorize"));
        const QUrlQuery query(url);
        QCOMPARE(query.queryItems().size(), 3);
        QCOMPARE(query.queryItemValue("client_id"), QString("12345"));
        QCOMPARE(query.queryItemValue("response_type"), QString("token"));
        QCOMPARE(query.queryItemValue("state"), QString("test-nonce"));
        QVERIFY(!query.hasQueryItem("redirect_uri"));
    }
    void progressRules() {
        QVERIFY(AniListClient::progressUpdate(media(5), 2).isEmpty());
        QVERIFY(AniListClient::progressUpdate(media(5), 5).isEmpty());
        QVERIFY(AniListClient::progressUpdate(media(0), 13).isEmpty());
        QCOMPARE(AniListClient::progressUpdate(media(0), 12).value("status").toString(), QString("COMPLETED"));
        QCOMPARE(AniListClient::progressUpdate(media(0, 0), 12).value("status").toString(), QString("CURRENT"));
        QVERIFY(!AniListClient::progressUpdate(media(0, 12, "REPEATING"), 2).contains("status"));
        QVERIFY(!AniListClient::progressUpdate(media(0, 12, "COMPLETED"), 2).contains("status"));
    }
    void episodesAndOffsets() {
        QCOMPARE(AniListClient::episodeFromName("Show S02E03.mkv"), 3);
        QCOMPARE(AniListClient::episodeFromName("Show - 13v2 [1080p].mkv"), 13);
        QCOMPARE(AniListClient::episodeFromName("Show [EP002].mkv"), 2);
        QCOMPARE(AniListClient::episodeFromName("Show [1080p] [2026].mkv"), 0);
        QCOMPARE(AniListClient::episodeFromName("Unknown movie.mkv"), 0);
        AniListClient c(nullptr); prepare(c);
        c.m_results = {QVariantMap{{"id", 101}, {"title", "Show"}}};
        c.linkTitle("show", 0, -1);
        QCOMPARE(c.currentEpisode(), 1);
        c.setCurrentEpisode(4); QCOMPARE(c.currentEpisode(), 4);
    }
    void thresholdAndDuplicateSuppression() {
        AniListClient c(nullptr); prepare(c);
        int reads = 0, writes = 0;
        c.m_transport = [&](QString query, QJsonObject vars, QString, AniListClient::Callback done) {
            if (query.startsWith("mutation")) {
                ++writes; QCOMPARE(vars.value("progress").toInt(), 2);
                done({{"SaveMediaListEntry", QJsonObject{{"progress", 2}}}}, {}, false, 0);
            } else { ++reads; done({{"Media", media(0)}}, {}, false, 0); }
        };
        c.observe(79, 100); QCOMPARE(reads, 0);
        c.observe(80, 100); QCOMPARE(reads, 1); QCOMPARE(writes, 1); QCOMPARE(c.pendingCount(), 0);
        c.observe(90, 100); QCOMPARE(writes, 1);
        c.m_confirmed.clear(); c.setEnabled(false); c.observe(99, 100); QCOMPARE(writes, 1);
    }
    void neverLowersRemoteProgress() {
        AniListClient c(nullptr); prepare(c);
        int writes = 0;
        c.m_transport = [&](QString query, QJsonObject, QString, AniListClient::Callback done) {
            if (query.startsWith("mutation")) ++writes;
            done({{"Media", media(8)}}, {}, false, 0);
        };
        c.syncNow(); QCOMPARE(writes, 0); QCOMPARE(c.pendingCount(), 0);
    }
    void retryAndPersistentQueue() {
        AniListClient c(nullptr); prepare(c);
        c.m_transport = [](QString, QJsonObject, QString, AniListClient::Callback done) {
            done({}, "Offline", false, 60);
        };
        c.syncNow(); QCOMPARE(c.pendingCount(), 1); QVERIFY(c.m_retry.isActive());
        c.observe(90, 100); QCOMPARE(c.pendingCount(), 1);
        QFile file(c.m_configPath); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(file.readAll()).object().value("pending").toArray().size(), 1);
#ifdef Q_OS_UNIX
        QVERIFY(!(file.permissions() & QFileDevice::ReadOther));
#endif
        c.m_transport = [](QString query, QJsonObject, QString, AniListClient::Callback done) {
            done(query.startsWith("mutation") ? QJsonObject{{"SaveMediaListEntry", QJsonObject{{"progress", 2}}}} : QJsonObject{{"Media", media(0)}}, {}, false, 0);
        };
        c.retryPending(); QCOMPARE(c.pendingCount(), 0);
    }
    void staleReadsCannotWriteAfterRelinkOrDisconnect() {
        AniListClient c(nullptr); prepare(c);
        AniListClient::Callback read;
        int writes = 0;
        c.m_transport = [&](QString query, QJsonObject, QString, AniListClient::Callback done) {
            if (query.startsWith("mutation")) ++writes; else read = done;
        };
        c.syncNow(); QVERIFY(bool(read));
        c.m_results = {QVariantMap{{"id", 101}, {"title", "Same show"}}};
        c.linkTitle("show", 0, 1); // Same ID, changed episode numbering.
        read({{"Media", media(0)}}, {}, false, 0); QCOMPARE(writes, 0);
        c.syncNow();
        c.disconnectAccount();
        read({{"Media", media(0)}}, {}, false, 0); QCOMPARE(writes, 0);
    }
    void invalidEpisodeDoesNotLoop() {
        AniListClient c(nullptr); prepare(c); c.setCurrentEpisode(13);
        int requests = 0;
        c.m_transport = [&](QString, QJsonObject, QString, AniListClient::Callback done) {
            ++requests; done({{"Media", media(0)}}, {}, false, 0);
        };
        c.observe(80, 100); c.observe(90, 100);
        QCOMPARE(requests, 1); QCOMPARE(c.pendingCount(), 0);
    }
    void changedAccountCannotReplayPending() {
        AniListClient c(nullptr); prepare(c);
        c.m_config.insert("pending", QJsonArray{QJsonObject{{"account", 7}, {"id", 101}, {"episode", 2}, {"key", "show"}}});
        c.m_transport = [](QString query, QJsonObject, QString, AniListClient::Callback done) {
            QVERIFY(query.contains("Viewer"));
            done({{"Viewer", QJsonObject{{"id", 8}, {"name", "Other"}}}}, {}, false, 0);
        };
        c.authenticate("different-test-token"); QCOMPARE(c.pendingCount(), 0); QCOMPARE(c.username(), QString("Other"));
    }
    void playingIdentityIndependentOfLibrarySelection() {
        EpisodeFolder library;
        const QString first = m_dir.filePath("First"), second = m_dir.filePath("Second");
        QDir().mkpath(first); QDir().mkpath(second);
        QFile a(first + "/Show - 01.mkv"), b(second + "/Other - 01.mkv");
        QVERIFY(a.open(QIODevice::WriteOnly)); a.write("test"); a.close();
        QVERIFY(b.open(QIODevice::WriteOnly)); b.write("test"); b.close();
        QVERIFY(library.addFolder(QUrl::fromLocalFile(first)) >= 0);
        const auto identity = library.playbackInfo(a.fileName());
        QVERIFY(!identity.value("key").toString().isEmpty());
        QVERIFY(library.addFolder(QUrl::fromLocalFile(second)) >= 0);
        QCOMPARE(library.playbackInfo(a.fileName()), identity);
        QVERIFY(library.currentEntry().value("id") != identity.value("key"));
    }
    void authorizationRejectsWrongState() {
        AniListClient c(nullptr); prepare(c);
        c.m_authState = "correct-state";
        QVERIFY(c.m_login.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, c.m_login.serverPort());
        QVERIFY(socket.waitForConnected());
        const QByteArray body = "access_token=test&state=wrong";
        socket.write("POST /token HTTP/1.1\r\nHost: 127.0.0.1:47832\r\nOrigin: http://127.0.0.1:47832\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
        QTRY_VERIFY(socket.bytesAvailable() > 0);
        QVERIFY(socket.readAll().contains("400 Bad Request"));
        QCOMPARE(c.m_authState, QString("correct-state"));
    }
    void authorizationAcceptsMatchingState() {
        AniListClient c(nullptr); prepare(c);
        c.m_authState = "correct-state";
        c.m_transport = [](QString query, QJsonObject, QString token, AniListClient::Callback done) {
            QVERIFY(query.contains("Viewer")); QCOMPARE(token, QString("test-token"));
            done({{"Viewer", QJsonObject{{"id", 7}, {"name", "Test"}}}}, {}, false, 0);
        };
        QVERIFY(c.m_login.listen(QHostAddress::LocalHost, 0));
        QTcpSocket socket;
        socket.connectToHost(QHostAddress::LocalHost, c.m_login.serverPort());
        QVERIFY(socket.waitForConnected());
        const QByteArray body = "access_token=test-token&state=correct-state";
        socket.write("POST /token HTTP/1.1\r\nHost: 127.0.0.1:47832\r\nOrigin: http://127.0.0.1:47832\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body);
        QTRY_VERIFY(socket.bytesAvailable() > 0);
        QVERIFY(socket.readAll().contains("200 OK"));
        QVERIFY(c.connected()); QVERIFY(c.m_authState.isEmpty()); QVERIFY(!c.m_login.isListening());
        QCOMPARE(c.m_config.value("access_token").toString(), QString("test-token"));
    }
    void expiredAuthorizationStopsRetries() {
        AniListClient c(nullptr); prepare(c);
        c.m_transport = [](QString, QJsonObject, QString, AniListClient::Callback done) {
            done({}, "Reconnect", true, 60);
        };
        c.syncNow(); QVERIFY(!c.connected()); QVERIFY(!c.m_retry.isActive());
        QCOMPARE(c.pendingCount(), 1);
    }
    void disablingDuringReadPreventsMutation() {
        AniListClient c(nullptr); prepare(c);
        AniListClient::Callback read;
        int writes = 0;
        c.m_transport = [&](QString query, QJsonObject, QString, AniListClient::Callback done) {
            if (query.startsWith("mutation")) ++writes; else read = done;
        };
        c.syncNow(); QVERIFY(bool(read));
        c.setEnabled(false); read({{"Media", media(0)}}, {}, false, 0);
        QCOMPARE(writes, 0); QCOMPARE(c.pendingCount(), 1);
    }
    void livePublicSearch() {
        if (!qEnvironmentVariableIsSet("MEMENTO_TEST_ONLINE")) QSKIP("Opt-in public API read only");
        AniListClient c(nullptr);
        c.search("Example Show");
        QTRY_VERIFY_WITH_TIMEOUT(!c.results().isEmpty() || c.status().contains("failed"), 30000);
        QVERIFY2(!c.results().isEmpty(), qPrintable(c.status()));
    }
};
QTEST_GUILESS_MAIN(AniListClientTest)
#include "test_anilist.moc"
