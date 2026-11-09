#include "state/videoexporter.h"

#include <algorithm>
#include <cmath>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>
#include <QUuid>
#include <QtConcurrentRun>
#include "state/context.h"
#include "util/directoryutils.h"

VideoExporter::VideoExporter(Context *context, QObject *parent)
    : QObject(parent), m_context(context)
{
    m_timer.setInterval(200);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_progress = m_control->progress.load();
        emit changed();
    });
    connect(&m_watcher, &QFutureWatcher<QVariantMap>::finished, this, [this] {
        m_timer.stop();
        const auto result = m_watcher.result();
        const bool ok = result.value("ok").toBool();
        m_busy = false;
        m_progress = ok ? 1.0 : 0.0;
        m_outputPath = result.value("path").toString();
        m_status = ok ? tr("Saved video to %1").arg(m_outputPath) : result.value("error").toString();
        emit changed();
        emit finished(ok, m_status);
    });
}

VideoExporter::~VideoExporter()
{
    if (m_control) m_control->cancel.store(true);
    m_watcher.waitForFinished();
}

QVariantMap VideoExporter::plan(int width, int height, double duration,
    double start, double end, int maxHeight, int quality, bool audio)
{
    const auto invalid = [](const QString &error) -> QVariantMap {
        return {{"valid", false}, {"error", error}};
    };
    if (width < 2 || height < 2 || !std::isfinite(duration) || duration <= 0)
        return invalid(tr("Open a video with a known duration first."));
    if (!std::isfinite(start) || !std::isfinite(end) || start < 0 || end <= start || end > duration + 0.001)
        return invalid(tr("Choose a start and end within the video, with the end after the start."));
    if (end - start < 0.1)
        return invalid(tr("Choose a section at least 0.1 seconds long."));
    if ((maxHeight != 0 && maxHeight != 480 && maxHeight != 720 && maxHeight != 1080) || quality < 0 || quality > 2)
        return invalid(tr("Choose a valid resolution and quality."));
    const double scale = maxHeight > 0 ? std::min(1.0, double(maxHeight) / height) : 1.0;
    const int outWidth = std::max(2, int(width * scale) / 2 * 2);
    const int outHeight = std::max(2, int(height * scale) / 2 * 2);
    const double factors[] = {0.5, 1.0, 1.8};
    const int kbps = std::clamp(int(double(outWidth) * outHeight * 2.7 * factors[quality] / 1000), 300, 60000);
    const double sizeMB = (kbps + (audio ? 128 : 0)) * (end - start) / 8000 * 1.02;
    return {{"valid", true}, {"width", outWidth}, {"height", outHeight},
        {"videoKbps", kbps}, {"sizeMB", sizeMB}, {"seconds", end - start}};
}

QVariantMap VideoExporter::estimate(double start, double end, int maxHeight, int quality, bool audio) const
{
    const auto *player = m_context->player();
    if (!player) return {{"valid", false}, {"error", tr("Open a video first.")}};
    const auto *state = player->state();
    return plan(state->videoWidth(), state->videoHeight(), state->duration(),
        start, end, maxHeight, quality, audio && state->aid() > 0);
}

QString VideoExporter::destination() const
{
    const auto *player = m_context->player();
    if (!player) return {};
    const auto info = m_context->episodeLibrary()->playbackInfo(player->state()->path());
    return DirectoryUtils::mediaCaptureDirectory(info.value("title").toString());
}

bool VideoExporter::start(const QString &mediaPath, double start, double end,
    int maxHeight, int quality, int primaryId, int secondaryId, bool audio)
{
    if (m_busy) return false;
    const auto reject = [this](const QString &message) {
        m_status = message;
        emit changed();
        emit finished(false, message);
        return false;
    };
    auto *player = m_context->player();
    if (!player || mediaPath.isEmpty() || player->state()->path() != mediaPath)
        return reject(tr("The playing video changed. Reopen Export Video to select a section."));
    const auto parameters = estimate(start, end, maxHeight, quality, audio);
    if (!parameters.value("valid").toBool()) return reject(parameters.value("error").toString());
    if (primaryId > 0 && primaryId == secondaryId)
        return reject(tr("Choose different primary and secondary subtitle tracks."));
    auto context = player->controller()->encodingContext();
    const auto trackSnapshot = [](const MpvTrack *track, double delay) {
        return MpvEncodingContext::SubtitleTrack{track->id(), track->sourceId(),
            track->externalFilename(), track->codec(), delay, track->external()};
    };
    const auto subtitle = [&](int id, double delay) {
        for (const auto *track : player->state()->subtitleTracks())
            if (track && track->id() == id) return trackSnapshot(track, delay);
        return MpvEncodingContext::SubtitleTrack{};
    };
    context.primarySubtitle = subtitle(primaryId, player->state()->subtitle()->delay());
    context.secondarySubtitle = subtitle(secondaryId, player->state()->secondarySubtitle()->delay());
    if ((primaryId > 0 && !context.primarySubtitle.valid()) ||
        (secondaryId > 0 && !context.secondarySubtitle.valid()))
        return reject(tr("A selected subtitle track is no longer available."));
    const QString folder = destination();
    if (folder.isEmpty() || !QDir().mkpath(folder))
        return reject(tr("Could not create the export folder in Pictures."));
    const auto info = m_context->episodeLibrary()->playbackInfo(mediaPath);
    const QString episode = info.value("filename").toString();
    const QString stem = DirectoryUtils::safeMediaName(QFileInfo(episode.isEmpty() ? mediaPath : episode).completeBaseName(), "Video");
    const QString output = QDir(folder).filePath(stem + QString("_%1-%2s_%3_%4.mp4")
        .arg(start, 0, 'f', 3).arg(end, 0, 'f', 3)
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"))
        .arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)));
    MpvVideoExportOptions options;
    options.outputPath = output + ".partial.mp4";
    QFile reserve(options.outputPath);
    if (!reserve.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return reject(tr("Could not create the exported video: %1").arg(reserve.errorString()));
    reserve.close();
    options.width = parameters.value("width").toInt();
    options.height = parameters.value("height").toInt();
    options.videoKbps = parameters.value("videoKbps").toInt();
    options.start = start;
    options.end = end;
    if (audio)
        for (const auto *track : player->state()->audioTracks())
            if (track && track->id() == context.aid && track->external())
                options.externalAudio = trackSnapshot(track, 0.0);
    options.control = m_control = std::make_shared<MpvEncodingControl>();
    MpvVideoClipArgs args;
    args.start = start;
    args.end = end;
    args.audio = audio && context.aid > 0;
    args.subtitles = primaryId > 0 || secondaryId > 0;
    m_status = tr("Exporting selected section…");
    m_outputPath.clear();
    m_progress = 0.0;
    m_busy = true;
    emit changed();
    m_timer.start();
    m_watcher.setFuture(QtConcurrent::run([context, args, options, output]() -> QVariantMap {
        const QString encoded = MpvController::encodeVideoClip(context, args, {}, false, false, &options);
        if (encoded.isEmpty() || options.control->cancel.load())
        {
            QFile::remove(options.outputPath);
            return {{"ok", false}, {"error", options.control->cancel.load() ?
                tr("Export cancelled.") : (options.control->error.isEmpty() ?
                    tr("Could not export the selected section.") : options.control->error)}};
        }
        if (!QFile::rename(encoded, output))
        {
            QFile::remove(encoded);
            return {{"ok", false}, {"error", tr("Could not save the completed video.")}};
        }
        return {{"ok", true}, {"path", output}};
    }));
    return true;
}

void VideoExporter::cancel()
{
    if (m_busy && m_control)
    {
        m_control->cancel.store(true);
        m_status = tr("Cancelling export…");
        emit changed();
    }
}

void VideoExporter::openFolder() const
{
    const QString folder = m_outputPath.isEmpty() ? destination() : QFileInfo(m_outputPath).absolutePath();
    if (!folder.isEmpty()) QDesktopServices::openUrl(QUrl::fromLocalFile(folder));
}
