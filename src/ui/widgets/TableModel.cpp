#include "TableModel.h"

#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QEvent>
#include <QGuiApplication>
#include <QHeaderView>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QTableView>
#include <QTreeView>

#include <algorithm>

namespace lh::ui {

TableModel::TableModel(QObject* parent) : QAbstractTableModel(parent) {}

void TableModel::setColumns(const QList<Column>& columns)
{
    beginResetModel();
    m_columns = columns;
    m_rows.clear();
    endResetModel();
}

void TableModel::setRows(QList<Row> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

int TableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_rows.size());
}

int TableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_columns.size());
}

QVariant TableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size())
        return {};
    const Row& row = m_rows[index.row()];
    if (role == KeyRole)
        return row.key;
    if (index.column() >= row.cells.size())
        return {};
    const Cell& cell = row.cells[index.column()];
    switch (role) {
    case Qt::DisplayRole:
        return cell.text;
    case SortRole:
        return cell.sortKey.isValid() ? cell.sortKey : QVariant(cell.text);
    case Qt::ToolTipRole:
        return cell.tooltip.isEmpty() ? (cell.text.size() > 24 ? cell.text : QString()) : cell.tooltip;
    case Qt::ForegroundRole:
        return cell.color.isValid() ? QVariant(cell.color) : QVariant();
    case Qt::DecorationRole:
        return cell.icon.isNull() ? QVariant() : QVariant(cell.icon);
    case DetailRole:
        return cell.detail;
    case Qt::TextAlignmentRole:
        if (index.column() < m_columns.size() && m_columns[index.column()].count)
            return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
        return {};
    default:
        return {};
    }
}

QVariant TableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || section < 0 || section >= m_columns.size())
        return {};
    if (role == Qt::DisplayRole)
        return m_columns[section].title;
    if (role == Qt::TextAlignmentRole && m_columns[section].count)
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

QSize TwoLineDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QSize size = QStyledItemDelegate::sizeHint(option, index);
    const QString detail = index.data(TableModel::DetailRole).toString();
    size.setWidth(std::max(size.width(), option.fontMetrics.horizontalAdvance(detail) + 24));
    size.setHeight(height(option.fontMetrics));
    return size;
}

// ── ColumnFitter ─────────────────────────────────────────────────────────────

ColumnFitter::ColumnFitter(QAbstractItemView* view, QHeaderView* header, int stretchColumn)
    : QObject(view), m_view(view), m_header(header), m_column(stretchColumn)
{
    m_header->setSectionResizeMode(QHeaderView::Interactive);
    m_header->setStretchLastSection(false);
    m_view->viewport()->installEventFilter(this);
    connect(m_header, &QHeaderView::sectionResized, this, [this](int section) {
        if (m_adjusting)
            return;
        if (section == m_column)
            m_manual = true;
        else
            populate();
    });
}

void ColumnFitter::fitToContent()
{
    m_adjusting = true;
    for (int column = 0; column < m_header->count(); ++column)
        if (column != m_column && !m_header->isSectionHidden(column))
            m_header->resizeSection(column,
                                    std::max(m_view->sizeHintForColumn(column), m_header->sectionSizeHint(column)));
    m_adjusting = false;
    populate();
}

bool ColumnFitter::eventFilter(QObject* object, QEvent* event)
{
    if (object == m_view->viewport() && event->type() == QEvent::Resize)
        populate();
    return QObject::eventFilter(object, event);
}

void ColumnFitter::populate()
{
    if (m_manual || m_column >= m_header->count())
        return;
    int others = 0;
    for (int column = 0; column < m_header->count(); ++column)
        if (column != m_column && !m_header->isSectionHidden(column))
            others += m_header->sectionSize(column);
    const int minimum = m_view->fontMetrics().averageCharWidth() * 16;
    m_adjusting = true;
    m_header->resizeSection(m_column, std::max(minimum, m_view->viewport()->width() - others));
    m_adjusting = false;
}

void TwoLineDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QStyleOptionViewItem element = option;
    initStyleOption(&element, index);
    const QString title = element.text;
    const QString detail = index.data(TableModel::DetailRole).toString();
    element.text.clear();
    const QWidget* widget = option.widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &element, painter, widget);

    const QRect zone = style->subElementRect(QStyle::SE_ItemViewItemText, &element, widget).adjusted(2, 0, -2, 0);
    const bool selected = option.state & QStyle::State_Selected;
    const QColor color = selected ? option.palette.color(QPalette::HighlightedText) : option.palette.color(QPalette::Text);
    const QColor secondary = selected ? color : theme::secondaryText(option.palette);
    const QFontMetrics metrics(option.font);
    const int y = zone.center().y() - metrics.height();
    painter->save();
    painter->setFont(option.font);
    painter->setPen(color);
    painter->drawText(QRect(zone.left(), y, zone.width(), metrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                      metrics.elidedText(title, Qt::ElideRight, zone.width()));
    const QFont smaller = theme::font(option.font, 0.9);
    const QFontMetrics smallMetrics(smaller);
    painter->setFont(smaller);
    painter->setPen(secondary);
    painter->drawText(QRect(zone.left(), y + metrics.height() + 1, zone.width(), metrics.height()),
                      Qt::AlignLeft | Qt::AlignVCenter, smallMetrics.elidedText(detail, Qt::ElideMiddle, zone.width()));
    painter->restore();
}

QSortFilterProxyModel* createSortProxy(QAbstractItemModel* model, QObject* parent)
{
    auto* proxy = new QSortFilterProxyModel(parent);
    proxy->setSourceModel(model);
    proxy->setSortRole(TableModel::SortRole);
    proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxy->setSortLocaleAware(true);
    return proxy;
}

void limitColumnWidths(QTableView* view, int maximum)
{
    view->resizeColumnsToContents();
    for (int column = 0; column < view->model()->columnCount(); ++column)
        view->setColumnWidth(column, std::min(view->columnWidth(column), maximum));
}

void configureTable(QTableView* view)
{
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setShowGrid(false);
    view->setAlternatingRowColors(true);
    view->setWordWrap(false);
    view->setTextElideMode(Qt::ElideMiddle);
    view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    view->verticalHeader()->hide();
    view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    view->verticalHeader()->setDefaultSectionSize(view->fontMetrics().height() + 10);
    view->horizontalHeader()->setHighlightSections(false);
    view->horizontalHeader()->setMinimumSectionSize(40);
    view->horizontalHeader()->setTextElideMode(Qt::ElideRight);

    auto* copy = new QAction(icon("copy"), qtTrId("common.copy"), view);
    copy->setShortcut(QKeySequence::Copy);
    copy->setShortcutContext(Qt::WidgetShortcut);
    QObject::connect(copy, &QAction::triggered, view, [view] { copyRows(view); });
    view->addAction(copy);
}

void configureTree(QTreeView* view)
{
    view->setSelectionBehavior(QAbstractItemView::SelectRows);
    view->setSelectionMode(QAbstractItemView::SingleSelection);
    view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view->setAlternatingRowColors(true);
    view->setUniformRowHeights(true);
    view->setTextElideMode(Qt::ElideRight);
    view->header()->setHighlightSections(false);
    view->header()->setStretchLastSection(false);
}

void copyRows(QTableView* view)
{
    const QAbstractItemModel* model = view->model();
    if (!model)
        return;
    QList<int> rows;
    for (const QModelIndex& index : view->selectionModel()->selectedRows())
        rows.append(index.row());
    if (rows.isEmpty() && view->currentIndex().isValid())
        rows.append(view->currentIndex().row());
    if (rows.isEmpty())
        return;
    std::sort(rows.begin(), rows.end());
    QString text;
    QStringList cells;
    for (int column = 0; column < model->columnCount(); ++column)
        if (!view->isColumnHidden(column))
            cells << model->headerData(column, Qt::Horizontal).toString();
    text += cells.join(QLatin1Char('\t')) + QLatin1Char('\n');
    for (const int row : rows) {
        cells.clear();
        for (int column = 0; column < model->columnCount(); ++column) {
            if (view->isColumnHidden(column))
                continue;
            const QModelIndex index = model->index(row, column);
            const QString complete = index.data(Qt::ToolTipRole).toString();
            cells << (complete.isEmpty() || complete.contains(QLatin1Char('\n')) ? index.data().toString() : complete);
        }
        text += cells.join(QLatin1Char('\t')) + QLatin1Char('\n');
    }
    QGuiApplication::clipboard()->setText(text);
}

} // namespace lh::ui
