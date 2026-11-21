#include "player/syncplaympv.h"

#include <mpv/client.h>

#include "quick/syncplayprocess.h"

namespace SyncplayMpv
{
QString restoreOptions(mpv_handle *handle, const QStringList &arguments)
{
    const QString syncplayScript = SyncplayProcess::scriptPath(arguments);
    if (syncplayScript.isEmpty())
    {
        return {};
    }

    // mpv.conf is loaded inside mpv_initialize() and overrides options set
    // before it. Syncplay starts Memento without a media file, so loadArgs()
    // cannot reapply these options. Only restore the Syncplay options that
    // must win over user config for the room connection to work.
    const QStringList runtimeOptions{
        QStringLiteral("force-window"),
        QStringLiteral("idle"),
        QStringLiteral("hr-seek"),
        QStringLiteral("keep-open"),
        QStringLiteral("input-terminal"),
        QStringLiteral("term-playing-msg"),
        QStringLiteral("keep-open-pause"),
        QStringLiteral("input-ipc-server")
    };
    for (qsizetype i = 1; i < arguments.size(); ++i)
    {
        const QString &argument = arguments.at(i);
        if (!argument.startsWith(QStringLiteral("--")))
        {
            continue;
        }
        const qsizetype equals = argument.indexOf(u'=');
        if (equals < 3)
        {
            continue;
        }
        const QString name = argument.mid(2, equals - 2);
        if (!runtimeOptions.contains(name))
        {
            continue;
        }
        const QByteArray nameUtf8 = name.toUtf8();
        const QByteArray valueUtf8 = argument.mid(equals + 1).toUtf8();
        const int result = ::mpv_set_property_string(
            handle, nameUtf8.constData(), valueUtf8.constData());
        if (result < 0)
        {
            return QStringLiteral("Could not set Syncplay mpv option '%1': %2")
                .arg(name, QString::fromUtf8(::mpv_error_string(result)));
        }
    }

    // Setting the scripts option after initialization does not load a Lua
    // script. Load Syncplay's script only if it lost to mpv.conf; otherwise
    // it was already loaded during mpv_initialize().
    bool scriptAlreadyLoaded = false;
    char *configuredScripts = ::mpv_get_property_string(handle, "scripts");
    if (configuredScripts != nullptr)
    {
        scriptAlreadyLoaded = QString::fromUtf8(configuredScripts)
            .split(u',', Qt::SkipEmptyParts).contains(syncplayScript);
        ::mpv_free(configuredScripts);
    }
    if (!scriptAlreadyLoaded)
    {
        const QByteArray scriptUtf8 = syncplayScript.toUtf8();
        const char *command[]{"load-script", scriptUtf8.constData(), nullptr};
        const int result = ::mpv_command(handle, command);
        if (result < 0)
        {
            return QStringLiteral("Could not load Syncplay's mpv script: %1")
                .arg(QString::fromUtf8(::mpv_error_string(result)));
        }
    }

    return {};
}
} // namespace SyncplayMpv
