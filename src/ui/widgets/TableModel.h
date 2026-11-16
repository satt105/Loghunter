// Generic table model and common configuration of table views.
#pragma once

#include <QAbstractTableModel>
#include <QStyledItemDelegate>
#include <QColor>
#include <QIcon>
#include <QList>
#include <QVariant>

class QAbstractItemView;
class QHeaderView;
class QSortFilterProxyModel;
class QTableView;
class QTreeView;

namespace lh::ui {

class TableModel : public QAbstractTableModel
{
    Q_OBJECT

public:
    static constexpr int SortRole = Qt::UserRole + 1;
    static constexpr int KeyRole = Qt::UserRole + 2;
    static constexpr int DetailRole = Qt::UserRole + 3;

    struct Column
    {
        QString title;
        bool count = false;
    };
    struct Cell
    {
        QString text;
        QVariant sortKey;
        QColor color;
        QIcon icon;
        QString tooltip;
        QString detail;
    };
    struct Row
    {
        QList<Cell> cells;
        QVariant key;
    };

    explicit TableModel(QObject* parent = nullptr);
    void setColumns(const QList<Column>& columns);
    void setRows(QList<Row> rows);
    [[nodiscard]] const QList<Row>& rows() const { return m_rows; }

    int rowCount(const QModelIndex& parent = {}) const override;
    int columnCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

private:
    QList<Column> m_columns;
    QList<Row> m_rows;
};

class TwoLineDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    static int height(const QFontMetrics& metrics) { return metrics.height() * 2 + 12; }
};

class ColumnFitter : public QObject
{
public:
    ColumnFitter(QAbstractItemView* view, QHeaderView* header, int stretchColumn);
    void fitToContent();

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void populate();

    QAbstractItemView* m_view;
    QHeaderView* m_header;
    int m_column;
    bool m_manual = false;
    bool m_adjusting = false;
};

QSortFilterProxyModel* createSortProxy(QAbstractItemModel* model, QObject* parent);
void configureTable(QTableView* view);
void configureTree(QTreeView* view);
void limitColumnWidths(QTableView* view, int maximum = 420);
void copyRows(QTableView* view);

} // namespace lh::ui
