#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

#include "jimaku/subtitlerelease.h"

class SubtitleReleaseTest : public QObject
{
    Q_OBJECT

private slots:
    void matchesOnlySelectedRelease()
    {
        const auto rule = SubtitleRelease::fromFile(
            "[Group2] 86 S01E05 [1080p][ABCDEF01].ja.ass", 5);
        QCOMPARE(rule.value("mode").toString(), QString("release"));
        QVERIFY(SubtitleRelease::matches("[Group2] 86 S01E06 [1080p][1234ABCD].ja.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("[Other] 86 S01E06 [1080p][1234ABCD].ja.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("[Group3] 86 S01E06 [1080p][1234ABCD].ja.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("[Group2] 87 S01E06 [1080p][1234ABCD].ja.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("[Group2] 86 S02E06 [1080p][1234ABCD].ja.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("[Group2] 86 S01E06 [720p][1234ABCD].ja.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("[Group2] 86 S01E06 [1080p][1234ABCD].en.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("[Group2] 86 S01E06 [1080p][1234ABCD].ja.srt", 6, rule));
        QVERIFY(!SubtitleRelease::matches("[Group2] 86 S01E05 [1080p][1234ABCD].ja.ass", 6, rule));
    }

    void recognizedEpisodeForms_data()
    {
        QTest::addColumn<QString>("selected");
        QTest::addColumn<QString>("next");
        QTest::newRow("season") << "Show S01E05.ass" << "Show S01E06.ass";
        QTest::newRow("episode") << "Show E05.ass" << "Show E06.ass";
        QTest::newRow("ep") << "Show EP005.srt" << "Show EP006.srt";
        QTest::newRow("episode word") << "Show Episode 05.ass" << "Show Episode 06.ass";
        QTest::newRow("dash") << "[Group2] Show - 05.ass" << "[Group2] Show - 06.ass";
        QTest::newRow("numeric title") << "[Group2] 86 - 05.ass" << "[Group2] 86 - 06.ass";
        QTest::newRow("brackets") << "Show [05].ass" << "Show [06].ass";
        QTest::newRow("bare brackets") << "[05].ass" << "[06].ass";
        QTest::newRow("bare") << "05.srt" << "06.srt";
        QTest::newRow("directory") << "Group2/Show/05.srt" << "Group2/Show/06.srt";
        QTest::newRow("japanese") << "作品 第05話.ass" << "作品 第06話.ass";
        QTest::newRow("revision") << "Show - 05v2.ass" << "Show - 06v2.ass";
        QTest::newRow("padding") << "Show - 5.ass" << "Show - 006.ass";
    }

    void recognizedEpisodeForms()
    {
        QFETCH(QString, selected);
        QFETCH(QString, next);
        const auto rule = SubtitleRelease::fromFile(selected, 5);
        QCOMPARE(rule.value("mode").toString(), QString("release"));
        QVERIFY(SubtitleRelease::matches(next, 6, rule));
        QVERIFY(!SubtitleRelease::matches(next, 7, rule));
    }

    void harmlessNormalization()
    {
        const auto rule = SubtitleRelease::fromFile("[GROUP] Café - ０５.JA.ASS", 5);
        QVERIFY(SubtitleRelease::matches("[group] Cafe\u0301-06 . ja . ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("[group] Cafe-06.ja.ass", 6, rule));
        QVERIFY(SubtitleRelease::matches("[group] Café_06.ja.ass", 6, rule));
        const auto spacedGroup = SubtitleRelease::fromFile("[Group A] Show - 05.ass", 5);
        QVERIFY(!SubtitleRelease::matches("[GroupA] Show - 06.ass", 6, spacedGroup));
    }

    void keepsVariantAndDirectory()
    {
        const auto rule = SubtitleRelease::fromFile("Group A/Show - 05.dialogue.ass", 5);
        QVERIFY(SubtitleRelease::matches("Group A/Show - 06.dialogue.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("Group B/Show - 06.dialogue.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("Group A/Show - 06.signs.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("Group A/Show - 06.songs.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("Group A/Show - 06.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("Group A/Show - 06v2.dialogue.ass", 6, rule));
        const auto crcGroup = SubtitleRelease::fromFile("[DEADBEEF] Show - 05 [11223344].ass", 5);
        QVERIFY(SubtitleRelease::matches("[DEADBEEF] Show - 06 [AABBCCDD].ass", 6, crcGroup));
        QVERIFY(!SubtitleRelease::matches("[AABBCCDD] Show - 06 [11223344].ass", 6, crcGroup));
    }

    void uncertainNamesAreExactOnly_data()
    {
        QTest::addColumn<QString>("name");
        QTest::newRow("no number") << "[Group] Show Japanese.ass";
        QTest::newRow("title number") << "86.ass";
        QTest::newRow("numeric group") << "[Group5] Show.ass";
        QTest::newRow("resolution") << "Show [1080p].ass";
        QTest::newRow("year") << "Show 2005.ass";
        QTest::newRow("ambiguous") << "Show - 05 - 06.ass";
        QTest::newRow("two markers") << "Show S01E05 [05].ass";
        QTest::newRow("range") << "Show - 05-06.ass";
        QTest::newRow("fraction") << "Show - 05.5.ass";
        QTest::newRow("numeric group only") << "[05] Show.ass";
        QTest::newRow("marker in group") << "[E05] Show.ass";
        QTest::newRow("dash in group") << "[Group-05] Show.ass";
        QTest::newRow("different episode") << "Show - 04.ass";
    }

    void uncertainNamesAreExactOnly()
    {
        QFETCH(QString, name);
        const auto rule = SubtitleRelease::fromFile(name, 5);
        QCOMPARE(rule.value("mode").toString(), QString("exact"));
        QVERIFY(SubtitleRelease::matches(name, 5, rule));
        QVERIFY(!SubtitleRelease::matches(name, 6, rule));
        QString changed = name;
        changed.replace("05", "06");
        QVERIFY(!SubtitleRelease::matches(changed, 6, rule));
    }

    void episodeTitlesAndSpacingAreIndependent_data()
    {
        QTest::addColumn<QString>("selected");
        QTest::addColumn<QString>("next");
        QTest::newRow("user-example") << "EP 1.ASS" << "EP2.ASS";
        QTest::newRow("marker-and-title") << "[Group] Show EP 1 - First morning.ass"
            << "[Group] Show Episode2 - A different day.ass";
        QTest::newRow("episode-only-title") << "EP1 - First.ass" << "E02 - Second.ass";
        QTest::newRow("leading-number") << "01 - First.ass" << "02 - Second.ass";
        QTest::newRow("spaces") << "Show 01.ass" << "Show_02.ass";
        QTest::newRow("plain-with-tags") << "[Group] Show 01 [1080p].ja.ass"
            << "[Group] Show_02 [1080p].ja.ass";
        QTest::newRow("plain-with-revision") << "[Group] Show 01v2.ass"
            << "[Group] Show 02v2.ass";
        QTest::newRow("season-padding") << "[Group] Show S1E1 - First.ass"
            << "[Group] Show S01E02 - Second.ass";
        QTest::newRow("episode-metadata") << "[Group] Show - 1 - First [1080p].ja.ass"
            << "[Group] Show E02 - Second [1080p].ja.ass";
        QTest::newRow("relative-directory") << "Selected group/EP 1.ass"
            << "Selected group/EP2 - Episode title.ass";
    }

    void episodeTitlesAndSpacingAreIndependent()
    {
        QFETCH(QString, selected); QFETCH(QString, next);
        const auto rule = SubtitleRelease::fromFile(selected, 1);
        QCOMPARE(rule.value("version").toInt(), 2);
        QCOMPARE(rule.value("mode").toString(), QString("release"));
        QVERIFY(SubtitleRelease::matches(next, 2, rule));
        QVERIFY(!SubtitleRelease::matches(next, 1, rule));
    }

    void sourceFormatAndGroupBoundaries()
    {
        const auto rule = SubtitleRelease::fromFile("Group/EP 1.ASS", 1);
        QVERIFY(SubtitleRelease::matches("Group/EP2.ASS", 2, rule));
        QVERIFY(!SubtitleRelease::matches("Group/EP2.SRT", 2, rule));
        QVERIFY(!SubtitleRelease::matches("Other/EP2.ASS", 2, rule));
        QVERIFY(!SubtitleRelease::matches("Group/[Other] EP2.ASS", 2, rule));
        QVERIFY(!SubtitleRelease::matches("Group/EP2.ASS", 3, rule));
        // A failed attempt must not consume or advance the saved release.
        QVERIFY(SubtitleRelease::matches("Group/EP2.ASS", 2, rule));
        const auto tagged = SubtitleRelease::fromFile("[Chosen] EP 1.ass", 1);
        QVERIFY(SubtitleRelease::matches("[Chosen] EP2 - New title.ass", 2, tagged));
        QVERIFY(!SubtitleRelease::matches("[Other] EP2 - New title.ass", 2, tagged));
        QVERIFY(!SubtitleRelease::matches("EP2 - New title.ass", 2, tagged));
    }

    void duplicateCandidatesRemainAmbiguous()
    {
        const auto rule = SubtitleRelease::fromFile("[Chosen] EP 1.ass", 1);
        const QStringList files{"[Chosen] EP2 - First title.ass", "[Chosen] EP 2 - Alternate.ass",
            "[Chosen] EP2.srt", "[Other] EP2.ass", "[Chosen] EP3.ass"};
        int matches = 0;
        for (const auto &file : files)
            if (SubtitleRelease::matches(file, 2, rule)) ++matches;
        // Provider/local selectors reject two candidates, instead of preferring
        // one due to spacing or an arbitrary episode-title similarity score.
        QCOMPARE(matches, 2);
    }

    void explicitMetadataCannotChange()
    {
        const auto language = SubtitleRelease::fromFile("EP1 - First.ja-JP.ass", 1);
        QVERIFY(SubtitleRelease::matches("EP2 - Second.ja-jp.ass", 2, language));
        QVERIFY(!SubtitleRelease::matches("EP2 - Second.en-us.ass", 2, language));
        QVERIFY(!SubtitleRelease::matches("EP2 - Second.ass", 2, language));
        const auto technical = SubtitleRelease::fromFile("Show EP1 - First 1080p Japanese.ass", 1);
        QVERIFY(SubtitleRelease::matches("Show EP2 - Second 1080p Japanese.ass", 2, technical));
        QVERIFY(!SubtitleRelease::matches("Show EP2 - Second 720p Japanese.ass", 2, technical));
        QVERIFY(!SubtitleRelease::matches("Show EP2 - Second 1080p English.ass", 2, technical));
        const auto variant = SubtitleRelease::fromFile("Show EP1 - First.signs.ass", 1);
        QVERIFY(!SubtitleRelease::matches("Show EP2 - Second.dialogue.ass", 2, variant));
        const auto season = SubtitleRelease::fromFile("Show S1E1 - First.ass", 1);
        QVERIFY(!SubtitleRelease::matches("Show S2E2 - Second.ass", 2, season));
        QVERIFY(!SubtitleRelease::matches("Show EP2 - Second.ass", 2, season));
    }

    void explicitTrailingTrackVariants()
    {
        const QStringList variants{"Signs", "Dialogue", "Full", "Songs"};
        for (const auto &selected : variants)
        {
            const auto rule = SubtitleRelease::fromFile("[Chosen] Show EP1 - First " + selected + ".ass", 1);
            for (const auto &candidate : variants)
                QCOMPARE(SubtitleRelease::matches("[Chosen] Show EP2 - Second " + candidate + ".ass", 2, rule),
                         selected == candidate);
            QVERIFY(SubtitleRelease::matches("[Chosen] Show EP2 - Second." + selected + ".ass", 2, rule));
            QVERIFY(!SubtitleRelease::matches("[Chosen] Show EP2 - Second.ass", 2, rule));
        }
        const auto language = SubtitleRelease::fromFile("[Chosen] EP1 - First Signs ja.ass", 1);
        QVERIFY(SubtitleRelease::matches("[Chosen] EP2 - Second.Signs.ja.ass", 2, language));
        QVERIFY(!SubtitleRelease::matches("[Chosen] EP2 - Second Signs en.ass", 2, language));
        QVERIFY(!SubtitleRelease::matches("[Chosen] EP2 - Second Dialogue ja.ass", 2, language));
        const auto fullTitle = SubtitleRelease::fromFile("[Chosen] Show EP1 - Full Moon.ass", 1);
        QVERIFY(SubtitleRelease::matches("[Chosen] Show EP2 - New Day.ass", 2, fullTitle));
    }

    void explicitRangesNeverSelectAnIntegerEpisode()
    {
        for (const auto &name : {QStringLiteral("Show EP1~2.ass"),
                                QStringLiteral("Show EP1–2.ass"),
                                QStringLiteral("Show EP1 - 2.ass")})
        {
            const auto rule = SubtitleRelease::fromFile(name, 1);
            QCOMPARE(rule.value("mode").toString(), QString("exact"));
            QVERIFY(SubtitleRelease::matches(name, 1, rule));
            QVERIFY(!SubtitleRelease::matches("Show EP3.ass", 3, rule));
            QVERIFY(!SubtitleRelease::matches(name, 2, rule));
        }
    }

    void savedVersionOneRulesUpgradeWithoutLosingGroup()
    {
        QVariantMap legacy{{"version", 1}, {"valid", true}, {"sampleName", "EP 1.ASS"},
            {"episode", 1}, {"mode", "template"}, {"pattern", QString("ep ") + QChar(1) + ".ass"}};
        QVERIFY(SubtitleRelease::matches("EP2.ASS", 2, legacy));
        QVERIFY(!SubtitleRelease::matches("EP2.SRT", 2, legacy));
        legacy = {{"version", 1}, {"valid", true}, {"sampleName", "[Chosen] Show - 01 - First.ass"},
            {"episode", 1}, {"mode", "template"},
            {"pattern", QString("[chosen]show-") + QChar(1) + "-first.ass"}};
        QVERIFY(SubtitleRelease::matches("[Chosen] Show EP2 - Second.ass", 2, legacy));
        QVERIFY(!SubtitleRelease::matches("[Other] Show EP2 - Second.ass", 2, legacy));
        legacy["pattern"] = "changed";
        QVERIFY(!SubtitleRelease::matches("[Chosen] Show EP2.ass", 2, legacy));
        legacy = {{"version", 1}, {"valid", true}, {"sampleName", "[Group] Show Japanese.ass"},
            {"episode", 1}, {"mode", "exact"}, {"pattern", "[group]show japanese.ass"}};
        QVERIFY(SubtitleRelease::matches("[Group] Show Japanese.ass", 1, legacy));
        QVERIFY(!SubtitleRelease::matches("[Group] Show Japanese.ass", 2, legacy));
    }

    void unknownEpisodeDoesNotGuess()
    {
        const auto rule = SubtitleRelease::fromFile("Show - 05.ass", 0);
        QCOMPARE(rule.value("mode").toString(), QString("exact"));
        QVERIFY(SubtitleRelease::matches("Show - 05.ass", 0, rule));
        QVERIFY(!SubtitleRelease::matches("Show - 05.ass", 5, rule));
        QVERIFY(!SubtitleRelease::matches("Show - 06.ass", 6, rule));
    }

    void savedRulesRoundTripAndValidate()
    {
        const auto original = SubtitleRelease::fromFile("Show - 05.ass", 5);
        auto rule = QJsonDocument::fromJson(QJsonDocument(QJsonObject::fromVariantMap(original))
                                              .toJson()).object().toVariantMap();
        QVERIFY(SubtitleRelease::matches("Show - 06.ass", 6, rule));
        rule["pattern"] = ".*";
        QVERIFY(!SubtitleRelease::matches("Show - 06.ass", 6, rule));
        rule = original;
        rule["version"] = 99;
        QVERIFY(!SubtitleRelease::matches("Show - 06.ass", 6, rule));
        rule = original;
        rule["valid"] = false;
        QVERIFY(!SubtitleRelease::matches("Show - 06.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("Show - 06.ass", 6, {}));
        QVERIFY(SubtitleRelease::fromFile({}, 5).isEmpty());
        QVERIFY(SubtitleRelease::fromFile("Show - 05", 5).isEmpty());
        QVERIFY(SubtitleRelease::fromFile(QStringLiteral("Show - 05.ass") + QChar(1), 5).isEmpty());
    }
};

QTEST_APPLESS_MAIN(SubtitleReleaseTest)
#include "test_subtitlerelease.moc"
