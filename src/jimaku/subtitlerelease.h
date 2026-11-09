#ifndef SUBTITLERELEASE_H
#define SUBTITLERELEASE_H

#include <QString>
#include <QVariantMap>

/** Conservative filename rules for a manually selected subtitle release. */
namespace SubtitleRelease
{

/**
 * Keep every part of the filename except one confidently identified episode
 * number and trailing CRC tags. An unnumbered or ambiguous filename gets an
 * exact rule, which is usable only for this same filename and episode.
 * Relative directories, season, group, language, variant and extension remain
 * part of the identity. An empty map means the filename is invalid.
 */
QVariantMap fromFile(const QString &name, int episode);

/** Match the requested episode without guessing another release or group. */
bool matches(const QString &name, int episode, const QVariantMap &rule);

} // namespace SubtitleRelease

#endif // SUBTITLERELEASE_H
