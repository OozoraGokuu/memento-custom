////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2024 Ripose
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

#include "dict/mecabquerygenerator.h"

#include <QDebug>
#include <QDir>
#include <QtGlobal>

#include "util/utils.h"

MeCabQueryGenerator::MeCabQueryGenerator()
{
#if defined(Q_OS_WIN) || defined(MEMENTO_BUNDLE)
    // Pass separate arguments: the string overload splits paths at spaces.
    // Windows uses the executable's UTF-8 active-code-page manifest.
    const QByteArray dictionary = (DirectoryUtils::getMecabDictionary() + "ipadic").toUtf8();
    const QByteArray config = dictionary + "/dicrc";
    const char *arguments[] = {"memento", "-d", dictionary.constData(), "-r", config.constData()};
    m_tagger.reset(MeCab::createTagger(5, const_cast<char **>(arguments)));
#else
    m_tagger.reset(MeCab::createTagger(""));
#endif
    if (m_tagger == nullptr)
    {
        qWarning("Could not create MeCab::Tagger: %s", MeCab::getTaggerError());
    }
}

/* End Constructor */
/* Begin Query Generator */

std::vector<SearchQuery> MeCabQueryGenerator::generateQueries(
    const QString &text) const
{
    if (!valid() || text.isEmpty())
    {
        return {};
    }

    std::unique_ptr<MeCab::Lattice> lattice(MeCab::createLattice());
    QByteArray textArr = text.toUtf8();
    lattice->set_sentence(textArr);
    if (!m_tagger->parse(lattice.get()))
    {
        qWarning("Cannot access MeCab: %s", MeCab::getLastError());
        return {};
    }
    std::vector<MeCabQuery> mecabQueries =
        generateQueriesHelper(lattice->bos_node()->next);

    std::vector<SearchQuery> queries;
    queries.reserve(mecabQueries.size());
    std::copy(
        std::begin(mecabQueries), std::end(mecabQueries),
        std::back_inserter(queries)
    );
    return queries;
}

std::vector<MeCabQueryGenerator::MeCabQuery>
MeCabQueryGenerator::generateQueriesHelper(const MeCab::Node *node)
{
    std::vector<MeCabQuery> queries;
    while (node)
    {
        QString deconj = extractDeconjugation(node);
        QString surface = extractSurface(node);
        QString surfaceClean = extractCleanSurface(node);
        if (deconj != "*")
        {
            MeCabQuery query;
            query.deconj = deconj;
            query.surface = surface;
            query.surfaceClean = surfaceClean;
            query.source = SearchQuery::Source::mecab;
            queries.emplace_back(std::move(query));
        }

        if (node->next)
        {
            std::vector<MeCabQuery> subQueries =
                generateQueriesHelper(node->next);
            for (MeCabQuery &p : subQueries)
            {
                p.deconj.prepend(surfaceClean);
                p.surface.prepend(surface);
                p.surfaceClean.prepend(surfaceClean);
                queries.emplace_back(std::move(p));
            }
        }

        node = node->bnext;
    }
    return queries;
}

inline QString MeCabQueryGenerator::extractDeconjugation(
    const MeCab::Node *node)
{
    constexpr int WORD_INDEX{6};
    QStringList features = QString::fromUtf8(node->feature).split(',');
    if (features.size() <= WORD_INDEX)
    {
        return "";
    }
    return features[WORD_INDEX];
}

inline QString MeCabQueryGenerator::extractSurface(const MeCab::Node *node)
{
    const char *rawText = node->surface;
    rawText -= node->rlength - node->length;
    return QString::fromUtf8(rawText, node->rlength);
}

inline QString MeCabQueryGenerator::extractCleanSurface(const MeCab::Node *node)
{
    return QString::fromUtf8(node->surface, node->length);
}

/* End Query Generator */
