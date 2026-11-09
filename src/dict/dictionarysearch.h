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

#pragma once

#include <QObject>

#include <QQmlListProperty>
#include <QTimer>
#include <QVariantMap>

#ifdef MEMENTO_SYSTEM_QCORO
#include <QCoroTask>
#else
#include <qcoro/qcorotask.h>
#endif // MEMENTO_SYSTEM_QCORO

#include "dict/data/term.h"
#include "dict/data/kanji.h"

/**
 * @brief Handles a single dictionary search.
 */
class DictionarySearch : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QVariantMap historyContext READ historyContext WRITE setHistoryContext
               NOTIFY historyContextChanged)

    Q_PROPERTY(
        QQmlListProperty<Term> terms
        READ termsQml
        NOTIFY termsChanged
    )

    Q_PROPERTY(
        Kanji *kanji
        READ kanji
        NOTIFY kanjiChanged
    )

public:
    DictionarySearch(QObject *parent = nullptr);
    virtual ~DictionarySearch();

    QVariantMap historyContext() const { return m_historyContext; }
    void setHistoryContext(const QVariantMap &context);

    /**
     * @brief Searches for all terms. Popuplates the terms property.
     *
     * @param query The query to look up. Only matches terms from the start of
     * the query.
     * @param text The text containing the query.
     * @param index The index into text where query start from.
     */
    Q_INVOKABLE void searchTerms(
        const QString &query, const QString &text, qsizetype index);

    /**
     * @brief Search for all kanji in a query. Populates the kanji property.
     *
     * @param character The character to search for.
     * @param text The text the kanji originates from.
     * @param index The index into the text where the kanji
     */
    Q_INVOKABLE void searchKanji(
        const QString &query, const QString &text, qsizetype index);

    /**
     * @brief Clears the result of the last search.
     */
    Q_INVOKABLE void clearResults();

    /**
     * @brief Clears the terms property.
     */
    Q_INVOKABLE void clearTerms();

    /**
     * @brief Clears the kanji property.
     */
    Q_INVOKABLE void clearKanji();

    /**
     * @brief Get the terms found from the last search.
     *
     * @return The terms found from the last search.
     */
    [[nodiscard]]
    QQmlListProperty<Term> termsQml();

    /**
     * @brief Get the kanji found from the last search.
     *
     * @return The kanji found in the last search.
     */
    [[nodiscard]]
    Kanji *kanji() const noexcept;

signals:
    void historyContextChanged();
    /** One settled successful lookup; paired term/kanji searches count once. */
    void lookupCompleted(const QString &word, const QString &reading,
                         const QString &text, const QVariantMap &context);
    /**
     * @brief Emitted when terms are changed.
     */
    void termsChanged();

    /**
     * @brief Emitted when the kanji are changed.
     */
    void kanjiChanged();

private:
    /**
     * @brief Searches for all terms. Popuplates the terms property.
     *
     * @param query The query to look up. Only matches terms from the start of
     * the query.
     * @param text The text containing the query.
     * @param index The index into text where query start from.
     * @return An awaitable task.
     */
    QCoro::Task<void> searchTermsAsync(
        const QString &query, const QString &text, qsizetype index, quint64 historyId);

    /**
     * @brief Search for all kanji in a query. Populates the kanji property.
     *
     * @param character The character to search for.
     * @param text The text the kanji originates from.
     * @param index The index into the text where the kanji
     * @return An awaitable task.
     */
    QCoro::Task<void> searchKanjiAsync(
        const QString &character, const QString &text, qsizetype index, quint64 historyId);

    /**
     * @brief Clears term results and schedules existing objects for deletion.
     */
    void clearTermsLater();

    /**
     * @brief Clears kanji results and schedules the existing object for
     * deletion.
     */
    void clearKanjiLater();

    quint64 beginHistoryLookup(bool terms, const QString &text, qsizetype index);
    void cancelHistoryLookup();
    void finishHistoryLookup();

    QVariantMap m_historyContext;
    QVariantMap m_lookupContext;
    QTimer m_historyTimer;
    QString m_lookupText;
    QString m_lookupWord;
    QString m_lookupReading;
    QString m_lookupKanji;
    qsizetype m_lookupIndex{-1};
    quint64 m_historyId{0};
    bool m_historyPendingTerms{false};
    bool m_historyPendingKanji{false};
    bool m_historyCanPairKanji{false};
    bool m_historySettled{false};
    bool m_historyEmitted{false};

    /* IDs used to ignore stale async search completions. */
    quint64 m_termsSearchId{0};
    quint64 m_kanjiSearchId{0};

    /* The terms of the last search */
    QList<Term *> m_terms;

    /* The kanji of the last search */
    Kanji *m_kanji{nullptr};
};
