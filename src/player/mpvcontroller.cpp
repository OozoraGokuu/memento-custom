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

#include "player/mpvcontroller.h"

#include <algorithm>
#include <cinttypes>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>
#include <ranges>
#include <vector>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QStringList>
#include <QTemporaryFile>
#include <QUrl>

#include "player/mpvplayer.h"
#include "util/directoryutils.h"

namespace
{

struct LoadedSubtitleTrack
{
    int64_t id{-1};
    int64_t sourceId{-1};
    QString externalFilename;
    QString codec;
    bool external{false};
};

const mpv_node *nodeMapValue(const mpv_node &node, const char *key)
{
    if (node.format != MPV_FORMAT_NODE_MAP || node.u.list == nullptr)
    {
        return nullptr;
    }
    for (int index = 0; index < node.u.list->num; ++index)
    {
        if (std::strcmp(node.u.list->keys[index], key) == 0)
        {
            return &node.u.list->values[index];
        }
    }
    return nullptr;
}

QString nodeString(const mpv_node &node, const char *key)
{
    const mpv_node *value = nodeMapValue(node, key);
    return value != nullptr && value->format == MPV_FORMAT_STRING ?
        QString::fromUtf8(value->u.string) : QString();
}

int64_t nodeInteger(const mpv_node &node, const char *key)
{
    const mpv_node *value = nodeMapValue(node, key);
    return value != nullptr && value->format == MPV_FORMAT_INT64 ?
        value->u.int64 : -1;
}

bool nodeFlag(const mpv_node &node, const char *key)
{
    const mpv_node *value = nodeMapValue(node, key);
    return value != nullptr && value->format == MPV_FORMAT_FLAG &&
        value->u.flag != 0;
}

QString normalizedSubtitleLocation(QString location)
{
    if (location.isEmpty())
    {
        return {};
    }

    const QUrl url(location);
    if (url.isLocalFile())
    {
        location = url.toLocalFile();
    }
    else if (!url.scheme().isEmpty() && !QDir::isAbsolutePath(location))
    {
        return url.adjusted(QUrl::NormalizePathSegments)
            .toString(QUrl::FullyEncoded);
    }

    const QFileInfo info(location);
    QString result = info.canonicalFilePath();
    if (result.isEmpty())
    {
        result = info.absoluteFilePath();
    }
    result = QDir::cleanPath(result);
#ifdef Q_OS_WIN
    result = result.toCaseFolded();
#endif
    return result;
}

std::vector<LoadedSubtitleTrack> loadedSubtitleTracks(mpv_handle *handle)
{
    mpv_node node{};
    if (::mpv_get_property(handle, "track-list", MPV_FORMAT_NODE, &node) < 0)
    {
        return {};
    }
    const auto cleanup = qScopeGuard([&node] {
        ::mpv_free_node_contents(&node);
    });
    if (node.format != MPV_FORMAT_NODE_ARRAY || node.u.list == nullptr)
    {
        return {};
    }

    std::vector<LoadedSubtitleTrack> tracks;
    for (int index = 0; index < node.u.list->num; ++index)
    {
        const mpv_node &item = node.u.list->values[index];
        if (nodeString(item, "type") != QStringLiteral("sub"))
        {
            continue;
        }
        tracks.emplace_back(LoadedSubtitleTrack{
            .id = nodeInteger(item, "id"),
            .sourceId = nodeInteger(item, "src-id"),
            .externalFilename = nodeString(item, "external-filename"),
            .codec = nodeString(item, "codec"),
            .external = nodeFlag(item, "external"),
        });
    }
    return tracks;
}

std::optional<int64_t> resolveSubtitleTrack(
    const MpvEncodingContext::SubtitleTrack &expected,
    const std::vector<LoadedSubtitleTrack> &tracks)
{
    if (!expected.valid())
    {
        return std::nullopt;
    }

    const QString expectedLocation =
        normalizedSubtitleLocation(expected.externalFilename);
    std::optional<int64_t> match;
    for (const LoadedSubtitleTrack &track : tracks)
    {
        if (track.external != expected.external)
        {
            continue;
        }
        if (expected.external)
        {
            if (expectedLocation.isEmpty() ||
                normalizedSubtitleLocation(track.externalFilename) !=
                    expectedLocation)
            {
                continue;
            }
            if (expected.sourceId >= 0 && track.sourceId >= 0 &&
                track.sourceId != expected.sourceId)
            {
                continue;
            }
        }
        else if (track.id != expected.id)
        {
            continue;
        }

        if (!expected.codec.isEmpty() && !track.codec.isEmpty() &&
            expected.codec.compare(track.codec, Qt::CaseInsensitive) != 0)
        {
            continue;
        }
        if (match.has_value())
        {
            return std::nullopt;
        }
        match = track.id;
    }
    return match;
}

std::optional<int64_t> resolveGeneratedSubtitle(
    const QString &path,
    const std::vector<LoadedSubtitleTrack> &tracks)
{
    const QString expectedLocation = normalizedSubtitleLocation(path);
    if (expectedLocation.isEmpty())
    {
        return std::nullopt;
    }

    std::optional<int64_t> match;
    for (const LoadedSubtitleTrack &track : tracks)
    {
        if (!track.external ||
            normalizedSubtitleLocation(track.externalFilename) !=
                expectedLocation)
        {
            continue;
        }
        if (match.has_value())
        {
            return std::nullopt;
        }
        match = track.id;
    }
    return match;
}

bool setSubtitleSelection(
    mpv_handle *handle,
    const char *property,
    const std::optional<int64_t> &trackId)
{
    if (!trackId.has_value())
    {
        return ::mpv_set_property_string(handle, property, "no") >= 0;
    }
    int64_t id = *trackId;
    if (::mpv_set_property(handle, property, MPV_FORMAT_INT64, &id) < 0)
    {
        return false;
    }
    return true;
}

bool configureEncoderSubtitles(
    mpv_handle *handle,
    const MpvEncodingContext &context,
    const QString &generatedSubtitleFile,
    bool nativePrimary,
    bool nativeSecondary)
{
    const std::vector<LoadedSubtitleTrack> tracks =
        loadedSubtitleTracks(handle);
    const bool hasGenerated = !generatedSubtitleFile.isEmpty();
    const bool primaryFallback =
        !nativePrimary && context.primarySubtitle.valid();
    const bool secondaryFallback =
        !nativeSecondary && context.secondarySubtitle.valid();
    const bool generatedAsPrimary = hasGenerated &&
        (nativePrimary || !primaryFallback);
    const bool generatedAsSecondary = hasGenerated && !generatedAsPrimary;

    std::optional<int64_t> generatedId;
    if (hasGenerated)
    {
        generatedId = resolveGeneratedSubtitle(generatedSubtitleFile, tracks);
        if (!generatedId.has_value())
        {
            qWarning("Could not identify generated subtitle track");
            return false;
        }
    }

    std::optional<int64_t> primaryId;
    double primaryDelay = 0.0;
    if (generatedAsPrimary)
    {
        primaryId = generatedId;
    }
    else if (primaryFallback)
    {
        primaryId = resolveSubtitleTrack(context.primarySubtitle, tracks);
        primaryDelay = context.primarySubtitle.delay;
        if (!primaryId.has_value())
        {
            qWarning("Could not identify primary fallback subtitle track");
            return false;
        }
    }

    std::optional<int64_t> secondaryId;
    double secondaryDelay = 0.0;
    if (generatedAsSecondary)
    {
        secondaryId = generatedId;
    }
    else if (secondaryFallback)
    {
        secondaryId = resolveSubtitleTrack(context.secondarySubtitle, tracks);
        secondaryDelay = context.secondarySubtitle.delay;
        if (!secondaryId.has_value())
        {
            qWarning("Could not identify secondary fallback subtitle track");
            return false;
        }
    }

    if (primaryId.has_value() && secondaryId == primaryId)
    {
        qWarning("Cannot select the same subtitle track twice");
        return false;
    }
    if (!setSubtitleSelection(handle, "sid", primaryId) ||
        !setSubtitleSelection(handle, "secondary-sid", secondaryId))
    {
        return false;
    }

    double ignoredSecondaryDelay = 0.0;
    const int secondaryDelaySupport = ::mpv_get_property(
        handle,
        "secondary-sub-delay",
        MPV_FORMAT_DOUBLE,
        &ignoredSecondaryDelay
    );
    const bool independentSecondaryDelay = secondaryDelaySupport >= 0;
    if (!independentSecondaryDelay &&
        secondaryDelaySupport != MPV_ERROR_PROPERTY_NOT_FOUND)
    {
        return false;
    }
    if (!independentSecondaryDelay && secondaryId.has_value())
    {
        secondaryDelay = context.sharedSubtitleDelay;
    }
    if (!independentSecondaryDelay && generatedAsPrimary && secondaryFallback)
    {
        primaryDelay = context.sharedSubtitleDelay;
    }
    if (!independentSecondaryDelay && generatedAsSecondary && primaryFallback)
    {
        secondaryDelay = context.sharedSubtitleDelay;
    }

    if (!independentSecondaryDelay && primaryId.has_value() &&
        secondaryId.has_value() &&
        std::abs(primaryDelay - secondaryDelay) > 0.000001)
    {
        qWarning(
            "This mpv version cannot encode two subtitle slots with "
            "different delays"
        );
        return false;
    }
    double sharedDelay = primaryId.has_value() ? primaryDelay : secondaryDelay;
    if ((primaryId.has_value() || secondaryId.has_value()) &&
        ::mpv_set_property(
            handle, "sub-delay", MPV_FORMAT_DOUBLE, &sharedDelay) < 0)
    {
        return false;
    }
    if (independentSecondaryDelay && secondaryId.has_value() &&
        ::mpv_set_property(
            handle,
            "secondary-sub-delay",
            MPV_FORMAT_DOUBLE,
            &secondaryDelay
        ) < 0)
    {
        return false;
    }
    return true;
}

QStringList encoderSubtitleFiles(
    const MpvEncodingContext &context,
    const QString &generatedSubtitleFile,
    bool nativePrimary,
    bool nativeSecondary)
{
    QStringList files;
    const auto appendFallback = [&files] (
        const MpvEncodingContext::SubtitleTrack &track, bool native)
    {
        if (!native && track.valid() && track.external &&
            !track.externalFilename.isEmpty())
        {
            const QString normalized =
                normalizedSubtitleLocation(track.externalFilename);
            const bool alreadyPresent = std::ranges::any_of(
                files,
                [&normalized] (const QString &file) {
                    return normalizedSubtitleLocation(file) == normalized;
                }
            );
            if (!alreadyPresent)
            {
                files.emplaceBack(track.externalFilename);
            }
        }
    };
    appendFallback(context.primarySubtitle, nativePrimary);
    appendFallback(context.secondarySubtitle, nativeSecondary);
    if (!generatedSubtitleFile.isEmpty())
    {
        files.emplaceBack(generatedSubtitleFile);
    }
    return files;
}

bool setStringListOption(
    mpv_handle *handle, const char *name, const QStringList &values)
{
    std::vector<QByteArray> encoded;
    encoded.reserve(static_cast<std::size_t>(values.size()));
    for (const QString &value : values)
    {
        encoded.emplace_back(value.toUtf8());
    }

    std::vector<mpv_node> nodes(encoded.size());
    for (std::size_t index = 0; index < encoded.size(); ++index)
    {
        nodes[index].format = MPV_FORMAT_STRING;
        nodes[index].u.string = encoded[index].data();
    }
    mpv_node_list list{
        .num = static_cast<int>(nodes.size()),
        .values = nodes.data(),
        .keys = nullptr,
    };
    mpv_node root{};
    root.format = MPV_FORMAT_NODE_ARRAY;
    root.u.list = &list;
    return ::mpv_set_option(handle, name, MPV_FORMAT_NODE, &root) >= 0;
}

} // namespace

MpvController::MpvController(MpvPlayer *parent) : QObject(parent)
{
    setPlayer(parent);
    m_subtitleExtensions = {
        "ass",
        "idx",
        "lrc",
        "mks",
        "pgs",
        "rt",
        "scc",
        "smi",
        "srt",
        "ssa",
        "sub",
        "sup",
        "utf-8",
        "utf",
        "utf8",
        "vtt",
    };
}

/* Begin Public Functions */

MpvPlayer *MpvController::player() const noexcept
{
    return m_player;
}

void MpvController::setPlayer(MpvPlayer *value)
{
    if (m_player == value)
    {
        return;
    }
    m_player = value;
    setParent(m_player);
    emit playerChanged();
}

bool MpvController::loadFile(
    const QString &file,
    bool append,
    const QStringList &options)
{
    /* Since mpv client API 2.3 (mpv 0.38.0) "loadfile" places an extra argument
     * before the options, so we need to conditionally re-arrange the arguments
     * array */
    constexpr bool IS_API_23{MPV_CLIENT_API_VERSION >= MPV_MAKE_VERSION(2, 3)};

    if (file.isEmpty())
    {
        return false;
    }

    if (isSubtitleFile(file))
    {
        return loadSubtitle(file);
    }

    QByteArray path = file.toUtf8();
    QByteArray opts = options.join(',').toUtf8();
    char *argOpts = opts.isEmpty() ? nullptr : opts.data();

    const char *args[]{
        "loadfile",
        path,
        append ? "append-play" : "replace",
        IS_API_23 ? "0" : argOpts,
        IS_API_23 ? argOpts : nullptr,
        nullptr
    };

    int ret = ::mpv_command(handle(), args);
    if (ret < 0)
    {
        qWarning(
            "Could not open file '%s': %s",
            qUtf8Printable(file),
            ::mpv_error_string(ret)
        );
        return false;
    }
    return true;
}

bool MpvController::loadArgs(const QStringList &args)
{
    if (args.size() <= 1)
    {
        return false;
    }

    if (!argsValid(args))
    {
        qInfo() << tr("Invalid command line arguments.");
        return false;
    }
    LoadFileNode parent{};
    if (buildArgsTree(args, 1, parent) == -1)
    {
        qInfo() << tr("Maximum number of nested per-file arguments exceeded.");
        return false;
    }
    QStringList emptyOpts;
    loadFilesFromTree(parent, emptyOpts);
    return true;
}

void MpvController::loadFile(const QStringList &files, bool append)
{
    if (files.isEmpty())
    {
        return;
    }

    for (const QString &file : files)
    {
        if (isSubtitleFile(file))
        {
            loadSubtitle(file);
        }
        else
        {
            if (!append)
            {
                stop();
            }
            loadFile(file, append);
            append = true;
        }
    }
}

bool MpvController::loadSubtitle(const QString &file)
{
    if (file.isEmpty())
    {
        return false;
    }

    QByteArray path = file.toUtf8();
    const char *args[]{
        "sub-add",
        path,
        nullptr
    };
    int ret = ::mpv_command_async(handle(), 0, args);
    if (ret < 0)
    {
        qWarning(
            "Could not add subtitle file '%s': %s",
            qUtf8Printable(file),
            ::mpv_error_string(ret)
        );
        return false;
    }
    return true;
}

void MpvController::seek(double time)
{
    QByteArray timestr = QString::number(time).toUtf8();
    const char *args[]{
        "seek",
        timestr,
        "absolute",
        NULL
    };
    if (::mpv_command_async(handle(), 0, args) < 0)
    {
        qWarning("Seeking failed");
    }
}

void MpvController::play()
{
    int flag = 0;
    if (::mpv_set_property(handle(), "pause", MPV_FORMAT_FLAG, &flag) < 0)
    {
        qWarning("Could not set mpv pause property to %d", flag);
    }
}

void MpvController::pause()
{
    int flag = 1;
    if (::mpv_set_property(handle(), "pause", MPV_FORMAT_FLAG, &flag) < 0)
    {
        qWarning("Could not set mpv pause property to %d", flag);
    }
}

void MpvController::stop()
{
    const char *args[]{
        "stop",
        nullptr
    };
    if (::mpv_command(handle(), args) < 0)
    {
        qWarning("Could not stop mpv");
    }
}

void MpvController::subtitleSeek(int count, bool primary)
{
    QByteArray countStr = QString::number(count).toUtf8();
    const char *args[]{
        "sub-seek",
        countStr,
        primary ? "primary" : "secondary",
        nullptr
    };
    if (::mpv_command_async(handle(), 0, args) < 0)
    {
        qWarning(
            "Could not seek %d subtitles in the %s track",
            count,
            primary ? "primary" : "secondary"
        );
    }
}

void MpvController::playlistNext()
{
    const char *args[]{
        "playlist-next",
        nullptr
    };
    if (::mpv_command_async(handle(), 0, args) < 0)
    {
        qWarning("Could not skip to the next file in the playlist");
    }
}

void MpvController::playlistPrev()
{
    const char *args[]{
        "playlist-prev",
        nullptr
    };
    if (::mpv_command_async(handle(), 0, args) < 0)
    {
        qWarning("Could not skip to the next file in the playlist");
    }
}

void MpvController::setAid(int64_t id)
{
    if (::mpv_set_property_async(handle(), 0, "aid", MPV_FORMAT_INT64, &id) < 0)
    {
        qWarning("Could not set audio track to %" PRId64, id);
    }
    else emit audioTrackSelected(id);
}

void MpvController::setSid(int64_t id)
{
    if (::mpv_set_property_async(handle(), 0, "sid", MPV_FORMAT_INT64, &id) < 0)
    {
        qWarning("Could not set subtitle track to %" PRId64, id);
    }
}

void MpvController::setSecondarySid(int64_t id)
{
    if (::mpv_set_property_async(handle(), 0, "secondary-sid", MPV_FORMAT_INT64, &id) < 0)
    {
        qWarning("Could not set secondary subtitle track to %" PRId64, id);
    }
}

void MpvController::setVid(int64_t id)
{
    if (::mpv_set_property_async(handle(), 0, "vid", MPV_FORMAT_INT64, &id) < 0)
    {
        qWarning("Could not set video track to %" PRId64, id);
    }
}

void MpvController::setSubtitleVisibility(bool visible)
{
    int flag = visible ? 1 : 0;
    if (::mpv_set_property_async(handle(), 0, "sub-visibility", MPV_FORMAT_FLAG, &flag) < 0)
    {
        qWarning("Could not set subtitle visibility");
    }
}

void MpvController::setSecondarySubtitleVisibility(bool visible)
{
    int flag = visible ? 1 : 0;
    if (::mpv_set_property_async(handle(), 0, "secondary-sub-visibility", MPV_FORMAT_FLAG, &flag) < 0)
    {
        qWarning("Could not set secondary subtitle visibility");
    }
}

void MpvController::setSubtitleDelay(double delay)
{
    if (::mpv_set_property_async(handle(), 0, "sub-delay", MPV_FORMAT_DOUBLE, &delay) < 0)
    {
        qWarning("Could not set subtitle delay to %lf", delay);
    }
}

void MpvController::setSecondarySubtitleDelay(double delay)
{
    if (::mpv_set_property_async(handle(), 0, "secondary-sub-delay", MPV_FORMAT_DOUBLE, &delay) < 0)
    {
        qWarning("Could not set secondary subtitle delay to %lf", delay);
    }
}

void MpvController::setFullscreen(bool value)
{
    int flag = value ? 1 : 0;
    if (mpv_set_property_async(handle(), 0, "fullscreen", MPV_FORMAT_FLAG, &flag) < 0)
    {
        qWarning("Could not set fullscreen");
    }
}

void MpvController::setVolume(int64_t value)
{
    if (::mpv_set_property_async(handle(), 0, "volume", MPV_FORMAT_INT64, &value) < 0)
    {
        qWarning("Could not set volume to %" PRId64, value);
    }
}

void MpvController::showText(const QString &text)
{
    QByteArray utf8Text = text.toUtf8();
    const char *command[] = {
        "show-text",
        utf8Text.data(),
        NULL
    };
    if (::mpv_command_async(handle(), 0, command))
    {
        qWarning("Could not show text '%s'", qUtf8Printable(text));
    }
}

void MpvController::sendKeyPress(int key, int modifiers)
{
    QString keypress = toModifierString(modifiers);
#if !defined(Q_OS_MACOS)
    if (modifiers & Qt::KeypadModifier)
    {
        switch (key)
        {
        case Qt::Key_0:
        case Qt::Key_1:
        case Qt::Key_2:
        case Qt::Key_3:
        case Qt::Key_4:
        case Qt::Key_5:
        case Qt::Key_6:
        case Qt::Key_7:
        case Qt::Key_8:
        case Qt::Key_9:
            keypress += "KP";
            break;
        case Qt::Key_Delete:
        case Qt::Key_Insert:
        case Qt::Key_Home:
        case Qt::Key_End:
        case Qt::Key_Clear:
        case Qt::Key_PageUp:
        case Qt::Key_PageDown:
        case Qt::Key_Right:
        case Qt::Key_Left:
        case Qt::Key_Down:
        case Qt::Key_Up:
        case Qt::Key_Enter:
            keypress += "KP_";
            break;
        }
    }
#endif // !defined(Q_OS_MACOS)

    if (key >= Qt::Key::Key_F1 && key <= Qt::Key::Key_F30)
    {
        keypress += "F" + QString::number(key - Qt::Key::Key_F1 + 1);
    }
    else
    {
        switch (key)
        {
        case Qt::Key::Key_Shift:
        case Qt::Key::Key_Control:
        case Qt::Key::Key_Alt:
        case Qt::Key::Key_Meta:
            return;
        case Qt::Key::Key_Left:
            keypress += "LEFT";
            break;
        case Qt::Key::Key_Right:
            keypress += "RIGHT";
            break;
        case Qt::Key::Key_Up:
            keypress += "UP";
            break;
        case Qt::Key::Key_Down:
            keypress += "DOWN";
            break;
        case Qt::Key::Key_Enter:
        case Qt::Key::Key_Return:
            keypress += "ENTER";
            break;
        case Qt::Key::Key_Escape:
            keypress += "ESC";
            break;
        case Qt::Key::Key_Backspace:
            keypress += "BS";
            break;
        case Qt::Key::Key_Pause:
            keypress += "PAUSE";
            break;
        case Qt::Key::Key_Delete:
            keypress += "DEL";
            break;
        case Qt::Key::Key_Insert:
            keypress += "INS";
            break;
        case Qt::Key::Key_Home:
            keypress += "HOME";
            break;
        case Qt::Key::Key_End:
            keypress += "END";
            break;
        case Qt::Key::Key_Clear:
            keypress += "DEC";
            break;
        case Qt::Key::Key_PageUp:
            keypress += "PGUP";
            break;
        case Qt::Key::Key_PageDown:
            keypress += "PGDWN";
            break;
        default:
        {
            QString text{QKeySequence(key).toString()};
            keypress += modifiers & Qt::ShiftModifier ?
                text : text.toLower();
        }
        }
    }

    QByteArray keypressUtf8 = keypress.toUtf8();
    const char *args[]{
        "keypress",
        keypressUtf8,
        nullptr
    };
    if (::mpv_command_async(handle(), 0, args) < 0)
    {
        qWarning("Could not send keypress for '%s'", qUtf8Printable(keypress));
    }
}

void MpvController::sendMouse(double x, double y)
{
    QByteArray xArg = QString::number(static_cast<int>(x)).toUtf8();
    QByteArray yArg = QString::number(static_cast<int>(y)).toUtf8();
    const char *args[]{
        "mouse",
        xArg,
        yArg,
        nullptr
    };
    if (::mpv_command_async(handle(), 0, args) < 0)
    {
        qWarning("Could not send mouse movement");
    }
}

void MpvController::sendMouseButton(
    double x, double y, Qt::MouseButton button, bool single)
{
    QByteArray buttonArg;
    switch (button)
    {
    case Qt::MouseButton::LeftButton:
        buttonArg = "0";
        break;
    case Qt::MouseButton::MiddleButton:
        buttonArg = "1";
        break;
    case Qt::MouseButton::RightButton:
        buttonArg = "2";
        break;
    case Qt::MouseButton::BackButton:
        buttonArg = "7";
        break;
    case Qt::MouseButton::ForwardButton:
        buttonArg = "8";
        break;
    default:
        return;
    }
    QByteArray xArg = QString::number(static_cast<int>(x)).toUtf8();
    QByteArray yArg = QString::number(static_cast<int>(y)).toUtf8();
    const char *mouseArgs[]{
        "mouse",
        xArg,
        yArg,
        buttonArg,
        single ? "single" : "double",
        nullptr
    };
    if (::mpv_command_async(handle(), 0, mouseArgs) < 0)
    {
        qWarning("Could not send mouse button");
    }
}

void MpvController::sendWheel(double x, double y, QPoint angleDelta)
{
    QByteArray buttonArg;
    if (angleDelta.y() > 0)
    {
        buttonArg = "3";
    }
    else if (angleDelta.y() < 0)
    {
        buttonArg = "4";
    }
    else if (angleDelta.x() < 0)
    {
        buttonArg = "5";
    }
    else if (angleDelta.x() > 0)
    {
        buttonArg = "6";
    }
    else
    {
        return;
    }
    QByteArray xArg = QString::number(static_cast<int>(x)).toUtf8();
    QByteArray yArg = QString::number(static_cast<int>(y)).toUtf8();
    const char *mouseArgs[]{
        "mouse",
        xArg,
        yArg,
        buttonArg,
        nullptr
    };
    if (::mpv_command_async(handle(), 0, mouseArgs) < 0)
    {
        qWarning("Could not send mouse button");
    }
}


QString MpvController::tempScreenshot(bool subtitles, const QString &ext)
{
    /* Create a valid temporary file name */
    QTemporaryFile file;
    if (!file.open())
    {
        return {};
    }
    QByteArray filename = (file.fileName() + ext).toUtf8();
    file.close();

    const char *args[] = {
        "screenshot-to-file",
        filename,
        subtitles ? NULL : "video",
        NULL
    };
    if (mpv_command(handle(), args) < 0)
    {
        qWarning("Could not take temporary screenshot");
        return {};
    }

    return filename;
}

QImage MpvController::screenshotRaw(bool subtitles)
{
    const char *args[] = {
        "screenshot-raw",
        subtitles ? "subtitles" : "video",
        "rgba",
        NULL,
    };

    mpv_node result{};
    int ret = ::mpv_command_ret(handle(), args, &result);
    if (ret < 0)
    {
        qWarning("Could not take raw screenshot: %s", ::mpv_error_string(ret));
        return {};
    }

    QImage image;
    auto cleanup = qScopeGuard(
        [&result] { ::mpv_free_node_contents(&result); }
    );

    std::optional<int64_t> width = mapInt(result, "w");
    std::optional<int64_t> height = mapInt(result, "h");
    std::optional<int64_t> stride = mapInt(result, "stride");
    QString format = mapString(result, "format");
    const mpv_byte_array *data = mapByteArray(result, "data");
    if (!width ||
        !height ||
        !stride ||
        format != "rgba" ||
        data == nullptr ||
        data->data == nullptr)
    {
        qWarning("Raw screenshot result had an unexpected format");
        return {};
    }
    if (*width <= 0 ||
        *height <= 0 ||
        *width > std::numeric_limits<int>::max() ||
        *height > std::numeric_limits<int>::max())
    {
        qWarning("Raw screenshot result had invalid dimensions");
        return {};
    }

    constexpr int BYTES_PER_PIXEL = 4;
    const int64_t minimumLineSize = *width * BYTES_PER_PIXEL;
    if (std::llabs(*stride) < minimumLineSize)
    {
        qWarning("Raw screenshot result had an invalid stride");
        return {};
    }

    image = QImage(
        static_cast<int>(*width),
        static_cast<int>(*height),
        QImage::Format_RGBA8888
    );
    if (image.isNull())
    {
        qWarning("Could not allocate raw screenshot image");
        return {};
    }

    const char *source = static_cast<const char *>(data->data);
    for (int y = 0; y < image.height(); ++y)
    {
        std::memcpy(
            image.scanLine(y),
            source + y * *stride,
            static_cast<size_t>(minimumLineSize)
        );
    }

    return image;
}

QImage MpvController::screenshotRawWithSubtitleVisibility(
    bool primary, bool secondary)
{
    if (player() == nullptr || player()->state() == nullptr)
    {
        return {};
    }

    int primaryFlag = primary ? 1 : 0;
    int secondaryFlag = secondary ? 1 : 0;
    int restorePrimary = player()->state()->subtitle()->visible() ? 1 : 0;
    int restoreSecondary =
        player()->state()->secondarySubtitle()->visible() ? 1 : 0;
    ::mpv_get_property(
        handle(), "sub-visibility", MPV_FORMAT_FLAG, &restorePrimary);
    ::mpv_get_property(
        handle(), "secondary-sub-visibility", MPV_FORMAT_FLAG,
        &restoreSecondary);

    const int primaryResult = ::mpv_set_property(
        handle(), "sub-visibility", MPV_FORMAT_FLAG, &primaryFlag);
    const int secondaryResult = ::mpv_set_property(
        handle(), "secondary-sub-visibility", MPV_FORMAT_FLAG,
        &secondaryFlag);
    const auto restore = qScopeGuard(
        [this, restorePrimary, restoreSecondary] () mutable {
            ::mpv_set_property(
                handle(), "sub-visibility", MPV_FORMAT_FLAG,
                &restorePrimary);
            ::mpv_set_property(
                handle(), "secondary-sub-visibility", MPV_FORMAT_FLAG,
                &restoreSecondary);
        }
    );

    if (primaryResult < 0 || secondaryResult < 0)
    {
        qWarning("Could not set temporary subtitle visibility");
        return {};
    }
    return screenshotRaw(true);
}

MpvFrameCapture MpvController::captureFrames(bool primary, bool secondary)
{
    MpvFrameCapture capture;
    if (player() == nullptr || player()->state() == nullptr)
    {
        return capture;
    }

    int restorePause = 0;
    int restorePrimary = 0;
    int restoreSecondary = 0;
    if (::mpv_get_property(
            handle(), "pause", MPV_FORMAT_FLAG, &restorePause) < 0 ||
        ::mpv_get_property(
            handle(), "sub-visibility", MPV_FORMAT_FLAG, &restorePrimary) < 0)
    {
        qWarning("Could not read player state for an atomic screenshot");
        return capture;
    }
    const int secondaryVisibilityResult = ::mpv_get_property(
        handle(), "secondary-sub-visibility", MPV_FORMAT_FLAG,
        &restoreSecondary);
    const bool hasSecondaryVisibility = secondaryVisibilityResult >= 0;
    if (!hasSecondaryVisibility && secondary)
    {
        qWarning("Could not read secondary subtitle visibility");
        return capture;
    }

    int paused = 1;
    if (::mpv_set_property(handle(), "pause", MPV_FORMAT_FLAG, &paused) < 0)
    {
        qWarning("Could not pause for an atomic screenshot");
        return capture;
    }
    const auto restore = qScopeGuard(
        [this, restorePause, restorePrimary, restoreSecondary,
         hasSecondaryVisibility] () mutable {
            ::mpv_set_property(
                handle(), "sub-visibility", MPV_FORMAT_FLAG,
                &restorePrimary);
            if (hasSecondaryVisibility)
            {
                ::mpv_set_property(
                    handle(), "secondary-sub-visibility", MPV_FORMAT_FLAG,
                    &restoreSecondary);
            }
            ::mpv_set_property(
                handle(), "pause", MPV_FORMAT_FLAG, &restorePause);
        }
    );

    const auto readString = [this] (const char *property) {
        char *value = ::mpv_get_property_string(handle(), property);
        if (value == nullptr)
        {
            return QString();
        }
        const QString result = QString::fromUtf8(value);
        ::mpv_free(value);
        return result;
    };
    const auto readDouble = [this] (const char *property) {
        double value = 0.0;
        ::mpv_get_property(handle(), property, MPV_FORMAT_DOUBLE, &value);
        return value;
    };

    if (::mpv_get_property(
            handle(), "time-pos", MPV_FORMAT_DOUBLE, &capture.position) < 0)
    {
        qWarning("Could not read the screenshot position");
        return capture;
    }
    capture.primaryText = readString("sub-text");
    capture.secondaryText = readString("secondary-sub-text");
    capture.primaryStart = readDouble("sub-start");
    capture.primaryEnd = readDouble("sub-end");
    capture.secondaryStart = readDouble("secondary-sub-start");
    capture.secondaryEnd = readDouble("secondary-sub-end");
    capture.stateCaptured = true;

    capture.videoFrame = screenshotRaw(false);
    if (capture.videoFrame.isNull())
    {
        return capture;
    }
    if (!primary && !secondary)
    {
        capture.subtitleFrame = capture.videoFrame;
        return capture;
    }

    int primaryFlag = primary ? 1 : 0;
    int secondaryFlag = secondary ? 1 : 0;
    if (::mpv_set_property(
            handle(), "sub-visibility", MPV_FORMAT_FLAG, &primaryFlag) < 0 ||
        (hasSecondaryVisibility &&
         ::mpv_set_property(
             handle(), "secondary-sub-visibility", MPV_FORMAT_FLAG,
             &secondaryFlag) < 0))
    {
        qWarning("Could not set temporary subtitle visibility");
        return capture;
    }
    capture.subtitleFrame = screenshotRaw(true);
    return capture;
}

QString MpvController::tempAudioClip(const MpvAudioClipArgs &args)
{
    return encodeAudioClip(encodingContext(), args);
}

MpvEncodingContext MpvController::encodingContext() const
{
    MpvEncodingContext context;
    if (player() == nullptr || player()->state() == nullptr)
    {
        return context;
    }

    context.input = player()->state()->path().toUtf8();
    context.scriptOptions = encodingScriptOptions();
    context.aid = player()->state()->aid();
    context.vid = player()->state()->vid();
    context.sharedSubtitleDelay = player()->state()->subtitle()->delay();
    double secondaryDelay = 0.0;
    context.independentSecondaryDelay = ::mpv_get_property(
        handle(),
        "secondary-sub-delay",
        MPV_FORMAT_DOUBLE,
        &secondaryDelay
    ) >= 0;

    const auto captureSubtitle = [this] (
        int64_t id, double delay)
    {
        MpvEncodingContext::SubtitleTrack result;
        if (id <= 0)
        {
            return result;
        }
        for (const MpvTrack *track : player()->state()->subtitleTracks())
        {
            if (track == nullptr || track->id() != id)
            {
                continue;
            }
            result.id = track->id();
            result.sourceId = track->sourceId();
            result.externalFilename = track->externalFilename();
            result.codec = track->codec();
            result.delay = delay;
            result.external = track->external();
            break;
        }
        return result;
    };
    context.primarySubtitle = captureSubtitle(
        player()->state()->sid(), player()->state()->subtitle()->delay());
    context.secondarySubtitle = captureSubtitle(
        player()->state()->secondarySid(),
        player()->state()->secondarySubtitle()->delay());
    return context;
}

QString MpvController::encodeAudioClip(
    const MpvEncodingContext &context,
    const MpvAudioClipArgs &args)
{
    if (context.aid <= 0 || context.input.isEmpty() ||
        !std::isfinite(args.start) || !std::isfinite(args.end) ||
        args.start >= args.end)
    {
        return {};
    }

    QByteArray argString = QString("start=%1,end=%2,aid=%3")
        .arg(args.start, 0, 'f', 3)
        .arg(args.end, 0, 'f', 3)
        .arg(context.aid)
        .toUtf8();

    QList<QPair<QByteArray, QByteArray>> options = {
        {"vid", "no"},
        {"aid", "auto"},
        {"sid", "no"},
        {"secondary-sid", "no"},
    };
    if (args.normalize)
    {
        QByteArray audioFilter = "loudnorm=I=";
        audioFilter += QByteArray::number(args.db, 'f', 1);
        options.emplaceBack("af", std::move(audioFilter));
    }

    return encodeFile(
        context.input,
        context.scriptOptions,
        argString,
        options,
        args.extension
    );
}

std::function<QString()> MpvController::makeTempAudioClipTask(
    const MpvAudioClipArgs &args) const
{
    const MpvEncodingContext context = encodingContext();
    if (context.aid <= 0 || context.input.isEmpty())
    {
        return {};
    }

    return [context, args] {
        return MpvController::encodeAudioClip(context, args);
    };
}

QString MpvController::tempVideoClip(const MpvVideoClipArgs &args)
{
    return encodeVideoClip(encodingContext(), args);
}

QString MpvController::encodeVideoClip(
    const MpvEncodingContext &context,
    const MpvVideoClipArgs &args,
    const QString &subtitleFile,
    bool nativePrimary,
    bool nativeSecondary)
{
    constexpr const char *FILE_EXTENSION = ".mp4";

    if (context.input.isEmpty() || context.vid <= 0 ||
        !std::isfinite(args.start) ||
        !std::isfinite(args.end) || args.start >= args.end)
    {
        return {};
    }

    QByteArray argString = QString("ovc=libx264,oac=aac,start=%1,end=%2")
        .arg(args.start, 0, 'f', 3)
        .arg(args.end, 0, 'f', 3)
        .toUtf8();
    argString += QString(",vid=%1").arg(context.vid).toUtf8();
    if (args.audio)
    {
        if (context.aid <= 0)
        {
            return {};
        }
        argString += QString(",aid=%1").arg(context.aid).toUtf8();
    }

    QList<QPair<QByteArray, QByteArray>> options = {
        {"vid", "auto"},
        {"aid", args.audio ? "auto" : "no"},
        {"sid", "no"},
        {"secondary-sid", "no"},
    };
    if (args.normalize)
    {
        options.emplaceBack(
            "af",
            QString("loudnorm=I=%1").arg(args.db, 'f', 1).toUtf8()
        );
    }

    return encodeFile(
        context.input,
        context.scriptOptions,
        argString,
        options,
        FILE_EXTENSION,
        args.subtitles ? &context : nullptr,
        args.subtitles ? subtitleFile : QString(),
        args.subtitles && nativePrimary,
        args.subtitles && nativeSecondary
    );
}

/* End Public Functions */
/* Begin Private Functions */

mpv_handle *MpvController::handle() const noexcept
{
    return player()->handle();
}

QString MpvController::quoteArg(QString arg)
{
    qsizetype eqidx = arg.indexOf('=');
    if (eqidx == -1)
    {
        return arg;
    }
    return arg.insert(eqidx + 1, "\"").append("\"");
}

bool MpvController::argsValid(const QStringList &args)
{
    qsizetype count = 0;
    for (qsizetype i = 1; i < args.size(); ++i)
    {
        if (args[i] == "--{")
        {
            count++;
        }
        else if (args[i] == "--}")
        {
            count--;
            if (count < 0)
            {
                return false;
            }
        }
    }

    return count == 0;
}

qsizetype MpvController::buildArgsTree(
    const QStringList &args, qsizetype i, LoadFileNode &parent, qsizetype depth)
{
    if (depth < 1)
    {
        return -1;
    }

    while (i < args.size())
    {
        if (args[i] == "--}")
        {
            return i;
        }
        else if (args[i] == "--{")
        {
            parent.files.emplaceBack("--"); // Used to indicate a level deeper
            parent.children.emplaceBack();
            LoadFileNode &child = parent.children.last();
            i = buildArgsTree(args, i + 1, child, depth - 1);
            if (i < 0)
            {
                return -1;
            }
        }
        else if (args[i].startsWith("--"))
        {
            parent.options.emplaceBack(quoteArg(args[i].mid(2)));
        }
        else
        {
            parent.files.emplaceBack(args[i]);
        }
        ++i;
    }

    return i;
}

void MpvController::loadFilesFromTree(
    const LoadFileNode &parent, QStringList &options)
{
    qsizetype lastSize = options.size();
    options.append(parent.options);

    qsizetype currentChild = 0;
    for (const QString &file : parent.files)
    {
        if (file == "--")
        {
            loadFilesFromTree(parent.children[currentChild], options);
            currentChild++;
        }
        else
        {
            loadFile(file, true, options);
        }
    }

    while (options.size() > lastSize)
    {
        options.removeLast();
    }
}

bool MpvController::isSubtitleFile(const QString &file) const
{
    QString ext =
        file.right(file.size() - file.lastIndexOf('.') - 1).toLower();
    return m_subtitleExtensions.contains(ext);
}

QString MpvController::toModifierString(int modifiers)
{
    QString keypress;
    if (modifiers & Qt::ShiftModifier)
    {
        keypress += "Shift+";
    }
    if (modifiers & Qt::ControlModifier)
    {
        keypress += "Ctrl+";
    }
    if (modifiers & Qt::AltModifier)
    {
        keypress += "Alt+";
    }
    if (modifiers & Qt::MetaModifier)
    {
        keypress += "Meta+";
    }
    return keypress;
}

QByteArray MpvController::encodingScriptOptions() const
{
    QByteArray result;
    char *scriptOptions = ::mpv_get_property_string(handle(), "script-opts");
    if (scriptOptions != nullptr)
    {
        const QByteArray configured(scriptOptions);
        if (configured.contains("ytdl_hook-ytdl_path="))
        {
            result = configured;
        }
    }
    ::mpv_free(scriptOptions);

#if MEMENTO_BUNDLE
    if (result.isEmpty())
    {
        result = "ytdl_hook-ytdl_path=";
        char *configDirectory = ::mpv_get_property_string(handle(), "config-dir");
        if (configDirectory != nullptr)
        {
            result += configDirectory;
            result += '/';
        }
        else
        {
            result += DirectoryUtils::getConfigDir().toUtf8();
        }
        ::mpv_free(configDirectory);
        result += "youtube-dl";
    }
#endif // MEMENTO_BUNDLE
    return result;
}

QString MpvController::encodeFile(
    const QByteArray &input,
    const QByteArray &scriptOptions,
    const QByteArray &argString,
    const QList<QPair<QByteArray, QByteArray>> &options,
    const QString &fileExtension,
    const MpvEncodingContext *subtitleContext,
    const QString &generatedSubtitleFile,
    bool nativePrimary,
    bool nativeSecondary)
{
    /* Create a valid temporary file name */
    QTemporaryFile file;
    if (!file.open())
    {
        return {};
    }
    const QString outputPath = file.fileName() + fileExtension;
    const QByteArray filename = outputPath.toUtf8();
    file.close();

    const char *argOpts = argString.isEmpty() ? NULL : argString.data();

    bool isApi23 = mpv_client_api_version() >= MPV_MAKE_VERSION(2, 3);
    const char *args[] = {
        "loadfile",
        input.constData(),
        "replace",
        isApi23 ? "0"     : argOpts,
        isApi23 ? argOpts : NULL,
        NULL
    };

    mpv_handle *enc_h = mpv_create();
    if (enc_h == NULL)
    {
        qWarning("Error creating encoder handle");
        return {};
    }
    const auto closeEncoder = [&enc_h] {
        if (enc_h != nullptr)
        {
            ::mpv_terminate_destroy(enc_h);
            enc_h = nullptr;
        }
    };
    const auto destroyEncoder = qScopeGuard([&closeEncoder] {
        closeEncoder();
    });
    const auto fail = [&closeEncoder, &outputPath] (
        const char *message) -> QString
    {
        qWarning("%s", message);
        closeEncoder();
        QFile::remove(outputPath);
        return {};
    };
    const auto setOption = [enc_h] (
        const QByteArray &name, const QByteArray &value)
    {
        const int result = ::mpv_set_option_string(
            enc_h, name.constData(), value.constData());
        if (result < 0)
        {
            qWarning(
                "Could not set encoder option '%s': %s",
                name.constData(), ::mpv_error_string(result));
            return false;
        }
        return true;
    };

    for (const auto &[k, v] : options)
    {
        if (!setOption(k, v))
        {
            return fail("Could not configure encoder");
        }
    }
    if (!setOption("cover-art-auto", "no") ||
        !setOption("keep-open", "no") ||
        !setOption("ytdl", "yes") ||
        !setOption("config", "no") ||
        !setOption("o", filename))
    {
        return fail("Could not configure encoder");
    }

    if (subtitleContext != nullptr)
    {
        const QStringList subtitleFiles = encoderSubtitleFiles(
            *subtitleContext,
            generatedSubtitleFile,
            nativePrimary,
            nativeSecondary
        );
        const int secondaryOverrideResult = ::mpv_set_option_string(
            enc_h, "secondary-sub-ass-override", "no");
        if (secondaryOverrideResult < 0 &&
            secondaryOverrideResult != MPV_ERROR_OPTION_NOT_FOUND)
        {
            qWarning(
                "Could not set encoder option 'secondary-sub-ass-override': %s",
                ::mpv_error_string(secondaryOverrideResult));
            return fail("Could not configure encoder subtitles");
        }
        if (!setOption("pause", "yes") ||
            !setOption("sub-auto", "no") ||
            !setOption("autoload-files", "no") ||
            !setOption("sub-ass-override", "no") ||
            (!subtitleFiles.isEmpty() &&
             !setStringListOption(enc_h, "sub-files", subtitleFiles)))
        {
            return fail("Could not configure encoder subtitles");
        }
    }

    /* Preserve the player-selected downloader without touching the live mpv
     * handle from a worker thread. */
    if (!scriptOptions.isEmpty())
    {
        if (!setOption("script-opts", scriptOptions))
        {
            return fail("Could not configure encoder downloader");
        }
    }

    if (::mpv_initialize(enc_h) < 0)
    {
        return fail("Could not initialize encoder");
    }

    if (::mpv_command(enc_h, args) < 0)
    {
        return fail("Could not start encoder");
    }

    bool subtitlesConfigured = subtitleContext == nullptr;
    while (true)
    {
        mpv_event *event = ::mpv_wait_event(enc_h, 100);
        if (event->event_id == MPV_EVENT_NONE ||
            event->event_id == MPV_EVENT_QUEUE_OVERFLOW)
        {
            qWarning()
                << QString("mpv returned a bad event: %1").arg(event->event_id);
            return fail("Encoder stopped responding");
        }
        if (event->event_id == MPV_EVENT_FILE_LOADED &&
            !subtitlesConfigured)
        {
            if (!configureEncoderSubtitles(
                    enc_h,
                    *subtitleContext,
                    generatedSubtitleFile,
                    nativePrimary,
                    nativeSecondary) ||
                ::mpv_set_property_string(enc_h, "pause", "no") < 0)
            {
                return fail("Could not select encoder subtitle tracks");
            }
            subtitlesConfigured = true;
        }
        if (event->event_id == MPV_EVENT_END_FILE)
        {
            const auto *end =
                static_cast<const mpv_event_end_file *>(event->data);
            if (!subtitlesConfigured || end == nullptr || end->error < 0)
            {
                return fail("Could not encode file");
            }
            break;
        }
    }

    closeEncoder();
    if (!QFileInfo(outputPath).isFile() || QFileInfo(outputPath).size() <= 0)
    {
        return fail("Encoder did not produce an output file");
    }
    return outputPath;
}

const mpv_node *MpvController::mapValue(const mpv_node &node, const char *key)
{
    if (node.format != MPV_FORMAT_NODE_MAP || node.u.list == nullptr)
    {
        return nullptr;
    }

    mpv_node_list *list = node.u.list;
    for (int i = 0; i < list->num; ++i)
    {
        if (std::strcmp(list->keys[i], key) == 0)
        {
            return &list->values[i];
        }
    }
    return nullptr;
}

std::optional<int64_t> MpvController::mapInt(
    const mpv_node &node, const char *key)
{
    const mpv_node *value = mapValue(node, key);
    if (value == nullptr || value->format != MPV_FORMAT_INT64)
    {
        return std::nullopt;
    }
    return value->u.int64;
}

QString MpvController::mapString(const mpv_node &node, const char *key)
{
    const mpv_node *value = mapValue(node, key);
    if (value == nullptr || value->format != MPV_FORMAT_STRING)
    {
        return {};
    }
    return QString::fromUtf8(value->u.string);
}

const mpv_byte_array *MpvController::mapByteArray(
    const mpv_node &node, const char *key)
{
    const mpv_node *value = mapValue(node, key);
    if (value == nullptr || value->format != MPV_FORMAT_BYTE_ARRAY)
    {
        return nullptr;
    }
    return value->u.ba;
}

/* End Private Functions */
