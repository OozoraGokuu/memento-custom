#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <zip.h>
#include "dict/databasemanager.h"
#include "dict/dictionarycontroller.h"
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>

class TestDictionaryDatabase : public Dictionary {
public:
    static void use(DatabaseManager *db) { m_db = db; }
};

class DictionaryTest : public QObject
{
    Q_OBJECT
private:
    QByteArray archiveFor(const QString &path, const QString &title) {
        int error = 0;
        auto *archive = zip_open(path.toUtf8().constData(), ZIP_CREATE, &error);
        if (!archive) return {};
        const QByteArray index = QString("{\"title\":\"%1\",\"format\":3,\"revision\":\"test\"}").arg(title).toUtf8();
        auto *source = zip_source_buffer(archive, index.constData(), index.size(), 0);
        if (!source || zip_file_add(archive, "index.json", source, 0) < 0) { zip_discard(archive); return {}; }
        if (zip_close(archive)) return {};
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) return {};
        return file.readAll();
    }
private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }
    void queuedImportsAndDefaultDeduplication() {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        DatabaseManager db(temporary.filePath("database.db"), temporary.filePath("resources"));
        TestDictionaryDatabase::use(&db);
        const auto first = temporary.filePath("definitions.zip"), second = temporary.filePath("frequency.zip");
        QVERIFY(!archiveFor(first, "Jitendex test").isEmpty());
        QVERIFY(!archiveFor(second, "JPDB test").isEmpty());
        {
            DictionaryController controller(nullptr);
            QVERIFY(!controller.busy()); // Test/smoke profiles never auto-download.
            controller.addDictionary(QUrl::fromLocalFile(first).toString());
            controller.addDictionary(second);
            QVERIFY(controller.busy());
            QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 15000);
            QCOMPARE(controller.dictionaries()->items().size(), 2);
            QCOMPARE(controller.progress(), 1.0);
            controller.installDefaults();
            QVERIFY(!controller.busy());
            QVERIFY(controller.activity().contains("installed"));
        }
        TestDictionaryDatabase::use(nullptr);
    }
    void downloadImportCancellationAndFailure() {
        QTemporaryDir temporary;
        DatabaseManager db(temporary.filePath("database.db"), temporary.filePath("resources"));
        TestDictionaryDatabase::use(&db);
        const QByteArray data = archiveFor(temporary.filePath("download.zip"), "Downloaded dictionary");
        QVERIFY(!data.isEmpty());
        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost));
        connect(&server, &QTcpServer::newConnection, &server, [&] {
            auto *socket = server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [socket, data] {
                const auto request = socket->readAll();
                if (request.contains("/wait")) return;
                const auto body = request.contains("/invalid") ? QByteArray("not an archive") : data;
                socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
        const QString origin = QString("http://127.0.0.1:%1").arg(server.serverPort());
        {
            DictionaryController controller(nullptr);
            controller.m_imports.enqueue({"Download", {}, QUrl(origin + "/valid")});
            controller.startNextImport();
            QVERIFY(controller.busy());
            QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 15000);
            QCOMPARE(controller.dictionaries()->items().size(), 1);
            QVERIFY(controller.m_archive == nullptr);
            controller.m_imports.enqueue({"Wait", {}, QUrl(origin + "/wait")});
            controller.startNextImport();
            controller.cancelPendingImports();
            QTRY_VERIFY(!controller.busy());
            QVERIFY(controller.activity().contains("cancelled"));
            QVERIFY(controller.m_archive == nullptr);
            controller.m_imports.enqueue({"Bad", {}, QUrl(origin + "/invalid")});
            controller.startNextImport();
            QTRY_VERIFY_WITH_TIMEOUT(!controller.busy(), 15000);
            QVERIFY(!controller.lastError().isEmpty());
            QCOMPARE(controller.dictionaries()->items().size(), 1);
        }
        TestDictionaryDatabase::use(nullptr);
    }
    void optionalRealDefaultImports() {
        const QString definitions = qEnvironmentVariable("MEMENTO_TEST_JITENDEX");
        const QString frequencies = qEnvironmentVariable("MEMENTO_TEST_JPDB");
        if (definitions.isEmpty() || frequencies.isEmpty()) QSKIP("Live dictionaries are verified separately from offline CI fixtures.");
        QTemporaryDir temporary;
        DatabaseManager db(temporary.filePath("database.db"), temporary.filePath("resources"));
        QCOMPARE(db.addDictionary(definitions), 0);
        QCOMPARE(db.addDictionary(frequencies), 0);
        QCOMPARE(db.getDictionaries(this).size(), 2);
        const auto terms = db.queryTerms(QStringLiteral("食べる"), this);
        QVERIFY(!terms.isEmpty());
        QVERIFY(!terms.first()->definitions().isEmpty());
        QVERIFY(!terms.first()->frequencies().isEmpty());
    }
    void importLookupDisableAndRemove()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const QString folder = temporary.filePath(QStringLiteral("日本語 العربية"));
        QVERIFY(QDir().mkpath(folder));
        const QString archivePath = folder + "/dictionary.zip";
        int error = 0;
        zip_t *archive = zip_open(archivePath.toUtf8().constData(), ZIP_CREATE, &error);
        QVERIFY(archive);
        const QList<QPair<QByteArray, QByteArray>> files{
            {"index.json", R"({"title":"Synthetic Dictionary","format":3,"revision":"test","sequenced":true})"},
            {"term_bank_1.json", QStringLiteral("[[\"食べる\",\"たべる\",\"\",\"v1\",0,[\"to eat\"],1,\"\"]]").toUtf8()},
            {"kanji_bank_1.json", QStringLiteral("[[\"食\",\"ショク\",\"た.べる\",\"\",[\"eat\"],{}]]").toUtf8()},
        };
        for (const auto &[name, contents] : files) {
            auto *source = zip_source_buffer(archive, contents.constData(), contents.size(), 0);
            QVERIFY(source);
            QVERIFY(zip_file_add(archive, name.constData(), source, ZIP_FL_ENC_UTF_8) >= 0);
        }
        QCOMPARE(zip_close(archive), 0);
        DatabaseManager db(folder + "/dictionary.db", folder + "/resources");
        QCOMPARE(db.addDictionary(archivePath), 0);
        const auto dictionaries = db.getDictionaries(this);
        QCOMPARE(dictionaries.size(), 1);
        QCOMPARE(dictionaries.first()->name(), QString("Synthetic Dictionary"));
        const auto id = dictionaries.first()->id();
        QString lookupError;
        const auto terms = db.queryTerms(QStringLiteral("食べる"), this, &lookupError);
        QVERIFY2(lookupError.isEmpty(), qPrintable(lookupError));
        QCOMPARE(terms.size(), 1);
        QCOMPARE(terms.first()->reading(), QStringLiteral("たべる"));
        QVERIFY(db.queryKanji(QStringLiteral("食"), this, &lookupError));
        QVERIFY(lookupError.isEmpty());
        QCOMPARE(db.disableDictionary(id), 0);
        QVERIFY(db.queryTerms(QStringLiteral("食べる"), this).isEmpty());
        QCOMPARE(db.enableDictionary(id), 0);
        QCOMPARE(db.queryTerms(QStringLiteral("食べる"), this).size(), 1);
        QCOMPARE(db.deleteDictionary(id), 0);
        QVERIFY(db.getDictionaries(this).isEmpty());
        QVERIFY(db.queryTerms(QStringLiteral("食べる"), this).isEmpty());
    }
};

QTEST_GUILESS_MAIN(DictionaryTest)
#include "test_dictionary.moc"
