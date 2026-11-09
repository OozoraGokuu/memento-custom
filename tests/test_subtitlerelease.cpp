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
        QCOMPARE(rule.value("mode").toString(), QString("template"));
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
        QCOMPARE(rule.value("mode").toString(), QString("template"));
        QVERIFY(SubtitleRelease::matches(next, 6, rule));
        QVERIFY(!SubtitleRelease::matches(next, 7, rule));
    }

    void harmlessNormalization()
    {
        const auto rule = SubtitleRelease::fromFile("[GROUP] Café - ０５.JA.ASS", 5);
        QVERIFY(SubtitleRelease::matches("[group] Cafe\u0301-06 . ja . ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("[group] Cafe-06.ja.ass", 6, rule));
        QVERIFY(!SubtitleRelease::matches("[group] Café_06.ja.ass", 6, rule));
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
        rule["version"] = 2;
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
