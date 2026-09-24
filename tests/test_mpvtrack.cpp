#include <QSignalSpy>
#include <QTest>

#include "player/mpvtrack.h"

class MpvTrackTest : public QObject
{
    Q_OBJECT

private slots:
    void recognizesBitmapSubtitleCodecs_data()
    {
        QTest::addColumn<QString>("codec");

        QTest::newRow("ffmpeg-pgs") << QStringLiteral("hdmv_pgs_subtitle");
        QTest::newRow("short-pgs") << QStringLiteral("pgs");
        QTest::newRow("pgssub") << QStringLiteral("PGSSUB");
        QTest::newRow("dvd-subtitle") << QStringLiteral("dvd_subtitle");
        QTest::newRow("vobsub") << QStringLiteral("vobsub");
        QTest::newRow("vob-subtitle") << QStringLiteral("vob-subtitle");
        QTest::newRow("idx-sub") << QStringLiteral("idx-sub");
        QTest::newRow("idx-sub-spaced") << QStringLiteral(" IDX SUB ");
        QTest::newRow("dvb-subtitle") << QStringLiteral("dvb_subtitle");
        QTest::newRow("dvbsub") << QStringLiteral("DVBSub");
        QTest::newRow("xsub") << QStringLiteral("xsub");
    }

    void recognizesBitmapSubtitleCodecs()
    {
        QFETCH(QString, codec);

        QVERIFY(MpvTrack::isBitmapSubtitleCodec(QStringView{codec}));

        MpvTrack track;
        track.setCodec(codec);
        QVERIFY(track.isBitmapSubtitle());
        QVERIFY(track.property("bitmapSubtitle").toBool());
    }

    void rejectsTextSubtitleCodecs_data()
    {
        QTest::addColumn<QString>("codec");

        QTest::newRow("empty") << QString{};
        QTest::newRow("ass") << QStringLiteral("ass");
        QTest::newRow("ssa") << QStringLiteral("ssa");
        QTest::newRow("subrip") << QStringLiteral("subrip");
        QTest::newRow("srt") << QStringLiteral("srt");
        QTest::newRow("webvtt") << QStringLiteral("webvtt");
        QTest::newRow("mov-text") << QStringLiteral("mov_text");
        QTest::newRow("lookalike") << QStringLiteral("pgs_text");
    }

    void rejectsTextSubtitleCodecs()
    {
        QFETCH(QString, codec);

        QVERIFY(!MpvTrack::isBitmapSubtitleCodec(QStringView{codec}));

        MpvTrack track;
        track.setCodec(codec);
        QVERIFY(!track.isBitmapSubtitle());
        QVERIFY(!track.property("bitmapSubtitle").toBool());
    }

    void notifiesOnlyWhenBitmapClassificationChanges()
    {
        MpvTrack track;
        QSignalSpy codecSpy(&track, &MpvTrack::codecChanged);
        QSignalSpy bitmapSpy(&track, &MpvTrack::bitmapSubtitleChanged);

        track.setCodec(QStringLiteral("ass"));
        QCOMPARE(codecSpy.count(), 1);
        QCOMPARE(bitmapSpy.count(), 0);
        QCOMPARE(codecSpy.constFirst().constFirst().toString(),
                 QStringLiteral("ass"));

        track.setCodec(QStringLiteral("hdmv_pgs_subtitle"));
        QCOMPARE(codecSpy.count(), 2);
        QCOMPARE(bitmapSpy.count(), 1);
        QCOMPARE(bitmapSpy.constFirst().constFirst().toBool(), true);

        track.setCodec(QStringLiteral("PGS"));
        QCOMPARE(codecSpy.count(), 3);
        QCOMPARE(bitmapSpy.count(), 1);

        track.setCodec(QStringLiteral("subrip"));
        QCOMPARE(codecSpy.count(), 4);
        QCOMPARE(bitmapSpy.count(), 2);
        QCOMPARE(bitmapSpy.constLast().constFirst().toBool(), false);
    }
};

QTEST_GUILESS_MAIN(MpvTrackTest)

#include "test_mpvtrack.moc"
