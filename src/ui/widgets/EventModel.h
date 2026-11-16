// Virtual table model of the events.
#pragma once

#include "core/Filters.h"
#include "core/Queries.h"
#include "ui/Executor.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QIcon>

#include <optional>
#include <vector>

namespace lh::ui {

class Workspace;

class EventModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    using Column = queries::Column;
    static constexpr int IdentifierRole = Qt::UserRole + 1;

    explicit EventModel(Workspace* workspace, QObject* parent = nullptr);

    void setFilters(const Filters& filters);
    void setSort(Column column, Qt::SortOrder order);
    void reload();
    void clear();
    void setAlertOnDate(bool active);

    [[nodiscard]] qint64 total() const { return m_ids ? static_cast<qint64>(m_ids->size()) : 0; }
    [[nodiscard]] qint64 identifier(int row) const;
    [[nodiscard]] std::optional<queries::TableRow> row(int row) const;
    [[nodiscard]] Column sortColumn() const { return m_sortColumn; }
    [[nodiscard]] Qt::SortOrder sortOrder() const { return m_order; }
    [[nodiscard]] static QString cellText(const queries::TableRow& row, int column);
    [[nodiscard]] static QString columnTitle(int column);

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    void sort(int column, Qt::SortOrder order) override;

signals:
    void loadingStarted();
    void loadingFinished(qint64 total);

private:
    static constexpr int ROWS_PER_PAGE = 200;
    static constexpr int CACHED_PAGES = 40;

    const queries::TableRow* loadedRow(int row) const;
    const QIcon& colorDot(const QColor& color) const;
    const QIcon& alertIcon(int level) const;

    Workspace* m_workspace;
    Filters m_filters;
    Column m_sortColumn = Column::Timestamp;
    Qt::SortOrder m_order = Qt::DescendingOrder;
    queries::Identifiers m_ids;
    TokenPtr m_token;
    bool m_filtersSet = false;
    bool m_alertOnDate = false;

    mutable QHash<int, QList<queries::TableRow>> m_pages;
    mutable QList<int> m_pageOrder;
    mutable QHash<QRgb, QIcon> m_dots;
    mutable QHash<int, QIcon> m_alertIcons;
};

} // namespace lh::ui
