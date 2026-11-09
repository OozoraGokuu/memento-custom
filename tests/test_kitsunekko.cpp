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
#include "jimaku/subtitlerelease.h"

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
        auto locked = link;
        locked.insert("releaseRule", SubtitleRelease::fromFile("[Chosen] Show - 23.ja.ass", 23));
        locked.insert("preferredFile", "[Chosen] Show - 23.ja.ass");
        locked.insert("episodeOffset", 0);
        locked.insert("autoLoad", false);
        QVERIFY(context.episodeLibrary()->setSubtitleLinkForFile(file.fileName(), locked));
        client.selectEntry(0, 23); client.cancel();
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(file.fileName()), locked);
        client.openLinkedEntry(locked, 24); client.cancel();
        QCOMPARE(client.m_entryLink, locked);
        client.m_entries = {QVariantMap{{"name", "Other title"}, {"sha", QString(40, 'b')},
            {"prefix", "subtitles/anime_tv/Other/"}}};
        client.selectEntry(0, 23); client.cancel();
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(file.fileName()), locked);
        QCOMPARE(client.m_entryLink.value("autoLoad").toBool(), false);
        QVERIFY(client.m_entryLink.value("releaseRule").toMap().isEmpty());
        player.state()->setPath("/outside.mkv");
        client.selectEntry(0, 23); client.cancel();
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(file.fileName()), locked);
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
        const QVariantMap link = {{"provider", "kitsunekko"}, {"name", "Chosen Show"}, {"sha", QString(40, 'a')}, {"prefix", "subtitles/anime_tv/Chosen/"},
            {"preferredFile", "[Chosen] Show - 23.ja.ass"},
            {"releaseRule", SubtitleRelease::fromFile("[Chosen] Show - 23.ja.ass", 23)}};
        QVERIFY(context.episodeLibrary()->setSubtitleLinkForFile(folder.filePath("Show - 23.mkv"), link));
        const QString next = folder.filePath("Show - 24.mkv");
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(next), link);
        player.state()->setPath(next);
        KitsunekkoClient client(&context);
        client.m_catalog = {link};
        client.m_catalogFetched = QDateTime::currentDateTime();
        client.fetchLinkedForCurrentMedia();
        QVERIFY(client.m_reply);
        QVERIFY(client.m_reply->url().path().endsWith("/git/trees/" + QString(40, 'a')));
        QCOMPARE(client.selectedEpisode(), 24);
        QCOMPARE(client.m_mediaPath, next);
        QCOMPARE(client.selectedEntryName(), QString("Chosen Show"));
        client.cancel();
        context.setPlayer(nullptr);
    }

    void currentCatalogRefreshKeepsChosenRelease()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        QFile video(folder.filePath("Show - 24.mkv"));
        QVERIFY(video.open(QIODevice::WriteOnly)); video.write("test"); video.close();
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        player.state()->setPath(video.fileName());
        const QVariantMap link{{"provider", "kitsunekko"}, {"name", "Chosen Show"},
            {"sha", QString(40, 'a')}, {"prefix", "subtitles/anime_tv/Chosen/"},
            {"preferredFile", "[Chosen] Show - 23.ja.ass"}, {"episodeOffset", 0},
            {"releaseRule", SubtitleRelease::fromFile("[Chosen] Show - 23.ja.ass", 23)}};
        QVERIFY(context.episodeLibrary()->setSubtitleLinkForFile(video.fileName(), link));
        KitsunekkoClient client(&context);
        auto current = link;
        current.insert("sha", QString(40, 'b'));
        client.m_catalog = {current};
        client.m_catalogFetched = QDateTime::currentDateTime();
        client.fetchLinkedForCurrentMedia();
        QVERIFY(client.m_reply);
        QVERIFY(client.m_reply->url().path().endsWith("/git/trees/" + QString(40, 'b')));
        QCOMPARE(client.m_entryLink.value("releaseRule"), link.value("releaseRule"));
        QCOMPARE(client.m_entryLink.value("preferredFile"), link.value("preferredFile"));
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(video.fileName()), link);
        client.cancel();

        // Missing new episodes retry the current catalog once, preserving the
        // release while following the exact same directory, not similar titles.
        client.resolveLinkedCatalog(link, 24, true);
        QVERIFY(client.m_reply);
        QVERIFY(client.m_reply->url().path().endsWith("/git/trees/main"));
        current.insert("sha", QString(40, 'c'));
        client.m_catalogBuild = {current};
        client.m_categories.clear();
        client.loadCategory(0);
        QVERIFY(client.m_reply);
        QVERIFY(client.m_reply->url().path().endsWith("/git/trees/" + QString(40, 'c')));
        QCOMPARE(client.m_entryLink.value("releaseRule"), link.value("releaseRule"));
        QCOMPARE(client.m_entryLink.value("preferredFile"), link.value("preferredFile"));
        QCOMPARE(client.selectedEpisode(), 24);
        client.cancel();
        context.setPlayer(nullptr);
    }

    void automaticAttachmentRequiresChosenRelease()
    {
        const QVariantMap first{{"name", "[Chosen] Show - 23.ja.ass"}},
            next{{"name", "[Chosen] Show - 24.ja.ass"}},
            other{{"name", "[Other] Show - 24.ja.ass"}},
            unknown{{"name", "untagged.ass"}};
        const auto rule = SubtitleRelease::fromFile(first.value("name").toString(), 23);
        QVERIFY(rule.value("valid").toBool());
        QCOMPARE(KitsunekkoClient::automaticFileIndex({first, other, next}, 24, rule), 2);
        QCOMPARE(KitsunekkoClient::automaticFileIndex({first, other}, 24, rule), -1);
        QCOMPARE(KitsunekkoClient::automaticFileIndex({next, next}, 24, rule), -1);
        QCOMPARE(KitsunekkoClient::automaticFileIndex({next}, 24, {}), -1);
        QCOMPARE(KitsunekkoClient::automaticFileIndex({unknown}, -1, rule), -1);
        QCOMPARE(KitsunekkoClient::automaticFileIndex({other, next}, 24, rule,
            other.value("name").toString()), 0);
        QCOMPARE(KitsunekkoClient::automaticFileIndex({next}, 24, rule,
            other.value("name").toString()), -1);
    }

    void legacyLinkNeedsAnExplicitRelease()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        QFile video(folder.filePath("Show - 24.mkv"));
        QVERIFY(video.open(QIODevice::WriteOnly)); video.write("test"); video.close();
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        player.state()->setPath(video.fileName());
        QVERIFY(context.episodeLibrary()->setSubtitleLinkForFile(video.fileName(),
            {{"provider", "kitsunekko"}, {"name", "Show"}, {"sha", QString(40, 'a')},
             {"prefix", "subtitles/anime_tv/Show/"}}));
        KitsunekkoClient client(&context);
        client.fetchLinkedForCurrentMedia();
        QVERIFY(!client.m_reply);
        QVERIFY(!client.busy());
        QVERIFY(client.status().contains("Choose a subtitle file once"));
        context.setPlayer(nullptr);
    }

    void releaseIsRememberedOnlyAfterConfirmedAttachment()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        QFile video(folder.filePath("Show - 23.mkv"));
        QVERIFY(video.open(QIODevice::WriteOnly)); video.write("test"); video.close();
        QFile subtitle(folder.filePath("[Chosen] Show - 24.ja.ass"));
        QVERIFY(subtitle.open(QIODevice::WriteOnly)); subtitle.write("subtitle"); subtitle.close();
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        player.state()->setPath(video.fileName());
        KitsunekkoClient client(&context);
        client.m_entryLink = {{"provider", "kitsunekko"}, {"name", "Show"},
            {"sha", QString(40, 'a')}, {"prefix", "subtitles/anime_tv/Show/"}};
        client.m_entryName = "Show";
        client.m_mediaPath = video.fileName();
        client.m_episode = 24;
        client.m_pendingFile = {{"name", QFileInfo(subtitle).fileName()}, {"sha", QString(40, 'b')}};
        client.m_pendingPath = QFileInfo(subtitle).canonicalFilePath();
        client.checkAttached();
        QVERIFY(context.episodeLibrary()->subtitleSelectionForFile(video.fileName()).isEmpty());
        QVERIFY(context.episodeLibrary()->subtitleLinkForFile(video.fileName()).isEmpty());
        auto *track = new MpvTrack;
        track->setId(3);
        track->setExternal(true);
        track->setExternalFilename(subtitle.fileName());
        player.state()->setSubtitleTracks({track});
        QSignalSpy attached(&client, &KitsunekkoClient::subtitleAttached);
        client.checkAttached();
        QCOMPARE(attached.size(), 0);
        QVERIFY(context.episodeLibrary()->subtitleSelectionForFile(video.fileName()).isEmpty());
        player.state()->setSid(3);
        client.checkAttached();
        QCOMPARE(attached.size(), 1);
        const auto link = context.episodeLibrary()->subtitleLinkForFile(video.fileName());
        QCOMPARE(link.value("preferredFile").toString(), QFileInfo(subtitle).fileName());
        QCOMPARE(link.value("episodeOffset").toInt(), 1);
        QVERIFY(SubtitleRelease::matches("[Chosen] Show - 25.ja.ass", 25,
                                        link.value("releaseRule").toMap()));
        const auto selection = context.episodeLibrary()->subtitleSelectionForFile(video.fileName());
        QCOMPARE(selection.value("filename").toString(), QFileInfo(subtitle).fileName());
        QCOMPARE(selection.value("cachePath").toString(), QFileInfo(subtitle).canonicalFilePath());
        QCOMPARE(selection.value("episode").toInt(), 24);
        QCOMPARE(selection.value("fileSha").toString(), QString(40, 'b'));
        context.setPlayer(nullptr);
    }

    void exactEpisodeRestoresItsOriginalTitleAndRelease()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        QFile video(folder.filePath("Show - 24.mkv"));
        QVERIFY(video.open(QIODevice::WriteOnly)); video.write("test"); video.close();
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        player.state()->setPath(video.fileName());
        QVariantMap original{{"provider", "kitsunekko"}, {"name", "Original title"},
            {"sha", QString(40, 'a')}, {"prefix", "subtitles/anime_tv/Original/"},
            {"preferredFile", "[Original] Show - 24.ass"},
            {"releaseRule", SubtitleRelease::fromFile("[Original] Show - 24.ass", 24)}};
        auto selection = original;
        selection.insert("filename", "[Original] Show - 24.ass");
        selection.insert("episode", 24);
        selection.insert("fileSha", QString(40, 'c'));
        QVERIFY(context.episodeLibrary()->rememberSubtitleSelection(video.fileName(), original, selection));
        QVariantMap newer{{"provider", "kitsunekko"}, {"name", "New title"},
            {"sha", QString(40, 'b')}, {"prefix", "subtitles/anime_tv/New/"},
            {"preferredFile", "[New] Show - 25.ass"},
            {"releaseRule", SubtitleRelease::fromFile("[New] Show - 25.ass", 25)}};
        QVERIFY(context.episodeLibrary()->setSubtitleLinkForFile(video.fileName(), newer));
        KitsunekkoClient client(&context);
        client.m_catalog = {newer};
        client.m_catalogFetched = QDateTime::currentDateTime();
        client.fetchLinkedForCurrentMedia();
        QVERIFY(client.m_reply);
        QVERIFY(client.m_reply->url().path().endsWith("/git/trees/" + QString(40, 'a')));
        QCOMPARE(client.selectedEntryName(), QString("Original title"));
        client.m_files = {QVariantMap{{"name", "[New] Show - 24.ass"}},
                          QVariantMap{{"name", "[Original] Show - 24.ass"}}};
        QCOMPARE(client.preferredFileIndex(24), 1);
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(video.fileName()), newer);
        client.cancel();
        QFile subtitle(folder.filePath("[Original] Show - 24.ass"));
        QVERIFY(subtitle.open(QIODevice::WriteOnly)); subtitle.write("subtitle"); subtitle.close();
        client.m_pendingPath = QFileInfo(subtitle).canonicalFilePath();
        client.m_pendingFile = {{"name", QFileInfo(subtitle).fileName()}, {"sha", QString(40, 'c')}};
        client.m_pendingAutomatic = true;
        client.m_pendingSelection = selection;
        auto *track = new MpvTrack;
        track->setId(3);
        track->setExternal(true);
        track->setExternalFilename(subtitle.fileName());
        player.state()->setSubtitleTracks({track});
        player.state()->setSid(3);
        client.checkAttached();
        QCOMPARE(context.episodeLibrary()->subtitleLinkForFile(video.fileName()), newer);
        const auto restored = context.episodeLibrary()->subtitleSelectionForFile(video.fileName());
        QCOMPARE(restored.value("filename").toString(), QString("[Original] Show - 24.ass"));
        QCOMPARE(restored.value("releaseRule").toMap(), original.value("releaseRule").toMap());
        QCOMPARE(restored.value("sha").toString(), QString(40, 'a'));
        context.setPlayer(nullptr);
    }

    void videoChangeInvalidatesInFlightLookupEvenIfItReturns()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        player.state()->setPath("/video/Show - 23.mkv");
        KitsunekkoClient client(&context);
        client.m_entries = {QVariantMap{{"name", "Show"}, {"sha", QString(40, 'a')},
            {"prefix", "subtitles/anime_tv/Show/"}}};
        client.selectEntry(0, 23);
        const auto generation = client.m_generation;
        QVERIFY(client.m_reply);
        player.state()->setPath("/video/Show - 24.mkv");
        player.state()->setPath("/video/Show - 23.mkv");
        QVERIFY(client.m_generation > generation);
        QVERIFY(!client.m_reply);
        QVERIFY(!client.busy());
        QVERIFY(client.m_mediaPath.isEmpty());
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
        client.m_catalogFetched = QDateTime::currentDateTime();
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
