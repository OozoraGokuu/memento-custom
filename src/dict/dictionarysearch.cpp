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
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Memento.  If not, see <https://www.gnu.org/licenses/>.
//
////////////////////////////////////////////////////////////////////////////////

#include "dict/dictionarysearch.h"

#include "dict/dictionarysearchcontroller.h"

/* Begin Constructor/Destructor */

DictionarySearch::DictionarySearch(QObject *parent) : QObject(parent)
{
    m_historyTimer.setSingleShot(true);
    m_historyTimer.setInterval(500);
    connect(&m_historyTimer, &QTimer::timeout, this, [this] {
        m_historySettled = true;
        finishHistoryLookup();
    });
}

DictionarySearch::~DictionarySearch()
{
    clearResults();
}

/* End Constructor/Destructor */
/* Begin Search Methods */

void DictionarySearch::searchTerms(
    const QString &query, const QString &text, qsizetype index)
{
    const quint64 historyId = beginHistoryLookup(true, text, index);
    searchTermsAsync(query, text, index, historyId);
}

QCoro::Task<void> DictionarySearch::searchTermsAsync(
    const QString &query, const QString &text, qsizetype index, quint64 historyId)
{
    QPointer<DictionarySearch> dictionarySearch{this};
    const quint64 termsSearchId = ++m_termsSearchId;

    QList<Term *> terms = co_await DictionarySearchController::instance()
        ->searchTermsAsync(query, text, index);

    /* Make sure this object hasn't been deleted and the search isn't stale */
    if (dictionarySearch == nullptr || termsSearchId != m_termsSearchId)
    {
        qDeleteAll(terms);
        terms.clear();
        co_return;
    }

    for (Term *term : terms)
    {
        term->setParent(this);
    }

    std::swap(m_terms, terms);
    if (historyId == m_historyId)
    {
        m_historyPendingTerms = false;
        if (!m_terms.isEmpty())
        {
            m_lookupWord = m_terms.first()->expression();
            m_lookupReading = m_terms.first()->reading();
        }
    }
    for (Term *term : terms)
    {
        term->deleteLater();
    }
    terms.clear();
    emit termsChanged();
    // A result observer can close/destroy the popup or start another lookup.
    if (dictionarySearch && historyId == m_historyId) finishHistoryLookup();
}

void DictionarySearch::searchKanji(
    const QString &character, const QString &text, qsizetype index)
{
    const quint64 historyId = beginHistoryLookup(false, text, index);
    searchKanjiAsync(character, text, index, historyId);
}

QCoro::Task<void> DictionarySearch::searchKanjiAsync(
    const QString &character, const QString &text, qsizetype index, quint64 historyId)
{
    QPointer<DictionarySearch> dictionarySearch{this};
    const quint64 kanjiSearchId = ++m_kanjiSearchId;

    Kanji *kanji = co_await DictionarySearchController::instance()
        ->searchKanjiAsync(character, text, index);

    /* Make sure this object hasn't been deleted and the search isn't stale */
    if (dictionarySearch == nullptr || kanjiSearchId != m_kanjiSearchId)
    {
        delete kanji;
        kanji = nullptr;
        co_return;
    }

    if (kanji != nullptr)
    {
        kanji->setParent(this);
    }

    std::swap(m_kanji, kanji);
    if (historyId == m_historyId)
    {
        m_historyPendingKanji = false;
        if (m_kanji) m_lookupKanji = m_kanji->character();
    }
    if (kanji != nullptr)
    {
        kanji->deleteLater();
        kanji = nullptr;
    }
    emit kanjiChanged();
    if (dictionarySearch && historyId == m_historyId) finishHistoryLookup();
}

/* End Search Methods */
/* Begin Properties */
/* Begin Clear Methods */

void DictionarySearch::clearResults()
{
    clearTerms();
    clearKanji();
}

void DictionarySearch::clearTerms()
{
    cancelHistoryLookup();
    ++m_termsSearchId;
    clearTermsLater();
}

void DictionarySearch::clearTermsLater()
{
    if (m_terms.isEmpty())
    {
        return;
    }
    QList<Term *> terms = std::move(m_terms);
    m_terms.clear();
    emit termsChanged();
    for (Term *term : terms)
    {
        term->deleteLater();
    }
}

void DictionarySearch::clearKanji()
{
    cancelHistoryLookup();
    ++m_kanjiSearchId;
    clearKanjiLater();
}

void DictionarySearch::clearKanjiLater()
{
    if (m_kanji)
    {
        m_kanji->deleteLater();
        m_kanji = nullptr;
        emit kanjiChanged();
    }
}

/* End Clear Methods */
/* Begin Properties */

QQmlListProperty<Term> DictionarySearch::termsQml()
{
    return QQmlListProperty<Term>(this, &m_terms);
}

Kanji *DictionarySearch::kanji() const noexcept
{
    return m_kanji;
}

void DictionarySearch::setHistoryContext(const QVariantMap &context)
{
    if (m_historyContext == context) return;
    m_historyContext = context;
    emit historyContextChanged();
}

quint64 DictionarySearch::beginHistoryLookup(bool terms, const QString &text,
                                            qsizetype index)
{
    // QML requests terms and kanji consecutively for one hovered position.
    // Only that immediate pair shares history; later searches invalidate it.
    if (!terms && m_historyCanPairKanji && m_lookupText == text &&
        m_lookupIndex == index && m_lookupContext == m_historyContext)
    {
        m_historyCanPairKanji = false;
        m_historyPendingKanji = true;
        return m_historyId;
    }
    cancelHistoryLookup();
    m_lookupText = text;
    m_lookupIndex = index;
    m_lookupContext = m_historyContext;
    m_historyPendingTerms = terms;
    m_historyPendingKanji = !terms;
    m_historyCanPairKanji = terms;
    m_historyTimer.start();
    const quint64 historyId = m_historyId;
    if (terms)
    {
        QTimer::singleShot(0, this, [this, historyId] {
            if (historyId == m_historyId) m_historyCanPairKanji = false;
        });
    }
    return historyId;
}

void DictionarySearch::cancelHistoryLookup()
{
    ++m_historyId;
    m_historyTimer.stop();
    m_historyPendingTerms = false;
    m_historyPendingKanji = false;
    m_historyCanPairKanji = false;
    m_historySettled = false;
    m_historyEmitted = false;
    m_lookupWord.clear();
    m_lookupReading.clear();
    m_lookupKanji.clear();
}

void DictionarySearch::finishHistoryLookup()
{
    if (!m_historySettled || m_historyEmitted ||
        m_historyPendingTerms || m_historyPendingKanji) return;
    const QString word = m_lookupWord.isEmpty() ? m_lookupKanji : m_lookupWord;
    if (word.isEmpty()) return;
    // Keep arguments stable if a direct signal listener clears/reuses us.
    const QString reading = m_lookupReading;
    const QString text = m_lookupText;
    const QVariantMap context = m_lookupContext;
    m_historyEmitted = true;
    emit lookupCompleted(word, reading, text, context);
}

/* End Properties */
