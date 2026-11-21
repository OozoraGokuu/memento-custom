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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Memento. If not, see <https://www.gnu.org/licenses/>.
//
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <QObject>
#include <QStringList>

/**
 * Starts Syncplay with this Memento installation as its media player.
 * Syncplay opens and controls a separate Memento process for the room.
 */
class WatchTogetherLauncher : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool syncplaySession READ syncplaySession CONSTANT)

public:
    explicit WatchTogetherLauncher(QObject *parent = nullptr);

    [[nodiscard]] bool syncplaySession() const;

    /** Return the installed Syncplay executable, or an empty string. */
    Q_INVOKABLE [[nodiscard]] QString detectedSyncplayPath() const;

    /** Generate a 128-bit, URL-safe room name suffix. */
    Q_INVOKABLE [[nodiscard]] QString createRoomCode() const;

    /**
     * Start Syncplay. The media path is forwarded only if it names an
     * existing local file. An empty return means the process was started;
     * otherwise the return value explains the failure.
     */
    Q_INVOKABLE [[nodiscard]] QString launch(
        const QString &host,
        const QString &room,
        const QString &name,
        const QString &mediaPath,
        const QString &syncplayPath = QString()
    ) const;

    /** Public for focused argument construction tests. */
    [[nodiscard]] static QStringList buildArguments(
        const QString &host,
        const QString &room,
        const QString &name,
        const QString &playerPath,
        const QString &mediaPath
    );

    /** Empty when the current media is remote, missing, or a directory. */
    [[nodiscard]] static QString localMediaFile(const QString &mediaPath);

private:
    [[nodiscard]] static QString resolveExecutable(const QString &path);
    [[nodiscard]] static QString playerExecutable();
};
