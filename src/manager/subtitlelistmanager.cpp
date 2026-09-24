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

#include "manager/subtitlelistmanager.h"

#include <QPointer>
#include <QtConcurrentRun>

#include <utility>

#ifdef MEMENTO_SYSTEM_QCORO
#include <QCoroFuture>
#else
#include <qcoro/core/qcorofuture.h>
#endif // MEMENTO_SYSTEM_QCORO

#include "player/mpvplayer.h"
#include "state/context.h"
#include "subtitle/subtitleparser.h"

SubtitleListManager::SubtitleListManager(Context *context, QObject *parent) :
    QObject(parent),
    m_context(context)
{
    connect(
        m_context->player(), &MpvPlayer::fileLoaded,
        this, &SubtitleListManager::handleFileLoaded
    );
    connect(
        m_context->player()->state(), &MpvState::subtitleTracksChanged,
        this, &SubtitleListManager::handleSubtitleTracksChanged
    );
    connect(
        m_context->player()->state(), &MpvState::sidChanged,
        this, &SubtitleListManager::handleSidChanged
    );
    connect(
        m_context->player()->state(), &MpvState::secondarySidChanged,
        this, &SubtitleListManager::handleSecondarySidChanged
    );
    connect(
        m_context->player()->state()->subtitle(), &MpvSubtitle::textChanged,
        this, &SubtitleListManager::handlePrimarySubtitleChanged
    );
    connect(
        m_context->player()->state()->subtitle(),
        &MpvSubtitle::startTimeChanged,
        this,
        &SubtitleListManager::handlePrimarySubtitleChanged,
        Qt::QueuedConnection
    );
    connect(
        m_context->player()->state()->subtitle(), &MpvSubtitle::endTimeChanged,
        this,
        &SubtitleListManager::handlePrimarySubtitleChanged,
        Qt::QueuedConnection
    );
    connect(
        m_context->player()->state()->secondarySubtitle(),
        &MpvSubtitle::textChanged,
        this,
        &SubtitleListManager::handleSecondarySubtitleChanged
    );
    connect(
        m_context->player()->state()->secondarySubtitle(),
        &MpvSubtitle::startTimeChanged,
        this,
        &SubtitleListManager::handleSecondarySubtitleChanged,
        Qt::QueuedConnection
    );
    connect(
        m_context->player()->state()->secondarySubtitle(),
        &MpvSubtitle::endTimeChanged,
        this,
        &SubtitleListManager::handleSecondarySubtitleChanged,
        Qt::QueuedConnection
    );
    connect(
        m_context->player()->state(), &MpvState::timePositionChanged,
        this, &SubtitleListManager::handleTimePositionChanged
    );
    connect(
        m_context->player()->state()->subtitle(), &MpvSubtitle::delayChanged,
        this, &SubtitleListManager::handlePrimaryDelayChanged
    );
    connect(
        m_context->player()->state()->secondarySubtitle(),
        &MpvSubtitle::delayChanged,
        this,
        &SubtitleListManager::handleSecondaryDelayChanged
    );
}

SubtitleListManager::~SubtitleListManager()
{

}

void SubtitleListManager::handleFileLoaded()
{
    clearLists();
    handleSubtitleTracksChanged();
}

void SubtitleListManager::clearLists()
{
    m_context->subtitleLists()->setPrimary(nullptr);
    m_context->subtitleLists()->setSecondary(nullptr);
    for (const TrackModel &trackModel : std::as_const(m_models))
    {
        delete trackModel.model;
    }
    m_models.clear();
    m_mediaPath.clear();
}

void SubtitleListManager::handleSubtitleTracksChanged()
{
    const QString mediaPath = m_context->player()->state()->path();
    if (m_mediaPath != mediaPath)
    {
        clearLists();
        m_mediaPath = mediaPath;
    }

    QHash<int64_t, TrackModel> updatedModels;
    const QList<MpvTrack *> &tracks =
        m_context->player()->state()->subtitleTracks();
    for (const MpvTrack *track : tracks)
    {
        const int64_t id = track->id();
        const QString externalFilename =
            track->external() ? track->externalFilename() : QString();

        TrackModel trackModel;
        auto existingIt = m_models.find(id);
        if (existingIt != m_models.end() &&
            existingIt->external == track->external() &&
            existingIt->externalFilename == externalFilename)
        {
            trackModel = *existingIt;
            m_models.erase(existingIt);
        }
        else
        {
            trackModel = {
                .model = new SubtitleListModel(m_context, this),
                .external = track->external(),
                .externalFilename = externalFilename,
            };
            if (trackModel.external)
            {
                readExternalSubtitles(
                    trackModel.model, trackModel.externalFilename);
            }
        }
        updatedModels.insert(id, std::move(trackModel));
    }

    SubtitleListModel *primary = m_context->subtitleLists()->primary();
    SubtitleListModel *secondary = m_context->subtitleLists()->secondary();
    for (const TrackModel &trackModel : std::as_const(m_models))
    {
        if (trackModel.model == primary)
        {
            m_context->subtitleLists()->setPrimary(nullptr);
        }
        if (trackModel.model == secondary)
        {
            m_context->subtitleLists()->setSecondary(nullptr);
        }
        delete trackModel.model;
    }
    m_models = std::move(updatedModels);

    handleSidChanged(m_context->player()->state()->sid());
    handleSecondarySidChanged(m_context->player()->state()->secondarySid());
}

void SubtitleListManager::handleSidChanged(int64_t sid)
{
    SubtitleListModel *model = nullptr;
    auto modelIt = m_models.constFind(sid);
    if (sid > 0 && modelIt != m_models.cend())
    {
        model = modelIt->model;
    }

    m_context->subtitleLists()->setPrimary(model);
    selectPosition(
        model,
        m_context->player()->state()->subtitle(),
        m_context->player()->state()->timePosition()
    );
}

void SubtitleListManager::handleSecondarySidChanged(int64_t sid)
{
    SubtitleListModel *model = nullptr;
    auto modelIt = m_models.constFind(sid);
    if (sid > 0 && modelIt != m_models.cend())
    {
        model = modelIt->model;
    }

    m_context->subtitleLists()->setSecondary(model);
    selectPosition(
        model,
        m_context->player()->state()->secondarySubtitle(),
        m_context->player()->state()->timePosition()
    );
}

void SubtitleListManager::handleTimePositionChanged(double position)
{
    SubtitleListModel *primary = m_context->subtitleLists()->primary();
    SubtitleListModel *secondary = m_context->subtitleLists()->secondary();

    selectPosition(
        primary, m_context->player()->state()->subtitle(), position);
    if (secondary != primary)
    {
        selectPosition(
            secondary,
            m_context->player()->state()->secondarySubtitle(),
            position
        );
    }
}

void SubtitleListManager::handlePrimaryDelayChanged(double delay)
{
    Q_UNUSED(delay)
    selectPosition(
        m_context->subtitleLists()->primary(),
        m_context->player()->state()->subtitle(),
        m_context->player()->state()->timePosition()
    );
}

void SubtitleListManager::handleSecondaryDelayChanged(double delay)
{
    Q_UNUSED(delay)
    SubtitleListModel *secondary = m_context->subtitleLists()->secondary();
    if (secondary == m_context->subtitleLists()->primary())
    {
        return;
    }

    selectPosition(
        secondary,
        m_context->player()->state()->secondarySubtitle(),
        m_context->player()->state()->timePosition()
    );
}

void SubtitleListManager::handlePrimarySubtitleChanged()
{
    addSubtitle(
        m_context->subtitleLists()->primary(),
        m_context->player()->state()->subtitle(),
        m_context->player()->state()->timePosition()
    );
}

void SubtitleListManager::handleSecondarySubtitleChanged()
{
    addSubtitle(
        m_context->subtitleLists()->secondary(),
        m_context->player()->state()->secondarySubtitle(),
        m_context->player()->state()->timePosition()
    );
}

void SubtitleListManager::addSubtitle(
    SubtitleListModel *model, MpvSubtitle *subtitle, double position)
{
    if (model == nullptr)
    {
        return;
    }

    if (!subtitle->text().isEmpty())
    {
        model->addSubtitle(
            subtitle->text(), subtitle->startTime(), subtitle->endTime());
    }
    selectPosition(model, subtitle, position);
}

void SubtitleListManager::selectPosition(
    SubtitleListModel *model,
    const MpvSubtitle *subtitle,
    double position)
{
    if (model == nullptr || subtitle == nullptr)
    {
        return;
    }

    model->selectPosition(position - subtitle->delay());
}

QCoro::Task<void> SubtitleListManager::readExternalSubtitles(
    QPointer<SubtitleListModel> model, QString path)
{
    // The parse can outlive both this manager and the model during shutdown or
    // a fast track change. Keep every worker-owned object in the worker and use
    // guarded pointers before resuming on the application thread.
    const QPointer<SubtitleListManager> manager(this);
    const QPointer<Context> contextGuard(m_context);
    if (model) model->setLoading(true);
    if (!model || !manager || !contextGuard) co_return;
    QFuture<std::vector<SubtitleEntry>> future = QtConcurrent::run(
        [path = std::move(path)] {
            return SubtitleParser().parseSubtitles(path);
        }
    );

    std::vector<SubtitleEntry> items = co_await qCoro(future).takeResult();
    if (model) model->setLoading(false);
    if (items.empty() || model == nullptr || manager == nullptr ||
        contextGuard == nullptr)
    {
        co_return;
    }
    model->setItems(std::move(items));

    // setItems() emits model signals and can trigger re-entrant teardown.
    if (manager == nullptr || model == nullptr || contextGuard == nullptr)
    {
        co_return;
    }
    Context *context = contextGuard.data();
    if (context->player() == nullptr)
    {
        co_return;
    }
    SubtitleListModel *primary = context->subtitleLists()->primary();
    SubtitleListModel *secondary = context->subtitleLists()->secondary();
    if (model == primary)
    {
        manager->selectPosition(
            model,
            context->player()->state()->subtitle(),
            context->player()->state()->timePosition()
        );
    }
    else if (model == secondary)
    {
        manager->selectPosition(
            model,
            context->player()->state()->secondarySubtitle(),
            context->player()->state()->timePosition()
        );
    }
}
