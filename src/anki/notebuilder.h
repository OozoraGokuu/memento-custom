////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2025 Ripose
//
// This file is part of Memento.
//
// Memento is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2 of the License.
//
// Memento is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Memento.  If not, see <https://www.gnu.org/licenses/>.
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <QImage>
#include <QJsonObject>

#include "anki/ankiprofile.h"
#include "anki/glossarybuilder.h"
#include "dict/data/kanji.h"
#include "dict/data/term.h"
#include "player/mpvcontroller.h"
#include "subtitle/subtitlemedia.h"

class Context;

namespace Anki
{
namespace Note
{

/**
 * @brief Context containing information to build a term note.
 */
struct Context
{
    /**
     * @brief Set the deck of this Anki object.
     *
     * @param deck The name of the deck to use.
     */
    void setDeck(const QString &deck);

    /**
     * @brief Set the model of this Anki object.
     *
     * @param model The name of the model to use.
     */
    void setModel(const QString &model);

    /**
     * @brief Set the tags of the Anki object.
     *
     * @param tags
     */
    void setTags(const QStringList &tags);

    /**
     * @brief Set the duplicate policy of the Anki object.
     *
     * @param policy The duplicate policy.
     */
    void setDuplicatePolicy(Anki::DuplicatePolicy policy);

    /**
     * @brief Set the fields for this context.
     *
     * @param fields The fields to use for this object.
     */
    void setFields(const QJsonValue &fields);

    /* AnkiConnect compatible note object */
    QJsonObject ankiObject;

    /* A mapping of file to filenames */
    QList<GlossaryBuilder::FileInfo> fileMap;
};

/**
 * @brief Immutable player and subtitle state used to build note media.
 *
 * This is captured on the player thread before note construction moves to a
 * worker. It deliberately contains no QObject pointers or live mpv handles.
 */
struct MediaSnapshot
{
    MpvEncodingContext encoding;
    QImage screenshotFrame;
    QImage videoFrame;
    SubtitleMedia::Style subtitleStyle;
    SubtitleMedia::Track primaryTrack;
    SubtitleMedia::Track secondaryTrack;
    QString title;
    QString primaryText;
    QString secondaryText;
    double position{0.0};
    double primaryStart{0.0};
    double primaryEnd{0.0};
    double secondaryStart{0.0};
    double secondaryEnd{0.0};
    int videoWidth{0};
    int videoHeight{0};
    bool stateCaptured{false};
    bool nativePrimary{false};
    bool nativeSecondary{false};
    bool completePrimaryTimeline{false};
    bool completeSecondaryTimeline{false};
};

/**
 * @brief Capture player media and native subtitle state on the owning thread.
 */
[[nodiscard]] MediaSnapshot captureMedia(const ::Context &context);

/**
 * @brief Create an AnkiConnect compatible note JSON object with corresponding
 * media.
 *
 * @param profile The profile to build the note to.
 * @param term The term to make the object from.
 * @param media true if screenshots and audio should be included in the object,
 * false otherwise.
 * @param mediaSnapshot Immutable player data captured before worker execution.
 * @return A Context containing an Anki compatible JSON object.
 */
[[nodiscard]]
Anki::Note::Context build(
    const AnkiProfile &profile,
    const Term &term,
    bool media,
    const MediaSnapshot &mediaSnapshot = {});

/**
 * @brief Create an AnkiConnect compatible note JSON object with corresponding
 * media.
 *
 * @param profile The profile to build the note to.
 * @param kanji The kanji to make the object from.
 * @param media true if screenshots and audio should be included in the object,
 * false otherwise.
 * @param mediaSnapshot Immutable player data captured before worker execution.
 * @return A Context containing an Anki compatible JSON object.
 */
[[nodiscard]]
Anki::Note::Context build(
    const AnkiProfile &profile,
    const Kanji &kanji,
    bool media,
    const MediaSnapshot &mediaSnapshot = {});
}
}
