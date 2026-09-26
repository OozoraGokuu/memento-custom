#include <QFile>
#include <QDir>
#include "state/context.h"
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QStandardPaths>
#include <QUuid>
#include <clocale>
#include <QTest>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QNetworkReply>
#include <QCryptographicHash>
#include "jimaku/kitsunekkoclient.h"

class KitsunekkoClientTest : public QObject
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
        KitsunekkoClient client(&context);
        client.m_entries = {QVariantMap{{"name", "Show"}, {"sha", QString(40, 'a')}, {"prefix", "subtitles/anime_tv/Show/"}}};
        client.selectEntry(0, 23);
        const auto link = context.episodeLibrary()->subtitleLinkForFile(file.fileName());
        QCOMPARE(link.value("name").toString(), QString("Show"));
        QCOMPARE(link.value("provider").toString(), QString("kitsunekko")); QCOMPARE(link.value("sha").toString(), QString(40, 'a')); QCOMPARE(link.value("prefix").toString(), QString("subtitles/anime_tv/Show/"));
        client.cancel();
        player.state()->setPath("/outside.mkv");
        client.selectEntry(0, 23); client.cancel();
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(file.fileName()), link);
        context.setPlayer(nullptr);
    }


    void linkedTitleBypassesCatalog()
    {
        KitsunekkoClient client(nullptr);
        QSignalSpy failed(&client, &KitsunekkoClient::failed);
        client.openLinkedEntry({{"provider", "kitsunekko"}, {"name", "Show"},
            {"sha", QString(40, 'a')}, {"prefix", "subtitles/anime_tv/Show/"}}, 23);
        QVERIFY(client.m_catalog.isEmpty());
        QVERIFY(client.m_reply);
        QVERIFY(client.m_reply->url().path().endsWith("/git/trees/" + QString(40, 'a')));
        QCOMPARE(client.selectedEpisode(), 23);
        QCOMPARE(client.selectedEntryName(), QString("Show"));
        client.cancel();
        client.openLinkedEntry({{"provider", "kitsunekko"}, {"sha", "bad"}}, 23);
        QCOMPARE(failed.size(), 1);
        QVERIFY(!client.busy());
    }
    void fileFiltering()
    {
        QJsonArray files;
        for (const QString name : {"Show - 01.ass", "Show - 02.srt", "Show [EP002].srt", "untagged.ssa", "Show - 01.zip", "../bad.srt"}) {
            files.append(QJsonObject{{"path", name}, {"type", "blob"}, {"size", 100}, {"sha", QString(40, 'a')}});
        }
        files.append(QJsonObject{{"path", "huge.srt"}, {"type", "blob"}, {"size", 20000000}, {"sha", QString(40, 'b')}});
        const auto data = QJsonDocument(QJsonObject{{"tree", files}}).toJson();
        auto result = KitsunekkoClient::parseFiles(data, "subtitles/anime_tv/Show/", 1);
        QCOMPARE(result.size(), 2);
        QCOMPARE(result[0].toMap().value("name").toString(), QString("Show - 01.ass"));
        QCOMPARE(result[1].toMap().value("episode").toInt(), -1);
        QCOMPARE(KitsunekkoClient::parseFiles(data, "subtitles/anime_tv/Show/", -1).size(), 4);
    }
    void matchingAndCancellation()
    {
        KitsunekkoClient client(nullptr);
        client.m_catalog = {QVariantMap{{"name", "Example Show"}}, QVariantMap{{"name", "Other Show"}}};
        client.search("ＥＸＡＭＰＬＥ show");
        QCOMPARE(client.searchResults().size(), 1);
        QVERIFY(!client.busy());
        const auto before = client.m_generation;
        client.cancel();
        QVERIFY(client.m_generation > before);
        QCOMPARE(client.searchResults().size(), 1);
        client.clearSearch();
        QVERIFY(client.searchResults().isEmpty());
    }
    void refuseAttachWithoutCurrentVideo()
    {
        KitsunekkoClient client(nullptr);
        client.m_files = {QVariantMap{{"name", "Show - 01.srt"}}};
        QSignalSpy failed(&client, &KitsunekkoClient::failed);
        client.attachResult(0);
        QCOMPARE(failed.size(), 1);
        QVERIFY(!client.busy());
    }
    void onlineCatalogAndDownload()
    {
        if (!qEnvironmentVariableIsSet("MEMENTO_TEST_ONLINE")) QSKIP("Optional live provider test");
        KitsunekkoClient client(nullptr);
        QSignalSpy failed(&client, &KitsunekkoClient::failed);
        client.refreshCatalog("Naruto");
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 60000);
        QVERIFY2(failed.isEmpty(), qPrintable(client.status()));
        QVERIFY(!client.searchResults().isEmpty());
        client.selectEntry(0, 1);
        QTRY_VERIFY_WITH_TIMEOUT(!client.busy(), 30000);
        QVERIFY2(failed.isEmpty(), qPrintable(client.status()));
        QVERIFY(!client.fileResults().isEmpty());
        const auto file = client.fileResults().first().toMap();
        QNetworkAccessManager manager;
        auto *reply = manager.get(QNetworkRequest(QUrl("https://raw.githubusercontent.com/Ajatt-Tools/kitsunekko-mirror/main/" +
            QString::fromLatin1(QUrl::toPercentEncoding(file.value("path").toString(), "/")))));
        QSignalSpy finished(reply, &QNetworkReply::finished);
        QVERIFY(finished.wait(30000));
        QCOMPARE(reply->error(), QNetworkReply::NoError);
        const auto data = reply->readAll();
        QVERIFY(data.size() > 100);
        const auto blob = QByteArray("blob ") + QByteArray::number(data.size()) + '\0' + data;
        QCOMPARE(QString::fromLatin1(QCryptographicHash::hash(blob, QCryptographicHash::Sha1).toHex()), file.value("sha").toString());
        reply->deleteLater();
    }
};
QTEST_MAIN(KitsunekkoClientTest)
#include "test_kitsunekko.moc"
