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

#pragma once

#include <vector>

#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QImage>
#include <QString>

#include "subtitle/subtitleentry.h"

namespace SubtitleMedia
{

/** Immutable subtitle presentation values safe to copy to a worker thread. */
struct Style
{
    QFont font;
    QColor textColor{Qt::white};
    QColor backgroundColor{Qt::transparent};
    QColor strokeColor{Qt::black};
    double scale{0.05};
    double offset{0.045};
    double strokeWidth{1.0};
    double lineSpacing{0.0};
    QString removeRegex;
    QString newlineReplacement;
    bool replaceNewlines{false};
};

/** Native cue data for one subtitle track. */
struct Track
{
    std::vector<SubtitleEntry> entries;
    QString activeText;
    double delay{0.0};
};

/** Apply the same text cleanup used by Memento's on-screen subtitles. */
[[nodiscard]] QString cleanText(QString text, const Style &style);

/**
 * Paint primary (bottom) and secondary (top) text over a video-only frame.
 */
[[nodiscard]] QImage renderOverlay(
    QImage frame,
    const QString &primary,
    const QString &secondary,
    const Style &style
);

/**
 * Build a self-contained ASS overlay from Memento's native cue timelines.
 * Times remain on the source media timeline; cues are clipped to the requested
 * interval so seeking encoders receive the correct active text immediately.
 */
[[nodiscard]] QByteArray makeAss(
    const Track &primary,
    const Track &secondary,
    const Style &style,
    int width,
    int height,
    double clipStart,
    double clipEnd
);

} // namespace SubtitleMedia
