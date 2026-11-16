#include "MainWindow.h"

#include "Application.h"
#include "Workspace.h"
#include "Icons.h"
#include "Settings.h"
#include "Theme.h"
#include "core/Export.h"
#include "core/Format.h"
#include "core/Import.h"
#include "core/DateTime.h"
#include "core/logs/Log.h"
#include "dialogs/MessageDialog.h"
#include "dialogs/AboutDialog.h"
#include "dialogs/GuideDialog.h"
#include "dialogs/ImportDialog.h"
#include "dialogs/ReportDialog.h"
#include "dialogs/RulesDialog.h"
#include "dialogs/SourcesDialog.h"
#include "loghunter_version.h"
#include "pages/HomePage.h"
#include "pages/AlertsPage.h"
#include "pages/EventsPage.h"
#include "pages/InvestigationPage.h"
#include "pages/DashboardPage.h"
#include "render/Report.h"
#include "widgets/FilterBar.h"
#include "widgets/Sidebar.h"
#include "widgets/Components.h"
#include "widgets/FilterPanel.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLoggingCategory>
#include <QMenuBar>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QScreen>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <algorithm>

namespace lh::ui {

namespace {

Q_LOGGING_CATEGORY(log, "loghunter.window")

constexpr char PROJECT_EXTENSION[] = ".loghunter";
constexpr int HISTORY_LIMIT = 100;

enum PageIndex { PAGE_HOME, PAGE_DASHBOARD, PAGE_ALERTS, PAGE_INVESTIGATION, PAGE_EVENTS };

bool isProject(const QString& path)
{
    return path.endsWith(QLatin1String(PROJECT_EXTENSION), Qt::CaseInsensitive);
}

bool isLog(const QString& path)
{
    return logs::isKnownExtension(path);
}

QString dayDate()
{
    return QDate::currentDate().toString(Qt::ISODate);
}

void offerOpen(QWidget* parent, const QString& text, const QString& path)
{
    MessageDialog box(MessageDialog::Type::Success, qtTrId("window.export_finished"), text, parent);
    QPushButton* openButton = box.addButton(qtTrId("window.open_file"), true);
    box.addButton(qtTrId("common.close"));
    box.exec();
    if (box.clickedButton() == openButton)
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

class ProgressWindow : public QDialog
{
public:
    ProgressWindow(const QString& title, const QString& text, bool cancellable, TaskProgress& taskProgress,
                   QWidget* parent)
        : QDialog(parent, Qt::Dialog | Qt::CustomizeWindowHint | Qt::WindowTitleHint), m_taskProgress(taskProgress)
    {
        setWindowTitle(QApplication::applicationDisplayName());
        setWindowModality(Qt::WindowModal);
        setMinimumWidth(480);
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        auto* body = new QHBoxLayout;
        body->setContentsMargins(24, 24, 28, 26);
        body->setSpacing(16);
        body->addWidget(new IconBadge("clock", theme::brand(QApplication::palette()), 44, this), 0,
                        Qt::AlignTop);
        auto* texts = new QVBoxLayout;
        texts->setSpacing(4);
        texts->addWidget(new Label(title, Label::Role::Title, this));
        m_text = new Label(text, Label::Role::Normal, this);
        m_text->setWordWrap(true);
        texts->addWidget(m_text);
        texts->addSpacing(10);
        m_sidebar = new QProgressBar(this);
        m_sidebar->setRange(0, 0);
        m_sidebar->setTextVisible(false);
        m_sidebar->setMaximumHeight(8);
        texts->addWidget(m_sidebar);
        body->addLayout(texts, 1);
        layout->addLayout(body);
        if (cancellable) {
            auto* footer = new DialogFooter(this);
            m_cancel = footer->addButton(qtTrId("common.cancel"));
            m_cancel->setAutoDefault(false);
            layout->addWidget(footer);
            connect(m_cancel, &QPushButton::clicked, this, [this] { cancel(); });
        }
        m_timer.setInterval(100);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            const int value = m_taskProgress.perMille.load();
            if (value < 0)
                return;
            if (m_sidebar->maximum() != 1000)
                m_sidebar->setRange(0, 1000);
            m_sidebar->setValue(std::clamp(value, 0, 1000));
        });
        m_timer.start();
    }

    void reject() override
    {
        if (m_cancel)
            cancel();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), theme::cardBackground(palette()));
    }

private:
    void cancel()
    {
        m_taskProgress.cancellation = true;
        m_text->setText(qtTrId("common.cancelling"));
        m_cancel->setEnabled(false);
    }

    TaskProgress& m_taskProgress;
    Label* m_text;
    QProgressBar* m_sidebar;
    QPushButton* m_cancel = nullptr;
    QTimer m_timer;
};

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    m_workspace = new Workspace(this);
    setAcceptDrops(true);
    setMinimumSize(1000, 660);

    createActions();
    createInterface();
    createMenus();

    connect(m_workspace, &Workspace::documentStateChanged, this, [this] {
        updateTitle();
        updateStatus();
    });
    connect(m_workspace, &Workspace::dataModified, this, [this] {
        if (m_workspace->isEmpty())
            clearHistory();
        updateStatus();
        updateActions();
        updateHeader();
    });
    connect(m_workspace, &Workspace::filtersChanged, this, &MainWindow::updateHeader);
    connect(m_workspace, &Workspace::parametersChanged, this, &MainWindow::updateAnalysisTypes);
    connect(m_workspace, &Workspace::busyChanged, this, &MainWindow::updateActions);
    connect(m_workspace, &Workspace::navigationRequested, this, &MainWindow::navigate);
    connect(&m_workspace->executor(), &Executor::error, this, [this](const QString& message) {
        qCWarning(log).noquote() << message;
        statusBar()->showMessage(qtTrId("window.status_error").arg(message), 10'000);
    });
    connect(m_workspace, &Workspace::detectionsAvailable, this, [this](const QList<Detection>& detections) {
        const int findings = findingCount(detections);
        m_sidebar->setBadge(PAGE_ALERTS, findings > 0 ? QString::number(findings) : QString(),
                            theme::severityColor(maxSeverity(detections)));
    });
    connect(m_workspace, &Workspace::detectionsInvalidated, this, [this] { m_sidebar->setBadge(PAGE_ALERTS, {}); });

    m_viewTimer.setSingleShot(true);
    m_viewTimer.setInterval(500);
    connect(&m_viewTimer, &QTimer::timeout, this, &MainWindow::rememberView);
    connect(m_workspace, &Workspace::sessionChanged, this, &MainWindow::clearHistory);
    connect(m_workspace, &Workspace::filtersChanged, this, &MainWindow::noteView);
    connect(m_investigation, &InvestigationPage::viewChanged, this, &MainWindow::noteView);
    qApp->installEventFilter(this);

    restoreSettings();
    updateAnalysisTypes();
    showPage(PAGE_HOME);
    updateTitle();
    updateStatus();
    updateActions();
    updateRecents();
    statusBar()->showMessage(qtTrId("window.status_ready"), 3000);
}

MainWindow::~MainWindow() = default;

// ── Construction ─────────────────────────────────────────────────────────────

void MainWindow::createActions()
{
    auto action = [this](const char* iconName, const QString& text, const QKeySequence& shortcut,
                         const std::function<void()>& slot) {
        auto* result = new QAction(iconName ? icon(iconName) : QIcon(), text, this);
        if (!shortcut.isEmpty())
            result->setShortcut(shortcut);
        if (slot)
            connect(result, &QAction::triggered, this, slot);
        return result;
    };
    m_backAction = action("back", qtTrId("window.action_back"), {}, [this] { goBack(); });
    m_backAction->setShortcuts({QKeySequence(Qt::ALT | Qt::Key_Left), QKeySequence(Qt::Key_Back)});
    m_backAction->setToolTip(qtTrId("window.back_help"));
    m_backAction->setEnabled(false);
    m_newAction =
        action("plus", qtTrId("window.action_new_project"), QKeySequence::New, [this] { newProject(); });
    m_openAction =
        action("open", qtTrId("window.action_open_project"), QKeySequence::Open, [this] { openProject(); });
    m_saveAction =
        action("save", qtTrId("window.action_save"), QKeySequence::Save, [this] { save(); });
    m_saveAsAction =
        action(nullptr, qtTrId("window.action_save_as"), QKeySequence::SaveAs, [this] { saveAs(); });
    m_importAction = action("import", qtTrId("window.action_import_logs"),
                            QKeySequence(Qt::CTRL | Qt::Key_I), [this] { importLogs(); });
    m_importFolderAction = action("folder", qtTrId("window.action_import_folder"),
                                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), [this] { importFolder(); });
    m_exportAction = action("export", qtTrId("window.action_export_list"), QKeySequence(Qt::CTRL | Qt::Key_E),
                            [this] { exportEvents(); });
    m_reportAction = action("report", qtTrId("window.action_create_report"), QKeySequence(Qt::CTRL | Qt::Key_R),
                            [this] { generateReport(); });
    m_quitAction =
        action(nullptr, qtTrId("window.action_quit"), QKeySequence(Qt::CTRL | Qt::Key_Q), [this] { close(); });
    m_quitAction->setMenuRole(QAction::QuitRole);

    m_searchAction = action("search", qtTrId("window.action_search"), QKeySequence::Find, [this] {
        if (!m_pageList[m_pages->currentIndex()]->usesFilters())
            showPage(PAGE_EVENTS);
        m_search->setFocus(Qt::ShortcutFocusReason);
        m_search->selectAll();
    });
    m_panelAction =
        action("filters", qtTrId("window.action_filters"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F), {});
    m_panelAction->setCheckable(true);
    m_resetAction = action("refresh", qtTrId("common.clear_filters"),
                           QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Delete),
                           [this] { m_workspace->resetFilters(); });
    m_sourcesAction = action("logs", qtTrId("window.action_imported_logs"), QKeySequence(Qt::CTRL | Qt::Key_J),
                             [this] { showSources(); });
    m_reanalyzeAction = action("alerts", qtTrId("window.action_rerun_detection"), QKeySequence(Qt::Key_F5),
                               [this] { reanalyze(true); });
    m_detectionSettingsAction = action("settings", qtTrId("window.action_settings"), QKeySequence::Preferences,
                                       [this] { showSettings(SettingsDialog::Tab::General); });
    if (m_detectionSettingsAction->shortcut().isEmpty())
        m_detectionSettingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
    m_detectionSettingsAction->setMenuRole(QAction::PreferencesRole);
    m_fullScreenAction = action(nullptr, qtTrId("window.action_full_screen"), QKeySequence::FullScreen,
                                [this] { setWindowState(windowState() ^ Qt::WindowFullScreen); });
    if (m_fullScreenAction->shortcut().isEmpty())
        m_fullScreenAction->setShortcut(QKeySequence(Qt::Key_F11));
    m_exportAlertsAction = action("alerts", qtTrId("window.action_export_alerts"),
                                  QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), [this] { exportAlerts(); });
    m_rulesAction = action("file", qtTrId("window.action_rules"), {}, [this] { showRules(false); });
    m_settingsAction = action("settings", qtTrId("window.action_detection_settings"), {},
                              [this] { showSettings(SettingsDialog::Tab::Detection); });
    m_timeZoneAction = action("clock", qtTrId("window.action_time_zone"), {},
                              [this] { showSettings(SettingsDialog::Tab::General); });
    auto analysisType = [this](const QString& text, const QString& help, bool AlertParameters::*type) {
        auto* result = new QAction(text, this);
        result->setCheckable(true);
        result->setToolTip(help);
        result->setStatusTip(help);
        connect(result, &QAction::triggered, this, [this, type](bool active) { toggleAnalysisType(type, active); });
        return result;
    };
    m_ruleAnalysisAction = analysisType(qtTrId("window.analysis_rules"), qtTrId("window.analysis_rules_help"),
                                        &AlertParameters::ruleAnalysis);
    m_fileAnalysisAction = analysisType(qtTrId("window.analysis_files"),
                                        qtTrId("window.analysis_files_help"), &AlertParameters::fileAnalysis);
    m_mitreAction =
        analysisType(qtTrId("window.analysis_mitre"), qtTrId("window.analysis_mitre_help"), &AlertParameters::mitre);
}

void MainWindow::createInterface()
{
    auto* central = new QWidget(this);
    setCentralWidget(central);
    auto* horizontal = new QHBoxLayout(central);
    horizontal->setContentsMargins(0, 0, 0, 0);
    horizontal->setSpacing(0);

    m_sidebar = new Sidebar(central);
    horizontal->addWidget(m_sidebar);

    auto* right = new QWidget(central);
    horizontal->addWidget(right, 1);
    auto* vertical = new QVBoxLayout(right);
    vertical->setContentsMargins(0, 0, 0, 0);
    vertical->setSpacing(0);

    auto* header = new QWidget(right);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(24, 16, 24, 12);
    headerLayout->setSpacing(10);
    m_backButton = new QToolButton(header);
    m_backButton->setDefaultAction(m_backAction);
    m_backButton->setAutoRaise(true);
    m_backButton->setIconSize(QSize(20, 20));
    headerLayout->addWidget(m_backButton, 0, Qt::AlignVCenter);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    m_pageTitle = new Label({}, Label::Role::PageTitle, header);
    m_pageSubtitle = new Label({}, Label::Role::Small, header);
    m_pageSubtitle->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    titles->addWidget(m_pageTitle);
    titles->addWidget(m_pageSubtitle);
    headerLayout->addLayout(titles, 1);

    m_search = new SearchField(qtTrId("window.search_placeholder"), header);
    m_search->setMinimumWidth(280);
    m_search->setMaximumWidth(440);
    m_search->setToolTip(qtTrId("window.search_help"));
    connect(m_search, &SearchField::searchChanged, this, [this](const QString& text) {
        Filters filters = m_workspace->filters();
        filters.text = text;
        m_workspace->setFilters(filters);
    });
    headerLayout->addWidget(m_search, 1, Qt::AlignVCenter);

    m_filtersButton = new QToolButton(header);
    m_filtersButton->setIcon(icon("filters"));
    m_filtersButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_filtersButton->setCheckable(true);
    m_filtersButton->setToolTip(qtTrId("window.show_filters_help"));
    m_filtersButton->setMinimumHeight(32);
    m_filtersButton->setProperty("style", QStringLiteral("border"));
    headerLayout->addWidget(m_filtersButton, 0, Qt::AlignVCenter);

    m_importButton = new QToolButton(header);
    m_importButton->setIcon(icon("import", Qt::white));
    m_importButton->setText(qtTrId("window.add_logs"));
    m_importButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_importButton->setToolTip(qtTrId("window.add_logs_help"));
    m_importButton->setMinimumHeight(32);
    m_importButton->setProperty("style", QStringLiteral("accent"));
    connect(m_importButton, &QToolButton::clicked, this, &MainWindow::importLogs);
    headerLayout->addWidget(m_importButton, 0, Qt::AlignVCenter);
    vertical->addWidget(header);

    m_chips = new FilterBar(m_workspace, right);
    vertical->addWidget(m_chips);

    auto* body = new QHBoxLayout;
    body->setSpacing(0);
    m_pages = new QStackedWidget(right);
    body->addWidget(m_pages, 1);
    m_panel = new FilterPanel(m_workspace, right);
    body->addWidget(m_panel);
    vertical->addLayout(body, 1);

    connect(m_filtersButton, &QToolButton::toggled, this, [this](bool visible) {
        settings::write(settings::key::FILTER_PANEL, visible);
        m_panelAction->setChecked(visible);
        updateHeader();
    });
    connect(m_panelAction, &QAction::toggled, m_filtersButton, &QToolButton::setChecked);
    connect(m_panel, &FilterPanel::closeRequested, this, [this] { m_filtersButton->setChecked(false); });

    m_home = new HomePage(m_workspace, m_pages);
    m_dashboard = new DashboardPage(m_workspace, m_pages);
    m_events = new EventsPage(m_workspace, m_pages);
    m_investigation = new InvestigationPage(m_workspace, m_pages);
    m_alerts = new AlertsPage(m_workspace, m_pages);
    m_pageList = {m_home, m_dashboard, m_alerts, m_investigation, m_events};
    const QStringList descriptions{
        qtTrId("window.navigation_home_help"),
        qtTrId("window.navigation_summary_help"),
        qtTrId("window.navigation_alerts_help"),
        qtTrId("window.navigation_investigation_help"),
        qtTrId("window.navigation_events_help"),
    };
    for (int i = 0; i < m_pageList.size(); ++i) {
        Page* page = m_pageList[i];
        m_pages->addWidget(page);
        m_sidebar->add(page->iconName(), page->title(), descriptions[i]);
        auto* action = new QAction(icon(page->iconName()), page->title(), this);
        action->setShortcut(QKeySequence(Qt::CTRL | static_cast<Qt::Key>(Qt::Key_1 + i)));
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, i] { showPage(i); });
        m_pageActions.append(action);
    }
    connect(m_sidebar, &Sidebar::pageRequested, this, &MainWindow::showPage);

    m_home->setAnalyses({m_ruleAnalysisAction, m_fileAnalysisAction, m_mitreAction});
    connect(m_home, &HomePage::importLogs, this, &MainWindow::importLogs);
    connect(m_home, &HomePage::importFolder, this, &MainWindow::importFolder);
    connect(m_home, &HomePage::openProject, this, &MainWindow::openProject);
    connect(m_home, &HomePage::openRecent, this, [this](const QString& path) {
        if (confirmDiscard())
            openProjectPath(path);
    });
    connect(m_home, &HomePage::auditGuide, this, [this] { GuideDialog(this).exec(); });
    connect(m_home, &HomePage::logsImported, this, &MainWindow::showSources);
    connect(m_dashboard, &DashboardPage::importLogs, this, &MainWindow::importLogs);
    connect(m_events, &EventsPage::exportEvents, this, &MainWindow::exportEvents);
    connect(m_alerts, &AlertsPage::settingsRequested, this,
            [this] { showSettings(SettingsDialog::Tab::Detection); });
    connect(m_alerts, &AlertsPage::reanalysisRequested, this, [this] { reanalyze(true); });
    connect(m_alerts, &AlertsPage::exportRequested, this, &MainWindow::exportAlerts);

    m_dataStatus = new QLabel(this);
    m_timeZoneStatus = new QToolButton(this);
    m_timeZoneStatus->setAutoRaise(true);
    m_timeZoneStatus->setToolTip(qtTrId("window.time_zone_help"));
    connect(m_timeZoneStatus, &QToolButton::clicked, this,
            [this] { showSettings(SettingsDialog::Tab::General); });
    m_dataStatus->setContentsMargins(0, 0, 10, 0);
    statusBar()->addPermanentWidget(m_dataStatus);
    statusBar()->addPermanentWidget(m_timeZoneStatus);
}

void MainWindow::createMenus()
{
    QMenu* project = menuBar()->addMenu(qtTrId("window.menu_project"));
    project->addAction(m_newAction);
    project->addAction(m_openAction);
    m_recentsMenu = project->addMenu(icon("clock"), qtTrId("window.menu_recent_projects"));
    project->addSeparator();
    project->addAction(m_saveAction);
    project->addAction(m_saveAsAction);
    project->addSeparator();
    project->addAction(m_detectionSettingsAction);
    project->addSeparator();
    project->addAction(m_quitAction);

    QMenu* logs = menuBar()->addMenu(qtTrId("window.menu_logs"));
    logs->addAction(m_importAction);
    logs->addAction(m_importFolderAction);
    logs->addSeparator();
    logs->addAction(m_sourcesAction);

    QMenu* analysis = menuBar()->addMenu(qtTrId("window.menu_analysis"));
    analysis->setToolTipsVisible(true);
    analysis->addAction(m_ruleAnalysisAction);
    analysis->addAction(m_fileAnalysisAction);
    analysis->addAction(m_mitreAction);
    analysis->addSeparator();
    analysis->addAction(m_reanalyzeAction);
    analysis->addAction(m_rulesAction);
    analysis->addAction(m_settingsAction);

    QMenu* reports = menuBar()->addMenu(qtTrId("window.menu_reports"));
    reports->addAction(m_reportAction);
    reports->addSeparator();
    reports->addAction(m_exportAction);
    reports->addAction(m_exportAlertsAction);

    QMenu* display = menuBar()->addMenu(qtTrId("window.menu_view"));
    display->addAction(m_backAction);
    display->addSeparator();
    auto* pageGroup = new QActionGroup(this);
    for (QAction* action : m_pageActions) {
        pageGroup->addAction(action);
        display->addAction(action);
    }
    display->addSeparator();
    display->addAction(m_searchAction);
    display->addAction(m_panelAction);
    display->addAction(m_resetAction);
    display->addSeparator();
    QMenu* themeMenu = display->addMenu(icon("theme"), qtTrId("window.menu_theme"));
    m_themeGroup = new QActionGroup(this);
    const QList<QPair<QString, ThemeMode>> themes{{qtTrId("common.same_as_system"), ThemeMode::System},
                                                  {qtTrId("common.theme_light"), ThemeMode::Light},
                                                  {qtTrId("common.theme_dark"), ThemeMode::Dark}};
    for (const auto& [label, mode] : themes) {
        QAction* action = themeMenu->addAction(label);
        action->setCheckable(true);
        action->setChecked(theme::mode() == mode);
        action->setData(static_cast<int>(mode));
        m_themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [mode] { theme::apply(mode); });
    }
    display->addAction(m_timeZoneAction);
    display->addAction(m_fullScreenAction);

    QMenu* help = menuBar()->addMenu(qtTrId("window.menu_help"));
    QAction* guide = help->addAction(icon("help"), qtTrId("window.action_guide"), this,
                                     [this] { GuideDialog(this).exec(); });
    guide->setShortcut(QKeySequence::HelpContents);
    help->addSeparator();
    QAction* about = help->addAction(icon("info"), qtTrId("window.action_about"), this,
                                     [this] { AboutDialog(this).exec(); });
    about->setMenuRole(QAction::AboutRole);
    QAction* qtAbout = help->addAction(qtTrId("window.action_about_qt"), qApp, &QApplication::aboutQt);
    qtAbout->setMenuRole(QAction::AboutQtRole);
}

void MainWindow::restoreSettings()
{
    const QByteArray geometry = settings::read(settings::key::GEOMETRY).toByteArray();
    if (geometry.isEmpty() || !restoreGeometry(geometry)) {
        const QRect screenArea = screen() ? screen()->availableGeometry() : QRect(0, 0, 1440, 900);
        resize(std::min(1440, screenArea.width() * 9 / 10), std::min(900, screenArea.height() * 9 / 10));
        move(screenArea.center() - rect().center());
    }
    m_filtersButton->setChecked(settings::read(settings::key::FILTER_PANEL, false).toBool());
    m_panelAction->setChecked(m_filtersButton->isChecked());
}

// ── Navigation and display ───────────────────────────────────────────────────

void MainWindow::showPage(int index)
{
    index = std::clamp(index, 0, static_cast<int>(m_pageList.size()) - 1);
    m_pages->setCurrentIndex(index);
    m_sidebar->select(index);
    m_pageActions[index]->setChecked(true);
    updateHeader();
    noteView();
}

void MainWindow::navigate(const Navigation& navigation)
{
    switch (navigation.target) {
    case Navigation::Target::Home:
        showPage(PAGE_HOME);
        break;
    case Navigation::Target::Dashboard:
        showPage(PAGE_DASHBOARD);
        break;
    case Navigation::Target::Events:
        showPage(PAGE_EVENTS);
        break;
    case Navigation::Target::Alerts:
        if (!navigation.value.isEmpty())
            m_alerts->select(navigation.value);
        showPage(PAGE_ALERTS);
        break;
    case Navigation::Target::User:
    case Navigation::Target::File:
    case Navigation::Target::Folder: {
        showPage(PAGE_INVESTIGATION);
        const auto tab = navigation.target == Navigation::Target::User   ? InvestigationPage::Tab::Users
                         : navigation.target == Navigation::Target::File ? InvestigationPage::Tab::Files
                                                                         : InvestigationPage::Tab::Folders;
        m_investigation->focusOn(tab, navigation.value);
        break;
    }
    }
}

MainWindow::ViewState MainWindow::currentView() const
{
    ViewState view;
    view.page = m_pages->currentIndex();
    view.filters = m_workspace->filters();
    if (view.page == PAGE_INVESTIGATION) {
        view.tab = static_cast<int>(m_investigation->tab());
        view.value = m_investigation->value();
    }
    return view;
}

void MainWindow::noteView()
{
    if (m_restoring)
        return;
    m_viewTimer.start();
    updateBack();
}

void MainWindow::rememberView()
{
    m_viewTimer.stop();
    const ViewState view = currentView();
    if (view == m_view)
        return;
    if (m_view.page >= 0) {
        m_history.append(m_view);
        if (m_history.size() > HISTORY_LIMIT)
            m_history.removeFirst();
    }
    m_view = view;
    updateBack();
}

void MainWindow::goBack()
{
    rememberView();
    if (m_history.isEmpty())
        return;
    const ViewState view = m_history.takeLast();
    m_restoring = true;
    m_workspace->setFilters(view.filters);
    showPage(view.page);
    if (view.page == PAGE_INVESTIGATION)
        m_investigation->restore(static_cast<InvestigationPage::Tab>(view.tab), view.value);
    m_restoring = false;
    m_viewTimer.stop();
    m_view = currentView();
    updateBack();
}

void MainWindow::clearHistory()
{
    m_viewTimer.stop();
    m_history.clear();
    m_view = {};
    updateBack();
}

void MainWindow::updateBack()
{
    m_backAction->setEnabled(!m_history.isEmpty() || (m_view.page >= 0 && currentView() != m_view));
}

bool MainWindow::eventFilter(QObject* object, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress
        && static_cast<QMouseEvent*>(event)->button() == Qt::BackButton && object->isWidgetType()
        && static_cast<QWidget*>(object)->window() == this) {
        m_backAction->trigger();
        return true;
    }
    return QMainWindow::eventFilter(object, event);
}

void MainWindow::updateHeader()
{
    const Page* page = m_pageList[m_pages->currentIndex()];
    m_pageTitle->setText(page->title());
    const bool filters = page->usesFilters();
    const Filters& f = m_workspace->filters();
    m_search->setVisible(filters);
    m_filtersButton->setVisible(filters);
    m_importButton->setVisible(page != m_home);
    m_chips->setActive(filters);
    m_panel->setVisible(filters && m_filtersButton->isChecked());
    m_search->changeText(f.text);

    const int criteria = f.activeCount() - (f.text.trimmed().isEmpty() ? 0 : 1);
    m_filtersButton->setText(criteria > 0 ? qtTrId("window.filters_count").arg(criteria) : qtTrId("common.filters"));

    QString subtitle;
    if (!filters)
        subtitle = m_workspace->isEmpty() ? QString() : qtTrId("common.project_name").arg(m_workspace->documentName());
    else if (m_workspace->isEmpty())
        subtitle = qtTrId("window.no_data_imported");
    else if (f.isEmpty())
        subtitle = qtTrId("window.all_data");
    else
        subtitle = qtTrId("window.status_filter").arg(f.description(m_workspace->timeZone()));
    m_pageSubtitle->setText(subtitle);
    m_pageSubtitle->setToolTip(subtitle);
}

void MainWindow::updateTitle()
{
    setWindowTitle(QStringLiteral("%1[*] · " LOGHUNTER_NAME).arg(m_workspace->documentName()));
    setWindowModified(m_workspace->isModified());
}

void MainWindow::updateActions()
{
    const bool idle = !m_workspace->busy();
    const bool hasData = !m_workspace->isEmpty();
    for (QAction* action : {m_newAction, m_openAction, m_importAction, m_importFolderAction})
        action->setEnabled(idle);
    m_saveAction->setEnabled(idle);
    m_saveAsAction->setEnabled(idle && hasData);
    m_exportAction->setEnabled(idle && hasData);
    m_exportAlertsAction->setEnabled(idle && hasData);
    m_reportAction->setEnabled(idle && hasData);
    for (QAction* action :
         {m_ruleAnalysisAction, m_fileAnalysisAction, m_mitreAction, m_rulesAction, m_settingsAction})
        action->setEnabled(idle);
    m_importButton->setEnabled(idle);
    m_recentsMenu->setEnabled(idle);
    m_backButton->setVisible(hasData);

    for (int i = PAGE_HOME + 1; i < m_pageList.size(); ++i) {
        m_sidebar->setItemVisible(i, hasData);
        m_pageActions[i]->setEnabled(hasData);
    }
    for (QAction* action : {m_searchAction, m_panelAction, m_resetAction})
        action->setEnabled(hasData);
    m_reanalyzeAction->setEnabled(idle && hasData);
    if (!hasData && m_pages->currentIndex() != PAGE_HOME)
        showPage(PAGE_HOME);
}

void MainWindow::updateStatus()
{
    if (m_workspace->isEmpty())
        m_dataStatus->setText(qtTrId("common.no_data"));
    else
        m_dataStatus->setText(QStringLiteral("%1 · %2").arg(
            formatEvents(m_workspace->eventCount()),
            qtTrId("common.log_count", pluralCount(m_workspace->sources().size()))));
    m_timeZoneStatus->setText(qtTrId("window.status_time_zone").arg(QString::fromLatin1(m_workspace->timeZone())));
}

void MainWindow::updateRecents()
{
    m_recentsMenu->clear();
    const QStringList projects = settings::recentProjects();
    for (int i = 0; i < projects.size(); ++i) {
        const QString path = projects[i];
        const QFileInfo info(path);
        QAction* action = m_recentsMenu->addAction(
            QStringLiteral("&%1  %2").arg(i + 1 < 10 ? QString::number(i + 1) : QStringLiteral("0"), info.completeBaseName()),
            this, [this, path] {
                if (confirmDiscard())
                    openProjectPath(path);
            });
        action->setToolTip(QDir::toNativeSeparators(path));
        action->setStatusTip(QDir::toNativeSeparators(path));
        action->setEnabled(info.exists());
    }
    if (projects.isEmpty()) {
        m_recentsMenu->addAction(qtTrId("window.no_recent_project"))->setEnabled(false);
        return;
    }
    m_recentsMenu->addSeparator();
    m_recentsMenu->addAction(qtTrId("window.clear_list"), this, [this] {
        settings::clearRecents();
        updateRecents();
    });
}

// ── Project ──────────────────────────────────────────────────────────────────

bool MainWindow::confirmDiscard()
{
    if (!m_workspace->isModified())
        return true;
    MessageDialog box(MessageDialog::Type::Warning,
                      qtTrId("window.save_project_question").arg(m_workspace->documentName()),
                      qtTrId("window.unsaved_data"),
                      this);
    QPushButton* saveButton = box.addButton(qtTrId("common.save"), true);
    QPushButton* discardButton = box.addButton(qtTrId("window.do_not_save"));
    box.addButton(qtTrId("common.cancel"));
    box.exec();
    if (box.clickedButton() == saveButton)
        return save();
    return box.clickedButton() == discardButton;
}

void MainWindow::newProject()
{
    if (m_workspace->busy() || !confirmDiscard())
        return;
    m_workspace->newSession();
    showPage(PAGE_HOME);
    statusBar()->showMessage(qtTrId("window.new_project"), 3000);
}

void MainWindow::openProject()
{
    if (m_workspace->busy() || !confirmDiscard())
        return;
    const QString path = QFileDialog::getOpenFileName(this, qtTrId("window.open_project"),
                                                      settings::rememberedFolder(settings::key::PROJECTS_FOLDER),
                                                      qtTrId("window.open_project_file_filter"));
    if (path.isEmpty())
        return;
    settings::rememberFolder(settings::key::PROJECTS_FOLDER, path);
    openProjectPath(path);
}

void MainWindow::openProjectPath(const QString& path)
{
    if (!QFileInfo::exists(path)) {
        MessageDialog::warn(this, qtTrId("window.project_not_found"),
                            qtTrId("window.file_missing").arg(QDir::toNativeSeparators(path)));
        settings::removeRecent(path);
        updateRecents();
        return;
    }
    std::unique_ptr<Session> session;
    const bool ok = execute(qtTrId("window.opening_project"),
                            qtTrId("window.opening_in_progress").arg(QFileInfo(path).fileName()), false,
                            [&](TaskProgress& taskProgress) {
                                session = Session::openProject(path, [&taskProgress](double fraction) {
                                    taskProgress.perMille = static_cast<int>(fraction * 1000);
                                });
                            });
    if (!ok || !session)
        return;
    m_workspace->replaceSession(std::move(session));
    settings::addRecent(path);
    updateRecents();
    showPage(m_workspace->isEmpty() ? PAGE_HOME : PAGE_DASHBOARD);
    statusBar()->showMessage(qtTrId("window.status_project_opened").arg(QDir::toNativeSeparators(path)), 5000);
}

bool MainWindow::save()
{
    const QString path = m_workspace->session().projectPath();
    return path.isEmpty() ? saveAs() : saveTo(path);
}

bool MainWindow::saveAs()
{
    const QString name = m_workspace->session().projectPath().isEmpty()
                             ? qtTrId("window.analysis_in_progress").arg(dayDate())
                             : m_workspace->documentName();
    const QString proposed = QDir(settings::rememberedFolder(settings::key::PROJECTS_FOLDER))
                                 .filePath(name + QLatin1String(PROJECT_EXTENSION));
    QString path = QFileDialog::getSaveFileName(this, qtTrId("window.save_project"), proposed,
                                                qtTrId("window.project_file_filter"));
    if (path.isEmpty())
        return false;
    if (!isProject(path))
        path += QLatin1String(PROJECT_EXTENSION);
    settings::rememberFolder(settings::key::PROJECTS_FOLDER, path);
    return saveTo(path);
}

bool MainWindow::saveTo(const QString& path)
{
    if (m_workspace->busy())
        return false;
    const QString work = m_workspace->session().workPath();
    const bool ok = execute(qtTrId("window.saving_project"),
                            qtTrId("window.saving_in_progress").arg(QFileInfo(path).fileName()), false,
                            [work, path](TaskProgress&) { Session::saveProject(work, path); });
    if (!ok)
        return false;
    m_workspace->markSaved(path);
    settings::addRecent(path);
    updateRecents();
    statusBar()->showMessage(qtTrId("window.status_project_saved").arg(QDir::toNativeSeparators(path)), 5000);
    return true;
}

void MainWindow::openPaths(const QStringList& paths)
{
    QStringList projects;
    QStringList logs;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.isDir())
            logs += Importer::listLogs(info.absoluteFilePath());
        else if (isProject(path))
            projects << info.absoluteFilePath();
        else if (isLog(path))
            logs << info.absoluteFilePath();
    }
    if (!projects.isEmpty()) {
        if (!confirmDiscard())
            return;
        openProjectPath(projects.first());
    }
    if (!logs.isEmpty())
        importFiles(logs);
    else if (projects.isEmpty() && !paths.isEmpty())
        MessageDialog::inform(this, qtTrId("window.no_recognized_file"),
                              qtTrId("window.no_recognized_file_explanation"));
}

// ── Data ─────────────────────────────────────────────────────────────────────

void MainWindow::importLogs()
{
    if (m_workspace->busy())
        return;
    const QStringList paths = QFileDialog::getOpenFileNames(this, qtTrId("window.import_logs"),
                                                            settings::rememberedFolder(settings::key::LOGS_FOLDER),
                                                            qtTrId("window.logs_file_filter"));
    if (paths.isEmpty())
        return;
    settings::rememberFolder(settings::key::LOGS_FOLDER, paths.first());
    importFiles(paths);
}

void MainWindow::importFolder()
{
    if (m_workspace->busy())
        return;
    const QString folder = QFileDialog::getExistingDirectory(
        this, qtTrId("window.import_folder"), settings::rememberedFolder(settings::key::LOGS_FOLDER));
    if (folder.isEmpty())
        return;
    settings::rememberFolder(settings::key::LOGS_FOLDER, folder);
    importFiles({folder});
}

void MainWindow::importFiles(const QStringList& paths)
{
    if (m_workspace->busy() || m_import)
        return;
    QStringList logs;
    for (const QString& path : paths) {
        const QFileInfo info(path);
        if (info.isDir())
            logs += Importer::listLogs(info.absoluteFilePath());
        else if (info.isFile())
            logs << info.absoluteFilePath();
    }
    logs.removeDuplicates();
    if (logs.isEmpty()) {
        MessageDialog::inform(this, qtTrId("window.no_log_found"),
                              qtTrId("window.no_log_found_explanation"));
        return;
    }
    const bool firstImport = m_workspace->isEmpty();
    auto* dialog = new ImportDialog(m_workspace, logs, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    m_import = dialog;
    connect(dialog, &ImportDialog::completed, this, [this, firstImport](const ImportReport& report) {
        statusBar()->showMessage(
            qtTrId("window.status_import_finished")
                .arg(qtTrId("window.events_added_count", pluralCount(report.addedEvents()))),
            8000);
        if (firstImport && !m_workspace->isEmpty() && m_pages->currentIndex() == PAGE_HOME)
            showPage(PAGE_DASHBOARD);
    });
    dialog->open();
    dialog->start();
}

QString MainWindow::chooseExportFile(const QString& title, const QString& name, exporter::Format& format)
{
    QString filter;
    QString path = QFileDialog::getSaveFileName(
        this, title, QDir(settings::rememberedFolder(settings::key::EXPORTS_FOLDER)).filePath(name),
        qtTrId("window.export_file_filter"), &filter);
    if (path.isEmpty())
        return {};
    QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix != QLatin1String("xlsx") && suffix != QLatin1String("csv") && suffix != QLatin1String("json")) {
        suffix = filter.contains(QLatin1String("csv")) ? QStringLiteral("csv")
                 : filter.contains(QLatin1String("json")) ? QStringLiteral("json")
                                                          : QStringLiteral("xlsx");
        path += QLatin1Char('.') + suffix;
    }
    settings::rememberFolder(settings::key::EXPORTS_FOLDER, path);
    format = suffix == QLatin1String("csv")    ? exporter::Format::Csv
             : suffix == QLatin1String("json") ? exporter::Format::Json
                                               : exporter::Format::Excel;
    return path;
}

void MainWindow::exportEvents()
{
    if (m_workspace->busy() || m_workspace->isEmpty())
        return;
    exporter::Format format = exporter::Format::Excel;
    const QString path = chooseExportFile(qtTrId("window.export_filtered_events"),
                                          qtTrId("window.export_file_name").arg(dayDate()), format);
    if (path.isEmpty())
        return;
    const Filters filters = m_workspace->filters();
    const QString work = m_workspace->session().workPath();
    exporter::Result result;
    const bool ok = execute(
        qtTrId("window.export"), qtTrId("window.export_in_progress").arg(QFileInfo(path).fileName()), true,
        [&](TaskProgress& taskProgress) {
            sql::Connection connection = db::open(work);
            result = exporter::exportEvents(
                connection, filters, format, path,
                [&taskProgress](qint64 rows, qint64 total) {
                    if (total > 0)
                        taskProgress.perMille = static_cast<int>(rows * 1000 / total);
                },
                &taskProgress.cancellation);
        });
    if (!ok)
        return;
    if (result.cancelled) {
        statusBar()->showMessage(qtTrId("window.export_cancelled"), 5000);
        return;
    }
    QString outcome = qtTrId("window.rows_exported_count", pluralCount(result.rows))
                          .arg(QFileInfo(path).fileName());
    if (result.sheets > 1)
        outcome += QLatin1Char(' ') + qtTrId("window.export_multiple_sheets").arg(result.sheets);
    offerOpen(this, outcome, path);
}

void MainWindow::exportAlerts()
{
    if (m_workspace->busy() || m_workspace->isEmpty())
        return;
    exporter::Format format = exporter::Format::Excel;
    const QString path = chooseExportFile(qtTrId("window.export_alerts"),
                                          qtTrId("window.alerts_file_name").arg(dayDate()), format);
    if (path.isEmpty())
        return;
    const QString scope = m_workspace->filters().description(m_workspace->timeZone());
    m_workspace->detections(this, [this, path, format, scope](QList<Detection> detections) {
        if (!m_workspace->parameters().mitre)
            for (Detection& detection : detections)
                detection.tags.clear();
        exporter::Result result;
        try {
            result = exporter::exportAlerts(detections, scope, format, path);
        } catch (const std::exception& e) {
            MessageDialog::showError(this, qtTrId("window.export"), QString::fromUtf8(e.what()));
            return;
        }
        offerOpen(this,
                  qtTrId("window.rows_exported_count", pluralCount(result.rows))
                      .arg(QFileInfo(path).fileName()),
                  path);
    });
}

void MainWindow::reanalyze(bool complete)
{
    if (m_workspace->busy() || m_workspace->isEmpty())
        return;
    const sigma::Configuration configuration = m_workspace->parameters().ruleConfiguration();
    const QString work = m_workspace->session().workPath();
    sigma::Outcome outcome;
    int errors = 0;
    const bool ok = execute(qtTrId("window.threat_detection"), qtTrId("window.detection_in_progress"), true,
                            [&](TaskProgress& taskProgress) {
                                const sigma::RuleSet rules = sigma::load(configuration);
                                errors = static_cast<int>(rules.errors.size());
                                sql::Connection connection = db::open(work, sql::Connection::ReadWrite);
                                outcome = sigma::analyze(
                                    connection, rules, complete,
                                    [&taskProgress](qint64 readCount, qint64 total) {
                                        if (total > 0)
                                            taskProgress.perMille = static_cast<int>(readCount * 1000 / total);
                                    },
                                    taskProgress.cancellation);
                            });
    if (!ok)
        return;
    if (outcome.cancelled) {
        statusBar()->showMessage(qtTrId("window.detection_cancelled"), 5000);
        return;
    }
    m_workspace->notifyDataModified();
    QString message = qtTrId("window.detection_finished", pluralCount(outcome.matches)).arg(outcome.rules);
    if (errors > 0)
        message += QLatin1Char(' ') + qtTrId("window.rules_ignored", errors);
    statusBar()->showMessage(message, 10'000);
}

void MainWindow::generateReport()
{
    if (m_workspace->busy() || m_workspace->isEmpty())
        return;
    const QString project = m_workspace->session().projectPath().isEmpty() ? QString() : m_workspace->documentName();
    const Filters filters = m_workspace->filters();
    ReportDialog dialog(project, filters.description(m_workspace->timeZone()), this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const report::Options options = dialog.options();
    const QString path = dialog.path();
    const AlertParameters parameters = m_workspace->parameters();
    const QByteArray timeZone = m_workspace->timeZone();
    const QString work = m_workspace->session().workPath();
    bool cancelled = false;
    Caches& caches = m_workspace->executor().caches();
    const bool ok = execute(qtTrId("window.report_creation"), qtTrId("window.report_generation"), true,
                            [&](TaskProgress& taskProgress) {
                                sql::Connection connection = db::open(work);
                                Context context{connection, caches, &taskProgress.cancellation};
                                try {
                                    const report::Data data =
                                        report::collect(context, filters, parameters, timeZone);
                                    report::write(data, options, path);
                                } catch (const sql::Error& error) {
                                    if (!error.isInterruption())
                                        throw;
                                    cancelled = true;
                                }
                            });
    if (!ok || cancelled)
        return;
    statusBar()->showMessage(qtTrId("window.status_report_saved").arg(QDir::toNativeSeparators(path)), 8000);
    if (dialog.openAfter())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::showSources()
{
    SourcesDialog dialog(m_workspace, this);
    connect(&dialog, &SourcesDialog::importRequested, this,
            [this] { QTimer::singleShot(0, this, &MainWindow::importLogs); });
    connect(&dialog, &SourcesDialog::removalRequested, this, [this](const QList<qint64>& identifiers) {
        QTimer::singleShot(0, this, [this, identifiers] { removeSources(identifiers); });
    });
    dialog.exec();
}

void MainWindow::removeSources(const QList<qint64>& identifiers)
{
    if (m_workspace->busy() || identifiers.isEmpty())
        return;
    const QString work = m_workspace->session().workPath();
    const bool ok = execute(qtTrId("window.removing_logs"), qtTrId("window.deleting_events"), false,
                            [work, identifiers](TaskProgress&) {
                                sql::Connection connection = db::open(work, sql::Connection::ReadWrite);
                                for (const qint64 identifier : identifiers)
                                    db::removeSource(connection, identifier);
                            });
    if (!ok)
        return;
    const auto forgetSource = [&identifiers](ViewState& view) {
        if (identifiers.contains(view.filters.sourceId))
            view.filters.sourceId = 0;
    };
    forgetSource(m_view);
    for (ViewState& view : m_history)
        forgetSource(view);
    m_history.erase(std::unique(m_history.begin(), m_history.end()), m_history.end());
    Filters filters = m_workspace->filters();
    if (identifiers.contains(filters.sourceId)) {
        filters.sourceId = 0;
        m_workspace->setFilters(filters);
    }
    m_workspace->notifyDataModified();
    statusBar()->showMessage(qtTrId("window.logs_removed_count", pluralCount(identifiers.size())), 5000);
}

void MainWindow::showSettings(SettingsDialog::Tab tab)
{
    SettingsDialog dialog(m_workspace->parameters(), m_workspace->timeZone(), tab, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const bool displayChanged =
        dialog.language() != chosenLanguage() || dialog.dateFormat() != chosenDateFormat();
    theme::apply(dialog.theme());
    chooseLanguage(dialog.language());
    chooseDateFormat(dialog.dateFormat());
    for (QAction* action : m_themeGroup->actions())
        action->setChecked(action->data().toInt() == static_cast<int>(dialog.theme()));
    const bool rulesChanged =
        !(dialog.parameters().ruleConfiguration() == m_workspace->parameters().ruleConfiguration());
    m_workspace->setParameters(dialog.parameters());
    const QByteArray chosen = dialog.timeZone();
    Workspace::setDefaultTimeZone(chosen);
    const QByteArray effective = chosen.isEmpty() ? systemTimeZone() : chosen;
    if (effective != m_workspace->timeZone())
        applyTimeZone(effective);
    if (dialog.rulesRequested())
        showRules(rulesChanged);
    else if (rulesChanged)
        reanalyze(true);
    if (displayChanged && restartRequired())
        proposeRestart();
}

void MainWindow::showRules(bool reanalysisPending)
{
    if (m_workspace->busy())
        return;
    RulesDialog dialog(m_workspace->parameters(), this);
    dialog.exec();
    AlertParameters parameters = m_workspace->parameters();
    parameters.disabledRules = dialog.disabledRules();
    parameters.minimumRuleLevel = dialog.minimumLevel();
    const bool rulesChanged =
        reanalysisPending || (parameters.ruleAnalysis && dialog.filesChanged())
        || !(parameters.ruleConfiguration() == m_workspace->parameters().ruleConfiguration());
    m_workspace->setParameters(parameters);
    if (rulesChanged)
        reanalyze(true);
}

void MainWindow::proposeRestart()
{
    MessageDialog box(MessageDialog::Type::Question, qtTrId("window.restart_question"),
                      qtTrId("window.restart_explanation"), this);
    QPushButton* restartButton = box.addButton(qtTrId("window.restart"), true);
    box.addButton(qtTrId("window.restart_later"));
    box.exec();
    if (box.clickedButton() != restartButton || !close())
        return;
    const QString project = m_workspace->session().projectPath();
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(),
                                 project.isEmpty() ? QStringList() : QStringList{project}))
        qCWarning(log) << "Restart failed";
}

void MainWindow::toggleAnalysisType(bool AlertParameters::*type, bool active)
{
    AlertParameters parameters = m_workspace->parameters();
    if (parameters.*type == active)
        return;
    parameters.*type = active;
    const bool rulesChanged = !(parameters.ruleConfiguration() == m_workspace->parameters().ruleConfiguration());
    m_workspace->setParameters(parameters);
    if (rulesChanged)
        reanalyze(true);
}

void MainWindow::updateAnalysisTypes()
{
    const AlertParameters& parameters = m_workspace->parameters();
    m_ruleAnalysisAction->setChecked(parameters.ruleAnalysis);
    m_fileAnalysisAction->setChecked(parameters.fileAnalysis);
    m_mitreAction->setChecked(parameters.mitre);
}

void MainWindow::applyTimeZone(const QByteArray& timeZone)
{
    if (m_workspace->busy()) {
        MessageDialog::inform(this, qtTrId("common.time_zone"),
                              qtTrId("window.time_zone_deferred"));
        return;
    }
    const QString work = m_workspace->session().workPath();
    const bool ok =
        execute(qtTrId("common.time_zone"), qtTrId("window.time_zone_conversion").arg(QString::fromLatin1(timeZone)),
                false, [work, timeZone](TaskProgress&) {
                    sql::Connection connection = db::open(work, sql::Connection::ReadWrite);
                    db::recomputeTimeZone(connection, timeZone);
                });
    if (ok)
        m_workspace->notifyDataModified(!m_workspace->isEmpty());
}

bool MainWindow::execute(const QString& title, const QString& text, bool cancellable,
                         const std::function<void(TaskProgress&)>& work)
{
    TaskProgress taskProgress;
    QString error;
    const bool wasBusy = m_workspace->busy();
    m_workspace->setBusy(true);
    QFuture<void> future = QtConcurrent::run([&] {
        try {
            work(taskProgress);
        } catch (const std::exception& e) {
            error = QString::fromUtf8(e.what());
        } catch (...) {
            error = qtTrId("window.unexpected_error");
        }
    });
    QFutureWatcher<void> watcher;
    QEventLoop loop;
    connect(&watcher, &QFutureWatcher<void>::finished, &loop, &QEventLoop::quit);
    watcher.setFuture(future);

    QTimer delay;
    delay.setSingleShot(true);
    connect(&delay, &QTimer::timeout, &loop, &QEventLoop::quit);
    delay.start(400);
    if (!future.isFinished())
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    if (!future.isFinished()) {
        ProgressWindow progress(title, text, cancellable, taskProgress, this);
        progress.show();
        loop.exec();
        progress.hide();
    }
    future.waitForFinished();
    m_workspace->setBusy(wasBusy);
    if (!error.isEmpty()) {
        qCWarning(log).noquote() << title << ":" << error;
        MessageDialog::showError(this, title, error);
        return false;
    }
    return true;
}

// ── Window events ────────────────────────────────────────────────────────────

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (m_import && m_import->isRunning()) {
        if (!MessageDialog::confirm(this, qtTrId("window.import_in_progress"),
                                    qtTrId("window.quit_during_import"),
                                    qtTrId("window.quit"), MessageDialog::Type::Warning)) {
           event->ignore();
           return;
       }
        m_import->cancelAndWait();
    }
    if (m_workspace->busy() || !confirmDiscard()) {
        event->ignore();
        return;
    }
    settings::write(settings::key::GEOMETRY, saveGeometry());
    event->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (m_workspace->busy() || !event->mimeData()->hasUrls())
        return;
    for (const QUrl& url : event->mimeData()->urls()) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty() && (QFileInfo(path).isDir() || isLog(path) || isProject(path))) {
            event->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls())
        if (url.isLocalFile())
            paths << url.toLocalFile();
    event->acceptProposedAction();
    QTimer::singleShot(0, this, [this, paths] { openPaths(paths); });
}

} // namespace lh::ui
