// Summary: indicators, alert banner and clickable charts.
#pragma once

#include "Page.h"
#include "core/Alerts.h"
#include "core/Queries.h"
#include "ui/Executor.h"

class QGridLayout;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace lh::ui {

class LinkButton;
class Card;
class EmptyState;
class Label;
class DonutChart;
class HeatmapChart;
class TimelineChart;
class RankingChart;
class HoursChart;
class LoadingIndicator;
class IndicatorTile;

class DashboardPage : public Page
{
    Q_OBJECT

public:
    explicit DashboardPage(Workspace* workspace, QWidget* parent = nullptr);
    [[nodiscard]] QString title() const override { return qtTrId("summary.title"); }
    [[nodiscard]] const char* iconName() const override { return "dashboard"; }

signals:
    void importLogs();

protected:
    void refresh() override;
    void clear() override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void showSummary(const queries::Summary& summary);
    void showDetections(const QList<Detection>& detections);
    void arrange(bool wide);
    void showDetailedCharts(bool shown);

    QStackedWidget* m_stack;
    EmptyState* m_empty;
    EmptyState* m_noResult;
    QWidget* m_content;
    LoadingIndicator* m_loading;

    IndicatorTile* m_events;
    IndicatorTile* m_users;
    IndicatorTile* m_computers;
    IndicatorTile* m_period;

    QLabel* m_alertsIcon;
    Label* m_alertsTitle;
    Label* m_alertsDetail;
    QPushButton* m_viewAlerts;

    QGridLayout* m_grid;
    LinkButton* m_moreCharts;
    bool m_detailedCharts = false;
    Card* m_timelineCard;
    Card* m_actionsCard;
    Card* m_hoursCard;
    Card* m_weekCard;
    Card* m_usersCard;
    Card* m_processCard;
    Card* m_eventsCard;
    Card* m_ipCard;
    TimelineChart* m_timeline;
    DonutChart* m_actions;
    HoursChart* m_hours;
    HeatmapChart* m_week;
    RankingChart* m_userRanking;
    RankingChart* m_processRanking;
    RankingChart* m_eventRanking;
    RankingChart* m_ipRanking;
    bool m_distributionByLog = false;

    TokenPtr m_token;
    int m_layout = -1;
};

} // namespace lh::ui
