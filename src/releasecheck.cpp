#include "releasecheck.h"
#include "state/context.h"
#include "player/mpvplayer.h"
#include "torrentsearch/torrentsearchnetworkreply.h"
#include <QCoreApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QTimer>
#include <QFile>
#include <QQuickWindow>
#ifdef MEMENTO_MECAB_SUPPORT
#include "dict/mecabquerygenerator.h"
#endif

namespace {
void fail(const QString &message)
{
    qCritical().noquote() << "Release check:" << message;
    QCoreApplication::exit(1);
}
}

void runReleaseCheck(Context &context, QQmlApplicationEngine &engine)
{
    if (!context.ankiConfig()->profile()->apiKey().isEmpty() ||
        context.settings()->behaviorSubtitleAutoCopy() ||
        context.jimakuClient()->apiKeyConfigured() ||
        !context.aniListClient()->clientId().isEmpty() ||
        !context.aniListClient()->username().isEmpty() ||
        context.aniListClient()->connected() || context.aniListClient()->enabled() ||
        context.migakuClient()->enabled() || !context.migakuClient()->exportFolder().isEmpty() ||
        !context.episodeLibrary()->library().isEmpty() ||
        !context.torrentsearchClient()->baseUrl().isEmpty() ||
        context.dictionaryController()->busy()) {
        fail("Fresh profile contains account, export or library state.");
        return;
    }
#ifdef MEMENTO_MECAB_SUPPORT
    MeCabQueryGenerator grammar;
    if (!grammar.valid() || grammar.generateQueries(QStringLiteral("食べました")).empty()) {
        fail("Bundled MeCab dictionary cannot analyze Japanese.");
        return;
    }
#endif
    // Options contains every settings page, including the lazy-loaded windows
    // that an ordinary main-window startup does not exercise.
    QQmlComponent options(&engine, "Ripose.Memento", "OptionsWindow");
    QObject *window = options.create();
    if (!window) {
        fail(options.errorString());
        return;
    }
    window->setParent(&engine);
    QObject *activity = engine.rootObjects().isEmpty() ? nullptr :
        engine.rootObjects().first()->findChild<QObject *>("activityPanel");
    if (!activity) { fail("Activity panel is missing."); return; }
    auto *previousSubtitles = context.subtitleLists()->primary();
    auto *subtitles = new SubtitleListModel(&context, &engine);
    context.subtitleLists()->setPrimary(subtitles);
    subtitles->setLoading(true);
    if (!activity->property("visible").toBool()) { fail("Subtitle loading is not visible in the Activity panel."); return; }
    subtitles->setLoading(false);
    if (activity->property("visible").toBool()) { fail("Activity panel did not clear completed subtitle loading."); return; }
    const QString uiScreenshot = qEnvironmentVariable("MEMENTO_TEST_UI_SCREENSHOT");
    if (!uiScreenshot.isEmpty()) {
        subtitles->setLoading(true);
        activity->setProperty("lastMessage", "Dictionary downloads and episode buffering appear here too.");
        auto *mainWindow = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QTimer::singleShot(500, &engine, [mainWindow, uiScreenshot] {
            if (!mainWindow || !mainWindow->grabWindow().save(uiScreenshot)) { fail("Could not capture activity UI."); return; }
            QCoreApplication::exit(0);
        });
        return;
    }
    context.subtitleLists()->setPrimary(previousSubtitles);
    subtitles->deleteLater();
    const QString fixture = qEnvironmentVariable("MEMENTO_TEST_MEDIA");
    if (fixture.isEmpty()) {
        if (qEnvironmentVariableIsSet("MEMENTO_TEST_NETWORK")) {
            QNetworkRequest request(QUrl("https://www.cloudflare.com/cdn-cgi/trace"));
            request.setRawHeader("User-Agent", "Memento-release-check/2.3");
            auto remaining = std::make_shared<int>(3);
            // AniList and subtitle providers use Qt's TLS backend; TorrentSearch uses
            // curl. Verify both from the deployed package without SDK paths.
            auto *network = new QNetworkAccessManager(&engine);
            auto *qtReply = network->get(request);
            QObject::connect(qtReply, &QNetworkReply::finished, &engine, [qtReply, remaining] {
                if (qtReply->error() != QNetworkReply::NoError ||
                    qtReply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
                    fail("Qt HTTPS transport failed: " + qtReply->errorString());
                    return;
                }
                qInfo("Verified HTTPS: Qt network backend.");
                if (--*remaining == 0) QCoreApplication::exit(0);
                qtReply->deleteLater();
            });
            for (bool doh : {false, true}) {
                auto *reply = new TorrentSearchNetworkReply(request, doh, &engine);
                QObject::connect(reply, &QNetworkReply::finished, &engine, [reply, remaining, doh] {
                    if (reply->error() != QNetworkReply::NoError ||
                        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200) {
                        fail("Verified HTTPS transport failed: " + reply->errorString());
                        return;
                    }
                    qInfo() << "Verified HTTPS:" << (doh ? "DoH preferred" : "system DNS")
                            << "used system DNS:" << reply->property("mementoSystemDns");
                    if (--*remaining == 0) QCoreApplication::exit(0);
                    reply->deleteLater();
                });
            }
            return;
        }
        qInfo("Release check: clean defaults and settings UI passed.");
        QCoreApplication::exit(0);
        return;
    }
    const QString output = qEnvironmentVariable("MEMENTO_TEST_EXPORT");
    if (!QFileInfo::exists(fixture) || output.isEmpty() || !QDir().mkpath(output) ||
        !QDir(output).entryList(QDir::Files).isEmpty()) {
        fail("Media fixture missing or export directory is not empty.");
        return;
    }
    auto observedLoading = std::make_shared<bool>(false);
    QObject::connect(context.player(), &MpvPlayer::playbackStatusChanged, &engine,
        [player = context.player(), observedLoading] { if (player->loading()) *observedLoading = true; });
    const QString previousClipboard = QGuiApplication::clipboard()->text();
    QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
        &engine, [previousClipboard] { QGuiApplication::clipboard()->setText(previousClipboard); });
    context.settings()->setBehaviorSubtitleAutoCopy(true);
    auto *client = context.migakuClient();
    client->setEnabled(true);
    client->setExportFolder(QUrl::fromLocalFile(output));
    auto *timer = new QTimer(&engine);
    timer->setInterval(100);
    QObject::connect(client, &MigakuClient::exportFailed, timer, [](const QString &error) { fail(error); });
    QObject::connect(client, &MigakuClient::filesExported, timer,
        [client, output, count = 0](const QString &) mutable {
            ++count;
            QDir folder(output);
            const auto images = folder.entryList({"*.jpg"}, QDir::Files);
            const auto audio = folder.entryList({"*.mp3"}, QDir::Files);
            if (images.size() != count || audio.size() != count ||
                QImage(folder.filePath(images.last())).isNull() ||
                QFileInfo(folder.filePath(audio.last())).size() < 1000) {
                fail("Export did not produce separate valid image/audio files.");
                return;
            }
            if (count == 1) {
                // Start after the previous coroutine has released its busy guard.
                QTimer::singleShot(0, client, [client] { client->exportCurrentSubtitle(); });
            } else {
                MpvEncodingContext decode;
                decode.input = folder.filePath(audio.first()).toUtf8();
                decode.aid = 1;
                MpvAudioClipArgs clip;
                clip.start = 0; clip.end = 1; clip.extension = ".wav";
                const QString wave = MpvController::encodeAudioClip(decode, clip);
                QFile pcm(wave);
                if (!pcm.open(QIODevice::ReadOnly) || pcm.size() < 10000 || pcm.read(4) != "RIFF") {
                    fail("Exported MP3 could not be decoded to PCM audio.");
                    return;
                }
                pcm.close();
                QFile::copy(wave, folder.filePath("decoded.wav"));
                QFile::remove(wave);
                qInfo("Release check: playback, subtitles, JPG/MP3 export, audio decode and duplicate capture passed.");
                QCoreApplication::exit(0);
            }
        });
    QObject::connect(timer, &QTimer::timeout, timer,
        [&context, timer, fixture, observedLoading, phase = 0, ticks = 0]() mutable {
            if (++ticks > 250) { timer->stop(); fail("Timed out waiting for video/subtitle/export."); return; }
            auto *player = context.player();
            auto *state = player->state();
            if (phase == 0) {
                if (!player->renderContext()) return;
                player->controller()->loadFile(fixture);
                phase = 1;
            } else if (phase == 1 && state->duration() > 3 && state->aid() > 0) {
                player->controller()->pause();
                player->controller()->seek(1.5);
                phase = 2;
            } else if (phase == 2 && state->pause() && state->timePosition() > 1.2 &&
                       state->timePosition() < 1.8 &&
                       !context.subtitleLists()->primary()->activeText().isEmpty()) {
                if (QGuiApplication::clipboard()->text() !=
                    context.subtitleLists()->primary()->activeText()) return;
                if (!*observedLoading || player->loading() || player->buffering()) { fail("Episode loading state did not start and clear around playback."); return; }
                phase = 3;
                context.migakuClient()->exportCurrentSubtitle();
            }
        });
    timer->start();
}
