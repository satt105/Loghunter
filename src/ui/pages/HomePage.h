// Home page: import, recent projects and current project.
#pragma once

#include "Page.h"

class QAction;
class QHBoxLayout;
class QVBoxLayout;

namespace lh::ui {

class Card;
class Label;

class HomePage : public Page
{
    Q_OBJECT

public:
    explicit HomePage(Workspace* workspace, QWidget* parent = nullptr);
    [[nodiscard]] QString title() const override { return qtTrId("home.title"); }
    [[nodiscard]] const char* iconName() const override { return "home"; }
    [[nodiscard]] bool usesFilters() const override { return false; }
    void setAnalyses(const QList<QAction*>& actions);

signals:
    void importLogs();
    void importFolder();
    void openProject();
    void openRecent(const QString& path);
    void auditGuide();
    void logsImported();

protected:
    void refresh() override;

private:
    void populateRecents();
    void populateProject();

    QHBoxLayout* m_analyses;
    Card* m_recents;
    QVBoxLayout* m_recentsList;
    Card* m_project;
    QWidget* m_currentProject;
    QWidget* m_prerequisites;
    Label* m_projectSummary;
    Label* m_projectDetail;
};

} // namespace lh::ui
