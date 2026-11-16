// All events: timeline, filtered or grouped table and detail of the selected event.
#pragma once

#include "Page.h"
#include "ui/Executor.h"

class QSplitter;
class QStackedWidget;
class QTableView;
class QToolButton;

namespace lh::ui {

class LinkButton;
class Timeline;
class EmptyState;
class Label;
class LoadingIndicator;
class TableModel;
class DetailPanel;
class EventTable;

class EventsPage : public Page
{
    Q_OBJECT

public:
    explicit EventsPage(Workspace* workspace, QWidget* parent = nullptr);
    [[nodiscard]] QString title() const override { return qtTrId("events.title"); }
    [[nodiscard]] const char* iconName() const override { return "events"; }

signals:
    void exportEvents();

protected:
    void refresh() override;
    void clear() override;

private:
    void updateCount(qint64 total);
    void showGrouping(bool active);
    void loadGroups();
    void openGroup(int row);

    EventTable* m_table;
    DetailPanel* m_detail;
    QSplitter* m_splitter;
    QStackedWidget* m_views;
    QTableView* m_groups;
    TableModel* m_groupsModel;
    EmptyState* m_noGroup;
    QStackedWidget* m_groupsStack;
    LoadingIndicator* m_groupsLoading;
    QWidget* m_timelineFrame;
    Timeline* m_timeline;
    LinkButton* m_wholePeriod;
    Label* m_count;
    Label* m_sortLabel;
    QToolButton* m_detailButton;
    QToolButton* m_groupButton;
    QToolButton* m_timelineButton;
    TokenPtr m_groupsToken;
};

} // namespace lh::ui
