#include "state/localsubtitleclient.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QUrl>
#include <limits>

#include "jimaku/jimakuclient.h"
#include "jimaku/subtitlerelease.h"
#include "player/mpvcontroller.h"
#include "player/mpvstate.h"
#include "player/mpvtrack.h"
#include "state/context.h"

namespace
{
bool supportedLocalSubtitle(const QString &name)
{
    // Keep the existing local Load Subtitle File formats, including bitmap tracks.
    static const QSet<QString> extensions{
        "ass", "idx", "lrc", "mks", "pgs", "rt", "scc", "smi", "srt",
        "ssa", "sub", "sup", "utf-8", "utf", "utf8", "vtt"
    };
    return extensions.contains(QFileInfo(name).suffix().toLower());
}
}

LocalSubtitleClient::LocalSubtitleClient(Context *context, QObject *parent)
    : QObject(parent), m_context(context)
{
    m_attachTimer.setSingleShot(true);
    m_attachTimer.setInterval(10000);
    connect(&m_attachTimer, &QTimer::timeout, this, [this] {
        error(tr("The local subtitle could not be loaded. Check that the file is valid."));
    });
}

QString LocalSubtitleClient::readableSubtitlePath(const QString &path)
{
    const QUrl url(path);
    const QString local = url.isLocalFile() ? url.toLocalFile() : path;
    const QFileInfo file(local);
    if (!file.isAbsolute() || !file.isFile() || !file.isReadable() ||
        file.size() <= 0 || !supportedLocalSubtitle(file.fileName())) return {};
    return file.canonicalFilePath();
}

QString LocalSubtitleClient::candidateForEpisode(const QString &directory,
    int episode, const QVariantMap &rule, bool *ambiguous)
{
    if (ambiguous) *ambiguous = false;
    if (!QFileInfo(directory).isAbsolute() || !QFileInfo(directory).isDir() || episode < 0)
        return {};
    QDirIterator files(directory, QDir::Files | QDir::Readable);
    QString candidate;
    int count = 0;
    while (files.hasNext())
    {
        files.next();
        if (++count > 10000) { if (ambiguous) *ambiguous = true; return {}; }
        const QFileInfo file = files.fileInfo();
        if (!supportedLocalSubtitle(file.fileName()) ||
            !SubtitleRelease::matches(file.fileName(), episode, rule)) continue;
        const QString path = readableSubtitlePath(file.absoluteFilePath());
        if (path.isEmpty()) continue;
        if (!candidate.isEmpty()) { if (ambiguous) *ambiguous = true; return {}; }
        candidate = path;
    }
    return candidate;
}

void LocalSubtitleClient::loadFile(const QString &path)
{
    cancel();
    if (!m_context || !m_context->player() || !m_context->player()->controller()) return;
    m_context->jimakuClient()->cancel();
    m_context->kitsunekkoClient()->cancel();
    m_mediaPath = m_context->player()->state()->path();
    if (m_mediaPath.isEmpty()) { error(tr("Open an episode before selecting subtitles.")); return; }
    const QString selected = readableSubtitlePath(path);
    if (selected.isEmpty()) { error(tr("The selected local subtitle is unavailable or unsupported.")); return; }
    const QFileInfo file(selected);
    const int videoEpisode = m_context->jimakuClient()->suggestedEpisode();
    const int subtitleEpisode = JimakuClient::episodeNumber(file.fileName());
    m_episode = subtitleEpisode >= 0 ? subtitleEpisode : videoEpisode;
    auto *library = m_context->episodeLibrary();
    const auto previousLink = library->subtitleLinkForFile(m_mediaPath);
    const QString title = library->playbackInfo(m_mediaPath).value("title").toString();
    m_link = {{"provider", "local"}, {"directory", file.absolutePath()},
        {"name", title.isEmpty() ? QDir(file.absolutePath()).dirName() : title},
        {"preferredFile", file.fileName()},
        {"releaseRule", SubtitleRelease::fromFile(file.fileName(), m_episode)},
        {"autoLoad", previousLink.value("autoLoad", true).toBool()}};
    if (videoEpisode >= 0 && subtitleEpisode >= 0)
        m_link.insert("episodeOffset", subtitleEpisode - videoEpisode);
    attach(selected, false);
}

void LocalSubtitleClient::fetchForCurrentMedia()
{
    cancel();
    if (!m_context || !m_context->player() || !m_context->player()->controller()) return;
    m_context->jimakuClient()->cancel();
    m_context->kitsunekkoClient()->cancel();
    m_mediaPath = m_context->player()->state()->path();
    if (m_mediaPath.isEmpty()) return;
    auto *library = m_context->episodeLibrary();
    const QVariantMap seriesLink = library->subtitleLinkForFile(m_mediaPath);
    if (!seriesLink.value("autoLoad", true).toBool()) return;
    m_savedSelection = library->subtitleSelectionForFile(m_mediaPath);
    // An explicit episode choice retains its own provider when series preferences change.
    m_link = m_savedSelection.isEmpty() ? seriesLink : m_savedSelection;
    if (m_link.value("provider").toString() != "local") return;
    if (!m_savedSelection.isEmpty())
    {
        const QString saved = readableSubtitlePath(m_savedSelection.value("cachePath").toString());
        if (saved.isEmpty() || QFileInfo(saved).fileName() != m_savedSelection.value("filename").toString())
        {
            error(tr("This episode's saved local subtitle is missing. Choose a replacement using Subtitle → Add Subtitle…"));
            return;
        }
        m_episode = m_savedSelection.value("episode", -1).toInt();
        attach(saved, true);
        return;
    }
    const int videoEpisode = m_context->jimakuClient()->suggestedEpisode();
    if (videoEpisode < 0)
    {
        error(tr("Could not determine this episode number. Choose its subtitle file manually."));
        return;
    }
    const qint64 episode = qint64(videoEpisode) + m_link.value("episodeOffset", 0).toInt();
    if (episode < 0 || episode > std::numeric_limits<int>::max())
    {
        error(tr("The saved episode offset is invalid. Choose this episode's subtitle file manually."));
        return;
    }
    m_episode = static_cast<int>(episode);
    bool ambiguous = false;
    const QString candidate = candidateForEpisode(m_link.value("directory").toString(),
        m_episode, m_link.value("releaseRule").toMap(), &ambiguous);
    if (candidate.isEmpty())
    {
        error(ambiguous ? tr("More than one local subtitle matches your selected release. Choose the file manually.") :
            tr("Your selected subtitle release is not available for episode %1 in the saved folder.").arg(m_episode));
        return;
    }
    attach(candidate, true);
}

void LocalSubtitleClient::attach(const QString &path, bool automatic)
{
    if (!m_context || !m_context->player() || m_context->player()->state()->path() != m_mediaPath)
    {
        error(tr("The episode changed before its subtitle was attached."));
        return;
    }
    m_automatic = automatic;
    m_pendingPath = path;
    m_pendingGeneration = m_generation;
    m_busy = true;
    m_status = tr("Loading local subtitle…");
    const auto generation = m_generation;
    auto *state = m_context->player()->state();
    m_trackConnection = connect(state, &MpvState::subtitleTracksChanged, this,
        [this, generation] { if (generation == m_generation) checkAttached(); });
    m_sidConnection = connect(state, &MpvState::sidChanged, this,
        [this, generation] { if (generation == m_generation) checkAttached(); });
    m_mediaConnection = connect(state, &MpvState::pathChanged, this,
        [this, generation](const QString &path) {
            if (generation == m_generation && path != m_mediaPath)
                error(tr("The episode changed before its subtitle was attached."));
        });
    m_attachTimer.start();
    emit changed();
    if (!m_context->player()->controller()->loadSubtitle(path))
        error(tr("Memento could not load the selected local subtitle."));
    else checkAttached();
}

void LocalSubtitleClient::checkAttached()
{
    if (m_pendingPath.isEmpty() || m_pendingGeneration != m_generation ||
        !m_context || !m_context->player()) return;
    auto *state = m_context->player()->state();
    if (state->path() != m_mediaPath)
    {
        error(tr("The episode changed before its subtitle was attached."));
        return;
    }
    bool selected = false;
    for (const auto *track : state->subtitleTracks())
    {
        if (!track || !track->external() || track->id() != state->sid()) continue;
        const QString actual = readableSubtitlePath(track->externalFilename());
#if defined(Q_OS_WIN)
        if (actual.compare(m_pendingPath, Qt::CaseInsensitive) != 0) continue;
#else
        if (actual != m_pendingPath) continue;
#endif
        selected = true;
        break;
    }
    if (!selected) return;
    auto *library = m_context->episodeLibrary();
    const QString filename = QFileInfo(m_pendingPath).fileName();
    const QString title = m_link.value("name").toString();
    const double delay = m_automatic ? m_savedSelection.value("delay", 0.0).toDouble() :
        state->subtitle()->delay();
    QVariantMap selection = m_link;
    selection.insert("filename", filename);
    selection.insert("cachePath", m_pendingPath);
    selection.insert("episode", m_episode);
    selection.insert("delay", delay);
    // Reopening an older explicit choice must not replace today's series preference.
    const auto seriesLink = m_automatic ? library->subtitleLinkForFile(m_mediaPath) : m_link;
    if (m_automatic) m_context->player()->controller()->setSubtitleDelay(delay);
    const bool saved = !library->containsFile(m_mediaPath) ||
        library->rememberSubtitleSelection(m_mediaPath, seriesLink, selection);
    cancel();
    m_status = saved ? tr("Local subtitle loaded: %1").arg(filename) :
        tr("The subtitle loaded, but its choice could not be saved. Check library storage permissions.");
    emit changed();
    if (!saved) emit failed(m_status);
    emit subtitleAttached(filename, title);
}

void LocalSubtitleClient::cancel()
{
    ++m_generation;
    m_attachTimer.stop();
    disconnect(m_trackConnection);
    disconnect(m_sidConnection);
    disconnect(m_mediaConnection);
    m_trackConnection = {};
    m_sidConnection = {};
    m_mediaConnection = {};
    m_pendingPath.clear();
    m_savedSelection.clear();
    m_link.clear();
    m_mediaPath.clear();
    const bool busy = m_busy;
    m_busy = false;
    if (busy) emit changed();
}

void LocalSubtitleClient::error(const QString &message)
{
    cancel();
    m_status = message;
    emit changed();
    emit failed(message);
}
