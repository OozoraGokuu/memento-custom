#include "subtitlerelease.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <algorithm>
#include <QVector>

namespace
{

struct EpisodeToken
{
    qsizetype start;
    qsizetype length;
    int number;
};

QString normalizedName(const QString &name)
{
    // The marker used in templates must never occur in an input filename.
    static const QRegularExpression controls(QStringLiteral("[\\x00-\\x1f\\x7f]"));
    if (name.isEmpty() || name.size() > 4096 || controls.match(name).hasMatch())
        return {};
    QString result = name.normalized(QString::NormalizationForm_KC).toCaseFolded();
    result = result.simplified();
    // Ignore whitespace around separators, while keeping the separators and
    // whitespace within titles and release names significant.
    static const QRegularExpression separatorSpace(
        QStringLiteral("\\s*([._\\-\\[\\]()/\\\\])\\s*"));
    result.replace(separatorSpace, QStringLiteral("\\1"));
    return result;
}

qsizetype leafStart(const QString &name)
{
    return qMax(name.lastIndexOf(QLatin1Char('/')),
                name.lastIndexOf(QLatin1Char('\\'))) + 1;
}

bool insideTag(const QString &name, qsizetype position)
{
    return name.lastIndexOf(QLatin1Char('['), position) >
               name.lastIndexOf(QLatin1Char(']'), position) ||
           name.lastIndexOf(QLatin1Char('('), position) >
               name.lastIndexOf(QLatin1Char(')'), position);
}

bool hasTitlePrefix(QString prefix)
{
    static const QRegularExpression tags(QStringLiteral("\\[[^\\]]*\\]|\\([^)]*\\)"));
    static const QRegularExpression separators(QStringLiteral("[\\s._\\-]+"));
    prefix.remove(tags);
    prefix.remove(separators);
    return !prefix.isEmpty();
}

QVector<EpisodeToken> episodeTokens(const QString &base)
{
    QVector<EpisodeToken> tokens;
    const auto add = [&tokens](const QRegularExpressionMatch &match) {
        const qsizetype start = match.capturedStart(1);
        const qsizetype length = match.capturedLength(1);
        for (const auto &token : tokens)
            if (token.start == start && token.length == length) return;
        tokens.push_back({start, length, match.captured(1).toInt()});
    };

    // A decimal episode (05.5), a range, or another number is never silently
    // treated as the integer episode. A fixed v2/v3 revision remains in the rule.
    const QString end = QStringLiteral(
        "(?=$|[^\\p{L}\\p{N}]|v[0-9]+(?:$|[^\\p{L}\\p{N}]))(?!\\.[0-9])");
    const QStringList explicitPatterns = {
        QStringLiteral("(?<![\\p{L}\\p{N}])s[0-9]{1,2}[ ._-]*e([0-9]{1,4})") + end,
        QStringLiteral("(?<![\\p{L}\\p{N}])(?:episode|ep|e)[ ._-]*([0-9]{1,4})") + end,
        QStringLiteral("第([0-9]{1,4})話")
    };
    for (const QString &pattern : explicitPatterns)
    {
        auto matches = QRegularExpression(pattern).globalMatch(base);
        while (matches.hasNext())
        {
            const auto match = matches.next();
            // Leading tags can be group names, even when they resemble E05.
            // Episode markers outside tags are unambiguous.
            if (!insideTag(base, match.capturedStart(1))) add(match);
        }
    }

    auto dashed = QRegularExpression(QStringLiteral("-([0-9]{1,4})") + end)
                      .globalMatch(base);
    while (dashed.hasNext())
    {
        const auto match = dashed.next();
        if (!insideTag(base, match.capturedStart(1)) &&
            hasTitlePrefix(base.left(match.capturedStart()))) add(match);
    }

    auto bracketed = QRegularExpression(QStringLiteral("\\[([0-9]{1,4})\\]"))
                         .globalMatch(base);
    while (bracketed.hasNext())
    {
        const auto match = bracketed.next();
        if (match.captured() == base ||
            hasTitlePrefix(base.left(match.capturedStart()))) add(match);
    }

    // A bare batch member is safe. An arbitrary number elsewhere in a title,
    // numeric group, season, codec, year or resolution is not an episode token.
    const auto bare = QRegularExpression(QStringLiteral("^([0-9]{1,4})$"))
                          .match(base);
    if (bare.hasMatch()) add(bare);
    return tokens;
}

QVariantMap legacyRule(const QString &name, int episode)
{
    const QString normalized = normalizedName(name);
    const qsizetype leaf = leafStart(normalized);
    const qsizetype extension = normalized.lastIndexOf(QLatin1Char('.'));
    if (normalized.isEmpty() || extension <= leaf || extension == normalized.size() - 1)
        return {};

    QString pattern = normalized;
    QString mode = QStringLiteral("exact");
    const auto tokens = episodeTokens(normalized.mid(leaf, extension - leaf));
    if (episode > 0 && tokens.size() == 1 && tokens.front().number == episode)
    {
        const auto &token = tokens.front();
        const qsizetype start = leaf + token.start;
        // CRCs are allowed to vary only after the episode. A leading tag such
        // as [DeadBEEF] could be a group name and must retain its identity.
        QString suffix = normalized.mid(start + token.length);
        static const QRegularExpression crc(QStringLiteral("\\[[0-9a-f]{8}\\]"));
        suffix.remove(crc);
        pattern = normalized.left(start) + QChar(1) + suffix;
        mode = QStringLiteral("template");
    }

    return {
        {QStringLiteral("version"), 1},
        {QStringLiteral("valid"), true},
        {QStringLiteral("sampleName"), name},
        {QStringLiteral("episode"), episode},
        {QStringLiteral("mode"), mode},
        {QStringLiteral("pattern"), pattern}
    };
}

QVector<EpisodeToken> independentEpisodeTokens(const QString &base)
{
    // Ranges and fractional episodes cannot select one integer episode safely.
    static const QRegularExpression bareRange(QStringLiteral(
        "^[0-9]{1,4}[-~–—−][0-9]{1,4}(?=$|[^\\p{L}\\p{N}])"));
    if (bareRange.match(base).hasMatch()) return {};

    auto tokens = episodeTokens(base);
    static const QRegularExpression rangeSuffix(QStringLiteral(
        "^\\s*[-~–—−]\\s*(?:(?:episode|ep|e)\\s*)?[0-9]{1,4}(?=$|[^\\p{L}\\p{N}])"));
    for (const auto &token : tokens)
        if (rangeSuffix.match(base.mid(token.start + token.length)).hasMatch()) return {};
    if (!tokens.isEmpty()) return tokens;
    static const QRegularExpression fraction(QStringLiteral("[0-9]+\\.[0-9]+"));
    if (fraction.match(base).hasMatch()) return {};

    // Plain numbered files and titles separated by spaces, dots or underscores
    // are common in a source's episode folder. Retain ambiguity if several
    // standalone numbers exist, instead of choosing whichever equals the target.
    static const QRegularExpression plain(QStringLiteral(
        "(?<![\\p{L}\\p{N}])([0-9]{1,4})(?=$|[^\\p{L}\\p{N}]|v[0-9]+(?:$|[^\\p{L}\\p{N}]))(?!\\.[0-9])"));
    auto matches = plain.globalMatch(base);
    while (matches.hasNext())
    {
        const auto match = matches.next();
        if (insideTag(base, match.capturedStart(1))) continue;
        tokens.push_back({match.capturedStart(1), match.capturedLength(1),
                         match.captured(1).toInt()});
    }
    return tokens;
}

QString releaseIdentity(const QString &normalized, qsizetype leaf,
                        qsizetype extension, const EpisodeToken &token)
{
    const QString base = normalized.mid(leaf, extension - leaf);
    QString prefix = base.left(token.start);
    QString suffix = base.mid(token.start + token.length);
    QString season;
    static const QRegularExpression seasonMarker(
        QStringLiteral("(?<![\\p{L}\\p{N}])s([0-9]{1,2})[ ._-]*e[ ._-]*$"));
    const auto seasonMatch = seasonMarker.match(prefix);
    if (seasonMatch.hasMatch())
    {
        season = QString::number(seasonMatch.captured(1).toInt());
        prefix.truncate(seasonMatch.capturedStart());
    }
    else
    {
        static const QRegularExpression marker(QStringLiteral(
            "(?<![\\p{L}\\p{N}])(?:episode|ep|e)[ ._-]*$|第$"));
        prefix.remove(marker);
    }
    // The brackets around an episode number are not a release-group tag.
    if (prefix.endsWith(QLatin1Char('['))) prefix.chop(1);
    if (suffix.startsWith(QLatin1Char(']')) || suffix.startsWith(QStringLiteral("話")))
        suffix.remove(0, 1);

    QStringList tags;
    static const QRegularExpression tag(QStringLiteral("\\[([^\\]]*)\\]|\\(([^)]*)\\)"));
    static const QRegularExpression crc(QStringLiteral("^[0-9a-f]{8}$"));
    const auto extractTags = [&tags](QString &text, bool trailing) {
        auto matches = tag.globalMatch(text);
        while (matches.hasNext())
        {
            const auto match = matches.next();
            const auto value = match.captured(1).isNull() ?
                match.captured(2) : match.captured(1);
            // A leading hexadecimal tag may be the actual group name.
            if (!trailing || !crc.match(value).hasMatch()) tags.append(value);
        }
        text.remove(tag);
    };
    extractTags(prefix, false);
    extractTags(suffix, true);
    std::sort(tags.begin(), tags.end());
    tags.removeDuplicates();

    static const QRegularExpression separators(QStringLiteral("[ ._-]+"));
    prefix.replace(separators, QStringLiteral(" "));
    prefix = prefix.trimmed();

    // Keep explicit revisions and common language/track suffixes. Free text
    // following the episode is its title, not a mandatory filename template.
    static const QRegularExpression revision(QStringLiteral("(?<![\\p{L}\\p{N}])v([0-9]+)(?=$|[^\\p{L}\\p{N}])"));
    const auto revisionMatch = revision.match(suffix);
    static const QRegularExpression metadata(QStringLiteral(
        "((?:\\.(?:[a-z]{2,3}(?:[-_][a-z]{2,4})?|japanese|english|dialogue|dialog|signs|songs|full|forced|sdh|cc|hearing[-_]impaired))+)\\s*$"));
    const auto metadataMatch = metadata.match(suffix);
    static const QRegularExpression technical(QStringLiteral(
        "(?<![\\p{L}\\p{N}])(?:[0-9]{3,4}p|4k|bd|bdrip|bluray|webrip|web[-_]dl|hdtv|x26[45]|h26[45]|hevc|avc|10bit|8bit|aac|flac)(?![\\p{L}\\p{N}])"));
    static const QRegularExpression marker(QStringLiteral(
        "^(?:japanese|english|jpn|eng|ja|jp|en|dialogue|dialog|signs|songs|full|forced|sdh|cc|v[0-9]+)$"));
    QStringList technicalTags;
    auto technicalMatches = technical.globalMatch(suffix);
    while (technicalMatches.hasNext()) technicalTags.append(technicalMatches.next().captured());
    // Only a trailing sequence of explicit track/language markers identifies
    // a variant. Ordinary episode titles such as "Full Moon" stay free text.
    static const QRegularExpression markerSeparators(QStringLiteral("[ ._&\\-]+"));
    const auto trailing = suffix.split(markerSeparators, Qt::SkipEmptyParts);
    for (qsizetype index = trailing.size(); index > 0; --index)
    {
        const auto &value = trailing.at(index - 1);
        const auto technicalMatch = technical.match(value);
        if (!marker.match(value).hasMatch() &&
            (!technicalMatch.hasMatch() || technicalMatch.capturedLength() != value.size())) break;
        technicalTags.append(value);
    }
    std::sort(technicalTags.begin(), technicalTags.end());
    technicalTags.removeDuplicates();
    // Known suffix markers have the same identity with dots or whitespace.
    // Keep unknown dot suffixes conservative instead of discarding them.
    QStringList otherMetadata;
    for (const auto &value : metadataMatch.captured(1).split(QLatin1Char('.'), Qt::SkipEmptyParts))
    {
        const auto match = technical.match(value);
        if (!marker.match(value).hasMatch() &&
            (!match.hasMatch() || match.capturedLength() != value.size())) otherMetadata.append(value);
    }
    QJsonArray jsonTags;
    for (const auto &value : tags) jsonTags.append(value);
    const QJsonObject identity{
        {QStringLiteral("directory"), normalized.left(leaf)},
        {QStringLiteral("title"), prefix},
        {QStringLiteral("extension"), normalized.mid(extension + 1)},
        {QStringLiteral("season"), season},
        {QStringLiteral("tags"), jsonTags},
        {QStringLiteral("revision"), revisionMatch.hasMatch() ? revisionMatch.captured(1) : QString()},
        {QStringLiteral("metadata"), otherMetadata.join(QLatin1Char('.'))},
        {QStringLiteral("technical"), QJsonArray::fromStringList(technicalTags)}
    };
    return QString::fromUtf8(QJsonDocument(identity).toJson(QJsonDocument::Compact));
}

} // namespace

QVariantMap SubtitleRelease::fromFile(const QString &name, int episode)
{
    const QString normalized = normalizedName(name);
    const qsizetype leaf = leafStart(normalized);
    const qsizetype extension = normalized.lastIndexOf(QLatin1Char('.'));
    if (normalized.isEmpty() || extension <= leaf || extension == normalized.size() - 1)
        return {};
    QString mode = QStringLiteral("exact");
    QString pattern = normalized;
    const auto tokens = independentEpisodeTokens(normalized.mid(leaf, extension - leaf));
    if (episode > 0 && tokens.size() == 1 && tokens.front().number == episode)
    {
        mode = QStringLiteral("release");
        pattern = releaseIdentity(normalized, leaf, extension, tokens.front());
    }
    return {{QStringLiteral("version"), 2}, {QStringLiteral("valid"), true},
            {QStringLiteral("sampleName"), name}, {QStringLiteral("episode"), episode},
            {QStringLiteral("mode"), mode}, {QStringLiteral("pattern"), pattern}};
}

bool SubtitleRelease::matches(const QString &name, int episode, const QVariantMap &rule)
{
    const int version = rule.value(QStringLiteral("version")).toInt();
    if ((version != 1 && version != 2) ||
        !rule.value(QStringLiteral("valid")).toBool()) return false;

    const int sourceEpisode = rule.value(QStringLiteral("episode")).toInt();
    const QString sample = rule.value(QStringLiteral("sampleName")).toString();
    const auto saved = version == 1 ? legacyRule(sample, sourceEpisode) : fromFile(sample, sourceEpisode);
    if (saved.isEmpty() ||
        saved.value(QStringLiteral("pattern")) != rule.value(QStringLiteral("pattern")) ||
        saved.value(QStringLiteral("mode")) != rule.value(QStringLiteral("mode")))
        return false;

    // Old settings retain their validated source identity, while gaining the
    // independent episode parser. No rewrite of the library file is required.
    const auto source = version == 1 ? fromFile(sample, sourceEpisode) : saved;
    if (source.value(QStringLiteral("mode")) == QStringLiteral("exact"))
        return episode == sourceEpisode &&
               normalizedName(name) == source.value(QStringLiteral("pattern")).toString();

    const auto candidate = fromFile(name, episode);
    return candidate.value(QStringLiteral("mode")) == QStringLiteral("release") &&
           candidate.value(QStringLiteral("pattern")) == source.value(QStringLiteral("pattern"));
}
