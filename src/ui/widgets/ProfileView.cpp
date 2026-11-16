#include "ProfileView.h"

#include "Components.h"
#include "TableModel.h"
#include "EventTable.h"
#include "ChartViews.h"
#include "core/Actions.h"
#include "core/AccessAnalyzer.h"
#include "core/Format.h"
#include "core/DateTime.h"
#include "ui/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QTabBar>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace lh::ui {

namespace {

constexpr int FILE_LIMIT = 20'000;

QString day(const QString& timestamp)
{
    return timestamp.size() < 10 ? QString() : formatDate(QStringView(timestamp).left(10));
}

QString hour(const QString& timestamp)
{
    return timestamp.size() < 16 ? QString() : qtTrId("profile.at_time").arg(timestamp.mid(11, 5));
}

} // namespace

ProfileView::ProfileView(Workspace* workspace, Type type, QWidget* parent) : QWidget(parent), m_workspace(workspace), m_type(type)
{
    m_stack = new QStackedWidget(this);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_stack);

    const char* iconName = type == Type::User ? "user" : type == Type::File ? "file" : "folder";
    const QString placeholder = type == Type::User   ? qtTrId("profile.select_user")
                                : type == Type::File ? qtTrId("profile.select_file")
                                                     : qtTrId("profile.select_folder");
    const QString explanation =
        type == Type::User
            ? qtTrId("profile.user_empty_explanation")
        : type == Type::File ? qtTrId("profile.file_empty_explanation")
                             : qtTrId("profile.folder_empty_explanation");
    m_empty = new EmptyState(iconName, placeholder, explanation, m_stack);
    m_stack->addWidget(m_empty);

    auto* content = new QWidget(m_stack);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_stack->addWidget(content);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(4, 0, 0, 0);
    header->setSpacing(12);
    m_listButton = new QToolButton(content);
    m_listButton->setAutoRaise(true);
    connect(m_listButton, &QToolButton::clicked, this, &ProfileView::listToggled);
    header->addWidget(m_listButton, 0, Qt::AlignTop);
    setListVisible(true);
    auto* targetIcon = new QLabel(content);
    targetIcon->setPixmap(icon(iconName, theme::brand(QApplication::palette())).pixmap(QSize(28, 28), devicePixelRatioF()));
    header->addWidget(targetIcon, 0, Qt::AlignTop);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(0);
    m_title = new Label({}, Label::Role::PageTitle, content);
    m_title->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_subtitle = new Label({}, Label::Role::Secondary, content);
    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);
    header->addLayout(titles, 1);
    auto* copyButton = new QToolButton(content);
    copyButton->setIcon(icon("copy"));
    copyButton->setAutoRaise(true);
    copyButton->setToolTip(qtTrId("common.copy"));
    connect(copyButton, &QToolButton::clicked, this, [this] { QGuiApplication::clipboard()->setText(m_value); });
    header->addWidget(copyButton, 0, Qt::AlignTop);
    auto* exploreButton = new QPushButton(icon("events"), qtTrId("profile.see_event_list"), content);
    exploreButton->setAutoDefault(false);
    exploreButton->setToolTip(qtTrId("profile.see_list_help"));
    connect(exploreButton, &QPushButton::clicked, this, [this] { m_workspace->explore(scope()); });
    header->addWidget(exploreButton, 0, Qt::AlignTop);
    layout->addLayout(header);
    layout->addSpacing(10);

    auto* tabStrip = new QHBoxLayout;
    tabStrip->setContentsMargins(0, 0, 0, 0);
    tabStrip->setSpacing(0);
    m_tabs = new QTabBar(content);
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->setDocumentMode(true);
    tabStrip->addWidget(m_tabs);
    tabStrip->addStretch();
    layout->addLayout(tabStrip);
    m_pages = new QStackedWidget(content);
    layout->addWidget(m_pages, 1);

    auto* scrollArea = new QScrollArea(m_pages);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    m_summary = new QWidget(scrollArea);
    scrollArea->setWidget(m_summary);
    auto* summaryLayout = new QVBoxLayout(m_summary);
    summaryLayout->setContentsMargins(0, 12, 8, 12);
    summaryLayout->setSpacing(12);

    auto* tiles = new QHBoxLayout;
    tiles->setSpacing(10);
    const QList<QPair<const char*, QString>> definitions =
        type == Type::User
            ? QList<QPair<const char*, QString>>{{"activity", qtTrId("common.events")},
                                                 {"file", qtTrId("profile.files_touched")},
                                                 {"clock", qtTrId("profile.first_activity")},
                                                 {"clock", qtTrId("common.last_activity")}}
        : type == Type::File
            ? QList<QPair<const char*, QString>>{{"activity", qtTrId("common.events")},
                                                 {"users", qtTrId("common.users")},
                                                 {"clock", qtTrId("profile.first_access")},
                                                 {"clock", qtTrId("common.last_access")}}
            : QList<QPair<const char*, QString>>{{"activity", qtTrId("common.events")},
                                                 {"file", qtTrId("common.files")},
                                                 {"users", qtTrId("common.users")},
                                                 {"clock", qtTrId("common.last_access")}};
    for (int i = 0; i < 4; ++i) {
        m_tiles[i] = new IndicatorTile(definitions[i].first, definitions[i].second, m_summary);
        tiles->addWidget(m_tiles[i]);
    }
    summaryLayout->addLayout(tiles);

    m_timeline = new TimelineChart(m_summary);
    m_timeline->setMinimumHeight(200);
    m_distribution = new DonutChart(m_summary);
    m_distribution->setMinimumHeight(200);
    auto* chartsRow = new QHBoxLayout;
    chartsRow->setSpacing(12);
    chartsRow->addWidget(cardWith(qtTrId("common.activity_over_time"), m_timeline, m_summary), 3);
    chartsRow->addWidget(cardWith(qtTrId("common.action_types"), m_distribution, m_summary), 2);
    summaryLayout->addLayout(chartsRow);

    m_rankingA = new RankingChart(m_summary);
    m_rankingA->setMinimumHeight(250);
    m_rankingB = new RankingChart(m_summary);
    m_rankingB->setMinimumHeight(250);
    auto* rankingsRow = new QHBoxLayout;
    rankingsRow->setSpacing(12);
    const QString titleA = type == Type::File ? qtTrId("profile.file_users")
                                              : qtTrId("common.most_accessed_files");
    rankingsRow->addWidget(cardWith(titleA, m_rankingA, m_summary), 1);
    m_cardB = cardWith(qtTrId("common.most_active_users"), m_rankingB, m_summary);
    m_cardB->setVisible(type == Type::Folder);
    rankingsRow->addWidget(m_cardB, 1);
    summaryLayout->addLayout(rankingsRow);
    summaryLayout->addStretch();
    m_pages->addWidget(scrollArea);
    m_tabs->addTab(icon("dashboard"), qtTrId("profile.overview"));

    if (type != Type::File)
        createFilesTab();

    m_table = new EventTable(workspace, m_pages);
    m_table->setSort(queries::Column::Timestamp, Qt::AscendingOrder);
    m_pages->addWidget(m_table);
    m_tabs->addTab(icon("events"), qtTrId("profile.event_timeline"));
    QToolButton* columnsButton = m_table->createColumnsButton(content);
    columnsButton->setVisible(false);
    tabStrip->addWidget(columnsButton, 0, Qt::AlignVCenter);
    connect(m_tabs, &QTabBar::currentChanged, this, [this, columnsButton](int index) {
        m_pages->setCurrentIndex(index);
        columnsButton->setVisible(m_pages->widget(index) == m_table);
        if (m_pages->widget(index) == m_table)
            loadTable();
        if (m_filesTab && m_pages->widget(index) == m_filesTab)
            loadFiles();
    });

    m_loading = new LoadingIndicator(scrollArea);

    connect(m_timeline, &ChartView::elementClicked, this, [this](int index) {
        const auto& periods = m_timeline->timeline().periods;
        if (index >= 0 && index < periods.size())
            m_workspace->modifyFilters([&](Filters& f) {
                f.since = periods[index].start;
                f.until = periods[index].end;
            });
    });
    connect(m_distribution, &ChartView::elementClicked, this, [this](int index) {
        if (index >= 0 && index < m_distribution->elements().size())
            m_workspace->modifyFilters([&](Filters& f) { f.category = m_distribution->elements()[index].key; });
    });
    connect(m_rankingA, &ChartView::elementClicked, this, [this](int index) {
        if (index < 0 || index >= m_rankingA->elements().size())
            return;
        const QString key = m_rankingA->elements()[index].key;
        m_workspace->navigate({m_type == Type::File ? Navigation::Target::User : Navigation::Target::File, key});
    });
    connect(m_rankingB, &ChartView::elementClicked, this, [this](int index) {
        if (index >= 0 && index < m_rankingB->elements().size())
            m_workspace->navigate({Navigation::Target::User, m_rankingB->elements()[index].key});
    });
}

void ProfileView::createFilesTab()
{
    m_filesTab = new QWidget(m_pages);
    auto* layout = new QVBoxLayout(m_filesTab);
    layout->setContentsMargins(0, 10, 0, 0);
    layout->setSpacing(6);
    auto* header = new QHBoxLayout;
    header->setSpacing(12);
    m_fileSearch = new SearchField(qtTrId("profile.search_file"), m_filesTab);
    m_fileSearch->setMaximumWidth(420);
    header->addWidget(m_fileSearch, 1);
    m_fileCount = new Label({}, Label::Role::Small, m_filesTab);
    header->addWidget(m_fileCount, 1);
    layout->addLayout(header);

    m_filesModel = new TableModel(this);
    QList<TableModel::Column> columns{{qtTrId("common.file")},
                                      {categoryLabel(QString::fromLatin1(category::Read)), true},
                                      {categoryLabel(QString::fromLatin1(category::Modification)), true},
                                      {categoryLabel(QString::fromLatin1(category::Deletion)), true},
                                      {categoryLabel(QString::fromLatin1(category::Permissions)), true},
                                      {qtTrId("common.events"), true}};
    if (m_type == Type::Folder)
        columns.append({qtTrId("common.users"), true});
    columns.append({qtTrId("profile.first_access")});
    columns.append({qtTrId("common.last_access")});
    m_filesModel->setColumns(columns);
    m_filesProxy = createSortProxy(m_filesModel, this);
    m_filesProxy->setFilterRole(TableModel::KeyRole);
    m_filesProxy->setFilterCaseSensitivity(Qt::CaseInsensitive);

    m_filesView = new QTableView(m_filesTab);
    configureTable(m_filesView);
    m_filesView->setModel(m_filesProxy);
    m_filesView->setSortingEnabled(true);
    m_filesView->sortByColumn(5, Qt::DescendingOrder);
    m_filesView->setItemDelegateForColumn(0, new TwoLineDelegate(m_filesView));
    m_filesView->verticalHeader()->setDefaultSectionSize(TwoLineDelegate::height(m_filesView->fontMetrics()));
    m_filesView->setContextMenuPolicy(Qt::CustomContextMenu);
    m_fileColumns = new ColumnFitter(m_filesView, m_filesView->horizontalHeader(), 0);
    layout->addWidget(m_filesView, 1);
    m_filesLoading = new LoadingIndicator(m_filesView);
    m_pages->addWidget(m_filesTab);
    m_tabs->addTab(icon("file"), qtTrId("common.files"));

    connect(m_fileSearch, &SearchField::searchChanged, this,
            [this](const QString& text) { m_filesProxy->setFilterFixedString(text.trimmed()); });
    connect(m_filesView, &QTableView::activated, this, [this](const QModelIndex& index) {
        const QString path = index.data(TableModel::KeyRole).toString();
        if (!path.isEmpty())
            m_workspace->navigate({Navigation::Target::File, path});
    });
    connect(m_filesView, &QWidget::customContextMenuRequested, this, &ProfileView::fileMenu);
}

void ProfileView::loadTable()
{
    if (m_value.trimmed().isEmpty())
        return;
    const Filters filters = scope();
    const QString key = filters.key();
    if (key == m_tableKey)
        return;
    m_tableKey = key;
    m_table->setFilters(filters);
}

void ProfileView::loadFiles()
{
    if (!m_filesTab || m_value.trimmed().isEmpty())
        return;
    const Filters filters = scope();
    const QString key = QStringLiteral("profile_files|") + filters.key();
    if (key == m_filesKey)
        return;
    cancel(m_filesToken);
    m_filesKey = key;
    m_filesLoading->start();
    m_filesToken = m_workspace->executor().runCached(
        this, key,
        [filters](Context& context) { return queries::files(context, filters, FILE_LIMIT); },
        [this](const QList<queries::FileRow>& list) {
            m_filesToken.reset();
            m_filesLoading->stop();
            const QColor secondary = theme::secondaryText(QApplication::palette());
            QList<TableModel::Row> rows;
            rows.reserve(list.size());
            for (const auto& f : list) {
                TableModel::Row row;
                row.key = f.path;
                TableModel::Cell path{fileName(f.path), f.path.toLower()};
                path.detail = parentFolder(f.path);
                path.icon = dot(theme::categoryColor(f.dominantCategory));
                path.tooltip = f.path;
                row.cells.append(path);
                for (const char* key : {category::Read, category::Modification, category::Deletion,
                                        category::Permissions}) {
                    const qint64 count = f.byCategory.value(QLatin1String(key), 0);
                    TableModel::Cell cell{count > 0 ? formatNumber(count) : QString(), count};
                    if (count > 0)
                        cell.color = theme::categoryColor(QLatin1String(key));
                    row.cells.append(cell);
                }
                row.cells.append({formatNumber(f.total), f.total, secondary});
                if (m_type == Type::Folder)
                    row.cells.append({formatNumber(f.users), f.users});
                for (const QString& timestamp : {f.first, f.last}) {
                    TableModel::Cell date{timestamp.size() < 10 ? QString() : formatTimestamp(timestamp),
                                          timestamp};
                    row.cells.append(date);
                }
                rows.append(row);
            }
            m_filesModel->setRows(rows);
            m_fileColumns->fitToContent();
            const int count = static_cast<int>(rows.size());
            QString text = qtTrId("investigation.file_count", count);
            if (count >= FILE_LIMIT)
                text += QStringLiteral(" · ") + qtTrId("investigation.list_truncated");
            text += QStringLiteral(" · ") + qtTrId("profile.files_help");
            m_fileCount->setText(text);
        },
        [this](const QString& message) {
            m_filesToken.reset();
            m_filesLoading->stop();
            m_filesKey.clear();
            emit m_workspace->executor().error(message);
        });
}

void ProfileView::fileMenu(const QPoint& position)
{
    const QModelIndex index = m_filesView->indexAt(position);
    if (!index.isValid())
        return;
    const QString path = index.data(TableModel::KeyRole).toString();
    QMenu menu(this);
    menu.addAction(icon("investigation"), qtTrId("common.file_history"), this,
                   [this, path] { m_workspace->navigate({Navigation::Target::File, path}); });
    menu.addAction(icon("events"), qtTrId("profile.see_file_events"), this, [this, path] {
        Filters filters = scope();
        filters.file = path;
        filters.exact = true;
        m_workspace->explore(filters);
    });
    const QString folder = parentFolder(path);
    if (!folder.isEmpty())
        menu.addAction(icon("folder"), qtTrId("events.show_folder"), this,
                       [this, folder] { m_workspace->navigate({Navigation::Target::Folder, folder}); });
    menu.addSeparator();
    menu.addAction(icon("copy"), qtTrId("profile.copy_path"), this,
                   [path] { QGuiApplication::clipboard()->setText(path); });
    menu.exec(m_filesView->viewport()->mapToGlobal(position));
}

Filters ProfileView::scope() const
{
    Filters filters = m_workspace->filters();
    switch (m_type) {
    case Type::User:
        filters.exact = filters.exact || filters.file.isEmpty();
        filters.user = m_value;
        break;
    case Type::File:
        filters.exact = filters.exact || filters.user.isEmpty();
        filters.file = m_value;
        break;
    case Type::Folder:
        filters.folder = m_value;
        break;
    }
    return filters;
}

void ProfileView::setListVisible(bool visible)
{
    m_listButton->setIcon(icon(visible ? "pane-hide" : "pane-show"));
    m_listButton->setToolTip(visible ? qtTrId("profile.hide_list") : qtTrId("profile.show_list"));
}

void ProfileView::display(const QString& value)
{
    const bool change = value != m_value;
    m_value = value;
    refresh();
    if (change)
        emit valueChanged();
}

void ProfileView::refresh()
{
    cancel(m_token);
    m_filesKey.clear();
    m_tableKey.clear();
    if (m_value.trimmed().isEmpty()) {
        m_loading->stop();
        m_stack->setCurrentWidget(m_empty);
        return;
    }
    m_stack->setCurrentIndex(1);
    switch (m_type) {
    case Type::User:
        m_title->setElidedText(m_value);
        m_subtitle->setElidedText(isSystemAccount(m_value) ? qtTrId("common.system_or_service_account")
                                                           : qtTrId("profile.user_account"));
        break;
    case Type::File:
        m_title->setElidedText(fileName(m_value));
        m_subtitle->setElidedText(parentFolder(m_value));
        break;
    case Type::Folder:
        m_title->setElidedText(fileName(m_value));
        m_subtitle->setElidedText(qtTrId("profile.folder_with_subfolders").arg(m_value));
        break;
    }

    const Filters filters = scope();
    if (m_pages->currentWidget() == m_table)
        loadTable();
    if (m_filesTab && m_pages->currentWidget() == m_filesTab)
        loadFiles();
    m_loading->start();
    m_token = m_workspace->executor().runCached(
        this, QStringLiteral("profile|") + filters.key(),
        [filters](Context& context) { return queries::profile(context, filters); },
        [this](queries::Profile profile) {
            m_token.reset();
            m_loading->stop();
            showProfile(profile);
        },
        [this](const QString& message) {
            m_token.reset();
            m_loading->stop();
            emit m_workspace->executor().error(message);
        });
}

void ProfileView::showProfile(const queries::Profile& p)
{
    m_tiles[0]->setValue(formatNumber(p.total));
    m_tabs->setTabText(m_pages->indexOf(m_table), qtTrId("profile.event_timeline_count").arg(formatNumber(p.total)));
    if (m_filesTab)
        m_tabs->setTabText(m_pages->indexOf(m_filesTab),
                           qtTrId("profile.files_count").arg(formatNumber(p.files)));
    switch (m_type) {
    case Type::User:
        m_tiles[1]->setValue(formatNumber(p.files));
        m_tiles[2]->setValue(day(p.first), hour(p.first));
        m_tiles[3]->setValue(day(p.last), hour(p.last));
        m_rankingA->set(render::elementsFromSeries(p.topFiles), true);
        break;
    case Type::File:
        m_tiles[1]->setValue(formatNumber(p.users));
        m_tiles[2]->setValue(day(p.first), hour(p.first));
        m_tiles[3]->setValue(day(p.last), hour(p.last));
        m_rankingA->set(render::elementsFromSeries(p.topUsers));
        break;
    case Type::Folder:
        m_tiles[1]->setValue(formatNumber(p.files));
        m_tiles[2]->setValue(formatNumber(p.users));
        m_tiles[3]->setValue(day(p.last), hour(p.last));
        m_rankingA->set(render::elementsFromSeries(p.topFiles), true);
        m_rankingB->set(render::elementsFromSeries(p.topUsers));
        break;
    }
    m_timeline->set(p.byDay);
    m_distribution->set(render::elementsByCategory(p.byCategory));
}

} // namespace lh::ui
