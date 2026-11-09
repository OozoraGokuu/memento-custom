#include "manager/subtitlelistmanager.h"
#include <QTemporaryDir>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QSemaphore>
#include <QScopeGuard>
#include <QThreadPool>
#include <QtConcurrent>
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

    void configureVideoContext(Context &context, MpvPlayer &player, const QString &path)
    {
        context.setPlayer(&player);
        auto *profile = context.ankiConfig()->profile();
        profile->setHostname("http://127.0.0.1");
        profile->setPort(QString::number(m_server.serverPort()));
        profile->setTermDeck("Sentence clips");
        profile->setTermModel("Synthetic video model");
        profile->termFields()->setItems(QJsonObject{{"Expression", "{expression}"},
            {"Sentence", "{sentence}"}, {"Video", ""}});
        // Inject a known player snapshot without opening a window or audio
        // device. The note builder still encodes the real fixture below.
        auto *state = player.state();
        state->setPath(path);
        state->setTitle("Frozen episode");
        state->setDuration(8.0);
        state->setVid(1);
        state->setAid(1);
        state->setVideoWidth(320);
        state->setVideoHeight(180);
        state->setTimePosition(1.5);
        state->subtitle()->setText(QStringLiteral("これはテストの字幕です。"));
        state->subtitle()->setStartTime(0.5);
        state->subtitle()->setEndTime(3.0);
        state->subtitle()->setDelay(0.25);
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

    void subtitleRegexUsesUnicodeAndReportsInvalidPatterns()
    {
        Settings settings;
        QCOMPARE(settings.searchMethod(), Setting::SearchMethodModifier);
        QCOMPARE(settings.searchModifier(), Setting::ModifierShift);
        QCOMPARE(settings.filterSubtitleText(QStringLiteral("猫ABC犬XYZ"), QStringLiteral("[A-Z]+")), QStringLiteral("猫犬"));
        QCOMPARE(settings.filterSubtitleText(QStringLiteral("猫ABC犬"), QStringLiteral("\\p{Latin}+")), QStringLiteral("猫犬"));
        QCOMPARE(settings.filterSubtitleText("ABC abc", "/abc/gi"), QString(" "));
        QCOMPARE(settings.filterSubtitleText("a\nb", "/^a$/m"), QString("\nb"));
        QCOMPARE(settings.filterSubtitleText("keep", "["), QString("keep"));
        QVERIFY(!settings.subtitleRegexError("[").isEmpty());
        QVERIFY(settings.subtitleRegexError("\\p{Han}+").isEmpty());
    }

    void videoDraftValidationCancellationAndSingleUse()
    {
        m_error.clear();
        m_lastRequest = {};
        Context context;
        MpvPlayer player;
        auto *client = context.ankiClient();
        QVERIFY(!client->prepareVideoNote(nullptr).value("success").toBool());
        configureVideoContext(context, player, "synthetic-video.mkv");
        Term term;
        term.setExpression(QStringLiteral("テスト"));
        auto draft = client->prepareVideoNote(&term);
        QVERIFY2(draft.value("success").toBool(), qPrintable(draft.value("error").toString()));
        QCOMPARE(draft.value("preferredField").toString(), QString("Video"));
        QCOMPARE(draft.value("start").toDouble(), 0.75);
        QCOMPARE(draft.value("end").toDouble(), 5.75);
        const auto cancelledToken = draft.value("token").toString();
        client->cancelVideoNote(cancelledToken);
        auto response = std::make_shared<std::optional<QVariantMap>>();
        [[maybe_unused]] auto cancelled = collect(client->addVideoNoteAsync(cancelledToken, "Video"), response);
        QTRY_VERIFY(response->has_value());
        QVERIFY(!response->value().value("success").toBool());

        draft = client->prepareVideoNote(&term);
        QVERIFY(draft.value("success").toBool());
        client->cancelVideoNote(cancelledToken); // A stale dialog cannot cancel this one.
        response = std::make_shared<std::optional<QVariantMap>>();
        [[maybe_unused]] auto invalid = collect(client->addVideoNoteAsync(draft.value("token").toString(), "Missing"), response);
        QTRY_VERIFY(response->has_value());
        QVERIFY(!response->value().value("success").toBool());
        response = std::make_shared<std::optional<QVariantMap>>();
        [[maybe_unused]] auto consumed = collect(client->addVideoNoteAsync(draft.value("token").toString(), "Video"), response);
        QTRY_VERIFY(response->has_value());
        QVERIFY(!response->value().value("success").toBool());
        QVERIFY(m_lastRequest.isEmpty());

        player.state()->subtitle()->setText("");
        QVERIFY(!client->prepareVideoNote(&term).value("success").toBool());
        player.state()->subtitle()->setText("Sentence");
        context.ankiConfig()->profile()->termFields()->setItems(QJsonObject{});
        QVERIFY(!client->prepareVideoNote(&term).value("success").toBool());
        context.setPlayer(nullptr);
    }

    void videoSubmissionKeepsCapturedMediaTermAndProfile()
    {
        m_error.clear();
        m_lastRequest = {};
        const QString fixture = QFINDTESTDATA("fixtures/sentence.mkv");
        QVERIFY(!fixture.isEmpty());
        QTemporaryDir temporary;
        const QString media = temporary.filePath(QStringLiteral("日本語 العربية episode.mkv"));
        QVERIFY(QFile::copy(fixture, media));
        Context context;
        MpvPlayer player;
        configureVideoContext(context, player, media);
        auto term = std::make_unique<Term>();
        term->setExpression(QStringLiteral("テスト"));
        term->setReading(QStringLiteral("てすと"));
        auto *client = context.ankiClient();
        const auto draft = client->prepareVideoNote(term.get());
        QVERIFY2(draft.value("success").toBool(), qPrintable(draft.value("error").toString()));
        const QString token = draft.value("token").toString();

        // Leaving the episode or closing its definition must not retarget the
        // pending note. The selected destination is frozen with the media too.
        player.state()->setPath(temporary.filePath("missing-next-episode.mkv"));
        player.state()->setDuration(0.0);
        player.state()->subtitle()->setText("Changed sentence");
        term.reset();
        context.ankiConfig()->profile()->setHostname("http://invalid.example");
        context.ankiConfig()->profile()->setTermDeck("Changed deck");
        context.ankiConfig()->profile()->termFields()->setItems(QJsonObject{{"Changed", ""}});
        auto response = std::make_shared<std::optional<QVariantMap>>();
        [[maybe_unused]] auto submission = collect(client->addVideoNoteAsync(token, "Video"), response);
        QTRY_VERIFY_WITH_TIMEOUT(response->has_value(), 30000);
        QVERIFY2(response->value().value("success").toBool(), qPrintable(response->value().value("error").toString()));
        const auto note = m_lastRequest.value("params").toObject().value("note").toObject();
        QCOMPARE(note.value("deckName").toString(), QString("Sentence clips"));
        const auto fields = note.value("fields").toObject();
        QCOMPARE(fields.value("Expression").toString(), QStringLiteral("テスト"));
        QCOMPARE(fields.value("Sentence").toString(), QStringLiteral("これはテストの字幕です。"));
        const auto videos = note.value("video").toArray();
        QCOMPARE(videos.size(), 1);
        const auto video = videos.first().toObject();
        QCOMPARE(video.value("fields").toArray(), QJsonArray{"Video"});
        QVERIFY(video.value("filename").toString().endsWith(".mp4"));
        const auto bytes = QByteArray::fromBase64(video.value("data").toString().toLatin1());
        QVERIFY(bytes.size() > 1000);
        QCOMPARE(bytes.mid(4, 4), QByteArray("ftyp"));
        m_lastRequest = {};
        response = std::make_shared<std::optional<QVariantMap>>();
        [[maybe_unused]] auto duplicate = collect(client->addVideoNoteAsync(token, "Video"), response);
        QTRY_VERIFY(response->has_value());
        QVERIFY(!response->value().value("success").toBool());
        QVERIFY(m_lastRequest.isEmpty());
        context.setPlayer(nullptr);
    }

    void closingClientDuringVideoEncodingDoesNotSubmit()
    {
        m_error.clear();
        m_lastRequest = {};
        const QString fixture = QFINDTESTDATA("fixtures/sentence.mkv");
        QVERIFY(!fixture.isEmpty());
        Context context;
        MpvPlayer player;
        configureVideoContext(context, player, fixture);
        auto client = std::make_unique<AnkiClient>(&context);
        Term term;
        term.setExpression("word");
        const auto draft = client->prepareVideoNote(&term);
        QVERIFY(draft.value("success").toBool());

        // Keep the encoder queued until its originating client is destroyed.
        // This exercises ownership independently of how fast the machine is.
        auto *pool = QThreadPool::globalInstance();
        pool->waitForDone();
        const int previousCount = pool->maxThreadCount();
        pool->setMaxThreadCount(1);
        QSemaphore gate;
        QSemaphore entered;
        auto blocker = QtConcurrent::run([&] {
            entered.release();
            gate.acquire();
        });
        const auto restorePool = qScopeGuard([&] {
            gate.release();
            blocker.waitForFinished();
            pool->waitForDone();
            pool->setMaxThreadCount(previousCount);
        });
        entered.acquire();
        auto response = std::make_shared<std::optional<QVariantMap>>();
        [[maybe_unused]] auto submission = collect(client->addVideoNoteAsync(
            draft.value("token").toString(), "Video"), response);
        client.reset();
        gate.release();
        QTRY_VERIFY_WITH_TIMEOUT(response->has_value(), 30000);
        QVERIFY(!response->value().value("success").toBool());
        QVERIFY(response->value().value("error").toString().contains("closed"));
        QVERIFY(m_lastRequest.isEmpty());
        context.setPlayer(nullptr);
    }

    void downloadedTrackReplacesNavigationTimeline()
    {
        Context context;
        MpvPlayer player;
        context.setPlayer(&player);
        SubtitleListManager manager(&context);
        QTemporaryDir folder;
        QFile oldFile(folder.filePath("old.srt")), downloaded(folder.filePath("downloaded.srt"));
        QVERIFY(oldFile.open(QIODevice::WriteOnly));
        oldFile.write("1\n00:00:01,000 --> 00:00:02,000\nOld\n\n"); oldFile.close();
        QVERIFY(downloaded.open(QIODevice::WriteOnly));
        downloaded.write("1\n00:00:08,000 --> 00:00:09,000\nNew\n\n"); downloaded.close();
        auto *oldTrack = new MpvTrack(&player), *newTrack = new MpvTrack(&player);
        oldTrack->setId(1); oldTrack->setExternal(true); oldTrack->setExternalFilename(oldFile.fileName());
        newTrack->setId(2); newTrack->setExternal(true); newTrack->setExternalFilename(downloaded.fileName());
        player.state()->setPath(folder.filePath("video.mkv"));
        player.state()->setSubtitleTracks({oldTrack}); player.state()->setSid(1);
        QTRY_VERIFY(context.subtitleLists()->primary() && context.subtitleLists()->primary()->fullTimelineReady());
        QCOMPARE(context.subtitleLists()->primary()->adjacentSubtitleStart(0, 1), 1.0);
        auto *retainedTrack = new MpvTrack(&player);
        retainedTrack->setId(1); retainedTrack->setExternal(true); retainedTrack->setExternalFilename(oldFile.fileName());
        player.state()->setSubtitleTracks({retainedTrack, newTrack});
        player.state()->setSecondarySid(1); player.state()->setSid(2);
        QTRY_VERIFY(context.subtitleLists()->primary() && context.subtitleLists()->primary()->fullTimelineReady());
        QCOMPARE(context.subtitleLists()->primary()->adjacentSubtitleStart(0, 1), 8.0);
        QCOMPARE(context.subtitleLists()->secondary()->adjacentSubtitleStart(0, 1), 1.0);
        player.state()->setSid(99);
        QVERIFY(!context.subtitleLists()->primary());
        context.setPlayer(nullptr);
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
