#include <QTest>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <clocale>
#include <limits>

#include "anki/notebuilder.h"
#include "anki/ankiconnect.h"

class AnkiVideoTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dir;
    QString m_ffmpeg;
    QString m_ffprobe;
    QString m_input;
    QString m_externalAudio;

    QByteArray run(const QString &program, const QStringList &arguments)
    {
        QProcess process;
        process.start(program, arguments);
        if (!process.waitForFinished(30000) || process.exitCode() != 0)
        {
            qWarning().noquote() << process.readAllStandardError();
            return {};
        }
        return process.readAllStandardOutput();
    }

    Anki::Note::MediaSnapshot snapshot() const
    {
        Anki::Note::MediaSnapshot result;
        result.encoding.input = m_input.toUtf8();
        result.encoding.vid = 1;
        result.encoding.aid = 2;
        result.videoWidth = 320;
        result.videoHeight = 180;
        result.duration = 12.0;
        // The requested sentence comes from its saved timings, not this cursor.
        result.position = 11.0;
        return result;
    }

private slots:
    void initTestCase()
    {
        std::setlocale(LC_NUMERIC, "C");
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_dir.isValid());
        m_ffmpeg = QStandardPaths::findExecutable("ffmpeg");
        m_ffprobe = QStandardPaths::findExecutable("ffprobe");
    }

    void interval_data()
    {
        QTest::addColumn<double>("duration");
        QTest::addColumn<double>("sentenceStart");
        QTest::addColumn<double>("sentenceEnd");
        QTest::addColumn<double>("expectedStart");
        QTest::addColumn<double>("expectedEnd");
        QTest::newRow("sentence-start") << 12.0 << 4.5 << 5.2 << 4.5 << 9.5;
        QTest::newRow("near-eof") << 12.0 << 10.0 << 11.0 << 7.0 << 12.0;
        QTest::newRow("short-source") << 3.0 << 1.0 << 2.0 << 0.0 << 3.0;
        QTest::newRow("negative-delay") << 12.0 << -1.0 << 1.0 << 0.0 << 5.0;
        QTest::newRow("long-sentence") << 12.0 << 2.0 << 10.0 << 2.0 << 7.0;
    }

    void interval()
    {
        QFETCH(double, duration); QFETCH(double, sentenceStart);
        QFETCH(double, sentenceEnd); QFETCH(double, expectedStart);
        QFETCH(double, expectedEnd);
        auto media = snapshot();
        media.encoding.input = "synthetic.mkv";
        media.duration = duration;
        const auto interval = Anki::Note::videoInterval(media, sentenceStart, sentenceEnd);
        QVERIFY(interval.error.isEmpty());
        QCOMPARE(interval.start, expectedStart);
        QCOMPARE(interval.end, expectedEnd);
    }

    void invalidInputs()
    {
        auto media = snapshot();
        media.encoding.input = "synthetic.mkv";
        const double nan = std::numeric_limits<double>::quiet_NaN();
        QVERIFY(!Anki::Note::videoInterval(media, nan, 2).error.isEmpty());
        QVERIFY(!Anki::Note::videoInterval(media, 1, nan).error.isEmpty());
        QVERIFY(!Anki::Note::videoInterval(media, 2, 1).error.isEmpty());
        QVERIFY(!Anki::Note::videoInterval(media, 2, 2).error.isEmpty());
        QVERIFY(!Anki::Note::videoInterval(media, -2, -1).error.isEmpty());
        QVERIFY(!Anki::Note::videoInterval(media, 12, 13).error.isEmpty());
        media.duration = nan;
        QVERIFY(!Anki::Note::videoInterval(media, 1, 2).error.isEmpty());
        media.duration = 12; media.encoding.vid = -1;
        QVERIFY(!Anki::Note::videoInterval(media, 1, 2).error.isEmpty());
        media.encoding.vid = 1; media.videoWidth = 0;
        QVERIFY(!Anki::Note::videoInterval(media, 1, 2).error.isEmpty());
    }

    void missingFieldAndSentenceAreErrors()
    {
        AnkiProfile profile;
        profile.termFields()->setItems(QJsonObject{{"Expression", "{expression}"}, {"Video", ""}});
        Term term;
        term.setExpression("word");
        term.setStartTime(1); term.setEndTime(2);
        auto media = snapshot();
        media.encoding.input = "synthetic.mkv";
        auto note = Anki::Note::build(profile, term, true, media, {true, "Missing"});
        QVERIFY(!note.error.isEmpty());
        QVERIFY(!note.ankiObject.contains(AnkiConnect::Note::VIDEO));
        note = Anki::Note::build(profile, term, true, media, {true, "Video"});
        QVERIFY(!note.error.isEmpty());
        QVERIFY(!note.ankiObject.contains(AnkiConnect::Note::VIDEO));
        note = Anki::Note::build(profile, term, true, media);
        QVERIFY(note.error.isEmpty());
        QVERIFY(!note.ankiObject.contains(AnkiConnect::Note::VIDEO));
    }

    void createFixture()
    {
        if (m_ffmpeg.isEmpty() || m_ffprobe.isEmpty()) QSKIP("FFmpeg tools unavailable");
        m_input = m_dir.filePath(QString::fromUtf8("日本語 with spaces.mkv"));
        run(m_ffmpeg, {"-v", "error", "-f", "lavfi", "-i",
            "color=red:s=320x180:r=20:d=12,drawbox=c=green:t=fill:enable='gte(t,4)',drawbox=c=blue:t=fill:enable='gte(t,8)'",
            "-f", "lavfi", "-i", "sine=frequency=440:duration=12",
            "-f", "lavfi", "-i", "sine=frequency=880:duration=12",
            "-map", "0:v", "-map", "1:a", "-map", "2:a",
            "-c:v", "mpeg4", "-c:a", "aac", "-y", m_input});
        QVERIFY(QFileInfo(m_input).size() > 0);
        m_externalAudio = m_dir.filePath(QString::fromUtf8("外部 audio.m4a"));
        run(m_ffmpeg, {"-v", "error", "-f", "lavfi", "-i",
            "sine=frequency=1320:duration=12", "-c:a", "aac", "-y", m_externalAudio});
        QVERIFY(QFileInfo(m_externalAudio).size() > 0);
    }

    void realAttachment_data()
    {
        QTest::addColumn<double>("start");
        QTest::addColumn<double>("sourceDuration");
        QTest::addColumn<int>("audioId");
        QTest::addColumn<bool>("marker");
        QTest::addColumn<bool>("subtitle");
        QTest::newRow("selected-second-audio-unicode-source") << 4.5 << 12.0 << 2 << false << false;
        QTest::newRow("near-eof-existing-video-marker") << 10.0 << 12.0 << 1 << true << false;
        QTest::newRow("source-shorter-than-five") << 1.0 << 3.0 << -1 << false << false;
        QTest::newRow("external-audio") << 4.5 << 12.0 << 3 << false << false;
        QTest::newRow("native-subtitle") << 4.5 << 12.0 << -1 << false << true;
    }

    void realAttachment()
    {
        if (m_input.isEmpty()) QSKIP("FFmpeg tools unavailable");
        QFETCH(double, start); QFETCH(double, sourceDuration);
        QFETCH(int, audioId); QFETCH(bool, marker); QFETCH(bool, subtitle);
        AnkiProfile profile;
        profile.setAudioPadStart(1.0); profile.setAudioPadEnd(2.0);
        profile.setAudioNormalize(false);
        profile.termFields()->setItems(QJsonObject{
            {"Expression", "{expression}"}, {"Video", marker ? "{video}" : "Existing text"}});
        Term term;
        term.setExpression("word"); term.setSubtitle("Specific sentence");
        term.setStartTime(start); term.setEndTime(start + 0.7);
        auto media = snapshot();
        media.duration = sourceDuration; media.encoding.aid = audioId;
        if (sourceDuration < 5.0)
        {
            const auto shortInput = m_dir.filePath("short-source.mkv");
            run(m_ffmpeg, {"-v", "error", "-i", m_input, "-t", "3",
                "-map", "0:v", "-c", "copy", "-y", shortInput});
            QVERIFY(QFileInfo(shortInput).size() > 0);
            media.encoding.input = shortInput.toUtf8();
        }
        if (audioId == 3)
        {
            media.externalAudio.id = 3;
            media.externalAudio.external = true;
            media.externalAudio.externalFilename = m_externalAudio;
        }
        if (subtitle)
        {
            media.completePrimaryTimeline = true;
            media.primaryTrack.entries.push_back({"Specific sentence", start, start + 2});
        }
        const auto note = Anki::Note::build(profile, term, true, media, {true, "Video"});
        QVERIFY2(note.error.isEmpty(), qPrintable(note.error));
        const auto attachments = note.ankiObject.value(AnkiConnect::Note::VIDEO).toArray();
        QCOMPARE(attachments.size(), 1);
        const auto attachment = attachments.first().toObject();
        QCOMPARE(attachment[AnkiConnect::Note::FIELDS].toArray(), QJsonArray{"Video"});
        QVERIFY(attachment[AnkiConnect::Note::FILENAME].toString().endsWith(".mp4"));
        QCOMPARE(note.ankiObject.value(AnkiConnect::Note::FIELDS).toObject()["Expression"].toString(), QString("word"));
        QCOMPARE(note.ankiObject.value(AnkiConnect::Note::FIELDS).toObject()["Video"].toString(), marker ? QString() : QString("Existing text"));
        const auto bytes = QByteArray::fromBase64(attachment[AnkiConnect::Note::DATA].toString().toLatin1());
        QVERIFY(!bytes.isEmpty());
        QVERIFY(bytes.indexOf("moov") > 0);
        QVERIFY(bytes.indexOf("moov") < bytes.indexOf("mdat"));
        const auto output = m_dir.filePath(QString(QTest::currentDataTag()) + ".mp4");
        QFile file(output);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(bytes), bytes.size()); file.close();
        const auto probe = QJsonDocument::fromJson(run(m_ffprobe,
            {"-v", "error", "-show_streams", "-show_format", "-of", "json", output})).object();
        const auto streams = probe["streams"].toArray();
        QCOMPARE(streams.size(), audioId > 0 ? 2 : 1);
        for (const auto &value : streams)
        {
            const auto stream = value.toObject();
            if (stream["codec_type"] == "video")
            {
                QCOMPARE(stream["codec_name"].toString(), QString("h264"));
                QCOMPARE(stream["pix_fmt"].toString(), QString("yuv420p"));
                QCOMPARE(stream["width"].toInt(), 320);
            }
            else QCOMPARE(stream["codec_name"].toString(), QString("aac"));
        }
        const auto duration = probe["format"].toObject()["duration"].toString().toDouble();
        const auto expectedDuration = std::min(5.0, sourceDuration);
        QVERIFY2(std::abs(duration - expectedDuration) < 0.2, qPrintable(QString::number(duration)));
        const auto frame = run(m_ffmpeg, {"-v", "error", "-i", output,
            "-frames:v", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", "-"});
        QCOMPARE(frame.size(), 320 * 180 * 3);
        const auto red = static_cast<unsigned char>(frame[0]);
        const auto green = static_cast<unsigned char>(frame[1]);
        const auto blue = static_cast<unsigned char>(frame[2]);
        if (sourceDuration < 5.0) QVERIFY(red > green && red > blue);
        else QVERIFY(green > red && green > blue);
        if (subtitle)
        {
            int textPixels = 0;
            for (int i = 320 * 90 * 3; i < frame.size(); i += 3)
                if (static_cast<unsigned char>(frame[i]) > 160 &&
                    static_cast<unsigned char>(frame[i + 1]) > 160 &&
                    static_cast<unsigned char>(frame[i + 2]) > 160) ++textPixels;
            QVERIFY(textPixels > 20);
        }
        if (audioId > 0)
        {
            const auto pcm = run(m_ffmpeg, {"-v", "error", "-ss", "0.3", "-i", output,
                "-t", "0.5", "-ac", "1", "-ar", "16000", "-f", "s16le", "-"});
            QVERIFY(pcm.size() >= 15000);
            QCOMPARE(pcm.size() % 2, 0);
            int rising = 0;
            int previous = 0;
            for (qsizetype i = 0; i + 1 < pcm.size(); i += 2)
            {
                // Decode only the two checked bytes. Avoid Qt's unaligned
                // pointer read, which MinGW can misdiagnose as an overread of
                // QByteArray's empty storage when warnings are errors.
                const int bits = static_cast<unsigned char>(pcm.at(i)) |
                    (static_cast<unsigned char>(pcm.at(i + 1)) << 8);
                const int sample = bits >= 0x8000 ? bits - 0x10000 : bits;
                if (i > 0 && previous <= 0 && sample > 0) ++rising;
                previous = sample;
            }
            const double frequency = rising / (pcm.size() / 2.0 / 16000);
            QVERIFY2(std::abs(frequency - audioId * 440.0) < 15.0,
                qPrintable(QString::number(frequency)));
        }
    }

    void encodeFailureIsNotSilentlyDropped()
    {
        AnkiProfile profile;
        profile.termFields()->setItems(QJsonObject{{"Video", ""}});
        Term term;
        term.setSubtitle("Sentence"); term.setStartTime(1); term.setEndTime(2);
        auto media = snapshot();
        media.encoding.input = m_dir.filePath("missing.mkv").toUtf8();
        const auto note = Anki::Note::build(profile, term, true, media, {true, "Video"});
        QVERIFY(!note.error.isEmpty());
        QVERIFY(!note.ankiObject.contains(AnkiConnect::Note::VIDEO));
    }
};

QTEST_MAIN(AnkiVideoTest)
#include "test_ankivideo.moc"
