#include <QTest>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>

#include <limits>

#include "quick/episodefolder.h"
#include "util/directoryutils.h"

namespace
{
bool writeFile(const QString &path, const QByteArray &data = "fixture")
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

QVariantMap releaseRule(const QString &group)
{
    return {{"version", 1}, {"valid", true}, {"sampleName", group + " - 01.srt"},
        {"episode", 1}, {"mode", "template"}, {"pattern", group + " - {episode}.srt"}};
}

QVariantMap providerLink(const QString &group)
{
    return {{"provider", "jimaku"}, {"name", "Show"}, {"entryId", 123},
        {"releaseRule", releaseRule(group)}, {"preferredFile", group + " - 01.srt"},
        {"episodeOffset", 0}};
}

QVariantMap subtitleChoice(const QString &group, const QString &cache, int episode)
{
    auto selection = providerLink(group);
    selection.insert("filename", group + QString(" - %1.srt").arg(episode, 2, 10, QChar('0')));
    selection.insert("cachePath", cache);
    selection.insert("episode", episode);
    selection.insert("delay", 0.25);
    return selection;
}

QByteArray torrent(const QByteArray &name, char piece, QByteArray *hash)
{
    QByteArray info = "d6:lengthi1e4:name" + QByteArray::number(name.size()) + ':' +
        name + "12:piece lengthi16384e6:pieces20:" + QByteArray(20, piece) + 'e';
    *hash = QCryptographicHash::hash(info, QCryptographicHash::Sha1).toHex();
    return "d4:info" + info + 'e';
}
}

class SubtitleSelectionTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(m_profile.isValid());
        QCoreApplication::setOrganizationName("memento-tests");
        QCoreApplication::setApplicationName("subtitle-selections-" +
            QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(qputenv("XDG_CONFIG_HOME", m_profile.filePath("config").toUtf8()));
        QVERIFY(qputenv("XDG_CACHE_HOME", m_profile.filePath("cache").toUtf8()));
    }

    void init()
    {
        QVERIFY(QDir(DirectoryUtils::getConfigDir()).removeRecursively());
        QVERIFY(QDir().mkpath(DirectoryUtils::getConfigDir()));
    }

    void nextEpisodeUsesPlayingSeriesAndDoesNotWrap()
    {
        QTemporaryDir first, second;
        QVERIFY(writeFile(first.filePath("Show - 01.mkv")));
        QVERIFY(writeFile(first.filePath("Show - 02.mkv")));
        QVERIFY(writeFile(first.filePath("Show - 10.mkv")));
        QVERIFY(writeFile(second.filePath("Other - 01.mkv")));
        EpisodeFolder library;
        const int series = library.addFolder(QUrl::fromLocalFile(first.path()));
        QVERIFY(series >= 0);
        const int other = library.addFolder(QUrl::fromLocalFile(second.path()));
        QVERIFY(other >= 0);
        library.setCurrentIndex(other);
        QVERIFY(library.hasNextEpisode(first.filePath("Show - 01.mkv")));
        QCOMPARE(library.currentIndex(), other);
        QCOMPARE(library.nextEpisodePath(first.filePath("Show - 01.mkv")), first.filePath("Show - 02.mkv"));
        QCOMPARE(library.currentIndex(), series);
        QCOMPARE(library.nextEpisodePath(first.filePath("Show - 02.mkv")), first.filePath("Show - 10.mkv"));
        QVERIFY(!library.hasNextEpisode(first.filePath("Show - 10.mkv")));
        QVERIFY(library.nextEpisodePath(first.filePath("Show - 10.mkv")).isEmpty());
        QVERIFY(library.nextEpisodePath("/not-in-library.mkv").isEmpty());
        QVERIFY(QFile::remove(first.filePath("Show - 02.mkv")));
        QVERIFY(!library.hasNextEpisode(first.filePath("Show - 01.mkv")));
    }

    void keepsExactEpisodeChoicesAcrossPreferenceChangesAndReload()
    {
        QTemporaryDir media;
        const QString first = media.filePath("Show - 01.mkv");
        const QString second = media.filePath("Show - 02.mkv");
        QVERIFY(writeFile(first));
        QVERIFY(writeFile(second));
        const auto firstChoice = subtitleChoice("Group A", media.filePath("one.srt"), 1);
        const auto secondChoice = subtitleChoice("Group B", media.filePath("two.srt"), 2);
        {
            EpisodeFolder library;
            QVERIFY(library.addFolder(QUrl::fromLocalFile(media.path())) >= 0);
            QVERIFY(library.subtitleSelectionForFile(first).isEmpty());
            QVERIFY(library.rememberSubtitleSelection(first, providerLink("Group A"), firstChoice));
            QVERIFY(library.subtitleSelectionForFile(second).isEmpty());
            QCOMPARE(library.subtitleLinkForFile(second), providerLink("Group A"));
            QVERIFY(library.rememberSubtitleSelection(second, providerLink("Group B"), secondChoice));
            QCOMPARE(library.subtitleSelectionForFile(first), firstChoice);
            QCOMPARE(library.subtitleSelectionForFile(second), secondChoice);
            QCOMPARE(library.subtitleLinkForFile(first), providerLink("Group B"));
            QVERIFY(!library.rememberSubtitleSelection(media.filePath("outside.mkv"),
                providerLink("Group A"), firstChoice));
            // Inserting a preceding episode changes rows but must not change identity.
            QVERIFY(writeFile(media.filePath("Show - 00.mkv")));
            QCOMPARE(library.rescan().size(), 3);
            QCOMPARE(library.subtitleSelectionForFile(first), firstChoice);
        }
        EpisodeFolder restored;
        QCOMPARE(restored.subtitleSelectionForFile(QUrl::fromLocalFile(first).toString()), firstChoice);
        QCOMPARE(restored.subtitleSelectionForFile(second), secondChoice);
        QCOMPARE(restored.subtitleLinkForFile(first), providerLink("Group B"));
    }

    void persistsOnlyMetadataAndLocalCacheReferences()
    {
        QTemporaryDir media;
        const QString episode = media.filePath("Show - 01.mkv");
        QVERIFY(writeFile(episode));
        EpisodeFolder library;
        QVERIFY(library.addFolder(QUrl::fromLocalFile(media.path())) >= 0);
        auto choice = subtitleChoice("Group A", media.filePath("one.srt"), 1);
        choice.insert("url", "https://example.invalid/file?authorization=test-only");
        choice.insert("apiKey", "test-only-value");
        auto rule = choice.value("releaseRule").toMap();
        rule.insert("Authorization", "test-only-value");
        choice.insert("releaseRule", rule);
        auto link = providerLink("Group A");
        link.insert("authorization", "test-only-value");
        QVERIFY(library.setSubtitleLinkForFile(episode, link));
        QCOMPARE(library.subtitleLinkForFile(episode), providerLink("Group A"));
        QVERIFY(library.rememberSubtitleSearch(choice, 5));
        auto rememberedSearch = providerLink("Group A");
        rememberedSearch.insert("episode", 5);
        QCOMPARE(library.lastSubtitleSearch(), rememberedSearch);
        QVERIFY(library.rememberSubtitleSelection(episode, link, choice));
        QCOMPARE(library.subtitleSelectionForFile(episode),
            subtitleChoice("Group A", media.filePath("one.srt"), 1));
        QCOMPARE(library.subtitleLinkForFile(episode), providerLink("Group A"));
        QFile json(QDir(DirectoryUtils::getConfigDir()).filePath("episode-library.json"));
        QVERIFY(json.open(QIODevice::ReadOnly));
        const auto data = json.readAll();
        // Windows cannot atomically replace the library while this reader is
        // open. Finish the inspection before exercising another saved choice.
        json.close();
        QVERIFY(!data.contains("test-only-value"));
        QVERIFY(!data.contains("example.invalid"));
        choice.insert("cachePath", "https://example.invalid/file?authorization=test-only");
        choice.insert("delay", std::numeric_limits<double>::infinity());
        QVERIFY(library.rememberSubtitleSelection(episode, link, choice));
        QVERIFY(!library.subtitleSelectionForFile(episode).contains("cachePath"));
        QVERIFY(!library.subtitleSelectionForFile(episode).contains("delay"));
    }

    void updatesDelayOnlyForTheRememberedExternalSubtitle()
    {
        QTemporaryDir media;
        const QString episode = media.filePath("Show - 01.mkv");
        const QString cache = media.filePath("one.srt");
        QVERIFY(writeFile(episode));
        auto choice = subtitleChoice("Group A", cache, 1);
        {
            EpisodeFolder library;
            QVERIFY(library.addFolder(QUrl::fromLocalFile(media.path())) >= 0);
            QVERIFY(library.rememberSubtitleSelection(episode, providerLink("Group A"), choice));
            QVERIFY(!library.updateSubtitleSelectionDelay(episode, media.filePath("other.srt"), 8));
            QVERIFY(!library.updateSubtitleSelectionDelay(episode, cache,
                std::numeric_limits<double>::quiet_NaN()));
            QCOMPARE(library.subtitleSelectionForFile(episode), choice);
            QVERIFY(library.updateSubtitleSelectionDelay(episode, QUrl::fromLocalFile(cache).toString(), -1.75));
            choice.insert("delay", -1.75);
            QCOMPARE(library.subtitleSelectionForFile(episode), choice);
            QCOMPARE(library.subtitleLinkForFile(episode), providerLink("Group A"));
        }
        EpisodeFolder restored;
        QCOMPARE(restored.subtitleSelectionForFile(episode), choice);
    }

    void localSourceDirectoryAndAutoLoadPersistSafely()
    {
        QTemporaryDir media;
        const QString episode = media.filePath("Show - 01.mkv");
        QVERIFY(writeFile(episode));
        QVariantMap link{{"provider", "local"}, {"name", "Show"},
            {"directory", media.path()}, {"preferredFile", "[Group] Show - 01.srt"},
            {"releaseRule", releaseRule("[Group] Show")}, {"autoLoad", false}};
        auto choice = link;
        choice.insert("filename", "[Group] Show - 01.srt");
        choice.insert("cachePath", media.filePath("[Group] Show - 01.srt"));
        {
            EpisodeFolder library;
            QVERIFY(library.addFolder(QUrl::fromLocalFile(media.path())) >= 0);
            QVERIFY(library.rememberSubtitleSelection(episode, link, choice));
            QCOMPARE(library.subtitleLinkForFile(episode), link);
            QCOMPARE(library.subtitleSelectionForFile(episode), choice);
        }
        EpisodeFolder restored;
        QCOMPARE(restored.subtitleLinkForFile(episode), link);
        QCOMPARE(restored.subtitleSelectionForFile(episode), choice);
        link.insert("directory", "https://example.invalid/folder?authorization=test-only");
        choice.insert("directory", "https://example.invalid/folder?authorization=test-only");
        QVERIFY(restored.rememberSubtitleSelection(episode, link, choice));
        QVERIFY(!restored.subtitleLinkForFile(episode).contains("directory"));
        QVERIFY(!restored.subtitleSelectionForFile(episode).contains("directory"));
    }

    void rollsBackBothFieldsWhenPersistenceFails()
    {
        QTemporaryDir media;
        const QString episode = media.filePath("Show - 01.mkv");
        QVERIFY(writeFile(episode));
        EpisodeFolder library;
        QVERIFY(library.addFolder(QUrl::fromLocalFile(media.path())) >= 0);
        const auto original = subtitleChoice("Group A", media.filePath("one.srt"), 1);
        QVERIFY(library.rememberSubtitleSelection(episode, providerLink("Group A"), original));
        const QString backup = QDir(DirectoryUtils::getConfigDir()).filePath("episode-library.backup.json");
        QVERIFY(QFile::remove(backup));
        // A directory at the backup destination fails atomically on every platform.
        QVERIFY(QDir().mkpath(backup));
        QVERIFY(!library.rememberSubtitleSelection(episode, providerLink("Group B"),
            subtitleChoice("Group B", media.filePath("two.srt"), 1)));
        QCOMPARE(library.subtitleLinkForFile(episode), providerLink("Group A"));
        QCOMPARE(library.subtitleSelectionForFile(episode), original);
        QVERIFY(!library.updateSubtitleSelectionDelay(episode, media.filePath("one.srt"), 4));
        QCOMPARE(library.subtitleSelectionForFile(episode), original);
        QVERIFY(!library.clearSubtitleLinkForFile(episode));
        QCOMPARE(library.subtitleSelectionForFile(episode), original);
        QVERIFY(QDir().rmdir(backup));
        EpisodeFolder restored;
        QCOMPARE(restored.subtitleSelectionForFile(episode), original);
    }

    void unlinkForgetsTheWholeSeriesButKeepsOtherSeries()
    {
        QTemporaryDir media, other;
        const QString first = media.filePath("Show - 01.mkv");
        const QString second = media.filePath("Show - 02.mkv");
        const QString third = other.filePath("Other - 01.mkv");
        for (const auto &file : {first, second, third}) QVERIFY(writeFile(file));
        const auto choice = subtitleChoice("Group A", media.filePath("one.srt"), 1);
        {
            EpisodeFolder library;
            QVERIFY(library.addFolder(QUrl::fromLocalFile(media.path())) >= 0);
            QVERIFY(library.addFolder(QUrl::fromLocalFile(other.path())) >= 0);
            for (const auto &file : {first, second, third})
                QVERIFY(library.rememberSubtitleSelection(file, providerLink("Group A"), choice));
            QVERIFY(library.clearSubtitleLinkForFile(first));
            QVERIFY(library.subtitleSelectionForFile(first).isEmpty());
            QVERIFY(library.subtitleSelectionForFile(second).isEmpty());
            QVERIFY(library.subtitleLinkForFile(second).isEmpty());
            QCOMPARE(library.subtitleSelectionForFile(third), choice);
        }
        EpisodeFolder restored;
        QVERIFY(restored.subtitleSelectionForFile(first).isEmpty());
        QVERIFY(restored.subtitleSelectionForFile(second).isEmpty());
        QCOMPARE(restored.subtitleSelectionForFile(third), choice);
    }

    void mergedMagnetsKeepChoicesForIdenticalEpisodeNames()
    {
        QTemporaryDir media;
        QByteArray hashA, hashB;
        const QString sourceA = media.filePath("a.torrent");
        const QString sourceB = media.filePath("b.torrent");
        QVERIFY(writeFile(sourceA, torrent("Episode 01.mkv", 'a', &hashA)));
        QVERIFY(writeFile(sourceB, torrent("Episode 01.mkv", 'b', &hashB)));
        const auto choiceA = subtitleChoice("Group A", media.filePath("one.srt"), 1);
        const auto choiceB = subtitleChoice("Group B", media.filePath("two.srt"), 1);
        {
            EpisodeFolder library;
            QVERIFY(library.addTorrent(QUrl::fromLocalFile(sourceA)) >= 0);
            const QString streamA = library.episodePath(0);
            QVERIFY(!streamA.isEmpty());
            QVERIFY(library.rememberSubtitleSelection(streamA, providerLink("Group A"), choiceA));
            QVERIFY(library.addTorrent(QUrl::fromLocalFile(sourceB)) >= 0);
            const QString streamB = library.episodePath(0);
            QVERIFY(!streamB.isEmpty());
            QVERIFY(library.rememberSubtitleSelection(streamB, providerLink("Group B"), choiceB));
            QCOMPARE(library.addMultipleMagnets("Combined", "magnet:?xt=urn:btih:" +
                QString::fromLatin1(hashA) + "\nmagnet:?xt=urn:btih:" + QString::fromLatin1(hashB)), 0);
            QCOMPARE(library.subtitleSelectionForFile(streamA), choiceA);
            QCOMPARE(library.subtitleSelectionForFile(streamB), choiceB);
        }
        EpisodeFolder restored;
        QCOMPARE(restored.totalCount(), 2);
        QSet<QString> filenames;
        for (int row = 0; row < restored.totalCount(); ++row)
        {
            const QString stream = restored.episodePath(row);
            QVERIFY(!stream.isEmpty());
            filenames.insert(restored.subtitleSelectionForFile(stream).value("filename").toString());
        }
        QCOMPARE(filenames, QSet<QString>({"Group A - 01.srt", "Group B - 01.srt"}));
        restored.removeEntry(0);
    }

    void cleanupTestCase()
    {
        QVERIFY(QDir(DirectoryUtils::getConfigDir()).removeRecursively());
        QVERIFY(QDir(DirectoryUtils::getCacheDir()).removeRecursively());
    }

private:
    QTemporaryDir m_profile;
};

QTEST_GUILESS_MAIN(SubtitleSelectionTest)
#include "test_subtitleselection.moc"
