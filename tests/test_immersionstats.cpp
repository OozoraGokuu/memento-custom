#include <QTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

#include "state/immersionstats.h"
#include "util/directoryutils.h"

class ImmersionStatsTest : public QObject
{
    Q_OBJECT

    QString m_dataDir;

    void writeFixture(const QJsonObject &root = {})
    {
        QFile file(m_dataDir + "/immersion-stats.json");
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray bytes = QJsonDocument(root).toJson();
        QCOMPARE(file.write(bytes), bytes.size());
    }

    static QVariantMap day(const QVariantMap &calendar, const QString &date)
    {
        for (const QVariant &entry : calendar.value("days").toList())
            if (entry.toMap().value("date").toString() == date) return entry.toMap();
        return {};
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("immersion-calendar-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        m_dataDir = DirectoryUtils::getDataDir();
        QVERIFY(QDir().mkpath(m_dataDir));
    }

    void init() { writeFixture(); }

    void emptyHistoryAndInvalidYears()
    {
        ImmersionStats stats(nullptr);
        const QDate today = QDate::currentDate();
        const auto calendar = stats.calendar(today.year());
        QCOMPARE(calendar.value("year").toInt(), today.year());
        QCOMPARE(calendar.value("years").toList(), QVariantList{today.year()});
        QCOMPARE(calendar.value("defaultDate").toString(), today.toString(Qt::ISODate));
        QCOMPARE(calendar.value("activeSeconds").toDouble(), 0.0);
        QCOMPARE(calendar.value("activeDays").toInt(), 0);
        QCOMPARE(calendar.value("completedCount").toInt(), 0);
        QCOMPARE(stats.calendar(0).value("year").toInt(), today.year());
        QCOMPARE(stats.calendar(-1).value("year").toInt(), today.year());
        QCOMPARE(stats.calendar(today.year() + 1).value("year").toInt(), today.year());
        for (const QVariant &entry : calendar.value("days").toList())
        {
            const auto cell = entry.toMap();
            const QDate date = QDate::fromString(cell.value("date").toString(), Qt::ISODate);
            QCOMPARE(cell.value("seconds").toDouble(), 0.0);
            QCOMPARE(cell.value("level").toInt(), 0);
            QCOMPARE(cell.value("future").toBool(), date > today);
            QCOMPARE(cell.value("enabled").toBool(), date.year() == today.year() && date <= today);
        }
    }

    void calendarGeometry_data()
    {
        QTest::addColumn<int>("year");
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("weeks");
        QTest::newRow("common-sunday-start") << 2023 << 365 << 53;
        QTest::newRow("leap-monday-start") << 2024 << 366 << 53;
        QTest::newRow("leap-sunday-start-54-weeks") << 2012 << 366 << 54;
        QTest::newRow("common-midweek-start") << 2025 << 365 << 53;
    }

    void calendarGeometry()
    {
        QFETCH(int, year); QFETCH(int, count); QFETCH(int, weeks);
        ImmersionStats stats(nullptr);
        const auto calendar = stats.calendar(year);
        const auto days = calendar.value("days").toList();
        QCOMPARE(calendar.value("weeks").toInt(), weeks);
        QCOMPARE(days.size(), weeks * 7);
        const QDate first = QDate::fromString(days.first().toMap().value("date").toString(), Qt::ISODate);
        const QDate last = QDate::fromString(days.last().toMap().value("date").toString(), Qt::ISODate);
        QCOMPARE(first.dayOfWeek(), 1);
        QCOMPARE(last.dayOfWeek(), 7);
        int inYearCount = 0;
        for (int i = 0; i < days.size(); ++i)
        {
            const auto cell = days.at(i).toMap();
            const QDate date = QDate::fromString(cell.value("date").toString(), Qt::ISODate);
            QCOMPARE(date, first.addDays(i));
            QCOMPARE(date.dayOfWeek(), i % 7 + 1);
            QCOMPARE(cell.value("inYear").toBool(), date.year() == year);
            if (cell.value("inYear").toBool()) ++inYearCount;
            else QVERIFY(!cell.value("enabled").toBool());
        }
        QCOMPARE(inYearCount, count);
        QCOMPARE(!day(calendar, QString::number(year) + "-02-29").isEmpty(), QDate::isLeapYear(year));
        const auto months = calendar.value("months").toList();
        QCOMPARE(months.size(), 12);
        for (int month = 1; month <= 12; ++month)
        {
            const auto marker = months.at(month - 1).toMap();
            QCOMPARE(marker.value("week").toInt(), int(first.daysTo(QDate(year, month, 1))) / 7);
            QVERIFY(!marker.value("label").toString().isEmpty());
        }
    }

    void historicalTotalsAndTitles()
    {
        writeFixture({
            {"dailySeconds", QJsonObject{{"2023-01-01", 10}, {"2024-01-01", 30},
                {"2024-02-29", 1800}, {"2024-12-31", 3600}, {"2024-03-01", -100},
                {"invalid", 999999}, {"2999-01-01", 999999}}},
            {"dailyTitles", QJsonObject{{"2024-02-29", QJsonObject{{"a", 1200}, {"b", 600}, {"bad", -10}}}}},
            {"titles", QJsonObject{{"a", "Anime A"}, {"b", "Anime B"}}},
            {"dailyCompletions", QJsonObject{{"2024-02-29", 1}, {"2024-12-31", 2}, {"2024-03-01", -5}}}
        });
        ImmersionStats stats(nullptr);
        const auto calendar = stats.calendar(2024);
        QCOMPARE(calendar.value("activeSeconds").toDouble(), 5430.0);
        QCOMPARE(calendar.value("activeDays").toInt(), 2);
        QCOMPARE(calendar.value("completedCount").toInt(), 3);
        QCOMPARE(calendar.value("defaultDate").toString(), QString("2024-12-31"));
        const auto years = calendar.value("years").toList();
        QCOMPARE(years.first().toInt(), QDate::currentDate().year());
        QCOMPARE(years.last().toInt(), 2023);
        for (int i = 1; i < years.size(); ++i) QCOMPARE(years[i].toInt(), years[i - 1].toInt() - 1);
        const auto leap = day(calendar, "2024-02-29");
        QCOMPARE(leap.value("seconds").toDouble(), 1800.0);
        QCOMPARE(leap.value("completedCount").toInt(), 1);
        const auto titles = leap.value("titles").toList();
        QCOMPARE(titles.size(), 2);
        QCOMPARE(titles[0].toMap().value("title").toString(), QString("Anime A"));
        QCOMPARE(titles[0].toMap().value("seconds").toDouble(), 1200.0);
        QCOMPARE(titles[1].toMap().value("title").toString(), QString("Anime B"));
        QCOMPARE(day(calendar, "2024-03-01").value("seconds").toDouble(), 0.0);
        QCOMPARE(day(calendar, "2024-03-01").value("completedCount").toInt(), 0);
    }

    void stableIntensityThresholds()
    {
        const QList<double> seconds{0, 1, 899, 900, 1799, 1800, 3599, 3600, 18000};
        const QList<int> levels{0, 1, 1, 2, 2, 3, 3, 4, 4};
        QJsonObject daily;
        for (int i = 0; i < seconds.size(); ++i)
            daily.insert(QDate(2024, 1, 1).addDays(i).toString(Qt::ISODate), seconds[i]);
        writeFixture({{"dailySeconds", daily}});
        ImmersionStats stats(nullptr);
        const auto calendar = stats.calendar(2024);
        for (int i = 0; i < seconds.size(); ++i)
            QCOMPARE(day(calendar, QDate(2024, 1, 1).addDays(i).toString(Qt::ISODate)).value("level").toInt(), levels[i]);
    }

    void futureActivityExcludedAndHistoryPreserved()
    {
        const QDate today = QDate::currentDate();
        const QString now = today.toString(Qt::ISODate);
        const QString future = today.addDays(1).toString(Qt::ISODate);
        writeFixture({{"dailySeconds", QJsonObject{{now, 60}, {future, 900}}},
            {"dailyTitles", QJsonObject{{now, QJsonObject{{"show", 60}}}, {future, QJsonObject{{"show", 900}}}}},
            {"titles", QJsonObject{{"show", "Synthetic show"}}},
            {"dailyCompletions", QJsonObject{{future, 3}}}});
        QVariantMap before;
        {
            ImmersionStats stats(nullptr);
            before = stats.calendar(today.year());
            QCOMPARE(before.value("activeSeconds").toDouble(), 60.0);
            QCOMPARE(before.value("completedCount").toInt(), 0);
            QCOMPARE(before.value("defaultDate").toString(), now);
            const auto tomorrow = day(before, future);
            if (!tomorrow.isEmpty())
            {
                QVERIFY(!tomorrow.value("enabled").toBool());
                QCOMPARE(tomorrow.value("seconds").toDouble(), 0.0);
                QVERIFY(tomorrow.value("titles").toList().isEmpty());
            }
        }
        ImmersionStats reloaded(nullptr);
        QCOMPARE(reloaded.calendar(today.year()), before);
    }

    void cleanupTestCase() { QVERIFY(QDir(m_dataDir).removeRecursively()); }
};

QTEST_GUILESS_MAIN(ImmersionStatsTest)
#include "test_immersionstats.moc"
