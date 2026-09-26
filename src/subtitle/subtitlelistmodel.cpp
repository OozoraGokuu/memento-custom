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

#include "subtitle/subtitlelistmodel.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QItemSelection>
#include <QRegularExpression>

#include "state/context.h"

SubtitleListModel::SubtitleListModel(Context *context, QObject *parent) :
    QAbstractListModel(parent),
    m_context(context)
{

}

QItemSelectionModel *SubtitleListModel::selectionModel() const noexcept
{
    return m_selectionModel;
}

const QString &SubtitleListModel::activeText() const noexcept
{
    return m_activeText;
}

double SubtitleListModel::activeStart() const noexcept
{
    return m_activeStart;
}

double SubtitleListModel::activeEnd() const noexcept
{
    return m_activeEnd;
}

bool SubtitleListModel::nativeReady() const noexcept
{
    return m_nativeReady;
}

bool SubtitleListModel::fullTimelineReady() const noexcept
{
    return m_fullTimelineReady;
}

int SubtitleListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
    {
        return 0;
    }
    return m_items.size();
}

QVariant SubtitleListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_items.size()))
    {
        return QVariant();
    }

    const SubtitleEntry &item = m_items[index.row()];
    switch (role)
    {
        case TextRole:
            return item.text;
        case StartRole:
            return item.start;
        case EndRole:
            return item.end;
    }

    return QVariant();
}

bool SubtitleListModel::setData(
    const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_items.size()))
    {
        return false;
    }

    SubtitleEntry &item = m_items[index.row()];
    switch (role)
    {
        case TextRole:
        {
            QString text = value.toString();
            if (item.text == text)
            {
                return true;
            }
            item.text = std::move(text);
            break;
        }

        case StartRole:
        {
            double start = value.toDouble();
            if (item.start == start)
            {
                return true;
            }

            m_selectionModel->clear();
            resetActiveSubtitle();
            beginResetModel();
            item.start = start;
            std::stable_sort(
                std::begin(m_items),
                std::end(m_items),
                [] (const SubtitleEntry &lhs, const SubtitleEntry &rhs)
                {
                    return lhs.start < rhs.start;
                }
            );
            rebuildTimingIndex();
            endResetModel();

            if (m_hasLastPosition)
            {
                selectPosition(m_lastPosition);
            }
            else
            {
                updateActiveSubtitle();
            }
            return true;
        }

        case EndRole:
        {
            double end = value.toDouble();
            if (item.end == end)
            {
                return true;
            }
            item.end = end;
            rebuildTimingIndex();
            break;
        }

        default:
            return false;
    }

    emit dataChanged(index, index, {role});

    if (role == TextRole)
    {
        if (std::binary_search(
            std::begin(m_activeRows), std::end(m_activeRows), index.row()))
        {
            updateActiveSubtitle();
        }
    }
    else if (role == EndRole && m_hasLastPosition)
    {
        selectPosition(m_lastPosition);
        updateActiveSubtitle();
    }
    return true;
}

QHash<int, QByteArray> SubtitleListModel::roleNames() const
{
    return QHash<int, QByteArray>{
        {TextRole, "text"},
        {StartRole, "start"},
        {EndRole, "end"},
    };
}

qsizetype SubtitleListModel::addSubtitle(
    const QString &text, double start, double end)
{
    constexpr double TIME_DELTA = 0.0001;

    if (m_blockAdds || !std::isfinite(start) || !std::isfinite(end) || end <= start)
    {
        return -1;
    }

    auto duplicateIt = std::lower_bound(
        std::begin(m_items),
        std::end(m_items),
        start - TIME_DELTA,
        [] (const SubtitleEntry &entry, double start) -> bool
        {
            return entry.start < start;
        }
    );
    while (duplicateIt != std::end(m_items) &&
           duplicateIt->start <= start + TIME_DELTA)
    {
        if (duplicateIt->text == text)
        {
            const qsizetype duplicateIndex =
                std::distance(std::begin(m_items), duplicateIt);
            if (duplicateIt->end != end)
            {
                duplicateIt->end = end;
                rebuildTimingIndex();
                const QModelIndex changedIndex = createIndex(
                    duplicateIndex, 0);
                emit dataChanged(
                    changedIndex, changedIndex, {EndRole});
                if (m_hasLastPosition)
                {
                    selectPosition(m_lastPosition);
                    updateActiveSubtitle();
                }
            }
            setNativeReady(true);
            return duplicateIndex;
        }
        duplicateIt = std::next(duplicateIt);
    }

    auto itemIt = std::upper_bound(
        std::begin(m_items),
        std::end(m_items),
        start,
        [] (double start, const SubtitleEntry &entry) -> bool
        {
            return start < entry.start;
        }
    );
    std::ptrdiff_t index = std::distance(std::begin(m_items), itemIt);
    beginInsertRows(QModelIndex(), index, index);
    m_items.insert(
        itemIt,
        SubtitleEntry{
            .text = text,
            .start = start,
            .end = end,
        }
    );
    endInsertRows();

    for (int &activeRow : m_activeRows)
    {
        if (activeRow >= index)
        {
            ++activeRow;
        }
    }
    rebuildTimingIndex();
    setNativeReady(true);
    if (m_hasLastPosition)
    {
        selectPosition(m_lastPosition);
    }
    return index;
}

void SubtitleListModel::selectPosition(double position)
{
    constexpr double TIME_DELTA = 0.0001;

    m_lastPosition = position;
    m_hasLastPosition = true;

    const double adjustedPosition = position + TIME_DELTA;
    auto upperIt = std::lower_bound(
        std::begin(m_items),
        std::end(m_items),
        adjustedPosition,
        [] (const SubtitleEntry &entry, double value)
        {
            return entry.start < value;
        }
    );

    std::vector<int> activeRows;
    activeRows.reserve(m_activeRows.size());
    size_t index = std::distance(std::begin(m_items), upperIt);
    while (index > 0)
    {
        --index;
        if (m_prefixMaximumEnds[index] <= position)
        {
            break;
        }

        if (position < m_items[index].end)
        {
            activeRows.emplace_back(static_cast<int>(index));
        }
    }
    std::reverse(std::begin(activeRows), std::end(activeRows));
    setActiveRows(std::move(activeRows));
}

void SubtitleListModel::rebuildTimingIndex()
{
    m_prefixMaximumEnds.resize(m_items.size());
    double maximumEnd = std::numeric_limits<double>::lowest();
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        maximumEnd = std::max(maximumEnd, m_items[i].end);
        m_prefixMaximumEnds[i] = maximumEnd;
    }
}

void SubtitleListModel::setActiveRows(std::vector<int> rows)
{
    if (m_activeRows == rows)
    {
        return;
    }

    m_activeRows = std::move(rows);

    if (m_activeRows.empty())
    {
        m_selectionModel->clear();
    }
    else
    {
        QItemSelection selection;
        int rangeStart = m_activeRows.front();
        int rangeEnd = rangeStart;
        for (size_t i = 1; i < m_activeRows.size(); ++i)
        {
            if (m_activeRows[i] == rangeEnd + 1)
            {
                rangeEnd = m_activeRows[i];
                continue;
            }

            selection.select(
                createIndex(rangeStart, 0), createIndex(rangeEnd, 0));
            rangeStart = m_activeRows[i];
            rangeEnd = rangeStart;
        }
        selection.select(
            createIndex(rangeStart, 0), createIndex(rangeEnd, 0));

        m_selectionModel->select(
            selection, QItemSelectionModel::ClearAndSelect);
        m_selectionModel->setCurrentIndex(
            createIndex(m_activeRows.front(), 0),
            QItemSelectionModel::NoUpdate
        );
    }

    updateActiveSubtitle();

    if (!m_activeRows.empty())
    {
        emit positionSelected(
            m_activeRows.front(), m_activeRows.back()
        );
    }
}

void SubtitleListModel::updateActiveSubtitle()
{
    QString text;
    double start = 0;
    double end = 0;
    bool first = true;

    for (int row : m_activeRows)
    {
        if (row < 0 || row >= static_cast<int>(m_items.size()))
        {
            continue;
        }

        const SubtitleEntry &item = m_items[row];
        if (!first)
        {
            text += '\n';
        }
        text += item.text;

        if (first)
        {
            start = item.start;
            end = item.end;
            first = false;
        }
        else
        {
            start = std::min(start, item.start);
            end = std::max(end, item.end);
        }
    }

    if (m_activeText == text &&
        m_activeStart == start &&
        m_activeEnd == end)
    {
        return;
    }

    m_activeText = std::move(text);
    m_activeStart = start;
    m_activeEnd = end;
    emit activeSubtitleChanged();
}

void SubtitleListModel::resetActiveSubtitle()
{
    m_activeRows.clear();
    if (m_activeText.isEmpty() && m_activeStart == 0 && m_activeEnd == 0)
    {
        return;
    }

    m_activeText.clear();
    m_activeStart = 0;
    m_activeEnd = 0;
    emit activeSubtitleChanged();
}

void SubtitleListModel::setNativeReady(bool value)
{
    if (m_nativeReady == value)
    {
        return;
    }

    m_nativeReady = value;
    emit nativeReadyChanged(m_nativeReady);
}

void SubtitleListModel::setFullTimelineReady(bool value)
{
    if (m_fullTimelineReady == value)
    {
        return;
    }

    m_fullTimelineReady = value;
    emit fullTimelineReadyChanged(m_fullTimelineReady);
}

QList<int> SubtitleListModel::find(QString str, bool ignoreWhitespace) const
{
    static const QRegularExpression REGEX_REMOVE_WHITESPACE("\\s*");

    if (ignoreWhitespace)
    {
        str.remove(REGEX_REMOVE_WHITESPACE);
    }
    if (str.isEmpty())
    {
        return {};
    }

    const auto removeRegex = Settings::subtitleRegex(m_context->settings()->searchRemoveRegex());

    QList<int> results;
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        QString subtitleText = m_items[i].text;
        if (removeRegex.isValid()) subtitleText.remove(removeRegex);
        if (ignoreWhitespace)
        {
            subtitleText.remove(REGEX_REMOVE_WHITESPACE);
        }
        if (subtitleText.contains(str, Qt::CaseInsensitive))
        {
            results.emplaceBack(i);
        }
    }
    return results;
}

void SubtitleListModel::clear()
{
    m_selectionModel->clear();
    resetActiveSubtitle();
    m_blockAdds = false;
    beginResetModel();
    m_items.clear();
    m_prefixMaximumEnds.clear();
    endResetModel();
    m_lastPosition = 0;
    m_hasLastPosition = false;
    setNativeReady(false);
    setFullTimelineReady(false);
}

void SubtitleListModel::setItems(std::vector<SubtitleEntry> &&items)
{
    std::erase_if(items, [](const SubtitleEntry &entry) {
        return !std::isfinite(entry.start) || !std::isfinite(entry.end) || entry.end <= entry.start;
    });
    const bool hasNativeItems = !items.empty();
    m_selectionModel->clear();
    resetActiveSubtitle();
    m_blockAdds = hasNativeItems;
    beginResetModel();
    m_items = std::move(items);
    std::stable_sort(
        std::begin(m_items),
        std::end(m_items),
        [] (const SubtitleEntry &lhs, const SubtitleEntry &rhs)
        {
            return lhs.start < rhs.start;
        }
    );
    rebuildTimingIndex();
    endResetModel();
    if (hasNativeItems)
    {
        setNativeReady(true);
    }
    setFullTimelineReady(hasNativeItems);

    if (m_hasLastPosition)
    {
        selectPosition(m_lastPosition);
    }
}

const std::vector<SubtitleEntry> &SubtitleListModel::items() const noexcept
{
    return m_items;
}

double SubtitleListModel::adjacentSubtitleStart(double position, int direction, double delay) const
{
    if (!std::isfinite(position) || !std::isfinite(delay) || direction == 0 ||
        m_items.empty()) return -1;
    const double local = position - delay;
    if (!std::isfinite(local)) return -1;
    // A tiny tolerance avoids repeatedly landing on the same cue after seeking.
    const double boundary = local + (direction > 0 ? 0.01 : -0.4);
    auto it = std::upper_bound(m_items.begin(), m_items.end(), boundary,
        [](double time, const SubtitleEntry &entry) { return time < entry.start; });
    if (direction < 0)
    {
        if (it == m_items.begin()) return -1;
        --it;
    }
    else if (it == m_items.end()) return -1;
    const double target = it->start + delay;
    return std::isfinite(target) ? std::max(0.0, target) : -1;
}
