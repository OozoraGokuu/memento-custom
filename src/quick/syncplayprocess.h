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

#include <QCoreApplication>
#include <QStringList>

namespace SyncplayProcess
{
/** Return Syncplay's mpv Lua integration script, if present in the arguments. */
[[nodiscard]] inline QString scriptPath(
    const QStringList &arguments = QCoreApplication::arguments()
)
{
    for (qsizetype i = 1; i < arguments.size(); ++i)
    {
        const QString &argument = arguments.at(i);
        QString scripts;

        if (argument.startsWith(QStringLiteral("--scripts=")) ||
            argument.startsWith(QStringLiteral("--script=")))
        {
            scripts = argument.sliced(argument.indexOf(u'=') + 1);
        }
        else if ((argument == QStringLiteral("--scripts") ||
                  argument == QStringLiteral("--script")) &&
                 i + 1 < arguments.size())
        {
            scripts = arguments.at(++i);
        }
        else
        {
            continue;
        }

        for (const QString &script : scripts.split(u',', Qt::SkipEmptyParts))
        {
            const qsizetype slash = qMax(script.lastIndexOf(u'/'),
                                         script.lastIndexOf(u'\\'));
            if (script.sliced(slash + 1).compare(
                    QStringLiteral("syncplayintf.lua"),
                    Qt::CaseInsensitive
                ) == 0)
            {
                return script;
            }
        }
    }

    return {};
}

/** Identify the separate Memento process started and controlled by Syncplay. */
[[nodiscard]] inline bool isSyncplayProcess(
    const QStringList &arguments = QCoreApplication::arguments()
)
{
    return !scriptPath(arguments).isEmpty();
}

/** Return Syncplay's requested mpv IPC socket, if it supplied one. */
[[nodiscard]] inline QString ipcServerPath(
    const QStringList &arguments = QCoreApplication::arguments()
)
{
    const QString prefix = QStringLiteral("--input-ipc-server=");
    QString path;
    for (qsizetype i = 1; i < arguments.size(); ++i)
    {
        if (arguments.at(i).startsWith(prefix))
        {
            path = arguments.at(i).sliced(prefix.size());
        }
    }
    return path;
}
} // namespace SyncplayProcess
