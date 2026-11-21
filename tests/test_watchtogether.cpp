#include <QFile>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include "quick/syncplayprocess.h"
#include "quick/watchtogetherlauncher.h"

class WatchTogetherTest : public QObject
{
    Q_OBJECT

private slots:
    void detectsSyncplayControlledProcess();
    void ignoresOtherMpvScripts();
    void buildsArgumentsWithLocalFile();
    void neverForwardsLoopbackStream();
    void generatesUnpredictableRoomCodes();
};

void WatchTogetherTest::detectsSyncplayControlledProcess()
{
    QVERIFY(SyncplayProcess::isSyncplayProcess({
        QStringLiteral("memento"),
        QStringLiteral("--scripts=/usr/share/syncplay/syncplayintf.lua"),
        QStringLiteral("--input-ipc-server=/tmp/syncplay-memento.sock")
    }));
    QCOMPARE(SyncplayProcess::ipcServerPath({
        QStringLiteral("memento"),
        QStringLiteral("--scripts=/usr/share/syncplay/syncplayintf.lua"),
        QStringLiteral("--input-ipc-server=/tmp/syncplay-memento.sock")
    }), QStringLiteral("/tmp/syncplay-memento.sock"));
    QCOMPARE(SyncplayProcess::scriptPath({
        QStringLiteral("memento"),
        QStringLiteral("--scripts=/home/user/other.lua,C:\\Syncplay\\syncplayintf.lua")
    }), QStringLiteral("C:\\Syncplay\\syncplayintf.lua"));
    QVERIFY(SyncplayProcess::isSyncplayProcess({
        QStringLiteral("memento"),
        QStringLiteral("--scripts=/home/user/other.lua,C:\\Syncplay\\syncplayintf.lua")
    }));
    QCOMPARE(SyncplayProcess::ipcServerPath({
        QStringLiteral("memento.exe"),
        QStringLiteral("--scripts=C:\\Syncplay\\syncplayintf.lua"),
        QStringLiteral("--input-ipc-server=syncplay-mpv-12345")
    }), QStringLiteral("syncplay-mpv-12345"));
    QVERIFY(SyncplayProcess::isSyncplayProcess({
        QStringLiteral("memento"),
        QStringLiteral("--script"),
        QStringLiteral("/opt/syncplay/syncplayintf.lua")
    }));
}

void WatchTogetherTest::ignoresOtherMpvScripts()
{
    QVERIFY(!SyncplayProcess::isSyncplayProcess({
        QStringLiteral("memento"),
        QStringLiteral("--script=/home/user/study.lua"),
        QStringLiteral("--input-ipc-server=/tmp/other.sock")
    }));
    QVERIFY(!SyncplayProcess::isSyncplayProcess({
        QStringLiteral("memento"),
        QStringLiteral("--script-opts=syncplayintf.lua")
    }));
    QVERIFY(SyncplayProcess::ipcServerPath({
        QStringLiteral("memento"), QStringLiteral("--script=/other.lua")
    }).isEmpty());
}

void WatchTogetherTest::buildsArgumentsWithLocalFile()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString mediaPath = dir.filePath(QStringLiteral("Episode 01.mkv"));
    QFile media(mediaPath);
    QVERIFY(media.open(QIODevice::WriteOnly));
    media.close();

    const QStringList expected{
        QStringLiteral("--host=syncplay.pl:8995"),
        QStringLiteral("--room=Example room"),
        QStringLiteral("--name=Viewer"),
        QStringLiteral("--player-path=C:/Program Files/Memento/memento.exe"),
        mediaPath
    };
    QCOMPARE(WatchTogetherLauncher::buildArguments(
        QStringLiteral(" syncplay.pl:8995 "),
        QStringLiteral(" Example room "),
        QStringLiteral(" Viewer "),
        QStringLiteral("C:/Program Files/Memento/memento.exe"),
        QUrl::fromLocalFile(mediaPath).toString()
    ), expected);
}

void WatchTogetherTest::neverForwardsLoopbackStream()
{
    const QStringList arguments = WatchTogetherLauncher::buildArguments(
        QStringLiteral("syncplay.pl:8995"),
        QStringLiteral("room"),
        QString(),
        QStringLiteral("C:/Program Files/Memento/memento.exe"),
        QStringLiteral("http://127.0.0.1:46123/torrent/episode.mkv")
    );
    QCOMPARE(arguments.size(), 3);
    QVERIFY(!arguments.join(u' ').contains(QStringLiteral("127.0.0.1")));
    QVERIFY(WatchTogetherLauncher::localMediaFile(
        QStringLiteral("http://localhost:46123/episode.mkv")
    ).isEmpty());
}

void WatchTogetherTest::generatesUnpredictableRoomCodes()
{
    WatchTogetherLauncher launcher;
    const QString first = launcher.createRoomCode();
    const QString second = launcher.createRoomCode();
    QCOMPARE(first.size(), 32);
    QVERIFY(QRegularExpression(QStringLiteral("^[0-9a-f]{32}$"))
                .match(first).hasMatch());
    QVERIFY(first != second);
}

QTEST_GUILESS_MAIN(WatchTogetherTest)
#include "test_watchtogether.moc"
