#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

#include "migaku/migakuclient.h"

class MigakuClientTest : public QObject
{
    Q_OBJECT

private slots:
    void savesSeparateEpisodeFilesWithoutOverwriting()
    {
        QTemporaryDir source, destination;
        QVERIFY(source.isValid()); QVERIFY(destination.isValid());
        const auto make = [](const QString &path, const QByteArray &bytes) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
        };
        const auto image = source.filePath("capture.jpg");
        const auto audio = source.filePath("capture.mp3");
        QVERIFY(make(image, "image-data")); QVERIFY(make(audio, "audio-data"));
        const auto first = MigakuClient::saveMediaPair(destination.path(),
            QStringLiteral("Example Show - 27.mkv"), 754.5, image, audio);
        QVERIFY(first.value("success").toBool());
        QCOMPARE(QFileInfo(first.value("image").toString()).fileName(),
                 QStringLiteral("Example Show - 27 - 00-12-34-500.jpg"));
        QCOMPARE(QFileInfo(first.value("audio").toString()).fileName(),
                 QStringLiteral("Example Show - 27 - 00-12-34-500.mp3"));
        QFile savedImage(first.value("image").toString()), savedAudio(first.value("audio").toString());
        QVERIFY(savedImage.open(QIODevice::ReadOnly)); QVERIFY(savedAudio.open(QIODevice::ReadOnly));
        QCOMPARE(savedImage.readAll(), QByteArray("image-data"));
        QCOMPARE(savedAudio.readAll(), QByteArray("audio-data"));
        const auto second = MigakuClient::saveMediaPair(destination.path(),
            QStringLiteral("Example Show - 27.mkv"), 754.5, image, audio);
        QVERIFY(second.value("success").toBool());
        QVERIFY(second.value("image").toString().endsWith(" - 2.jpg"));
        QCOMPARE(QDir(destination.path()).entryList(QDir::Files).size(), 4);
        const auto failed = MigakuClient::saveMediaPair(destination.path(), "bad", 0, image,
                                                       source.filePath("missing.mp3"));
        QVERIFY(!failed.value("success").toBool());
        QCOMPARE(QDir(destination.path()).entryList(QDir::Files).size(), 4);
        QVERIFY(!MigakuClient::saveMediaPair(destination.filePath("missing"), "bad", 0,
                                             image, audio).value("success").toBool());
    }

    void sanitizesEpisodeNames()
    {
        QCOMPARE(MigakuClient::episodeStem(QStringLiteral("../猫: episode/27?.mkv")),
                 QStringLiteral("_猫_ episode_27_"));
        QCOMPARE(MigakuClient::episodeStem("..."), QStringLiteral("Episode"));
        QCOMPARE(MigakuClient::episodeStem("CON.mp4"), QStringLiteral("Episode - CON"));
        const auto longTitle = MigakuClient::episodeStem(QString(400, QChar(0x732b)));
        QVERIFY(longTitle.toUtf8().size() <= 160);
        QVERIFY(!longTitle.isEmpty());
    }

    void validatesAndClampsClipRange()
    {
        double start = -1.0;
        double end = -1.0;

        QVERIFY(MigakuClient::prepareClipRange(
            -0.25, 2.5, 10.0, &start, &end));
        QCOMPARE(start, 0.0);
        QCOMPARE(end, 2.5);

        QVERIFY(MigakuClient::prepareClipRange(
            8.0, 12.0, 10.0, &start, &end));
        QCOMPARE(start, 8.0);
        QCOMPARE(end, 10.0);

        QVERIFY(!MigakuClient::prepareClipRange(
            0.0, 60.001, 120.0, &start, &end));
        QVERIFY(!MigakuClient::prepareClipRange(
            5.0, 5.0, 10.0, &start, &end));
        QVERIFY(!MigakuClient::prepareClipRange(
            std::numeric_limits<double>::quiet_NaN(),
            5.0,
            10.0,
            &start,
            &end));
        QVERIFY(!MigakuClient::prepareClipRange(
            0.0,
            std::numeric_limits<double>::infinity(),
            10.0,
            &start,
            &end));
        QVERIFY(!MigakuClient::prepareClipRange(
            0.0, 1.0, 10.0, nullptr, &end));
    }

    void enforcesGeneratedMediaSizeLimit()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        const QString path = directory.filePath(QStringLiteral("media.bin"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(QByteArrayLiteral("1234")), qint64{4});
        file.close();

        QVERIFY(MigakuClient::fileWithinLimit(path, 4));
        QVERIFY(!MigakuClient::fileWithinLimit(path, 3));
        QVERIFY(!MigakuClient::fileWithinLimit(path, 0));
        QVERIFY(!MigakuClient::fileWithinLimit(
            directory.filePath(QStringLiteral("missing.bin")), 10));

        const QString emptyPath = directory.filePath(QStringLiteral("empty.bin"));
        QFile empty(emptyPath);
        QVERIFY(empty.open(QIODevice::WriteOnly));
        empty.close();
        QVERIFY(!MigakuClient::fileWithinLimit(emptyPath, 10));
    }
};

QTEST_GUILESS_MAIN(MigakuClientTest)

#include "test_migaku.moc"
