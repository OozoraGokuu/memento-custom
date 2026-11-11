#ifndef SUBTITLERELEASE_H
#define SUBTITLERELEASE_H

#include <QString>
#include <QVariantMap>

/** Episode-independent identity for a manually selected subtitle release. */
namespace SubtitleRelease
{

/**
 * Remember the format, source-relative directory, group tags and explicit
 * season/language/variant metadata, including the service after a release
 * medium such as WEBRip. Paired season/absolute episodes must keep their offset.
 * Parse the episode separately so spacing,
 * episode markers and free episode titles can vary. Unnumbered or ambiguous
 * names remain exact-only. Callers must constrain candidates to the linked
 * source and refuse multiple matching files. An empty map means invalid input.
 */
QVariantMap fromFile(const QString &name, int episode);

/** Match the episode and release identity, migrating validated v1/v2 rules. */
bool matches(const QString &name, int episode, const QVariantMap &rule);

} // namespace SubtitleRelease

#endif // SUBTITLERELEASE_H
