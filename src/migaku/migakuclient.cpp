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

#include "migaku/migakuclient.h"

#include <algorithm>
#include <cmath>

#include <QFile>
#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QPointer>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSettings>
#include <QtConcurrent>

#ifdef MEMENTO_SYSTEM_QCORO
#include <QCoroFuture>
#else
#include <qcoro/core/qcorofuture.h>
#endif // MEMENTO_SYSTEM_QCORO

#include "player/mpvcontroller.h"
#include "player/mpvplayer.h"
#include "player/mpvstate.h"
#include "player/mpvsubtitle.h"
#include "state/context.h"

namespace
{

constexpr double AUDIO_PADDING_SECONDS = 0.25;
constexpr double MAX_AUDIO_CLIP_SECONDS = 60.0;
constexpr double MEDIA_POSITION_TOLERANCE_SECONDS = 0.5;
constexpr qint64 MAX_IMAGE_SIZE = 25 * 1024 * 1024;
constexpr qint64 MAX_AUDIO_SIZE = 32 * 1024 * 1024;

constexpr const char *KEY_SUCCESS = "success";
constexpr const char *KEY_ERROR = "error";

} // namespace

struct MigakuClient::CardData
{
    QString title;
    double timestamp{0};
    double start{0};
    double end{0};
};

struct MigakuClient::MediaData
{
    QString imagePath;
    QString audioPath;
    QString error;
};

MigakuClient::MigakuClient(Context *context, QObject *parent) :
    QObject(parent), m_context(context)
{
    m_enabled = QSettings().value("migaku/enabled", false).toBool();
    m_exportFolder = QSettings().value("migaku/exportFolder").toUrl();
}

bool MigakuClient::busy() const noexcept
{
    return m_busy;
}

void MigakuClient::setEnabled(bool enabled)
{
    if (m_enabled == enabled) return;
    ++m_generation;
    m_enabled = enabled;
    QSettings().setValue("migaku/enabled", enabled);
    emit enabledChanged();
}

void MigakuClient::setExportFolder(const QUrl &folder)
{
    if (folder == m_exportFolder) return;
    if (!folder.isLocalFile() || !QFileInfo(folder.toLocalFile()).isDir())
    {
        emit exportFailed(tr("Choose an existing local folder for Migaku exports."));
        return;
    }
    ++m_generation;
    m_exportFolder = folder;
    QSettings().setValue("migaku/exportFolder", folder);
    m_lastExport.clear();
    emit exportFolderChanged();
    emit lastExportChanged();
}

bool MigakuClient::openExportFolder()
{
    return m_exportFolder.isLocalFile() && QDesktopServices::openUrl(m_exportFolder);
}

QCoro::QmlTask MigakuClient::exportCurrentSubtitle()
{
    return exportCurrentSubtitleAsync();
}

QCoro::Task<QVariantMap> MigakuClient::exportCurrentSubtitleAsync()
{
    if (!m_enabled || !m_exportFolder.isLocalFile())
    {
        const QString error = tr("Enable Migaku integration and choose an export folder in Settings first.");
        emit exportFailed(error);
        co_return errorResult(error);
    }
    const QString destination = m_exportFolder.toLocalFile();
    if (m_busy)
    {
        co_return errorResult(tr("A Migaku export is already being prepared."));
    }
    if (m_context == nullptr ||
        m_context->player() == nullptr)
    {
        const QString error = tr("Open a video before exporting sentence media.");
        emit exportFailed(error);
        co_return errorResult(error);
    }

    // Capture player state before the encoder suspends this coroutine.
    CardData card;
    MpvState *state = m_context->player()->state();
    const MpvSubtitle *subtitle = state->subtitle();
    const SubtitleListModel *primaryList =
        m_context->subtitleLists()->primary();
    const bool useNativePrimary =
        primaryList != nullptr && primaryList->fullTimelineReady();
    const QString primaryText = useNativePrimary ?
        primaryList->activeText() : subtitle->text();
    const QString mediaPath = state->path();
    const int64_t audioTrackId = state->aid();

    QString error;
    if (!state->pause() || state->path().isEmpty() || primaryText.trimmed().isEmpty())
    {
        error = tr("Pause on a subtitle before exporting sentence media.");
        emit exportFailed(error);
        co_return errorResult(error);
    }
    if (audioTrackId <= 0)
    {
        error = tr("Select an audio track before exporting sentence media.");
        emit exportFailed(error);
        co_return errorResult(error);
    }

    card.title = state->title().trimmed();
    if (card.title.isEmpty()) card.title = QFileInfo(mediaPath).fileName();
    card.timestamp = state->timePosition();
    const double requestedStart = std::max(
        0.0,
        (useNativePrimary ? primaryList->activeStart() :
            subtitle->startTime()) + subtitle->delay() - AUDIO_PADDING_SECONDS
    );
    const double requestedEnd = std::max(
        requestedStart,
        (useNativePrimary ? primaryList->activeEnd() :
            subtitle->endTime()) + subtitle->delay() + AUDIO_PADDING_SECONDS
    );

    if (!std::isfinite(card.timestamp) ||
        !prepareClipRange(
            requestedStart,
            requestedEnd,
            state->duration(),
            &card.start,
            &card.end))
    {
        error = tr(
            "The subtitle timing is invalid or exceeds "
            "the 60-second sentence-audio limit."
        );
        emit exportFailed(error);
        co_return errorResult(error);
    }

    const auto generation = m_generation;
    QPointer<MigakuClient> self(this);
    QPointer<MpvController> controller(m_context->player()->controller());
    QPointer<MpvState> stateSnapshot(state);
    const auto playbackMatchesSnapshot = [
        stateSnapshot,
        mediaPath,
        audioTrackId,
        position = card.timestamp
    ] {
        return stateSnapshot != nullptr &&
            stateSnapshot->path() == mediaPath &&
            stateSnapshot->aid() == audioTrackId &&
            std::abs(stateSnapshot->timePosition() - position) <=
                MEDIA_POSITION_TOLERANCE_SECONDS;
    };

    setBusy(true);
    auto busyGuard = qScopeGuard([self] {
        if (self != nullptr)
        {
            self->setBusy(false);
        }
    });

    MediaData media;
    auto mediaGuard = qScopeGuard([&media] {
        if (!media.imagePath.isEmpty())
        {
            QFile::remove(media.imagePath);
        }
        if (!media.audioPath.isEmpty())
        {
            QFile::remove(media.audioPath);
        }
    });
    if (controller == nullptr)
    {
        media.error = tr("The player closed while preparing the Migaku export.");
    }
    else
    {
        // The screenshot and job snapshot touch the live player and therefore
        // stay on its owning thread. Only the self-contained encoder task runs
        // in the worker pool.
        media.imagePath = controller->tempScreenshot(false, ".jpg");
        MpvAudioClipArgs args;
        args.start = card.start;
        args.end = card.end;
        args.extension = ".mp3";
        std::function<QString()> audioTask =
            controller->makeTempAudioClipTask(args);

        if (media.imagePath.isEmpty() ||
            !QFileInfo::exists(media.imagePath))
        {
            media.error = tr("Memento could not capture the video screenshot.");
        }
        else if (!audioTask)
        {
            media.error = tr("Memento could not prepare the sentence audio.");
        }
        else
        {
            media.audioPath = co_await QtConcurrent::run(std::move(audioTask));
        }
    }

    if (self == nullptr)
    {
        co_return errorResult(QStringLiteral("Migaku request was cancelled."));
    }
    if (!playbackMatchesSnapshot())
    {
        error = tr(
            "Playback changed while preparing the Migaku export. Pause on the "
            "intended subtitle and try again."
        );
        emit self->exportFailed(error);
        co_return errorResult(error);
    }
    if (!media.error.isEmpty())
    {
        emit self->exportFailed(media.error);
        co_return errorResult(media.error);
    }
    if (media.audioPath.isEmpty() || !QFileInfo::exists(media.audioPath))
    {
        error = tr("Memento could not create the sentence audio.");
        emit self->exportFailed(error);
        co_return errorResult(error);
    }
    if (!fileWithinLimit(media.imagePath, MAX_IMAGE_SIZE) ||
        !fileWithinLimit(media.audioPath, MAX_AUDIO_SIZE))
    {
        error = tr("The generated Migaku media is unexpectedly large.");
        emit self->exportFailed(error);
        co_return errorResult(error);
    }

    if (!m_enabled || generation != m_generation)
    {
        error = tr("Migaku export settings changed during capture. Try again.");
        emit self->exportFailed(error);
        co_return errorResult(error);
    }
    const auto result = saveMediaPair(destination, card.title, card.timestamp,
                                      media.imagePath, media.audioPath);
    if (!result.value(KEY_SUCCESS).toBool())
    {
        emit self->exportFailed(result.value(KEY_ERROR).toString());
        co_return result;
    }
    m_lastExport = result.value("image").toString() + "\n" + result.value("audio").toString();
    emit lastExportChanged();
    emit filesExported(result.value("baseName").toString());
    co_return result;
}

void MigakuClient::setBusy(bool value)
{
    if (m_busy == value)
    {
        return;
    }
    m_busy = value;
    emit busyChanged(m_busy);
}

QString MigakuClient::episodeStem(QString title)
{
    title.remove(QRegularExpression("\\.(mkv|mp4|webm|avi|mov|m4v|ts|ogg)$",
                                    QRegularExpression::CaseInsensitiveOption));
    title.replace(QRegularExpression("[<>:\"/\\\\|?*\\x{0000}-\\x{001f}\\x{007f}]"), "_");
    title = title.simplified();
    while (title.endsWith('.') || title.endsWith(' ')) title.chop(1);
    while (title.toUtf8().size() > 160) {
        if (title.back().isLowSurrogate()) title.chop(2);
        else title.chop(1);
    }
    while (title.startsWith('.')) title.remove(0, 1);
    if (title.isEmpty()) title = QStringLiteral("Episode");
    static const QRegularExpression reserved("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])($|\\.)",
        QRegularExpression::CaseInsensitiveOption);
    if (reserved.match(title).hasMatch()) title.prepend("Episode - ");
    return title;
}

QVariantMap MigakuClient::saveMediaPair(const QString &folder, const QString &episode,
    double position, const QString &image, const QString &audio)
{
    if (!QFileInfo(folder).isDir())
        return errorResult(tr("The export folder is missing. Choose it again in Settings."));
    if (!std::isfinite(position) || position < 0 || position > 360000000 ||
        !fileWithinLimit(image, MAX_IMAGE_SIZE) || !fileWithinLimit(audio, MAX_AUDIO_SIZE))
        return errorResult(tr("The captured image, audio, or timestamp is invalid."));
    const qint64 ms = qRound64(position * 1000);
    const QString time = QStringLiteral("%1-%2-%3-%4")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg((ms / 60000) % 60, 2, 10, QLatin1Char('0'))
        .arg((ms / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
    const QString stem = episodeStem(episode) + " - " + time;
    const QDir directory(folder);
    for (int number = 1; number <= 10000; ++number)
    {
        const QString base = stem + (number == 1 ? QString{} : QStringLiteral(" - %1").arg(number));
        const QString imagePath = directory.filePath(base + ".jpg");
        const QString audioPath = directory.filePath(base + ".mp3");
        if (QFileInfo::exists(imagePath) || QFileInfo::exists(audioPath)) continue;
        // QFile::copy never replaces existing files, including concurrent exports.
        QFile imageFile(image), audioFile(audio);
        if (!imageFile.copy(imagePath))
        {
            if (QFileInfo::exists(imagePath)) continue;
            return errorResult(tr("Could not save the image in %1: %2").arg(folder, imageFile.errorString()));
        }
        if (!audioFile.copy(audioPath))
        {
            QFile::remove(imagePath); // Roll back only the file this capture created.
            if (QFileInfo::exists(audioPath)) continue;
            return errorResult(tr("Could not save the audio in %1: %2").arg(folder, audioFile.errorString()));
        }
        return {{KEY_SUCCESS, true}, {"image", imagePath}, {"audio", audioPath}, {"baseName", base}};
    }
    return errorResult(tr("Too many captures with the same name in the export folder."));
}

bool MigakuClient::prepareClipRange(
    double requestedStart,
    double requestedEnd,
    double mediaDuration,
    double *start,
    double *end) noexcept
{
    if (start == nullptr || end == nullptr ||
        !std::isfinite(requestedStart) || !std::isfinite(requestedEnd))
    {
        return false;
    }

    requestedStart = std::max(0.0, requestedStart);
    if (std::isfinite(mediaDuration) && mediaDuration > 0)
    {
        requestedStart = std::min(requestedStart, mediaDuration);
        requestedEnd = std::min(requestedEnd, mediaDuration);
    }
    if (requestedEnd <= requestedStart ||
        requestedEnd - requestedStart > MAX_AUDIO_CLIP_SECONDS)
    {
        return false;
    }

    *start = requestedStart;
    *end = requestedEnd;
    return true;
}

bool MigakuClient::fileWithinLimit(
    const QString &path, qint64 maximumSize)
{
    const QFileInfo info(path);
    return maximumSize > 0 && info.isFile() && info.size() > 0 &&
        info.size() <= maximumSize;
}

QVariantMap MigakuClient::errorResult(const QString &error)
{
    return {{KEY_SUCCESS, false}, {KEY_ERROR, error}};
}
