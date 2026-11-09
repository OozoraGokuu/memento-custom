#include <QTest>
#include <QTemporaryDir>
#include <QProcess>
#include <QStandardPaths>
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <clocale>
#include <limits>
#include "state/videoexporter.h"
#include "util/directoryutils.h"

class VideoExportTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QString m_ffmpeg, m_ffprobe, m_input;
    QByteArray run(const QString &program, const QStringList &args)
    {
        QProcess p;
        p.start(program, args);
        if (!p.waitForFinished(30000) || p.exitCode() != 0)
        {
            qWarning().noquote() << p.readAllStandardError();
            return {};
        }
        return p.readAllStandardOutput();
    }
private slots:
    void initTestCase()
    {
        std::setlocale(LC_NUMERIC, "C");
        m_ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        m_ffprobe = QStandardPaths::findExecutable("ffprobe");
        QVERIFY(m_dir.isValid());
    }
    void rangesAndSize()
    {
        QVERIFY(!VideoExporter::plan(1920, 1080, 30, 8, 7, 0, 1, true)["valid"].toBool());
        QVERIFY(!VideoExporter::plan(1920, 1080, 30, -1, 7, 0, 1, true)["valid"].toBool());
        QVERIFY(!VideoExporter::plan(1920, 1080, 30, 0, 31, 0, 1, true)["valid"].toBool());
        QVERIFY(!VideoExporter::plan(1920, 1080, 30, std::numeric_limits<double>::quiet_NaN(), 7, 0, 1, true)["valid"].toBool());
        auto small = VideoExporter::plan(1920, 1080, 30, 3, 13, 720, 0, false);
        auto large = VideoExporter::plan(1920, 1080, 30, 3, 23, 720, 2, true);
        QCOMPARE(small["width"].toInt(), 1280);
        QCOMPARE(small["height"].toInt(), 720);
        QVERIFY(large["sizeMB"].toDouble() > small["sizeMB"].toDouble() * 2);
        auto native = VideoExporter::plan(321, 181, 30, 3, 13, 1080, 1, false);
        QCOMPARE(native["width"].toInt(), 320);
        QCOMPARE(native["height"].toInt(), 180);
        QCOMPARE(DirectoryUtils::safeMediaName("../CON:", "Video"), ".._CON_");
        QCOMPARE(DirectoryUtils::safeMediaName("NUL", "Video"), "_NUL");
    }
    void createFixture()
    {
        if (m_ffmpeg.isEmpty() || m_ffprobe.isEmpty()) QSKIP("FFmpeg tools unavailable");
        for (const auto &name : {"primary", "secondary"})
        {
            QFile file(m_dir.filePath(QString(name) + ".srt"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray("1\n00:00:03,200 --> 00:00:04,800\n") + name + "\n\n");
        }
        m_input = m_dir.filePath("input.mkv");
        run(m_ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "color=black:s=320x180:r=10:d=6",
            "-f", "lavfi", "-i", "sine=frequency=440:duration=6",
            "-i", m_dir.filePath("primary.srt"), "-i", m_dir.filePath("secondary.srt"),
            "-map", "0:v", "-map", "1:a", "-map", "2:s", "-map", "3:s",
            "-c:v", "mpeg4", "-c:a", "aac", "-c:s", "srt", "-y", m_input});
        QVERIFY(QFileInfo(m_input).size() > 0);
    }
    void realClips_data()
    {
        QTest::addColumn<int>("primary");
        QTest::addColumn<int>("secondary");
        QTest::addColumn<bool>("audio");
        QTest::addColumn<double>("delay");
        QTest::addColumn<int>("width");
        QTest::newRow("no-subtitles-muted") << -1 << -1 << false << 0.0 << 320;
        QTest::newRow("primary-with-audio") << 1 << -1 << true << 0.0 << 320;
        QTest::newRow("secondary-only") << -1 << 2 << true << 0.0 << 320;
        QTest::newRow("both") << 1 << 2 << true << 0.0 << 320;
        QTest::newRow("swapped") << 2 << 1 << true << 0.0 << 320;
        QTest::newRow("external") << 3 << -1 << true << 0.0 << 320;
        QTest::newRow("delayed-primary") << 1 << -1 << true << 2.0 << 320;
        QTest::newRow("delayed-secondary") << -1 << 2 << true << 2.0 << 320;
        QTest::newRow("scaled-both") << 1 << 2 << true << 0.0 << 160;
        QTest::newRow("external-audio") << -1 << -1 << true << 0.0 << 320;
    }
    void realClips()
    {
        if (m_input.isEmpty()) QSKIP("FFmpeg tools unavailable");
        QFETCH(int, primary); QFETCH(int, secondary); QFETCH(bool, audio); QFETCH(double, delay); QFETCH(int, width);
        MpvEncodingContext context;
        context.input = m_input.toUtf8(); context.vid = 1; context.aid = 1;
        context.primarySubtitle.id = primary; context.secondarySubtitle.id = secondary;
        context.primarySubtitle.delay = delay; context.secondarySubtitle.delay = delay;
        context.sharedSubtitleDelay = delay;
        if (primary == 3)
        {
            context.primarySubtitle.external = true;
            context.primarySubtitle.externalFilename = m_dir.filePath("primary.srt");
        }
        MpvVideoClipArgs args;
        args.start = 3; args.end = 5; args.audio = audio;
        args.subtitles = primary > 0 || secondary > 0;
        MpvVideoExportOptions options;
        options.width = width; options.height = width * 9 / 16; options.videoKbps = 300;
        if (QByteArray(QTest::currentDataTag()) == "external-audio")
        {
            options.externalAudio.id = 2;
            options.externalAudio.external = true;
            options.externalAudio.externalFilename = m_dir.filePath("external.m4a");
            run(m_ffmpeg, {"-v", "error", "-i", m_input, "-map", "0:a", "-c", "copy", "-y", options.externalAudio.externalFilename});
            context.aid = 2;
        }
        options.start = 3; options.end = 5;
        options.control = std::make_shared<MpvEncodingControl>();
        options.outputPath = m_dir.filePath(QString(QTest::currentDataTag()) + ".mp4");
        const auto path = MpvController::encodeVideoClip(context, args, {}, false, false, &options);
        QVERIFY2(!path.isEmpty(), qPrintable(options.control->error));
        const auto probe = QJsonDocument::fromJson(run(m_ffprobe, {"-v", "error", "-show_streams", "-show_format", "-of", "json", path})).object();
        auto streams = probe["streams"].toArray();
        QCOMPARE(streams.size(), audio ? 2 : 1);
        QJsonObject video;
        for (const auto &stream : streams)
        {
            const auto object = stream.toObject();
            if (object["codec_type"] == "video") video = object;
            if (object["codec_type"] == "audio") QCOMPARE(object["codec_name"].toString(), QString("aac"));
        }
        QCOMPARE(video["codec_name"].toString(), QString("h264"));
        QCOMPARE(video["width"].toInt(), width);
        QCOMPARE(video["height"].toInt(), width * 9 / 16);
        const double duration = probe["format"].toObject()["duration"].toString().toDouble();
        QVERIFY2(duration >= 1.9 && duration <= 2.2, qPrintable(QString::number(duration)));
        const QString frame = path + ".png";
        run(m_ffmpeg, {"-v", "error", "-ss", "0.8", "-i", path, "-frames:v", "1", "-y", frame});
        const QImage image(frame);
        QVERIFY(!image.isNull());
        // Antialiased text may be gray at small resolutions; the fixture is black.
        int top = 0, bottom = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                if (qGray(image.pixel(x, y)) > 40) (y < image.height() / 2 ? top : bottom)++;
        QCOMPARE(top > 0, secondary > 0 && delay == 0.0);
        QCOMPARE(bottom > 0, primary > 0 && delay == 0.0);
    }
    void cancelRemovesPartial()
    {
        if (m_input.isEmpty()) QSKIP("FFmpeg tools unavailable");
        MpvEncodingContext context;
        context.input = m_input.toUtf8(); context.vid = 1;
        MpvVideoClipArgs args; args.start = 0; args.end = 6; args.audio = false; args.subtitles = false;
        MpvVideoExportOptions options;
        options.width = 320; options.height = 180; options.videoKbps = 300;
        options.start = 0; options.end = 6;
        options.outputPath = m_dir.filePath("cancelled.partial.mp4");
        options.control = std::make_shared<MpvEncodingControl>();
        options.control->cancel.store(true);
        QVERIFY(MpvController::encodeVideoClip(context, args, {}, false, false, &options).isEmpty());
        QVERIFY(!QFile::exists(options.outputPath));
    }
};
QTEST_GUILESS_MAIN(VideoExportTest)
#include "test_videoexport.moc"
