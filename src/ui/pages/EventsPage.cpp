#include "EventsPage.h"

#include "core/Actions.h"
#include "core/AccessAnalyzer.h"
#include "core/Catalog.h"
#include "core/Format.h"
#include "core/DateTime.h"
#include "ui/Workspace.h"
#include "ui/Icons.h"
#include "ui/Settings.h"
#include "ui/widgets/Timeline.h"
#include "ui/widgets/Components.h"
#include "ui/widgets/EventModel.h"
#include "ui/widgets/TableModel.h"
#include "ui/widgets/DetailPanel.h"
#include "ui/widgets/EventTable.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace lh::ui {

namespace {

constexpr int GROUP_LIMIT = 20'000;

QString sortDescription(queries::Column column, Qt::SortOrder order)
{
    const bool ascending = order == Qt::AscendingOrder;
    switch (column) {
    case queries::Column::Timestamp:
        return ascending ? qtTrId("events.sort_oldest_first") : qtTrId("events.sort_newest_first");
    case queries::Column::Level:
        return qtTrId("events.sort_level");
    case queries::Column::User:
        return qtTrId("events.sort_user");
    case queries::Column::Action:
        return qtTrId("events.sort_action");
    case queries::Column::Target:
        return qtTrId("events.sort_target");
    case queries::Column::Process:
        return qtTrId("events.sort_process");
    case queries::Column::Machine:
        return qtTrId("events.sort_machine");
    case queries::Column::Ip:
        return qtTrId("events.sort_ip");
    case queries::Column::Channel:
        return qtTrId("events.sort_log");
    case queries::Column::EventId:
        break;
    }
    return qtTrId("events.sort_id");
}

} // namespace

EventsPage::EventsPage(Workspace* workspace, QWidget* parent) : Page(workspace, parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(24, 2, 24, 8);
    bar->setSpacing(8);
    m_count = new Label({}, Label::Role::Title, this);
    m_sortLabel = new Label({}, Label::Role::Secondary, this);
    bar->addWidget(m_count);
    bar->addWidget(m_sortLabel);
    bar->addStretch();
    m_timelineButton = toolButton("activity", qtTrId("events.timeline"), this);
    m_timelineButton->setCheckable(true);
    m_timelineButton->setToolTip(qtTrId("events.timeline_help"));
    m_groupButton = toolButton("dashboard", qtTrId("events.group"), this);
    m_groupButton->setCheckable(true);
    m_groupButton->setToolTip(qtTrId("events.group_help"));
    auto* copyButton = toolButton("copy", qtTrId("common.copy"), this);
    copyButton->setToolTip(qtTrId("events.copy_rows"));
    auto* exportButton = toolButton("export", qtTrId("events.export"), this);
    exportButton->setToolTip(qtTrId("events.export_help"));
    m_detailButton = toolButton("panel", qtTrId("common.detail"), this);
    m_detailButton->setCheckable(true);
    m_detailButton->setToolTip(qtTrId("events.detail_panel"));
    layout->addLayout(bar);

    m_timelineFrame = new QWidget(this);
    auto* timelineLayout = new QVBoxLayout(m_timelineFrame);
    timelineLayout->setContentsMargins(24, 0, 24, 8);
    timelineLayout->setSpacing(2);
    auto* timelineHeader = new QHBoxLayout;
    timelineHeader->addWidget(new Label(qtTrId("timeline.help"), Label::Role::Small, m_timelineFrame));
    timelineHeader->addStretch();
    m_wholePeriod = new LinkButton(qtTrId("timeline.whole_period"), m_timelineFrame);
    timelineHeader->addWidget(m_wholePeriod);
    timelineLayout->addLayout(timelineHeader);
    m_timeline = new Timeline(workspace, m_timelineFrame);
    timelineLayout->addWidget(m_timeline);
    layout->addWidget(m_timelineFrame);

    m_views = new QStackedWidget(this);
    m_splitter = new QSplitter(Qt::Horizontal, m_views);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->setHandleWidth(1);
    m_table = new EventTable(workspace, m_splitter);
    QToolButton* columnsButton = m_table->createColumnsButton(this);
    for (QToolButton* button : {m_timelineButton, m_groupButton, columnsButton, copyButton, exportButton, m_detailButton})
        bar->addWidget(button);
    m_detail = new DetailPanel(workspace, m_splitter);
    m_splitter->addWidget(m_table);
    m_splitter->addWidget(m_detail);
    m_splitter->setStretchFactor(0, 1);
    m_splitter->setStretchFactor(1, 0);
    m_splitter->setSizes({900, 380});
    m_views->addWidget(m_splitter);

    m_groupsStack = new QStackedWidget(m_views);
    m_groupsModel = new TableModel(this);
    QSortFilterProxyModel* proxy = createSortProxy(m_groupsModel, this);
    m_groups = new QTableView(m_groupsStack);
    configureTable(m_groups);
    m_groups->setModel(proxy);
    m_groups->setSortingEnabled(true);
    m_groups->sortByColumn(0, Qt::DescendingOrder);
    m_groups->horizontalHeader()->setStretchLastSection(false);
    m_noGroup = new EmptyState("search", qtTrId("events.no_event"),
                               qtTrId("events.none_for_active_filters"), m_groupsStack);
    m_groupsStack->addWidget(m_groups);
    m_groupsStack->addWidget(m_noGroup);
    m_views->addWidget(m_groupsStack);
    m_groupsLoading = new LoadingIndicator(m_groupsStack);
    layout->addWidget(m_views, 1);

    const bool detailVisible = settings::read(settings::key::DETAIL_VISIBLE, false).toBool();
    m_detailButton->setChecked(detailVisible);
    m_detail->setVisible(detailVisible);
    m_timelineButton->setChecked(settings::read(settings::key::TIMELINE, true).toBool());
    m_timelineFrame->setVisible(m_timelineButton->isChecked());
    m_groupButton->setChecked(settings::read(settings::key::GROUP, false).toBool());
    m_views->setCurrentIndex(m_groupButton->isChecked() ? 1 : 0);
    m_detailButton->setVisible(!m_groupButton->isChecked());

    connect(m_detailButton, &QToolButton::toggled, this, [this](bool visible) {
        m_detail->setVisible(visible);
        settings::write(settings::key::DETAIL_VISIBLE, visible);
        if (visible)
            m_detail->display(m_table->currentIdentifier());
    });
    connect(m_timelineButton, &QToolButton::toggled, this, [this](bool visible) {
        m_timelineFrame->setVisible(visible);
        settings::write(settings::key::TIMELINE, visible);
        if (visible)
            m_timeline->setFilters(m_workspace->filters());
    });
    connect(m_groupButton, &QToolButton::toggled, this, [this](bool active) {
        settings::write(settings::key::GROUP, active);
        showGrouping(active);
    });
    connect(m_timeline, &Timeline::periodChosen, this, [this](qint64 start, qint64 end) {
        m_workspace->modifyFilters([&](Filters& filters) {
            filters.startUtc = start;
            filters.endUtc = end;
        });
    });
    connect(m_wholePeriod, &LinkButton::clicked, this, [this] {
        m_workspace->modifyFilters([](Filters& filters) {
            filters.startUtc = filters.endUtc = 0;
            filters.since = filters.until = {};
        });
    });

    connect(copyButton, &QToolButton::clicked, this, [this] {
        if (m_groupButton->isChecked())
            copyRows(m_groups);
        else
            m_table->copySelection();
    });
    connect(exportButton, &QToolButton::clicked, this, &EventsPage::exportEvents);
    connect(m_table, &EventTable::selectionChanged, this, [this](qint64 identifier) {
        if (!m_detail->isHidden())
            m_detail->display(identifier);
    });
    connect(m_table, &EventTable::eventActivated, this, [this](qint64 identifier) {
        if (!m_detailButton->isChecked())
            m_detailButton->setChecked(true);
        m_detail->display(identifier);
    });
    connect(m_groups, &QTableView::doubleClicked, this,
            [this, proxy](const QModelIndex& index) { openGroup(proxy->mapToSource(index).row()); });
    connect(m_table->model(), &EventModel::loadingStarted, this, [this] {
        if (!m_groupButton->isChecked())
            m_count->setText(qtTrId("events.loading"));
    });
    connect(m_table->model(), &EventModel::loadingFinished, this, &EventsPage::updateCount);
    updateCount(0);
}

void EventsPage::refresh()
{
    if (m_workspace->isEmpty())
        m_table->setEmptyMessage(qtTrId("common.no_data"), qtTrId("events.empty_explanation"));
    else
        m_table->setEmptyMessage(qtTrId("events.no_event"), qtTrId("events.none_for_active_filters"));
    const Filters& filters = m_workspace->filters();
    m_table->setFilters(filters);
    m_wholePeriod->setVisible(filters.hasExactPeriod() || filters.since.isValid() || filters.until.isValid());
    if (m_timelineButton->isChecked())
        m_timeline->setFilters(filters);
    if (m_groupButton->isChecked())
        loadGroups();
}

void EventsPage::clear()
{
    cancel(m_groupsToken);
    m_groupsLoading->stop();
    m_groupsModel->setRows({});
}

void EventsPage::updateCount(qint64 total)
{
    if (m_groupButton->isChecked())
        return;
    m_count->setText(formatEvents(total));
    const EventModel* model = m_table->model();
    m_sortLabel->setText(total > 1 ? QStringLiteral("· ") + sortDescription(model->sortColumn(), model->sortOrder()) : QString());
}

void EventsPage::showGrouping(bool active)
{
    m_views->setCurrentIndex(active ? 1 : 0);
    m_detailButton->setVisible(!active);
    if (active)
        loadGroups();
    else
        updateCount(m_table->model()->total());
}

void EventsPage::loadGroups()
{
    cancel(m_groupsToken);
    m_count->setText(qtTrId("events.loading"));
    m_sortLabel->clear();
    m_groupsLoading->start();
    const Filters filters = m_workspace->filters();
    m_groupsToken = m_workspace->executor().runCached(
        this, QStringLiteral("groups|") + filters.key(),
        [filters](Context& context) { return queries::groups(context, filters, GROUP_LIMIT); },
        [this](queries::Grouping grouping) {
            m_groupsToken.reset();
            m_groupsLoading->stop();
            m_groupsModel->setColumns({{qtTrId("events.column_occurrences"), true},
                                       {qtTrId("common.event")},
                                       {qtTrId("common.user")},
                                       {qtTrId("common.computer")},
                                       {qtTrId("common.program")},
                                       {qtTrId("common.target")},
                                       {qtTrId("events.column_first_seen")},
                                       {qtTrId("events.column_last_seen")}});
            QList<TableModel::Row> rows;
            rows.reserve(grouping.groups.size());
            for (const queries::Group& group : std::as_const(grouping.groups)) {
                const QString event = group.action.isEmpty()
                                          ? eventLabel(group.eventId, group.channel, group.provider)
                                          : qtTrId("catalog.event_number_description")
                                                .arg(group.eventId)
                                                .arg(actionLabel(group.action));
                const QString program = fileName(group.process);
                TableModel::Row row;
                row.key = QVariantList{group.identifier, event};
                row.cells = {{formatNumber(group.total), group.total},
                             {event, event},
                             {group.user, group.user},
                             {group.machine.section(QLatin1Char('.'), 0, 0), group.machine},
                             {program, program},
                             {group.target, group.target},
                             {formatTimestamp(group.first), group.first},
                             {formatTimestamp(group.last), group.last}};
                row.cells[4].tooltip = group.process;
                row.cells[5].tooltip = group.target;
                rows.append(std::move(row));
            }
            m_groupsModel->setRows(std::move(rows));
            limitColumnWidths(m_groups);
            m_groupsStack->setCurrentWidget(grouping.groupCount > 0 ? static_cast<QWidget*>(m_groups)
                                                                     : m_noGroup);
            if (!m_groupButton->isChecked())
                return;
            m_count->setText(qtTrId("events.group_count", pluralCount(grouping.groupCount))
                                 .arg(formatNumber(grouping.events)));
            const double reduction = grouping.events > 0
                                         ? 1.0 - static_cast<double>(grouping.groupCount)
                                                     / static_cast<double>(grouping.events)
                                         : 0.0;
            const QString noise = qtTrId("events.noise_reduction").arg(formatPercentage(reduction));
            m_sortLabel->setText(grouping.events > 0 ? QString(QStringLiteral("· ") + noise) : QString());
        },
        [this](const QString& message) {
            m_groupsToken.reset();
            m_groupsLoading->stop();
            emit m_workspace->executor().error(message);
        });
}

void EventsPage::openGroup(int row)
{
    const auto& rows = m_groupsModel->rows();
    if (row < 0 || row >= rows.size())
        return;
    const QVariantList key = rows[row].key.toList();
    m_groupButton->setChecked(false);
    m_workspace->modifyFilters([&](Filters& filters) {
        filters.group = key.value(0).toLongLong();
        filters.groupLabel = key.value(1).toString();
    });
}

} // namespace lh::ui
