#include <QTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <clocale>
#include <limits>

#include "jimaku/subtitlerelease.h"
#include "player/mpvtrack.h"
#include "state/context.h"
#include "state/localsubtitleclient.h"

namespace
{
bool writeFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) &&
        file.write("1\n00:00:00,000 --> 00:00:05,000\nTest subtitle\n") > 0;
}

QVariantMap localLink(const QString &directory)
{
    return {{"provider", "local"}, {"name", "Show"}, {"directory", directory},
        {"preferredFile", "[Chosen] Show - 01.ja.srt"}, {"autoLoad", true},
        {"releaseRule", SubtitleRelease::fromFile("[Chosen] Show - 01.ja.srt", 1)}};
}
}

class LocalSubtitleClientTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        std::setlocale(LC_NUMERIC, "C");
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("local-subtitles-test-" +
            QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(QDir().mkpath(DirectoryUtils::getDataDir()));
        QVERIFY(QDir().mkpath(DirectoryUtils::getConfigDir()));
        Dictionary::createDatabaseInstance();
    }

    void matchesOnlyTheSelectedReleaseInTheChosenDirectory()
    {
        QTemporaryDir folder;
        const auto rule = localLink(folder.path()).value("releaseRule").toMap();
        const QString expected = folder.filePath("[Chosen] Show - 02.ja.srt");
        for (const QString &name : {QString("[Chosen] Show - 01.ja.srt"),
                QString("[Other] Show - 02.ja.srt"), QString("[Chosen] Show - 02.en.srt"),
                QString("[Chosen] Show - 02.ja.ass"), QString("[Chosen] Show - 02.ja.srt")})
            QVERIFY(writeFile(folder.filePath(name)));
        bool ambiguous = true;
        QCOMPARE(LocalSubtitleClient::candidateForEpisode(folder.path(), 2, rule, &ambiguous), expected);
        QVERIFY(!ambiguous);
        QVERIFY(QFile::remove(expected));
        QVERIFY(QDir().mkpath(folder.filePath("nested")));
        QVERIFY(writeFile(folder.filePath("nested/[Chosen] Show - 02.ja.srt")));
        QVERIFY(LocalSubtitleClient::candidateForEpisode(folder.path(), 2, rule).isEmpty());
        QVERIFY(LocalSubtitleClient::candidateForEpisode(folder.path(), 2, {}).isEmpty());
    }

    void keepsAllExistingLocalFormatsAndLargeBitmapFiles()
    {
        QTemporaryDir folder;
        for (const auto *extension : {"ass", "idx", "lrc", "mks", "pgs", "rt",
                "scc", "smi", "srt", "ssa", "sub", "sup", "utf-8", "utf", "utf8", "vtt"})
        {
            const QString path = folder.filePath(QString("[Chosen] Show - 02.%1").arg(extension));
            QVERIFY(writeFile(path));
            QCOMPARE(LocalSubtitleClient::readableSubtitlePath(path), path);
            const auto rule = SubtitleRelease::fromFile(QString("[Chosen] Show - 01.%1").arg(extension), 1);
            QCOMPARE(LocalSubtitleClient::candidateForEpisode(folder.path(), 2, rule), path);
        }
        const QString bitmap = folder.filePath("large.sup");
        QFile file(bitmap);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QVERIFY(file.resize(64 * 1024 * 1024 + 1));
        file.close();
        QCOMPARE(LocalSubtitleClient::readableSubtitlePath(bitmap), bitmap);
    }

    void refusesAmbiguousReleaseVariants()
    {
        QTemporaryDir folder;
        const auto rule = SubtitleRelease::fromFile("[Chosen] Show - 01.ja [AABBCCDD].srt", 1);
        QVERIFY(writeFile(folder.filePath("[Chosen] Show - 02.ja [11223344].srt")));
        QVERIFY(writeFile(folder.filePath("[Chosen] Show - 02.ja [55667788].srt")));
        bool ambiguous = false;
        QVERIFY(LocalSubtitleClient::candidateForEpisode(folder.path(), 2, rule, &ambiguous).isEmpty());
        QVERIFY(ambiguous);
    }

    void missingExactChoiceDoesNotFallBackToAnotherGroup()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        const QString episode = folder.filePath("Show - 01.mkv");
        QVERIFY(writeFile(episode));
        QVERIFY(writeFile(folder.filePath("[Other] Show - 01.ja.srt")));
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        const auto link = localLink(folder.path());
        auto selection = link;
        selection.insert("filename", "[Chosen] Show - 01.ja.srt");
        selection.insert("cachePath", folder.filePath("[Chosen] Show - 01.ja.srt"));
        selection.insert("episode", 1);
        QVERIFY(context.episodeLibrary()->rememberSubtitleSelection(episode, link, selection));
        player.state()->setPath(episode);
        LocalSubtitleClient client(&context);
        QSignalSpy failed(&client, &LocalSubtitleClient::failed);
        client.fetchForCurrentMedia();
        QCOMPARE(failed.count(), 1);
        QVERIFY(client.status().contains("missing"));
        QVERIFY(!client.busy());
        QCOMPARE(context.episodeLibrary()->subtitleSelectionForFile(episode), selection);
        auto disabled = link;
        disabled.insert("autoLoad", false);
        QVERIFY(context.episodeLibrary()->setSubtitleLinkForFile(episode, disabled));
        client.fetchForCurrentMedia();
        QCOMPARE(failed.count(), 1);
        context.setPlayer(nullptr);
    }

    void persistsOnlyAfterTheExpectedExternalTrackIsSelected()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        const QString episode = folder.filePath("Show - 01.mkv");
        const QString subtitle = folder.filePath("[Chosen] Show - 01.ja.srt");
        QVERIFY(writeFile(episode));
        QVERIFY(writeFile(subtitle));
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        player.state()->setPath(episode);
        LocalSubtitleClient client(&context);
        client.m_mediaPath = episode;
        client.m_pendingPath = subtitle;
        client.m_pendingGeneration = client.m_generation;
        client.m_link = localLink(folder.path());
        client.m_episode = 1;
        client.m_busy = true;
        QSignalSpy attached(&client, &LocalSubtitleClient::subtitleAttached);
        auto *track = new MpvTrack(player.state());
        track->setId(42);
        track->setType(MpvTrack::Type::Subtitle);
        track->setExternal(true);
        track->setExternalFilename(subtitle);
        player.state()->setSubtitleTracks({track});
        player.state()->setSid(7);
        client.checkAttached();
        QVERIFY(context.episodeLibrary()->subtitleSelectionForFile(episode).isEmpty());
        QCOMPARE(attached.count(), 0);
        player.state()->setSid(42);
        client.checkAttached();
        QCOMPARE(attached.count(), 1);
        const auto selected = context.episodeLibrary()->subtitleSelectionForFile(episode);
        QCOMPARE(selected.value("provider").toString(), QString("local"));
        QCOMPARE(selected.value("filename").toString(), QFileInfo(subtitle).fileName());
        QCOMPARE(selected.value("cachePath").toString(), subtitle);
        QCOMPARE(selected.value("directory").toString(), folder.path());
        QVERIFY(!client.busy());
        context.setPlayer(nullptr);
    }

    void invalidFilesAndChangedMediaDoNotChangePreferences()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        const QString episode = folder.filePath("Show - 01.mkv");
        QVERIFY(writeFile(episode));
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        player.state()->setPath(episode);
        LocalSubtitleClient client(&context);
        QSignalSpy failed(&client, &LocalSubtitleClient::failed);
        client.loadFile("https://example.invalid/subtitle.srt");
        QCOMPARE(failed.count(), 1);
        QVERIFY(context.episodeLibrary()->subtitleLinkForFile(episode).isEmpty());
        client.m_mediaPath = episode;
        client.m_pendingPath = folder.filePath("[Chosen] Show - 01.ja.srt");
        client.m_pendingGeneration = client.m_generation;
        client.m_link = localLink(folder.path());
        player.state()->setPath(folder.filePath("Show - 02.mkv"));
        client.checkAttached();
        QCOMPARE(failed.count(), 2);
        QVERIFY(context.episodeLibrary()->subtitleLinkForFile(episode).isEmpty());
        QVERIFY(client.m_pendingPath.isEmpty());
        context.setPlayer(nullptr);
    }

    void rejectsAnOverflowingSavedEpisodeOffset()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        QTemporaryDir folder;
        const QString episode = folder.filePath("Show - 02.mkv");
        QVERIFY(writeFile(episode));
        QVERIFY(context.episodeLibrary()->addFolder(QUrl::fromLocalFile(folder.path())) >= 0);
        auto link = localLink(folder.path());
        link.insert("episodeOffset", std::numeric_limits<int>::max());
        QVERIFY(context.episodeLibrary()->setSubtitleLinkForFile(episode, link));
        player.state()->setPath(episode);
        LocalSubtitleClient client(&context);
        QSignalSpy failed(&client, &LocalSubtitleClient::failed);
        client.fetchForCurrentMedia();
        QCOMPARE(failed.count(), 1);
        QVERIFY(client.status().contains("offset is invalid"));
        QVERIFY(context.episodeLibrary()->subtitleSelectionForFile(episode).isEmpty());
        context.setPlayer(nullptr);
    }

    void cleanupTestCase()
    {
        Dictionary::destroyDatabaseInstance();
        QVERIFY(QDir(DirectoryUtils::getDataDir()).removeRecursively());
        QVERIFY(QDir(DirectoryUtils::getConfigDir()).removeRecursively());
        QVERIFY(QDir(DirectoryUtils::getCacheDir()).removeRecursively());
    }
};

QTEST_MAIN(LocalSubtitleClientTest)
#include "test_localsubtitles.moc"
