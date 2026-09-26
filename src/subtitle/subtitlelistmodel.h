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

#include <QAbstractListModel>

#include <vector>

#include <QItemSelectionModel>

#include "subtitle/subtitleentry.h"

class Context;

/**
 * @brief A model of subtitles.
 */
class SubtitleListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

    Q_PROPERTY(
        QItemSelectionModel *selectionModel
        READ selectionModel
        CONSTANT
    )

    Q_PROPERTY(
        QString activeText
        READ activeText
        NOTIFY activeSubtitleChanged
    )

    Q_PROPERTY(
        double activeStart
        READ activeStart
        NOTIFY activeSubtitleChanged
    )

    Q_PROPERTY(
        double activeEnd
        READ activeEnd
        NOTIFY activeSubtitleChanged
    )

    Q_PROPERTY(
        bool nativeReady
        READ nativeReady
        NOTIFY nativeReadyChanged
    )

    Q_PROPERTY(
        bool fullTimelineReady
        READ fullTimelineReady
        NOTIFY fullTimelineReadyChanged
    )

public:
    bool loading() const { return m_loading; }
    void setLoading(bool value) { if (m_loading != value) { m_loading = value; emit loadingChanged(); } }
    enum ItemRoles
    {
        TextRole = Qt::UserRole + 1,
        StartRole,
        EndRole,
    };
    Q_ENUM(ItemRoles)

    /**
     * @brief Construct a SubtitleListModel.
     *
     * @param context The application context.
     * @param parent The parent of this object.
     */
    explicit SubtitleListModel(Context *context, QObject *parent = nullptr);
    virtual ~SubtitleListModel() = default;

    /**
     * @brief Get the selection model.
     *
     * @return The item selection model.
     */
    [[nodiscard]]
    QItemSelectionModel *selectionModel() const noexcept;

    /**
     * @brief Get the text of the subtitles active at the last selected
     * position.
     *
     * Overlapping subtitles are joined with newlines in model order.
     *
     * @return The active subtitle text, or an empty string when no subtitle is
     * active.
     */
    [[nodiscard]]
    const QString &activeText() const noexcept;

    /**
     * @brief Get the earliest start time of the active subtitles.
     *
     * @return The earliest active start time, or 0 when no subtitle is active.
     */
    [[nodiscard]]
    double activeStart() const noexcept;

    /**
     * @brief Get the latest end time of the active subtitles.
     *
     * @return The latest active end time, or 0 when no subtitle is active.
     */
    [[nodiscard]]
    double activeEnd() const noexcept;

    /**
     * @brief Get whether this model is authoritative for the active subtitle.
     *
     * Once native data has been captured, an empty activeText represents a
     * legitimate cue gap and consumers must not fall back to stale mpv text.
     *
     * @return true after a cue is captured or a non-empty parsed list is set.
     */
    [[nodiscard]]
    bool nativeReady() const noexcept;

    /**
     * @brief Get whether items() contains the complete subtitle timeline.
     *
     * Incrementally observed embedded cues are authoritative for the current
     * frame but are not a complete source for context-video generation.
     *
     * @return true only after a complete external subtitle file was parsed.
     */
    [[nodiscard]]
    bool fullTimelineReady() const noexcept;


    /**
     * @brief Returns the number of rows under the parent.
     *
     * @param parent The parent to get the number of rows under.
     * @return The number of rows below parent.
     */
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;

    /**
     * @brief Gets an element from the model.
     *
     * @param index The index of the row.
     * @param role The role to get from that row.
     * @return The requested role. Empty QVariant on error.
     */
    QVariant data(
        const QModelIndex &index, int role = Qt::DisplayRole) const override;

    /**
     * @brief Sets the data role at the given index.
     *
     * @param index The index/row to set.
     * @param value The value to set.
     * @param role The data role to update.
     * @return true if the data was successfully set,
     * @return false otherwise.
     */
    bool setData(
        const QModelIndex &index,
        const QVariant &value,
        int role = Qt::EditRole) override;

    /**
     * @brief The name of each role supported by this model.
     *
     * @return A hashtable of roles and names.
     */
    QHash<int, QByteArray> roleNames() const override;

    /**
     * @brief Add a subtitle to the model if it doesn't already exist.
     *
     * @param text Text of the subtitle.
     * @param start Start time of the subtitle.
     * @param end End time of the subtitle.
     * @return The row index of the subtitle.
     */
    Q_INVOKABLE qsizetype addSubtitle(
        const QString &text, double start, double end);

    /**
     * @brief Select all items a time position overlaps with and deselects all
     * subtitles that don't overlap.
     *
     * @param position The position to test.
     */
    Q_INVOKABLE void selectPosition(double position);

    /** Playback-time target, applying delay once; -1 means no adjacent cue.
     * Previous restarts the latest cue after 0.4s, otherwise skips back.
     */
    Q_INVOKABLE double adjacentSubtitleStart(
        double position, int direction, double delay = 0) const;

    /**
     * @brief Get a list of rows that contain a search string.
     *
     * @param str The string to search for.
     * @param ignoreWhitespace true if results should match across whitespace,
     * false otherwise.
     * @return A list of rows containing the search string.
     */
    [[nodiscard]]
    Q_INVOKABLE QList<int> find(
        QString str, bool ignoreWhitespace = false) const;

    /**
     * @brief Clear out all elements in the model.
     */
    Q_INVOKABLE void clear();

    /**
     * @brief Set the items of the model and block adds.
     *
     * @param entries The entries to set.
     */
    void setItems(std::vector<SubtitleEntry> &&entries);

    /**
     * @brief Get the backing list of items.
     *
     * @return The backing list of items.
     */
    [[nodiscard]]
    const std::vector<SubtitleEntry> &items() const noexcept;

signals:
    void loadingChanged();
    /**
     * @brief Emitted when native subtitle data becomes available or is reset.
     */
    void nativeReadyChanged(bool value);

    /**
     * @brief Emitted when complete-timeline availability changes.
     */
    void fullTimelineReadyChanged(bool value);

    /**
     * @brief Emitted when the active subtitle text or timing changes.
     */
    void activeSubtitleChanged();

    /**
     * @brief Emitted when a new position is selected.
     *
     * @param start The starting row selected (inclusive).
     * @param end The ending row selected (inclusive).
     */
    void positionSelected(int start, int end);

private:
    bool m_loading{false};
    /**
     * @brief Rebuild the prefix maximum-end index used by selectPosition().
     */
    void rebuildTimingIndex();

    /**
     * @brief Apply a new set of active rows and update the exposed subtitle.
     *
     * @param rows Active model rows in ascending order.
     */
    void setActiveRows(std::vector<int> rows);

    /**
     * @brief Refresh the active subtitle properties from m_activeRows.
     */
    void updateActiveSubtitle();

    /**
     * @brief Clear the active rows and exposed subtitle properties.
     */
    void resetActiveSubtitle();

    /**
     * @brief Set whether native subtitle data is authoritative.
     *
     * @param value The new readiness state.
     */
    void setNativeReady(bool value);

    /**
     * @brief Set whether the entire track timeline is available.
     */
    void setFullTimelineReady(bool value);

    /* The application context */
    Context *m_context{nullptr};

    /* The selection model */
    QItemSelectionModel *m_selectionModel{new QItemSelectionModel(this, this)};

    /* true to block addSubtitle() calls, false to accept */
    bool m_blockAdds{false};

    /* true once native cue data is authoritative for this track */
    bool m_nativeReady{false};

    /* true only when m_items is a complete parsed track */
    bool m_fullTimelineReady{false};

    /* List of items sorted by start time */
    std::vector<SubtitleEntry> m_items;

    /* Maximum end time in m_items[0..i], used to prune position lookups */
    std::vector<double> m_prefixMaximumEnds;

    /* Rows active at the last selected position, sorted in model order */
    std::vector<int> m_activeRows;

    /* Text and timing exposed to QML for the active subtitles */
    QString m_activeText;
    double m_activeStart{0};
    double m_activeEnd{0};

    /* Last requested position, retained across asynchronous model population */
    double m_lastPosition{0};
    bool m_hasLastPosition{false};
};
