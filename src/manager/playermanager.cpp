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

#include "manager/playermanager.h"

#include <algorithm>
#include <cmath>

#include "player/mpvplayer.h"
#include "state/context.h"

PlayerManager::PlayerManager(Context *context, QObject *parent) :
    QObject(parent),
    m_context(context)
{
    connect(
        m_context->settings(), &Settings::behaviorSubtitlePauseChanged,
        this, &PlayerManager::resetAutoPause,
        Qt::QueuedConnection
    );
    connect(
        m_context->player(), &MpvPlayer::fileLoaded,
        this, &PlayerManager::resetAutoPause,
        Qt::QueuedConnection
    );
    connect(
        m_context->player()->state()->subtitle(), &MpvSubtitle::delayChanged,
        this, &PlayerManager::resetAutoPause,
        Qt::QueuedConnection
    );
    connect(
        m_context->player()->state(), &MpvState::timePositionChanged,
        this, &PlayerManager::handleAutoPausePosition,
        Qt::QueuedConnection
    );
    connect(
        m_context->player()->state()->subtitle(), &MpvSubtitle::textChanged,
        this, &PlayerManager::handleMpvSubtitleChanged,
        Qt::QueuedConnection
    );
    connect(
        m_context->subtitleLists(), &SubtitleLists::primaryChanged,
        this, &PlayerManager::handlePrimarySubtitleListChanged
    );
    handlePrimarySubtitleListChanged(m_context->subtitleLists()->primary());
    connect(m_context->player()->controller(), &MpvController::audioTrackSelected, this, [this](int64_t id) {
        const auto *state = m_context->player()->state();
        int ordinal = id == 0 ? 0 : -1;
        for (int i = 0; i < state->audioTracks().size(); ++i)
            if (state->audioTracks()[i]->id() == id) ordinal = i + 1;
        if (ordinal >= 0 && !m_restoringAudio)
            m_context->episodeLibrary()->setDefaultAudioTrackForFile(state->path(), ordinal);
    });
    connect(m_context->player(), &MpvPlayer::fileLoaded, this, [this] {
        const auto *state = m_context->player()->state();
        const int ordinal = m_context->episodeLibrary()->defaultAudioTrackForFile(state->path());
        if (ordinal < 0 || ordinal > state->audioTracks().size()) return;
        m_restoringAudio = true;
        m_context->player()->controller()->setAid(ordinal == 0 ? 0 : state->audioTracks()[ordinal - 1]->id());
        m_restoringAudio = false;
    }, Qt::QueuedConnection);
}

PlayerManager::~PlayerManager()
{

}

void PlayerManager::resetAutoPause()
{
    m_autoPauseData.reset = true;
}

void PlayerManager::handleAutoPausePosition(double position)
{
    constexpr double SEEK_DELTA{1};
    const double subtitleDelay =
        m_context->player()->state()->subtitle()->delay();
    const double displayedStart = m_autoPauseData.startTime + subtitleDelay;
    const double displayedEnd = m_autoPauseData.endTime + subtitleDelay;

    if (m_autoPauseData.previousPosition < 0)
    {
        /* noop */
    }
    else if (displayedStart <= position && position <= displayedEnd)
    {
        /* noop */
    }
    else if (std::abs(m_autoPauseData.previousPosition - position) > SEEK_DELTA)
    {
        resetAutoPause();
    }
    m_autoPauseData.previousPosition = position;
}

void PlayerManager::handlePrimarySubtitleListChanged(SubtitleListModel *model)
{
    QObject::disconnect(m_nativeSubtitleConnection);
    m_nativeSubtitleConnection = {};
    QObject::disconnect(m_nativeReadyConnection);
    m_nativeReadyConnection = {};
    QObject::disconnect(m_nativeModelResetConnection);
    m_nativeModelResetConnection = {};
    resetAutoPause();

    if (model != nullptr)
    {
        m_nativeSubtitleConnection = connect(
            model, &SubtitleListModel::activeSubtitleChanged,
            this, &PlayerManager::handleAutoPause,
            Qt::QueuedConnection
        );
        m_nativeReadyConnection = connect(
            model, &SubtitleListModel::nativeReadyChanged,
            this, &PlayerManager::resetAutoPause
        );
        m_nativeModelResetConnection = connect(
            model, &SubtitleListModel::modelReset,
            this, &PlayerManager::resetAutoPause
        );
    }
}

void PlayerManager::handleMpvSubtitleChanged()
{
    const SubtitleListModel *model = m_context->subtitleLists()->primary();
    if (model == nullptr || !model->nativeReady())
    {
        handleAutoPause();
    }
}

bool PlayerManager::isOverlappingNativeContinuation(
    double pendingEndTime,
    double startTime,
    double endTime
) noexcept
{
    return pendingEndTime > 0 && endTime > 0 &&
        startTime < pendingEndTime && endTime >= pendingEndTime;
}

void PlayerManager::handleAutoPause()
{
    if (!m_context->settings()->behaviorSubtitlePause())
    {
        return;
    }

    if (m_autoPauseData.reset)
    {
        m_autoPauseData = {};
    }

    const SubtitleListModel *subtitleList =
        m_context->subtitleLists()->primary();
    const bool useNativeTiming =
        subtitleList != nullptr && subtitleList->nativeReady();
    double startTime = useNativeTiming ? subtitleList->activeStart() :
        m_context->player()->state()->subtitle()->startTime();
    double endTime = useNativeTiming ? subtitleList->activeEnd() :
        m_context->player()->state()->subtitle()->endTime();

    // A zero end marks the transition into a cue gap. It must reach the
    // pause path even when lastEndTime still has its initial zero value.
    if (endTime != 0 && m_autoPauseData.lastEndTime == endTime)
    {
        return;
    }
    if (m_autoPauseData.startTime == startTime &&
        m_autoPauseData.endTime == endTime)
    {
        return;
    }

    if (endTime == 0)
    {
        if (m_autoPauseData.endTime == 0)
        {
            return;
        }
    }
    else if (m_autoPauseData.endTime == 0)
    {
        m_autoPauseData.startTime = startTime;
        m_autoPauseData.endTime = endTime;
        m_autoPauseData.alreadyPaused = false;
        return;
    }
    else if (useNativeTiming && isOverlappingNativeContinuation(
        m_autoPauseData.endTime, startTime, endTime))
    {
        // Native timing aggregates all active cues. If an overlapping cue
        // extends that aggregate, defer the pause to its later boundary.
        m_autoPauseData.startTime = startTime;
        m_autoPauseData.endTime = endTime;
        m_autoPauseData.alreadyPaused = false;
        return;
    }
    else if (m_autoPauseData.endTime > endTime)
    {
        m_autoPauseData.startTime = startTime;
        m_autoPauseData.endTime = endTime;
        m_autoPauseData.alreadyPaused = false;
        return;
    }

    if (m_context->player()->state()->pause())
    {
        return;
    }

    if (m_autoPauseData.alreadyPaused)
    {
        m_autoPauseData.alreadyPaused = false;
        return;
    }

    m_context->player()->controller()->pause();
    double idealSeekTime = std::clamp(
        m_autoPauseData.endTime +
            m_context->player()->state()->subtitle()->delay() -
            0.1,
        0.0,
        m_context->player()->state()->duration()
    );
    m_context->player()->controller()->seek(idealSeekTime);

    m_autoPauseData.lastEndTime = m_autoPauseData.endTime;
    m_autoPauseData.startTime = startTime;
    m_autoPauseData.endTime = endTime;
    m_autoPauseData.alreadyPaused = true;
}
