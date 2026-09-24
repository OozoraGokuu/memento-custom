////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2026 Ripose
//
// This file is part of Memento.
//
// Memento is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2 of the License.
//
////////////////////////////////////////////////////////////////////////////////

#include "subtitle/subtitlemedia.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QStringList>

namespace
{

struct Segment
{
    double start{0.0};
    double end{0.0};
    QString text;
};

struct Boundary
{
    double time{0.0};
    int index{-1};
    bool start{false};
};

QString safeAssField(QString value)
{
    value.replace('\r', ' ');
    value.replace('\n', ' ');
    value.replace(',', ' ');
    return value.trimmed();
}

QString assColor(const QColor &color)
{
    return QStringLiteral("&H%1%2%3%4")
        .arg(255 - color.alpha(), 2, 16, QLatin1Char('0'))
        .arg(color.blue(), 2, 16, QLatin1Char('0'))
        .arg(color.green(), 2, 16, QLatin1Char('0'))
        .arg(color.red(), 2, 16, QLatin1Char('0'))
        .toUpper();
}

QString assTime(double seconds)
{
    const qint64 centiseconds = std::max<qint64>(
        0,
        static_cast<qint64>(std::llround(seconds * 100.0))
    );
    const qint64 hours = centiseconds / 360000;
    const qint64 minutes = (centiseconds / 6000) % 60;
    const qint64 wholeSeconds = (centiseconds / 100) % 60;
    const qint64 fraction = centiseconds % 100;
    return QStringLiteral("%1:%2:%3.%4")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(wholeSeconds, 2, 10, QLatin1Char('0'))
        .arg(fraction, 2, 10, QLatin1Char('0'));
}

QString escapeAssText(QString value)
{
    // Braces and backslashes introduce ASS override/control sequences. Use
    // their full-width display forms so subtitle content remains inert.
    value.replace('\\', QChar(0xFF3C));
    value.replace('{', QChar(0xFF5B));
    value.replace('}', QChar(0xFF5D));
    value.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    value.replace('\r', '\n');
    value.replace('\n', QStringLiteral("\\N"));
    return value;
}

std::vector<Segment> timeline(
    const SubtitleMedia::Track &track,
    const SubtitleMedia::Style &style,
    double clipStart,
    double clipEnd)
{
    std::vector<SubtitleEntry> entries;
    entries.reserve(track.entries.size());
    std::vector<Boundary> boundaries;
    boundaries.reserve(track.entries.size() * 2);

    for (const SubtitleEntry &source : track.entries)
    {
        SubtitleEntry entry = source;
        entry.text = SubtitleMedia::cleanText(std::move(entry.text), style);
        entry.start = std::max(0.0, entry.start + track.delay);
        entry.end = std::max(0.0, entry.end + track.delay);
        entry.start = std::max(entry.start, clipStart);
        entry.end = std::min(entry.end, clipEnd);
        if (entry.text.isEmpty() || !std::isfinite(entry.start) ||
            !std::isfinite(entry.end) || entry.start >= entry.end)
        {
            continue;
        }
        const int index = static_cast<int>(entries.size());
        entries.emplace_back(std::move(entry));
        boundaries.emplace_back(Boundary{entries.back().start, index, true});
        boundaries.emplace_back(Boundary{entries.back().end, index, false});
    }

    std::sort(
        boundaries.begin(), boundaries.end(),
        [] (const Boundary &left, const Boundary &right) {
            if (left.time != right.time)
            {
                return left.time < right.time;
            }
            // Half-open cues: remove ending entries before adding starters.
            return left.start < right.start;
        }
    );

    std::set<int> active;
    std::vector<Segment> result;
    std::size_t position = 0;
    while (position < boundaries.size())
    {
        const double time = boundaries[position].time;
        std::size_t next = position;
        while (next < boundaries.size() && boundaries[next].time == time)
        {
            if (!boundaries[next].start)
            {
                active.erase(boundaries[next].index);
            }
            ++next;
        }
        for (std::size_t index = position; index < next; ++index)
        {
            if (boundaries[index].start)
            {
                active.insert(boundaries[index].index);
            }
        }

        if (next < boundaries.size() && boundaries[next].time > time &&
            !active.empty())
        {
            QStringList text;
            for (const int index : active)
            {
                text.emplaceBack(entries.at(static_cast<std::size_t>(index)).text);
            }
            Segment segment{time, boundaries[next].time, text.join('\n')};
            if (!result.empty() && result.back().end == segment.start &&
                result.back().text == segment.text)
            {
                result.back().end = segment.end;
            }
            else
            {
                result.emplace_back(std::move(segment));
            }
        }
        position = next;
    }
    return result;
}

void paintTrack(
    QPainter &painter,
    const QString &text,
    const SubtitleMedia::Style &style,
    bool top)
{
    if (text.isEmpty() || painter.device() == nullptr)
    {
        return;
    }

    const int frameWidth = painter.device()->width();
    const int frameHeight = painter.device()->height();
    if (frameWidth <= 0 || frameHeight <= 0)
    {
        return;
    }

    QFont font = style.font;
    int pixelSize = std::max(1, qRound(frameHeight * style.scale));
    font.setPixelSize(pixelSize);
    QStringList lines = text.split('\n');

    QFontMetricsF metrics(font);
    qreal maximumWidth = 0.0;
    for (const QString &line : lines)
    {
        maximumWidth = std::max(maximumWidth, metrics.horizontalAdvance(line));
    }
    const qreal availableWidth = std::max(1.0, frameWidth - style.strokeWidth);
    qreal sizeFactor = 1.0;
    if (maximumWidth > availableWidth)
    {
        sizeFactor = availableWidth / maximumWidth;
        pixelSize = std::max(1, qFloor(pixelSize * sizeFactor));
        font.setPixelSize(pixelSize);
        metrics = QFontMetricsF(font);
    }

    const qreal spacing = style.lineSpacing * sizeFactor;
    const qreal lineHeight = metrics.height();
    const qreal totalHeight = lines.size() * lineHeight +
        std::max<qsizetype>(0, lines.size() - 1) * spacing;
    const qreal margin = std::clamp(
        frameHeight * style.offset,
        0.0,
        std::max(0.0, frameHeight - totalHeight)
    );
    const qreal firstTop = top ? margin : frameHeight - margin - totalHeight;
    const qreal strokeWidth = std::max(0.0, style.strokeWidth * sizeFactor);

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    for (qsizetype index = 0; index < lines.size(); ++index)
    {
        const QString &line = lines.at(index);
        const qreal lineWidth = metrics.horizontalAdvance(line);
        const qreal x = (frameWidth - lineWidth) / 2.0;
        const qreal topY = firstTop + index * (lineHeight + spacing);
        const qreal baseline = topY + metrics.ascent();

        if (style.backgroundColor.alpha() > 0)
        {
            painter.fillRect(
                QRectF(x, topY, lineWidth, lineHeight)
                    .adjusted(-strokeWidth / 2.0, 0.0,
                              strokeWidth / 2.0, 0.0),
                style.backgroundColor
            );
        }
        if (line.isEmpty())
        {
            continue;
        }

        QPainterPath path;
        path.addText(QPointF(x, baseline), font, line);
        if (strokeWidth > 0.0 && style.strokeColor.alpha() > 0)
        {
            QPen pen(style.strokeColor, strokeWidth);
            pen.setCapStyle(Qt::RoundCap);
            pen.setJoinStyle(Qt::RoundJoin);
            painter.strokePath(path, pen);
        }
        painter.fillPath(path, style.textColor);
    }
    painter.restore();
}

} // namespace

QString SubtitleMedia::cleanText(QString text, const Style &style)
{
    if (!style.removeRegex.isEmpty())
    {
        const QRegularExpression expression(style.removeRegex);
        if (expression.isValid())
        {
            text.remove(expression);
        }
    }
    if (style.replaceNewlines)
    {
        text.replace('\n', style.newlineReplacement);
    }
    return text;
}

QImage SubtitleMedia::renderOverlay(
    QImage frame,
    const QString &primary,
    const QString &secondary,
    const Style &style)
{
    if (frame.isNull())
    {
        return {};
    }
    QPainter painter(&frame);
    paintTrack(painter, cleanText(secondary, style), style, true);
    paintTrack(painter, cleanText(primary, style), style, false);
    return frame;
}

QByteArray SubtitleMedia::makeAss(
    const Track &primary,
    const Track &secondary,
    const Style &style,
    int width,
    int height,
    double clipStart,
    double clipEnd)
{
    if (width <= 0 || height <= 0 || !std::isfinite(clipStart) ||
        !std::isfinite(clipEnd) || clipStart >= clipEnd)
    {
        return {};
    }

    const std::vector<Segment> primarySegments = timeline(
        primary, style, clipStart, clipEnd);
    const std::vector<Segment> secondarySegments = timeline(
        secondary, style, clipStart, clipEnd);
    if (primarySegments.empty() && secondarySegments.empty())
    {
        return {};
    }

    const QString family = safeAssField(style.font.family()).isEmpty() ?
        QStringLiteral("sans-serif") : safeAssField(style.font.family());
    const int fontSize = std::max(1, qRound(height * style.scale));
    const int margin = std::max(0, qRound(height * style.offset));
    const int bold = style.font.bold() || style.font.weight() >= QFont::DemiBold ?
        -1 : 0;
    const int italic = style.font.italic() ? -1 : 0;
    const int underline = style.font.underline() ? -1 : 0;
    const int strikeout = style.font.strikeOut() ? -1 : 0;
    const bool hasBackground = style.backgroundColor.alpha() > 0;

    QString document = QStringLiteral(
        "[Script Info]\n"
        "ScriptType: v4.00+\n"
        "WrapStyle: 0\n"
        "ScaledBorderAndShadow: yes\n"
        "PlayResX: %1\n"
        "PlayResY: %2\n\n"
        "[V4+ Styles]\n"
        "Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, "
        "OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, "
        "ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, "
        "Alignment, MarginL, MarginR, MarginV, Encoding\n"
    ).arg(width).arg(height);

    const auto appendStyle = [&] (
        const QString &name,
        int alignment,
        const QColor &primaryColor,
        const QColor &outlineColor,
        const QColor &backColor,
        int borderStyle,
        double outlineWidth)
    {
        document += QStringLiteral(
            "Style: %1,%2,%3,%4,%4,%5,%6,%7,%8,%9,%10,100,100,0,0,"
            "%11,%12,0,%13,0,0,%14,1\n"
        )
            .arg(name, family)
            .arg(fontSize)
            .arg(assColor(primaryColor))
            .arg(assColor(outlineColor))
            .arg(assColor(backColor))
            .arg(bold)
            .arg(italic)
            .arg(underline)
            .arg(strikeout)
            .arg(borderStyle)
            .arg(std::max(0.0, outlineWidth), 0, 'f', 2)
            .arg(alignment)
            .arg(margin);
    };
    const QColor transparent(Qt::transparent);
    appendStyle(
        QStringLiteral("Primary"), 2, style.textColor, style.strokeColor,
        transparent, 1, style.strokeWidth);
    appendStyle(
        QStringLiteral("Secondary"), 8, style.textColor, style.strokeColor,
        transparent, 1, style.strokeWidth);
    if (hasBackground)
    {
        appendStyle(
            QStringLiteral("PrimaryBackground"), 2, transparent,
            style.backgroundColor, style.backgroundColor, 3,
            style.strokeWidth);
        appendStyle(
            QStringLiteral("SecondaryBackground"), 8, transparent,
            style.backgroundColor, style.backgroundColor, 3,
            style.strokeWidth);
    }
    document += QStringLiteral(
        "\n[Events]\n"
        "Format: Layer, Start, End, Style, Name, MarginL, MarginR, "
        "MarginV, Effect, Text\n"
    );

    const auto appendSegments = [&] (
        const std::vector<Segment> &segments,
        const QString &styleName,
        int layer)
    {
        for (const Segment &segment : segments)
        {
            document += QStringLiteral("Dialogue: %1,%2,%3,%4,,0,0,0,,%5\n")
                .arg(layer)
                .arg(assTime(segment.start))
                .arg(assTime(segment.end))
                .arg(styleName)
                .arg(escapeAssText(segment.text));
        }
    };
    if (hasBackground)
    {
        appendSegments(
            primarySegments, QStringLiteral("PrimaryBackground"), 0);
        appendSegments(
            secondarySegments, QStringLiteral("SecondaryBackground"), 2);
        appendSegments(primarySegments, QStringLiteral("Primary"), 1);
        appendSegments(secondarySegments, QStringLiteral("Secondary"), 3);
    }
    else
    {
        appendSegments(primarySegments, QStringLiteral("Primary"), 0);
        appendSegments(secondarySegments, QStringLiteral("Secondary"), 1);
    }
    return document.toUtf8();
}
