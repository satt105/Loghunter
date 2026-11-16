// Alerts: suspicious activity found and the events behind each finding.
#pragma once

#include "Page.h"
#include "core/Alerts.h"

class QListWidget;
class QPushButton;
class QStackedWidget;
class QTableView;

namespace lh::ui {

class Card;
class EmptyState;
class Label;
class LoadingIndicator;
class TableModel;

class AlertsPage : public Page
{
    Q_OBJECT

public:
    explicit AlertsPage(Workspace* workspace, QWidget* parent = nullptr);
    [[nodiscard]] QString title() const override { return qtTrId("common.alerts"); }
    [[nodiscard]] const char* iconName() const override { return "alerts"; }

    void select(const QString& identifier);

signals:
    void settingsRequested();
    void reanalysisRequested();
    void exportRequested();

protected:
    void refresh() override;

private:
    void showDetections(const QList<Detection>& detections);
    void showDetection(int index);
    void exploreRow(int row);
    void resultsMenu(const QPoint& position);

    QStackedWidget* m_stack;
    EmptyState* m_empty;
    QWidget* m_content;
    Label* m_summary;
    Label* m_risk;
    QListWidget* m_list;
    Card* m_detail;
    Label* m_severity;
    Label* m_description;
    Label* m_mitre;
    Label* m_origin;
    Label* m_note;
    QTableView* m_results;
    TableModel* m_model;
    QPushButton* m_exploreButton;
    QPushButton* m_all;
    LoadingIndicator* m_loading;
    EmptyState* m_noFinding;
    QStackedWidget* m_detailStack;

    QList<Detection> m_detections;
    QString m_identifier;
};

} // namespace lh::ui
