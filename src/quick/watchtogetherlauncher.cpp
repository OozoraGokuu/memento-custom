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

#include "quick/watchtogetherlauncher.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>

#include "quick/syncplayprocess.h"

namespace
{
#if defined(Q_OS_WIN)
bool syncplayIsSupported(const QString &command)
{
    QProcess process;
    process.start(command, {QStringLiteral("--version")});
    if (!process.waitForStarted(3000))
    {
        return false;
    }
    if (!process.waitForFinished(10000))
    {
        process.kill();
        process.waitForFinished(3000);
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        return false;
    }

    const QString versionText = QString::fromLocal8Bit(
        process.readAllStandardOutput() + process.readAllStandardError()
    );
    const auto match = QRegularExpression(
        QStringLiteral(R"(\b(\d+)\.(\d+)\.(\d+)\b)")
    ).match(versionText);
    if (!match.hasMatch())
    {
        // Custom builds may omit their version from --version output. Syncplay
        // started successfully; let its own compatibility check decide.
        return true;
    }

    const int major = match.captured(1).toInt();
    const int minor = match.captured(2).toInt();
    const int patch = match.captured(3).toInt();
    return major > 1 || (major == 1 &&
        (minor > 7 || (minor == 7 && patch >= 5)));
}
#endif
} // namespace

WatchTogetherLauncher::WatchTogetherLauncher(QObject *parent) : QObject(parent)
{
}

bool WatchTogetherLauncher::syncplaySession() const
{
#if !defined(Q_OS_WIN)
    return false;
#else
    return SyncplayProcess::isSyncplayProcess();
#endif
}

QString WatchTogetherLauncher::resolveExecutable(const QString &path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty())
    {
        return {};
    }

    const QFileInfo info(trimmed);
    if (info.isAbsolute() && info.isFile() && info.isExecutable())
    {
        return info.absoluteFilePath();
    }

    if (!info.isAbsolute() && !trimmed.contains(u'/') &&
        !trimmed.contains(u'\\'))
    {
        return QStandardPaths::findExecutable(trimmed);
    }

    return {};
}

QString WatchTogetherLauncher::detectedSyncplayPath() const
{
#if !defined(Q_OS_WIN)
    return {};
#else
    const QStringList names{QStringLiteral("Syncplay.exe"),
                            QStringLiteral("syncplay.exe")};

    for (const QString &name : names)
    {
        const QString result = QStandardPaths::findExecutable(name);
        if (!result.isEmpty())
        {
            return result;
        }
    }

    const QStringList roots{qEnvironmentVariable("ProgramFiles"),
                            qEnvironmentVariable("ProgramFiles(x86)"),
                            qEnvironmentVariable("LOCALAPPDATA")};
    for (const QString &root : roots)
    {
        if (root.isEmpty())
        {
            continue;
        }
        const QString result = resolveExecutable(
            root + QStringLiteral("/Syncplay/Syncplay.exe")
        );
        if (!result.isEmpty())
        {
            return result;
        }
    }
    return {};
#endif
}

QString WatchTogetherLauncher::createRoomCode() const
{
    QString code;
    code.reserve(32);
    for (int i = 0; i < 4; ++i)
    {
        code += QStringLiteral("%1").arg(
            QRandomGenerator::system()->generate(), 8, 16, QLatin1Char('0')
        );
    }
    return code;
}

QString WatchTogetherLauncher::localMediaFile(const QString &mediaPath)
{
    QString path = mediaPath;
    if (path.isEmpty())
    {
        return {};
    }
    if (path.startsWith(QStringLiteral("file://"), Qt::CaseInsensitive))
    {
        const QUrl url(path);
        if (!url.isValid() || !url.isLocalFile())
        {
            return {};
        }
        path = url.toLocalFile();
    }
    else if (path.contains(QStringLiteral("://")))
    {
        return {};
    }

    const QFileInfo info(path);
    if (!info.isFile())
    {
        return {};
    }
    return info.absoluteFilePath();
}

QStringList WatchTogetherLauncher::buildArguments(
    const QString &host,
    const QString &room,
    const QString &name,
    const QString &playerPath,
    const QString &mediaPath
)
{
    QStringList args{
        QStringLiteral("--host=%1").arg(host.trimmed()),
        QStringLiteral("--room=%1").arg(room.trimmed())
    };

    if (!name.trimmed().isEmpty())
    {
        args << QStringLiteral("--name=%1").arg(name.trimmed());
    }

    args << QStringLiteral("--player-path=%1").arg(playerPath);

    const QString localFile = localMediaFile(mediaPath);
    if (!localFile.isEmpty())
    {
        args << localFile;
    }

    return args;
}

QString WatchTogetherLauncher::playerExecutable()
{
    QString path = QCoreApplication::applicationFilePath();
    // Syncplay 1.7.5 identifies Memento by a case-sensitive substring check.
    // Windows paths are case insensitive, so a lowercase spelling is valid.
    if (!path.contains(QStringLiteral("memento")))
    {
        path = path.toLower();
    }
    return path;
}

QString WatchTogetherLauncher::launch(
    const QString &host,
    const QString &room,
    const QString &name,
    const QString &mediaPath,
    const QString &syncplayPath
) const
{
#if !defined(Q_OS_WIN)
    Q_UNUSED(host)
    Q_UNUSED(room)
    Q_UNUSED(name)
    Q_UNUSED(mediaPath)
    Q_UNUSED(syncplayPath)
    return tr("Watch Together is available in Memento for Windows.");
#else
    if (host.trimmed().isEmpty())
    {
        return tr("Enter a Syncplay server address (for example, syncplay.pl:8995).");
    }
    if (host.contains(u'\n') || host.contains(u'\r') ||
        host.contains(QStringLiteral("://")))
    {
        return tr("Enter a server address such as syncplay.pl:8995, without a URL scheme.");
    }
    if (room.trimmed().isEmpty())
    {
        return tr("Enter or create a room name.");
    }

    const QString executable = syncplayPath.trimmed().isEmpty()
        ? detectedSyncplayPath() : resolveExecutable(syncplayPath);
    const QString playerPath = playerExecutable();

    if (executable.isEmpty())
    {
        return syncplayPath.trimmed().isEmpty()
            ? tr("Syncplay was not found. Install Syncplay 1.7.5 or newer, or enter its executable path in Watch Together.")
            : tr("The Syncplay executable does not exist or cannot be run. Check its path in Watch Together.");
    }
    if (!syncplayIsSupported(executable))
    {
        return tr("The Syncplay executable could not be run, or its version is older than 1.7.5. Check its path in Watch Together.");
    }

    if (!QFileInfo(playerPath).isExecutable())
    {
        return tr("This Memento executable could not be passed to Syncplay.");
    }

    const QStringList args = buildArguments(
        host, room, name, playerPath, mediaPath
    );
    if (!QProcess::startDetached(executable, args))
    {
        return tr("Syncplay could not be started. Check its executable path and try again.");
    }

    return {};
#endif
}
