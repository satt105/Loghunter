// Main window: navigation, menus, project, imports, exports and reports.
#pragma once

#include "core/Export.h"
#include "core/Filters.h"
#include "dialogs/SettingsDialog.h"

#include <QMainWindow>
#include <QPointer>
#include <QTimer>

#include <atomic>
#include <functional>
#include <optional>

class QAction;
class QActionGroup;
class QLabel;
class QMenu;
class QStackedWidget;
class QToolButton;

namespace lh::ui {

class FilterBar;
class Sidebar;
class SearchField;
class ImportDialog;
class Workspace;
class Label;
class Page;
class HomePage;
class AlertsPage;
class EventsPage;
class InvestigationPage;
class DashboardPage;
class FilterPanel;
struct Navigation;

struct TaskProgress
{
    std::atomic<int> perMille{-1};
    std::atomic_bool cancellation{false};
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    void openPaths(const QStringList& paths);
    [[nodiscard]] Workspace* workspace() const { return m_workspace; }

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    struct ViewState
    {
        int page = -1;
        int tab = 0;
        QString value;
        Filters filters;

        bool operator==(const ViewState&) const = default;
    };

    void createActions();
    void createMenus();
    void createInterface();
    void restoreSettings();

    void showPage(int index);
    void navigate(const Navigation& navigation);
    [[nodiscard]] ViewState currentView() const;
    void noteView();
    void rememberView();
    void goBack();
    void clearHistory();
    void updateBack();
    void updateHeader();
    void updateTitle();
    void updateActions();
    void updateStatus();
    void updateRecents();

    bool confirmDiscard();
    void newProject();
    void openProject();
    void openProjectPath(const QString& path);
    bool save();
    bool saveAs();
    bool saveTo(const QString& path);

    void importLogs();
    void importFolder();
    void importFiles(const QStringList& paths);
    void exportEvents();
    void exportAlerts();
    QString chooseExportFile(const QString& title, const QString& name, exporter::Format& format);
    void reanalyze(bool complete);
    void generateReport();
    void showSources();
    void removeSources(const QList<qint64>& identifiers);
    void showSettings(SettingsDialog::Tab tab);
    void showRules(bool reanalysisPending);
    void proposeRestart();
    void applyTimeZone(const QByteArray& timeZone);
    void toggleAnalysisType(bool AlertParameters::*type, bool active);
    void updateAnalysisTypes();

    bool execute(const QString& title, const QString& text, bool cancellable,
                 const std::function<void(TaskProgress&)>& work);

    Workspace* m_workspace;
    Sidebar* m_sidebar;
    QStackedWidget* m_pages;
    QList<Page*> m_pageList;
    HomePage* m_home;
    DashboardPage* m_dashboard;
    EventsPage* m_events;
    InvestigationPage* m_investigation;
    AlertsPage* m_alerts;
    QToolButton* m_backButton;
    Label* m_pageTitle;
    Label* m_pageSubtitle;
    SearchField* m_search;
    QToolButton* m_filtersButton;
    QToolButton* m_importButton;
    FilterBar* m_chips;
    FilterPanel* m_panel;
    QLabel* m_dataStatus;
    QToolButton* m_timeZoneStatus;
    QPointer<ImportDialog> m_import;
    QList<ViewState> m_history;
    ViewState m_view;
    QTimer m_viewTimer;
    bool m_restoring = false;

    QAction* m_backAction;
    QAction* m_newAction;
    QAction* m_openAction;
    QAction* m_saveAction;
    QAction* m_saveAsAction;
    QAction* m_importAction;
    QAction* m_importFolderAction;
    QAction* m_exportAction;
    QAction* m_reportAction;
    QAction* m_quitAction;
    QAction* m_searchAction;
    QAction* m_panelAction;
    QAction* m_resetAction;
    QAction* m_sourcesAction;
    QAction* m_reanalyzeAction;
    QAction* m_detectionSettingsAction;
    QAction* m_fullScreenAction;
    QAction* m_exportAlertsAction;
    QAction* m_rulesAction;
    QAction* m_settingsAction;
    QAction* m_timeZoneAction;
    QAction* m_ruleAnalysisAction;
    QAction* m_fileAnalysisAction;
    QAction* m_mitreAction;
    QList<QAction*> m_pageActions;
    QActionGroup* m_themeGroup;
    QMenu* m_recentsMenu;
};

} // namespace lh::ui
