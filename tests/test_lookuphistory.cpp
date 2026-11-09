#include <QTest>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <zip.h>

#include "dict/databasemanager.h"
#include "dict/dictionarysearch.h"
#include "dict/dictionarysearchcontroller.h"
#include "setting/settings.h"
#include "state/lookuphistory.h"
#include "util/directoryutils.h"

class LookupTestDatabase : public Dictionary
{
public:
    static void use(DatabaseManager *database) { m_db = database; }
};

class LookupHistoryTest : public QObject
{
    Q_OBJECT

    static QVariantList entries(const LookupHistory &history)
    {
        return history.query({}, 0, 0, 100).value("entries").toList();
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("lookup-history-test-" +
            QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(QDir().mkpath(DirectoryUtils::getDataDir()));
        QVERIFY(QDir().mkpath(DirectoryUtils::getConfigDir()));
    }

    void persistenceAndSourceContext()
    {
        QTemporaryDir temporary;
        const QString path = temporary.filePath("history.json");
        const QString media = temporary.filePath(QStringLiteral("作品/第01話.mkv"));
        QString firstId;
        {
            LookupHistory history(nullptr, nullptr, path);
            const QVariantMap context = history.contextFor(media, 89.25);
            QCOMPARE(context.value("episode").toString(), QStringLiteral("第01話.mkv"));
            history.record(QStringLiteral("食べる"), QStringLiteral("たべる"),
                           QStringLiteral("何を食べる？"), context);
            history.record(QStringLiteral("食べる"), QStringLiteral("たべる"),
                           QStringLiteral("何を食べる？"), context);
            QCOMPARE(entries(history).size(), 1); // Duplicate hover notification.
            const QVariantMap first = entries(history).first().toMap();
            firstId = first.value("id").toString();
            QCOMPARE(first.value("path").toString(), media);
            QCOMPARE(first.value("position").toDouble(), 89.25);
            history.record(QStringLiteral("猫"), QStringLiteral("ねこ"), QStringLiteral("猫"), {});
            QCOMPARE(entries(history).size(), 2);
            const QVariantMap manual = entries(history).first().toMap();
            QVERIFY(manual.value("title").toString().isEmpty());
            QVERIFY(manual.value("path").toString().isEmpty());
            QCOMPARE(manual.value("position").toDouble(), -1.0);
            QVERIFY(history.lastError().isEmpty());
        }
        LookupHistory loaded(nullptr, nullptr, path);
        QCOMPARE(entries(loaded).size(), 2);
        QCOMPARE(entries(loaded).last().toMap().value("text").toString(), QStringLiteral("何を食べる？"));
        QCOMPARE(loaded.query(QStringLiteral("たべ"), 1, 0, 20).value("total").toInt(), 1);
        QCOMPARE(loaded.query(QStringLiteral("第01"), 0, 0, 20).value("total").toInt(), 1);
        QVERIFY(loaded.remove(firstId));
        QVERIFY(!loaded.remove(firstId));
        LookupHistory afterRemove(nullptr, nullptr, path);
        QCOMPARE(entries(afterRemove).size(), 1);
        QVERIFY(afterRemove.clear());
        LookupHistory afterClear(nullptr, nullptr, path);
        QVERIFY(entries(afterClear).isEmpty());
    }

    void dateFilteringPaginationAndUntrustedFiles()
    {
        QTemporaryDir temporary;
        const QString path = temporary.filePath("history.json");
        QJsonArray fixture;
        const QDateTime today(QDate::currentDate(), QTime(12, 0));
        for (int index = 0; index < 5; ++index)
        {
            fixture.append(QJsonObject{{"id", QString::number(index)},
                {"word", QString("word%1").arg(index)},
                {"lookedUp", today.addDays(-index).toUTC().toString(Qt::ISODateWithMs)}});
        }
        fixture.append(fixture.first()); // Duplicate IDs are ignored.
        fixture.append(QJsonObject{{"id", "bad"}, {"word", "invalid date"}, {"lookedUp", "broken"}});
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(QJsonObject{{"version", 1}, {"entries", fixture}}).toJson());
        file.close();
        LookupHistory history(nullptr, nullptr, path);
        QCOMPARE(history.query({}, 0, 0, 20).value("total").toInt(), 5);
        QCOMPARE(history.query({}, 3, 0, 20).value("total").toInt(), 3);
        const QVariantMap page = history.query({}, 0, 2, 2);
        QCOMPARE(page.value("total").toInt(), 5);
        QCOMPARE(page.value("entries").toList().size(), 2);
        QCOMPARE(page.value("entries").toList().first().toMap().value("word").toString(), QString("word2"));
        QVERIFY(history.query({}, 0, 99, 20).value("entries").toList().isEmpty());

        const QString remote = "https://username:secret@example.invalid/video.mkv?token=secret";
        history.record("safe", {}, "scene", {{"path", remote}, {"title", "Stream"}, {"position", 5.0}});
        QVERIFY(entries(history).first().toMap().value("path").toString().isEmpty());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("secret"));
    }

    void failedWritesDoNotPretendToPersist()
    {
        QTemporaryDir temporary;
        // A directory cannot be atomically replaced by the history file.
        LookupHistory history(nullptr, nullptr, temporary.path());
        history.record("word", {}, {}, {});
        QVERIFY(!history.lastError().isEmpty());
        QVERIFY(entries(history).isEmpty());
        QVERIFY(!history.clear());
        QCOMPARE(history.revision(), 0);
    }

    void successfulSearchesAreDebouncedAndCoalesced()
    {
        QTemporaryDir temporary;
        const QString archivePath = temporary.filePath("dictionary.zip");
        int error = 0;
        zip_t *archive = zip_open(archivePath.toUtf8().constData(), ZIP_CREATE, &error);
        QVERIFY(archive);
        const QList<QPair<QByteArray, QByteArray>> files{
            {"index.json", R"({"title":"Lookup fixture","format":3,"revision":"test","sequenced":true})"},
            {"term_bank_1.json", QStringLiteral("[[\"食べる\",\"たべる\",\"\",\"v1\",0,[\"to eat\"],1,\"\"],[\"猫\",\"ねこ\",\"\",\"\",0,[\"cat\"],2,\"\"]]").toUtf8()},
            {"kanji_bank_1.json", QStringLiteral("[[\"食\",\"ショク\",\"た.べる\",\"\",[\"eat\"],{}],[\"犬\",\"ケン\",\"いぬ\",\"\",[\"dog\"],{}]]").toUtf8()}
        };
        for (const auto &[name, contents] : files)
        {
            auto *source = zip_source_buffer(archive, contents.constData(), contents.size(), 0);
            QVERIFY(source);
            QVERIFY(zip_file_add(archive, name.constData(), source, ZIP_FL_ENC_UTF_8) >= 0);
        }
        QCOMPARE(zip_close(archive), 0);
        DatabaseManager database(temporary.filePath("dictionary.db"), temporary.filePath("resources"));
        QCOMPARE(database.addDictionary(archivePath), 0);
        LookupTestDatabase::use(&database);
        Settings settings;
        DictionarySearchController::createInstance(&settings);
        struct Cleanup {
            ~Cleanup() {
                DictionarySearchController::destroyInstance();
                LookupTestDatabase::use(nullptr);
            }
        } cleanup;
        DictionarySearch search;
        QSignalSpy recorded(&search, &DictionarySearch::lookupCompleted);
        const QVariantMap context{{"title", "Original episode"}, {"position", 89.0}};
        search.setHistoryContext(context);
        search.searchTerms(QStringLiteral("食べる"), QStringLiteral("食べる"), 0);
        search.searchKanji(QStringLiteral("食"), QStringLiteral("食べる"), 0);
        search.setHistoryContext({{"title", "Changed after invocation"}});
        QTRY_COMPARE_WITH_TIMEOUT(recorded.size(), 1, 5000);
        QCOMPARE(recorded.first().at(0).toString(), QStringLiteral("食べる"));
        QCOMPARE(recorded.first().at(1).toString(), QStringLiteral("たべる"));
        QCOMPARE(recorded.first().at(3).toMap(), context);
        QTest::qWait(600);
        QCOMPARE(recorded.size(), 1);

        // Cancelled results and a failed query must never enter history.
        search.searchTerms(QStringLiteral("食べる"), QStringLiteral("食べる"), 0);
        search.searchKanji(QStringLiteral("食"), QStringLiteral("食べる"), 0);
        search.clearResults();
        search.searchTerms("zzzz", "zzzz", 0);
        search.searchKanji("z", "zzzz", 0);
        QTest::qWait(700);
        QCOMPARE(recorded.size(), 1);

        // Rapid typing/hovering keeps only the final successful query.
        search.searchTerms(QStringLiteral("食べる"), QStringLiteral("食べる"), 0);
        search.searchKanji(QStringLiteral("食"), QStringLiteral("食べる"), 0);
        search.searchTerms(QStringLiteral("猫"), QStringLiteral("猫"), 0);
        search.searchKanji(QStringLiteral("猫"), QStringLiteral("猫"), 0);
        QTRY_COMPARE_WITH_TIMEOUT(recorded.size(), 2, 5000);
        QCOMPARE(recorded.last().at(0).toString(), QStringLiteral("猫"));

        // A successful kanji lookup is retained when there is no term match.
        search.searchTerms(QStringLiteral("犬"), QStringLiteral("犬"), 0);
        search.searchKanji(QStringLiteral("犬"), QStringLiteral("犬"), 0);
        QTRY_COMPARE_WITH_TIMEOUT(recorded.size(), 3, 5000);
        QCOMPARE(recorded.last().at(0).toString(), QStringLiteral("犬"));
        QCOMPARE(recorded.last().at(1).toString(), QString{});

        // Direct observers may clear results before the next observer receives
        // the signal. Every observer must still get the original source data.
        const auto clearing = connect(&search, &DictionarySearch::lookupCompleted,
            &search, [&search] { search.clearResults(); });
        QSignalSpy afterClear(&search, &DictionarySearch::lookupCompleted);
        search.setHistoryContext(context);
        search.searchTerms(QStringLiteral("食べる"), QStringLiteral("食べる"), 0);
        search.searchKanji(QStringLiteral("食"), QStringLiteral("食べる"), 0);
        QTRY_COMPARE_WITH_TIMEOUT(afterClear.size(), 1, 5000);
        QCOMPARE(afterClear.first().at(0).toString(), QStringLiteral("食べる"));
        QCOMPARE(afterClear.first().at(1).toString(), QStringLiteral("たべる"));
        QCOMPARE(afterClear.first().at(2).toString(), QStringLiteral("食べる"));
        QCOMPARE(afterClear.first().at(3).toMap(), context);
        disconnect(clearing);

        // A popup can disappear synchronously while receiving search results.
        QPointer<DictionarySearch> disappearing = new DictionarySearch;
        QSignalSpy abandoned(disappearing.data(), &DictionarySearch::lookupCompleted);
        connect(disappearing.data(), &DictionarySearch::termsChanged, this,
            [&disappearing] {
                if (disappearing) {
                    auto *searchToDelete = disappearing.data();
                    disappearing.clear();
                    delete searchToDelete;
                }
            });
        disappearing->searchTerms(QStringLiteral("食べる"), QStringLiteral("食べる"), 0);
        disappearing->searchKanji(QStringLiteral("食"), QStringLiteral("食べる"), 0);
        QTRY_VERIFY_WITH_TIMEOUT(disappearing.isNull(), 5000);
        QTest::qWait(600);
        QCOMPARE(abandoned.size(), 0);
    }

    void cleanupTestCase()
    {
        QDir(DirectoryUtils::getDataDir()).removeRecursively();
        QDir(DirectoryUtils::getConfigDir()).removeRecursively();
    }
};

QTEST_MAIN(LookupHistoryTest)
#include "test_lookuphistory.moc"
