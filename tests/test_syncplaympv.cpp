#include <memory>

#include <mpv/client.h>

#include <QFile>
#include <QFileInfo>
#include <QLocalSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include "player/syncplaympv.h"

namespace
{
bool writeFile(const QString &path, const QByteArray &content)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) &&
        file.write(content) == content.size();
}

QByteArray luaScript(const QString &marker)
{
    QString escaped = marker;
    escaped.replace(u'\\', QStringLiteral("\\\\"));
    escaped.replace(u'\'', QStringLiteral("\\'"));
    return QStringLiteral(
        "local f = assert(io.open('%1', 'a'))\n"
        "f:write('x')\n"
        "f:close()\n"
    ).arg(escaped).toUtf8();
}
} // namespace

class SyncplayMpvTest : public QObject
{
    Q_OBJECT

private slots:
    void restoresSocketAndScriptAfterUserConfig();
    void doesNotLoadScriptTwice();
};

void SyncplayMpvTest::restoresSocketAndScriptAfterUserConfig()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
#ifdef Q_OS_WIN
    // Windows mpv IPC uses a named pipe identifier, just as Syncplay does.
    const QString pipeSuffix = QUuid::createUuid().toString(QUuid::Id128);
    const QString configuredSocket = QStringLiteral("memento-config-%1")
        .arg(pipeSuffix);
    const QString syncplaySocket = QStringLiteral("syncplay-mpv-%1")
        .arg(pipeSuffix);
#else
    const QString configuredSocket = dir.filePath(QStringLiteral("config.sock"));
    const QString syncplaySocket = dir.filePath(QStringLiteral("syncplay.sock"));
#endif
    const QString configuredScript = dir.filePath(QStringLiteral("config.lua"));
    const QString syncplayScript = dir.filePath(QStringLiteral("syncplayintf.lua"));
    const QString configuredMarker = dir.filePath(QStringLiteral("config.marker"));
    const QString syncplayMarker = dir.filePath(QStringLiteral("syncplay.marker"));

    QVERIFY(writeFile(configuredScript, luaScript(configuredMarker)));
    QVERIFY(writeFile(syncplayScript, luaScript(syncplayMarker)));
    const QString config = QStringLiteral(
        "input-ipc-server=%1\n"
        "scripts=%2\n"
        "idle=no\n"
        "keep-open=no\n"
        "term-playing-msg=CONFIG\n"
    ).arg(configuredSocket, configuredScript);
    QVERIFY(writeFile(dir.filePath(QStringLiteral("mpv.conf")),
                      config.toUtf8()));

    std::unique_ptr<mpv_handle, decltype(&mpv_terminate_destroy)> mpv(
        ::mpv_create(), &mpv_terminate_destroy);
    QVERIFY(mpv != nullptr);
    const QByteArray configDir = dir.path().toUtf8();
    const QByteArray scriptPath = syncplayScript.toUtf8();
    const QByteArray ipcPath = syncplaySocket.toUtf8();
    QCOMPARE(::mpv_set_option_string(mpv.get(), "config", "yes"), 0);
    QCOMPARE(::mpv_set_option_string(mpv.get(), "config-dir",
                                    configDir.constData()), 0);
    QCOMPARE(::mpv_set_option_string(mpv.get(), "scripts",
                                    scriptPath.constData()), 0);
    QCOMPARE(::mpv_set_option_string(mpv.get(), "input-ipc-server",
                                    ipcPath.constData()), 0);
    QCOMPARE(::mpv_set_option_string(mpv.get(), "vo", "null"), 0);
    QCOMPARE(::mpv_set_option_string(mpv.get(), "terminal", "yes"), 0);
    QCOMPARE(::mpv_initialize(mpv.get()), 0);

    char *configuredIpc = ::mpv_get_property_string(mpv.get(), "input-ipc-server");
    QVERIFY(configuredIpc != nullptr);
    QCOMPARE(QString::fromUtf8(configuredIpc), configuredSocket);
    ::mpv_free(configuredIpc);

    char *configuredScripts = ::mpv_get_property_string(mpv.get(), "scripts");
    QVERIFY(configuredScripts != nullptr);
    QCOMPARE(QByteArray(configuredScripts), configuredScript.toUtf8());
    ::mpv_free(configuredScripts);
    QVERIFY(!QFileInfo::exists(syncplayMarker));

    const QStringList arguments{
        QStringLiteral("memento"),
        QStringLiteral("--scripts=%1").arg(syncplayScript),
        QStringLiteral("--input-ipc-server=%1").arg(syncplaySocket),
        QStringLiteral("--idle=yes"),
        QStringLiteral("--keep-open=always"),
        QStringLiteral("--term-playing-msg=SYNCPLAY")
    };
    QCOMPARE(SyncplayMpv::restoreOptions(mpv.get(), arguments), QString());
    char *restoredIpc = ::mpv_get_property_string(mpv.get(), "input-ipc-server");
    QVERIFY(restoredIpc != nullptr);
    QCOMPARE(QString::fromUtf8(restoredIpc), syncplaySocket);
    ::mpv_free(restoredIpc);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(configuredMarker), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(syncplayMarker), 3000);
    QFile marker(syncplayMarker);
    QVERIFY(marker.open(QIODevice::ReadOnly));
    QCOMPARE(marker.readAll(), QByteArray("x"));

    QLocalSocket socket;
    socket.connectToServer(syncplaySocket);
    QVERIFY(socket.waitForConnected(3000));
    const QByteArray request("{\"command\":[\"get_property\",\"pause\"]}\n");
    QCOMPARE(socket.write(request), request.size());
    QVERIFY(socket.waitForReadyRead(3000));
    QVERIFY(socket.readAll().contains("\"error\":\"success\""));

    char *playingMessage = ::mpv_get_property_string(mpv.get(), "term-playing-msg");
    QVERIFY(playingMessage != nullptr);
    QCOMPARE(QByteArray(playingMessage), QByteArray("SYNCPLAY"));
    ::mpv_free(playingMessage);
}

void SyncplayMpvTest::doesNotLoadScriptTwice()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString script = dir.filePath(QStringLiteral("syncplayintf.lua"));
    const QString markerPath = dir.filePath(QStringLiteral("syncplay.marker"));
    QVERIFY(writeFile(script, luaScript(markerPath)));

    std::unique_ptr<mpv_handle, decltype(&mpv_terminate_destroy)> mpv(
        ::mpv_create(), &mpv_terminate_destroy);
    QVERIFY(mpv != nullptr);
    const QByteArray scriptUtf8 = script.toUtf8();
    QCOMPARE(::mpv_set_option_string(mpv.get(), "config", "no"), 0);
    QCOMPARE(::mpv_set_option_string(mpv.get(), "scripts",
                                    scriptUtf8.constData()), 0);
    QCOMPARE(::mpv_set_option_string(mpv.get(), "vo", "null"), 0);
    QCOMPARE(::mpv_initialize(mpv.get()), 0);
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(markerPath), 3000);

    QCOMPARE(SyncplayMpv::restoreOptions(mpv.get(), {
        QStringLiteral("memento"),
        QStringLiteral("--scripts=%1").arg(script)
    }), QString());
    QFile marker(markerPath);
    QVERIFY(marker.open(QIODevice::ReadOnly));
    QCOMPARE(marker.readAll(), QByteArray("x"));
}

QTEST_GUILESS_MAIN(SyncplayMpvTest)
#include "test_syncplaympv.moc"
