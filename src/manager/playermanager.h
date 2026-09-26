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

#include <QObject>

class Context;
class PlayerManagerTest;
class SubtitleListModel;

/**
 * @brief Manages the main player state, implementing backend features.
 */
class PlayerManager : public QObject
{
    Q_OBJECT

public:
    /**
     * @brief Creates a new player manager.
     *
     * @param context The context of the application.
     * @param parent The parent of this manager.
     */
    PlayerManager(Context *context, QObject *parent = nullptr);
    virtual ~PlayerManager();

private slots:
    /**
     * @brief Resets auto pause state.
     */
    void resetAutoPause();

    /**
     * @brief Corrects auto-pause in the case of seeking.
     *
     * @param position The current time position.
     */
    void handleAutoPausePosition(double position);

    /**
     * @brief Follow the active Memento subtitle model used for auto-pause.
     *
     * @param model The newly selected primary subtitle model.
     */
    void handlePrimarySubtitleListChanged(SubtitleListModel *model);

    /**
     * @brief Use mpv subtitle changes only when no native model is usable.
     */
    void handleMpvSubtitleChanged();

    /**
     * @brief Handles auto pausing on subtitles if enabled.
     */
    void handleAutoPause();

private:
    friend class PlayerManagerTest;

    /* The application context */
    Context *m_context{nullptr};
    bool m_restoringAudio{false};

    /**
     * @brief Holds data about auto pausing.
     */
    struct AutoPauseData
    {
        /* true if this should be reset on next handle */
        bool reset{false};

        /* The end time of the previous subtitle to pause to */
        double lastEndTime{0};

        /* The start time of the subtitle to pause to */
        double startTime{0};

        /* The end time of the subtitle to pause to */
        double endTime{0};

        /* true if we already paused at the last end time */
        bool alreadyPaused{false};

        /* The previous player position. Used to determine seeking behavior. */
        double previousPosition{-1};
    };
    AutoPauseData m_autoPauseData{};

    /**
     * @brief Tests whether a native cue update continues the pending window.
     *
     * Adjacent cues deliberately do not count as overlapping, since their
     * shared boundary is where auto-pause should occur.
     */
    static bool isOverlappingNativeContinuation(
        double pendingEndTime,
        double startTime,
        double endTime
    ) noexcept;

    /* Connection to Memento's current native subtitle timing signal. */
    QMetaObject::Connection m_nativeSubtitleConnection;

    /* Connection used to reset fallback state when native authority changes. */
    QMetaObject::Connection m_nativeReadyConnection;

    /* Connection used to reset timing when parsed native data replaces cues. */
    QMetaObject::Connection m_nativeModelResetConnection;
};
