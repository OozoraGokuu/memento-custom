#include "subtitlerelease.h"

#include <QRegularExpression>
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

} // namespace

QVariantMap SubtitleRelease::fromFile(const QString &name, int episode)
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

bool SubtitleRelease::matches(const QString &name, int episode, const QVariantMap &rule)
{
    if (rule.value(QStringLiteral("version")).toInt() != 1 ||
        !rule.value(QStringLiteral("valid")).toBool()) return false;

    const int sourceEpisode = rule.value(QStringLiteral("episode")).toInt();
    const auto source = fromFile(rule.value(QStringLiteral("sampleName")).toString(),
                                 sourceEpisode);
    if (source.isEmpty() ||
        source.value(QStringLiteral("pattern")) != rule.value(QStringLiteral("pattern")) ||
        source.value(QStringLiteral("mode")) != rule.value(QStringLiteral("mode")))
        return false;

    if (source.value(QStringLiteral("mode")) == QStringLiteral("exact"))
        return episode == sourceEpisode &&
               normalizedName(name) == source.value(QStringLiteral("pattern")).toString();

    const auto candidate = fromFile(name, episode);
    return candidate.value(QStringLiteral("mode")) == QStringLiteral("template") &&
           candidate.value(QStringLiteral("pattern")) == source.value(QStringLiteral("pattern"));
}
