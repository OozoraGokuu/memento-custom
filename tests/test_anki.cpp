#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QUuid>
#include <optional>
#include <clocale>
#include "state/context.h"

class AnkiClientTest : public QObject
{
    Q_OBJECT
    QTcpServer m_server;
    QJsonObject m_lastRequest;
    QString m_error;

    static QCoro::Task<void> collect(QCoro::Task<QVariantMap> task,
        std::shared_ptr<std::optional<QVariantMap>> result)
    {
        *result = co_await task;
    }

private slots:
    void initTestCase()
    {
        // Match main.cpp: libmpv requires C numeric formatting after Qt starts.
        std::setlocale(LC_NUMERIC, "C");
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("anki-test-" + QUuid::createUuid().toString(QUuid::WithoutBraces));
        QVERIFY(QDir().mkpath(DirectoryUtils::getDataDir()));
        QVERIFY(QDir().mkpath(DirectoryUtils::getConfigDir()));
        Dictionary::createDatabaseInstance();
        QVERIFY(m_server.listen(QHostAddress::LocalHost));
        connect(&m_server, &QTcpServer::newConnection, this, [this] {
            auto *socket = m_server.nextPendingConnection();
            connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer = QByteArray()]() mutable {
                buffer += socket->readAll();
                const auto split = buffer.indexOf("\r\n\r\n");
                if (split < 0) return;
                qsizetype length = 0;
                for (const auto &line : buffer.left(split).split('\n'))
                    if (line.toLower().startsWith("content-length:")) length = line.mid(15).trimmed().toLongLong();
                if (buffer.size() - split - 4 < length) return;
                m_lastRequest = QJsonDocument::fromJson(buffer.mid(split + 4, length)).object();
                const auto action = m_lastRequest.value("action").toString();
                QJsonValue result;
                if (action == "version") result = 6;
                else if (action == "deckNames") result = QJsonArray{QStringLiteral("日本語")};
                else if (action == "modelFieldNames") result = QJsonArray{"Expression"};
                else if (action == "addNote") result = 12345;
                const auto body = QJsonDocument(QJsonObject{{"result", result},
                    {"error", m_error.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(m_error)}}).toJson();
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                    QByteArray::number(body.size()) + "\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }

    void connectionFieldsAndNoteOverRealHttp()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        auto *profile = context.ankiConfig()->profile();
        profile->setHostname("http://127.0.0.1");
        profile->setPort(QString::number(m_server.serverPort()));
        profile->setUseApiKey(true);
        profile->setApiKey("synthetic-test-key");
        profile->setTermDeck(QStringLiteral("日本語"));
        profile->setTermModel("Synthetic Model");
        profile->termFields()->setItems(QJsonObject{{"Expression", "{expression}"}});
        auto *client = context.ankiClient();

        auto response = std::make_shared<std::optional<QVariantMap>>();
        [[maybe_unused]] auto connection = collect(client->testConnectionAsync(), response);
        QTRY_VERIFY_WITH_TIMEOUT(response->has_value(), 5000);
        QVERIFY(response->value().value("success").toBool());
        QCOMPARE(m_lastRequest.value("key").toString(), QString("synthetic-test-key"));

        response = std::make_shared<std::optional<QVariantMap>>();
        [[maybe_unused]] auto decks = collect(client->getDeckNamesAsync(), response);
        QTRY_VERIFY_WITH_TIMEOUT(response->has_value(), 5000);
        QVERIFY(response->value().value("success").toBool());
        QCOMPARE(response->value().value("decks").toStringList(), QStringList{QStringLiteral("日本語")});

        response = std::make_shared<std::optional<QVariantMap>>();
        [[maybe_unused]] auto fields = collect(client->getFieldNamesAsync("Synthetic Model"), response);
        QTRY_VERIFY_WITH_TIMEOUT(response->has_value(), 5000);
        QCOMPARE(response->value().value("fields").toStringList(), QStringList{"Expression"});

        Term term;
        term.setExpression(QStringLiteral("食べる"));
        term.setReading(QStringLiteral("たべる"));
        response = std::make_shared<std::optional<QVariantMap>>();
        [[maybe_unused]] auto note = collect(client->addNoteAsync(&term), response);
        QTRY_VERIFY_WITH_TIMEOUT(response->has_value(), 5000);
        QVERIFY2(response->value().value("success").toBool(), qPrintable(response->value().value("error").toString()));
        QCOMPARE(response->value().value("id").toInt(), 12345);
        const auto sentNote = m_lastRequest.value("params").toObject().value("note").toObject();
        QCOMPARE(sentNote.value("deckName").toString(), QStringLiteral("日本語"));
        QCOMPARE(sentNote.value("fields").toObject().value("Expression").toString(), QStringLiteral("食べる"));

        m_error = "Synthetic authentication failure";
        response = std::make_shared<std::optional<QVariantMap>>();
        [[maybe_unused]] auto rejected = collect(client->testConnectionAsync(), response);
        QTRY_VERIFY_WITH_TIMEOUT(response->has_value(), 5000);
        QVERIFY(!response->value().value("success").toBool());
        QCOMPARE(response->value().value("error").toString(), m_error);
        context.setPlayer(nullptr);
    }

    void cleanupTestCase() { Dictionary::destroyDatabaseInstance(); }
};

QTEST_MAIN(AnkiClientTest)
#include "test_anki.moc"
