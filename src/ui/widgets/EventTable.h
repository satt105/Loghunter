// Event table: sorting, copying and context menu.
#pragma once

#include "core/Filters.h"
#include "core/Queries.h"
#include "ui/Executor.h"

#include <QSet>
#include <QWidget>

class QMenu;
class QStackedWidget;
class QTableView;
class QToolButton;

namespace lh::ui {

class ColumnFitter;
class LinkButton;
class EmptyState;
class Workspace;
class Label;
class LoadingIndicator;
class EventModel;

class EventTable : public QWidget
{
    Q_OBJECT

public:
    explicit EventTable(Workspace* workspace, QWidget* parent = nullptr);

    void setFilters(const Filters& filters);
    void setSort(queries::Column column, Qt::SortOrder order);
    void setEmptyMessage(const QString& title, const QString& message);
    [[nodiscard]] EventModel* model() const { return m_model; }
    [[nodiscard]] QTableView* view() const { return m_view; }
    [[nodiscard]] qint64 currentIdentifier() const;
    void copySelection();
    QMenu* createColumnsMenu(QWidget* parent);
    QToolButton* createColumnsButton(QWidget* parent);

signals:
    void selectionChanged(qint64 identifier);
    void eventActivated(qint64 identifier);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void contextMenu(const QPoint& position);
    void loadingFinished(qint64 total);
    void loadPreferences();
    void savePreferences() const;
    void applyColumns();
    [[nodiscard]] bool isUselessColumn(int column) const;

    Workspace* m_workspace;
    EventModel* m_model;
    QStackedWidget* m_stack;
    QTableView* m_view;
    EmptyState* m_empty;
    LoadingIndicator* m_loading;
    ColumnFitter* m_fitter;
    QWidget* m_banner;
    Label* m_bannerText;
    QSet<int> m_hidden;
    bool m_hideUseless = true;
    queries::Uniformity m_uniformity;
    TokenPtr m_uniformityToken;
};

} // namespace lh::ui
