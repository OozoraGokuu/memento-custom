#pragma once

#include <QString>
#include <QStringList>

struct mpv_handle;

namespace SyncplayMpv
{
/**
 * Restore Syncplay's runtime options after mpv has loaded mpv.conf.
 * Returns an error for a Syncplay-controlled player that could not be set up.
 */
[[nodiscard]] QString restoreOptions(
    mpv_handle *handle,
    const QStringList &arguments
);
} // namespace SyncplayMpv
