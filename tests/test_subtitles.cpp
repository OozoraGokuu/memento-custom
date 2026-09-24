#include <QFile>
#include <QItemSelectionModel>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <utility>

#include "subtitle/subtitlelistmodel.h"
#include "subtitle/subtitlemedia.h"
#include "subtitle/subtitleparser.h"

class SubtitleTests : public QObject
{
    Q_OBJECT

private slots:
    void selectsOverlapsAndHonorsHalfOpenBoundaries()
    {
        SubtitleListModel model(nullptr);
        model.addSubtitle(QStringLiteral("second"), 2.0, 4.0);
        model.addSubtitle(QStringLiteral("first"), 1.0, 3.0);
        model.addSubtitle(QStringLiteral("third"), 3.0, 5.0);

        model.selectPosition(2.5);
        QCOMPARE(model.activeText(), QStringLiteral("first\nsecond"));
        QCOMPARE(model.activeStart(), 1.0);
        QCOMPARE(model.activeEnd(), 4.0);

        model.selectPosition(3.0);
        QCOMPARE(model.activeText(), QStringLiteral("second\nthird"));
        QCOMPARE(model.activeStart(), 2.0);
        QCOMPARE(model.activeEnd(), 5.0);

        model.selectPosition(5.0);
        QVERIFY(model.activeText().isEmpty());
        QCOMPARE(model.activeStart(), 0.0);
        QCOMPARE(model.activeEnd(), 0.0);
    }

    void repeatedTickPreservesManualSelection()
    {
        SubtitleListModel model(nullptr);
        model.addSubtitle(QStringLiteral("one"), 0.0, 2.0);
        model.addSubtitle(QStringLiteral("two"), 3.0, 5.0);
        model.selectPosition(1.0);

        const QModelIndex manual = model.index(1, 0);
        model.selectionModel()->select(
            manual, QItemSelectionModel::ClearAndSelect);
        model.selectPosition(1.25);

        const QModelIndexList selected =
            model.selectionModel()->selectedIndexes();
        QCOMPARE(selected.size(), 1);
        QCOMPARE(selected.front().row(), 1);
        QCOMPARE(model.activeText(), QStringLiteral("one"));
    }

    void nativeReadinessDistinguishesCueGapsFromFallback()
    {
        SubtitleListModel model(nullptr);
        QSignalSpy readinessSpy(
            &model, &SubtitleListModel::nativeReadyChanged);

        QVERIFY(!model.nativeReady());
        QVERIFY(!model.fullTimelineReady());
        model.selectPosition(1.5);
        QCOMPARE(
            model.addSubtitle(QStringLiteral("captured"), 1.0, 2.0),
            qsizetype{0}
        );
        QVERIFY(model.nativeReady());
        QVERIFY(!model.fullTimelineReady());
        QCOMPARE(model.activeText(), QStringLiteral("captured"));

        model.selectPosition(2.5);
        QVERIFY(model.activeText().isEmpty());
        QVERIFY(model.nativeReady());

        std::vector<SubtitleEntry> emptyReplacement;
        model.setItems(std::move(emptyReplacement));
        QVERIFY(model.nativeReady());
        QVERIFY(model.activeText().isEmpty());

        model.clear();
        QVERIFY(!model.nativeReady());
        QVERIFY(!model.fullTimelineReady());

        std::vector<SubtitleEntry> emptyEntries;
        model.setItems(std::move(emptyEntries));
        QVERIFY(!model.nativeReady());
        QVERIFY(!model.fullTimelineReady());

        model.selectPosition(4.0);
        std::vector<SubtitleEntry> parsedEntries{
            SubtitleEntry{
                .text = QStringLiteral("parsed"),
                .start = 5.0,
                .end = 6.0,
            },
        };
        model.setItems(std::move(parsedEntries));
        QVERIFY(model.nativeReady());
        QVERIFY(model.fullTimelineReady());
        QVERIFY(model.activeText().isEmpty());

        model.selectPosition(5.5);
        QCOMPARE(model.activeText(), QStringLiteral("parsed"));
        QCOMPARE(readinessSpy.count(), 3);
    }

    void acceptsLongAndMultilineCuesForNativeRendering()
    {
        SubtitleListModel model(nullptr);
        QCOMPARE(
            model.addSubtitle(QStringLiteral("short"), 0.0, 1.0),
            qsizetype{0}
        );

        const QString longCue(2'500, QChar(u'長'));
        QCOMPARE(model.addSubtitle(longCue, 1.0, 2.0), qsizetype{1});
        model.selectPosition(1.5);
        QCOMPARE(model.activeText(), longCue);

        QStringList lines;
        for (int index = 0; index < 20; ++index)
        {
            lines.append(QStringLiteral("line %1").arg(index));
        }
        const QString multiline = lines.join(QLatin1Char('\n'));
        QCOMPARE(model.addSubtitle(multiline, 2.0, 3.0), qsizetype{2});
        model.selectPosition(2.5);
        QCOMPARE(model.activeText(), multiline);
    }

    void parsesSsaThroughTheAssParser()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("episode.ssa"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        const QByteArray data(
            "[Script Info]\n"
            "Title: test\n"
            "[Events]\n"
            "Format: Layer, Start, End, Style, Name, MarginL, MarginR, "
            "MarginV, Effect, Text\n"
            "Dialogue: 0,0:00:01.00,0:00:03.50,Default,,0,0,0,,hello\\Nworld\n"
        );
        QCOMPARE(file.write(data), data.size());
        file.close();

        const std::vector<SubtitleEntry> entries =
            SubtitleParser().parseSubtitles(path);
        QCOMPARE(entries.size(), size_t{1});
        QCOMPARE(entries.front().text, QStringLiteral("hello\nworld"));
        QCOMPARE(entries.front().start, 1.0);
        QCOMPARE(entries.front().end, 3.5);
    }

    void cleansAndRendersNativeSubtitleText()
    {
        SubtitleMedia::Style style;
        style.font = QFont(QStringLiteral("sans-serif"));
        style.scale = 0.10;
        style.offset = 0.05;
        style.strokeWidth = 1.0;
        style.removeRegex = QStringLiteral("<[^>]+>");
        style.replaceNewlines = true;
        style.newlineReplacement = QStringLiteral(" / ");

        QCOMPARE(
            SubtitleMedia::cleanText(
                QStringLiteral("<i>first</i>\nsecond"), style),
            QStringLiteral("first / second")
        );

        const QImage base(640, 360, QImage::Format_ARGB32_Premultiplied);
        QImage blank = base;
        blank.fill(Qt::black);
        const QImage unchanged = SubtitleMedia::renderOverlay(
            blank, QString(), QString(), style);
        QCOMPARE(unchanged, blank);

        const QImage primary = SubtitleMedia::renderOverlay(
            blank, QStringLiteral("primary"), QString(), style);
        const QImage secondary = SubtitleMedia::renderOverlay(
            blank, QString(), QStringLiteral("secondary"), style);
        QVERIFY(primary != blank);
        QVERIFY(secondary != blank);

        const auto changedPixels = [&blank] (
            const QImage &image, int beginY, int endY)
        {
            qsizetype changed = 0;
            for (int y = beginY; y < endY; ++y)
            {
                for (int x = 0; x < image.width(); ++x)
                {
                    changed += image.pixel(x, y) != blank.pixel(x, y);
                }
            }
            return changed;
        };
        QVERIFY(changedPixels(primary, 180, 360) > 0);
        QCOMPARE(changedPixels(primary, 0, 120), qsizetype{0});
        QVERIFY(changedPixels(secondary, 0, 180) > 0);
        QCOMPARE(changedPixels(secondary, 240, 360), qsizetype{0});
    }

    void generatesClippedOverlappingSanitizedAss()
    {
        SubtitleMedia::Style style;
        style.font = QFont(QStringLiteral("sans-serif"));
        style.scale = 0.05;
        style.offset = 0.04;
        style.strokeWidth = 2.0;

        SubtitleMedia::Track primary;
        primary.entries = {
            SubtitleEntry{
                .text = QStringLiteral("A{B}\\C\nD"),
                .start = 1.0,
                .end = 4.0,
            },
            SubtitleEntry{
                .text = QStringLiteral("overlap"),
                .start = 2.0,
                .end = 3.0,
            },
        };
        SubtitleMedia::Track secondary;
        secondary.delay = 0.5;
        secondary.entries = {
            SubtitleEntry{
                .text = QStringLiteral("translation"),
                .start = 1.0,
                .end = 2.0,
            },
        };

        const QByteArray ass = SubtitleMedia::makeAss(
            primary, secondary, style, 1920, 1080, 1.5, 3.5);
        QVERIFY(!ass.isEmpty());
        QVERIFY(ass.contains("PlayResX: 1920"));
        QVERIFY(ass.contains("Style: Primary"));
        QVERIFY(ass.contains("Style: Secondary"));
        QVERIFY(ass.contains("Dialogue: 0,0:00:01.50,0:00:02.00,Primary"));
        QVERIFY(ass.contains("Dialogue: 1,0:00:01.50,0:00:02.50,Secondary"));
        QVERIFY(ass.contains("overlap"));
        QVERIFY(ass.contains("\\N"));
        QVERIFY(ass.contains(QString(QChar(0xFF5B)).toUtf8()));
        QVERIFY(ass.contains(QString(QChar(0xFF5D)).toUtf8()));
        QVERIFY(ass.contains(QString(QChar(0xFF3C)).toUtf8()));
        QVERIFY(!ass.contains("A{B}"));
    }

    void preservesBackgroundAndStrokeAsSeparateAssLayers()
    {
        SubtitleMedia::Style style;
        style.font = QFont(QStringLiteral("sans-serif"));
        style.backgroundColor = QColor(10, 20, 30, 200);
        style.strokeColor = QColor(220, 30, 40);
        style.strokeWidth = 2.0;

        SubtitleMedia::Track primary;
        primary.entries = {
            SubtitleEntry{
                .text = QStringLiteral("styled"),
                .start = 1.0,
                .end = 2.0,
            },
        };
        const QByteArray ass = SubtitleMedia::makeAss(
            primary, {}, style, 1280, 720, 0.0, 3.0);

        QVERIFY(ass.contains("Style: PrimaryBackground"));
        QVERIFY(ass.contains("&H371E140A"));
        QVERIFY(ass.contains(
            "Dialogue: 0,0:00:01.00,0:00:02.00,PrimaryBackground"));
        QVERIFY(ass.contains(
            "Dialogue: 1,0:00:01.00,0:00:02.00,Primary"));
    }
};

QTEST_MAIN(SubtitleTests)

#include "test_subtitles.moc"
