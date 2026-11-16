#include "HomePage.h"

#include "core/Format.h"
#include "core/DateTime.h"
#include "ui/Workspace.h"
#include "ui/Icons.h"
#include "ui/Settings.h"
#include "ui/Theme.h"
#include "ui/widgets/Components.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace lh::ui {

namespace {

class DropZone : public QWidget
{
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPalette& pal = palette();
        const QColor accent = theme::brand(pal);
        painter.setBrush(theme::blend(theme::cardBackground(pal), accent, 0.04));
        QPen stroke(theme::blend(theme::border(pal), accent, 0.45), 1.4, Qt::DashLine);
        stroke.setDashPattern({5, 4});
        painter.setPen(stroke);
        painter.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 10, 10);
    }
};

class RecentRow : public QAbstractButton
{
public:
    RecentRow(const QString& path, QWidget* parent) : QAbstractButton(parent), m_path(path)
    {
        const QFileInfo info(path);
        m_exists = info.exists();
        m_name = info.completeBaseName();
        m_detail = m_exists ? qtTrId("home.project_modified_on")
                                  .arg(QDir::toNativeSeparators(info.absolutePath()), formatDateTime(info.lastModified()))
                            : qtTrId("home.file_not_found").arg(QDir::toNativeSeparators(path));
        setToolTip(QDir::toNativeSeparators(path));
        setCursor(m_exists ? Qt::PointingHandCursor : Qt::ArrowCursor);
        setEnabled(m_exists);
        setAttribute(Qt::WA_Hover);
        setFocusPolicy(Qt::TabFocus);
    }

    [[nodiscard]] QSize sizeHint() const override
    {
        return {300, fontMetrics().height() * 2 + 18};
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QPalette& pal = palette();
        if (underMouse() || hasFocus()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(theme::hoverBackground(pal));
            painter.drawRoundedRect(QRectF(rect()), 6, 6);
        }
        const int side = 20;
        const QRect iconZone(10, (height() - side) / 2, side, side);
        ui::icon("file", m_exists ? theme::brand(pal) : theme::secondaryText(pal)).paint(&painter, iconZone);
        const int left = iconZone.right() + 12;
        const int availableWidth = width() - left - 10;
        const QFontMetrics metrics(font());
        QFont bold = font();
        bold.setWeight(QFont::DemiBold);
        painter.setFont(bold);
        painter.setPen(m_exists ? theme::text(pal) : theme::secondaryText(pal));
        const int y = (height() - metrics.height() * 2 - 2) / 2;
        painter.drawText(QRect(left, y, availableWidth, metrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                         QFontMetrics(bold).elidedText(m_name, Qt::ElideRight, availableWidth));
        painter.setFont(font());
        painter.setPen(theme::secondaryText(pal));
        painter.drawText(QRect(left, y + metrics.height() + 2, availableWidth, metrics.height()),
                         Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(m_detail, Qt::ElideMiddle, availableWidth));
    }

private:
    QString m_path;
    QString m_name;
    QString m_detail;
    bool m_exists = false;
};

QWidget* step(int number, const QString& title, const QString& text, QWidget* parent)
{
    auto* container = new QWidget(parent);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 2, 0, 2);
    layout->setSpacing(12);
    auto* numberDot = new Label(QString::number(number), Label::Role::Title, container);
    numberDot->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    numberDot->setFixedWidth(22);
    numberDot->setColor(theme::brand(QApplication::palette()));
    layout->addWidget(numberDot, 0, Qt::AlignTop);
    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    texts->addWidget(new Label(title, Label::Role::Normal, container));
    auto* detail = new Label(text, Label::Role::Small, container);
    detail->setWordWrap(true);
    texts->addWidget(detail);
    layout->addLayout(texts, 1);
    return container;
}

} // namespace

HomePage::HomePage(Workspace* workspace, QWidget* parent) : Page(workspace, parent)
{
    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scrollArea);

    auto* page = new QWidget(scrollArea);
    scrollArea->setWidget(page);
    auto* centering = new QHBoxLayout(page);
    centering->setContentsMargins(24, 28, 24, 28);
    auto* column = new QWidget(page);
    column->setMaximumWidth(980);
    centering->addStretch();
    centering->addWidget(column, 20);
    centering->addStretch();
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);

    // ── Introduction ──
    auto* presentation = new QHBoxLayout;
    presentation->setSpacing(18);
    auto* logo = new QLabel(column);
    QIcon applicationIcon;
    for (const int size : {48, 64, 128, 256})
        applicationIcon.addFile(QStringLiteral(":/images/logo-%1.png").arg(size));
    logo->setPixmap(applicationIcon.pixmap(QSize(64, 64), devicePixelRatioF()));
    presentation->addWidget(logo);
    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    auto* name = new Label(QStringLiteral("LogHunter"), Label::Role::Large, column);
    texts->addWidget(name);
    texts->addWidget(new Label(qtTrId("home.introduction"),
                               Label::Role::Secondary, column));
    presentation->addLayout(texts, 1);
    layout->addLayout(presentation);

    // ── Get started ──
    auto* dropZone = new DropZone(column);
    auto* dropLayout = new QVBoxLayout(dropZone);
    dropLayout->setContentsMargins(28, 26, 28, 24);
    dropLayout->setSpacing(10);
    auto* dropIcon = new QLabel(dropZone);
    dropIcon->setAlignment(Qt::AlignCenter);
    dropIcon->setPixmap(icon("drop", theme::brand(QApplication::palette())).pixmap(QSize(40, 40), devicePixelRatioF()));
    dropLayout->addWidget(dropIcon);
    auto* placeholder = new Label(qtTrId("home.drop_title"), Label::Role::Title, dropZone);
    placeholder->setAlignment(Qt::AlignCenter);
    placeholder->setWordWrap(true);
    dropLayout->addWidget(placeholder);
    auto* subPrompt = new Label(qtTrId("home.drop_explanation"),
                                Label::Role::Secondary, dropZone);
    subPrompt->setAlignment(Qt::AlignCenter);
    subPrompt->setWordWrap(true);
    dropLayout->addWidget(subPrompt);
    dropLayout->addSpacing(6);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(8);
    buttons->addStretch();
    auto* importButton = actionButton(qtTrId("common.import_logs"), "import", dropZone, true);
    auto* folderButton = actionButton(qtTrId("home.import_folder"), "folder", dropZone);
    auto* projectButton = actionButton(qtTrId("home.open_project"), "open", dropZone);
    buttons->addWidget(importButton);
    buttons->addWidget(folderButton);
    buttons->addWidget(projectButton);
    buttons->addStretch();
    dropLayout->addLayout(buttons);
    dropLayout->addSpacing(4);
    m_analyses = new QHBoxLayout;
    m_analyses->setSpacing(18);
    m_analyses->addStretch();
    m_analyses->addWidget(new Label(qtTrId("home.analyses"), Label::Role::Secondary, dropZone));
    dropLayout->addLayout(m_analyses);
    layout->addWidget(dropZone);
    connect(importButton, &QPushButton::clicked, this, &HomePage::importLogs);
    connect(folderButton, &QPushButton::clicked, this, &HomePage::importFolder);
    connect(projectButton, &QPushButton::clicked, this, &HomePage::openProject);

    // ── Récents et projet__BTN en cours ──
    auto* row = new QHBoxLayout;
    row->setSpacing(18);
    m_recents = new Card(qtTrId("home.recent_projects"), column);
    m_recentsList = new QVBoxLayout;
    m_recentsList->setSpacing(2);
    m_recents->content()->addLayout(m_recentsList);
    m_recents->content()->addStretch();
    row->addWidget(m_recents, 1);

    m_project = new Card(qtTrId("home.current_project"), column);
    m_currentProject = new QWidget(m_project);
    auto* projectLayout = new QVBoxLayout(m_currentProject);
    projectLayout->setContentsMargins(0, 0, 0, 0);
    projectLayout->setSpacing(6);
    m_projectSummary = new Label({}, Label::Role::Value, m_currentProject);
    m_projectDetail = new Label({}, Label::Role::Secondary, m_currentProject);
    m_projectDetail->setWordWrap(true);
    projectLayout->addWidget(m_projectSummary);
    projectLayout->addWidget(m_projectDetail);
    projectLayout->addSpacing(8);
    auto* projectButtons = new QHBoxLayout;
    auto* dashboardButton = actionButton(qtTrId("home.see_summary"), "dashboard", m_currentProject, true);
    auto* logsButton = actionButton(qtTrId("home.imported_logs"), "logs", m_currentProject);
    projectButtons->addWidget(dashboardButton);
    projectButtons->addWidget(logsButton);
    projectButtons->addStretch();
    projectLayout->addLayout(projectButtons);
    projectLayout->addStretch();
    connect(dashboardButton, &QPushButton::clicked, this,
            [this] { m_workspace->navigate({Navigation::Target::Dashboard, {}}); });
    connect(logsButton, &QPushButton::clicked, this, &HomePage::logsImported);

    m_prerequisites = new QWidget(m_project);
    auto* prerequisitesLayout = new QVBoxLayout(m_prerequisites);
    prerequisitesLayout->setContentsMargins(0, 0, 0, 0);
    prerequisitesLayout->setSpacing(8);
    prerequisitesLayout->addWidget(step(1, qtTrId("home.enable_server_audit"),
                                        qtTrId("home.audit_explanation"),
                                        m_prerequisites));
    prerequisitesLayout->addWidget(step(2, qtTrId("home.get_security_log"),
                                        qtTrId("home.get_log_explanation"),
                                        m_prerequisites));
    prerequisitesLayout->addWidget(
        step(3, qtTrId("home.drop_here"), qtTrId("home.sorting_explanation"), m_prerequisites));
    auto* guide = new LinkButton(qtTrId("home.guide"), m_prerequisites);
    connect(guide, &LinkButton::clicked, this, &HomePage::auditGuide);
    prerequisitesLayout->addWidget(guide);
    prerequisitesLayout->addStretch();

    m_project->content()->addWidget(m_currentProject);
    m_project->content()->addWidget(m_prerequisites);
    row->addWidget(m_project, 1);
    layout->addLayout(row);
    layout->addStretch();

    connect(workspace, &Workspace::documentStateChanged, this, &HomePage::invalidate);
}

void HomePage::setAnalyses(const QList<QAction*>& actions)
{
    for (QAction* action : actions) {
        auto* analysisCheckBox = new QCheckBox(action->text(), this);
        analysisCheckBox->setToolTip(action->toolTip());
        analysisCheckBox->setChecked(action->isChecked());
        analysisCheckBox->setCursor(Qt::PointingHandCursor);
        connect(analysisCheckBox, &QCheckBox::clicked, action, &QAction::trigger);
        connect(action, &QAction::toggled, analysisCheckBox, &QCheckBox::setChecked);
        connect(action, &QAction::changed, analysisCheckBox, [analysisCheckBox, action] {
            analysisCheckBox->setEnabled(action->isEnabled());
        });
        m_analyses->addWidget(analysisCheckBox);
    }
    m_analyses->addStretch();
}

void HomePage::refresh()
{
    populateRecents();
    populateProject();
}

void HomePage::populateRecents()
{
    clearLayout(m_recentsList);
    const QStringList projects = settings::recentProjects();
    if (projects.isEmpty()) {
        auto* empty = new Label(qtTrId("home.recent_projects_empty"), Label::Role::Secondary, m_recents);
        empty->setWordWrap(true);
        m_recentsList->addWidget(empty);
        return;
    }
    for (const QString& path : projects.mid(0, 6)) {
        auto* row = new RecentRow(path, m_recents);
        row->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(row, &QAbstractButton::clicked, this, [this, path] { emit openRecent(path); });
        connect(row, &QWidget::customContextMenuRequested, this, [this, row, path](const QPoint& position) {
            QMenu menu(row);
            menu.addAction(icon("close"), qtTrId("home.remove_from_list"), this, [this, path] {
                settings::removeRecent(path);
                populateRecents();
            });
            menu.exec(row->mapToGlobal(position));
        });
        m_recentsList->addWidget(row);
    }
}

void HomePage::populateProject()
{
    const bool hasData = !m_workspace->isEmpty();
    m_project->setTitle(hasData ? qtTrId("home.current_project_name").arg(m_workspace->documentName())
                                : qtTrId("home.before_starting"));
    m_currentProject->setVisible(hasData);
    m_prerequisites->setVisible(!hasData);
    if (!hasData)
        return;
    m_projectSummary->setText(formatEvents(m_workspace->eventCount()));
    QString detail = qtTrId("common.imported_log_count", static_cast<int>(m_workspace->sources().size()));
    if (m_workspace->firstDate().isValid())
        detail += QStringLiteral(" · ")
                  + qtTrId("common.period_from_to")
                        .arg(formatDate(m_workspace->firstDate()), formatDate(m_workspace->lastDate()));
    m_projectDetail->setText(detail);
}

} // namespace lh::ui
