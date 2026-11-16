#include "InvestigationPage.h"

#include "core/Actions.h"
#include "core/AccessAnalyzer.h"
#include "core/Format.h"
#include "core/Queries.h"
#include "core/DateTime.h"
#include "ui/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/Components.h"
#include "ui/widgets/TableModel.h"
#include "ui/widgets/ProfileView.h"

#include <QApplication>
#include <QHeaderView>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStandardItemModel>
#include <QTabBar>
#include <QTabWidget>
#include <QTableView>
#include <QTreeView>
#include <QVBoxLayout>

#include <memory>

namespace lh::ui {

class InvestigationTab : public QWidget
{
public:
    InvestigationTab(Workspace* workspace, ProfileView::Type type, QWidget* parent) : QWidget(parent), m_workspace(workspace)
    {
        auto* splitter = new QSplitter(Qt::Horizontal, this);
        splitter->setChildrenCollapsible(false);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(24, 12, 24, 16);
        layout->addWidget(splitter);

        m_left = new Card({}, splitter);
        m_left->setMargins(12, 12, 12, 12);
        m_left->setMinimumWidth(300);
        const QString placeholder = type == ProfileView::Type::User ? qtTrId("investigation.search_user")
                                    : type == ProfileView::Type::File
                                        ? qtTrId("investigation.search_file_or_path")
                                        : qtTrId("investigation.filter_folders");
        m_search = new SearchField(placeholder, m_left);
        m_count = new Label({}, Label::Role::Small, m_left);
        m_left->content()->addWidget(m_search);
        m_left->content()->addWidget(m_count);

        m_profile = new ProfileView(workspace, type, splitter);
        splitter->addWidget(m_left);
        splitter->addWidget(m_profile);
        splitter->setStretchFactor(0, 0);
        splitter->setStretchFactor(1, 1);
        const int left = type == ProfileView::Type::User ? 480 : type == ProfileView::Type::File ? 600 : 480;
        splitter->setSizes({left, 1400 - left});

        connect(m_profile, &ProfileView::listToggled, this, [this] { showList(m_left->isHidden()); });
        connect(m_profile, &ProfileView::valueChanged, this, [this] {
            if (m_profile->value().trimmed().isEmpty() && m_left->isHidden())
                showList(true);
        });
    }

    [[nodiscard]] ProfileView* profile() const { return m_profile; }

    void refresh()
    {
        m_needsRefresh = false;
        load();
        m_profile->refresh();
    }
    void invalidate() { m_needsRefresh = true; }
    [[nodiscard]] bool needsRefresh() const { return m_needsRefresh; }

    virtual void focusOn(const QString& value)
    {
        m_selection = value;
        select();
        m_profile->display(value);
    }

    virtual void clear()
    {
        cancel(m_token);
        m_needsRefresh = true;
        m_selection.clear();
        m_search->changeText({});
        m_count->clear();
        showList(true);
        m_profile->display({});
    }

protected:
    virtual void load() = 0;
    virtual void select() = 0;

    void choose(const QString& value)
    {
        if (value == m_selection && m_profile->value() == value)
            return;
        m_selection = value;
        m_profile->display(value);
    }

    void showList(bool visible)
    {
        m_left->setVisible(visible);
        m_profile->setListVisible(visible);
    }

    Workspace* m_workspace;
    Card* m_left;
    SearchField* m_search;
    Label* m_count;
    ProfileView* m_profile;
    QString m_selection;
    TokenPtr m_token;
    bool m_needsRefresh = true;
};

namespace {

constexpr int USER_LIMIT = 10'000;
constexpr int FILE_LIMIT = 5'000;
constexpr int FOLDER_LIMIT = 20'000;
constexpr int ROLE_PATH = Qt::UserRole + 10;

TableModel::Cell date(const QString& timestamp)
{
    TableModel::Cell cell;
    cell.text = timestamp.size() < 10 ? QString() : formatDate(QStringView(timestamp).left(10));
    cell.sortKey = timestamp;
    cell.tooltip = timestamp.isEmpty() ? QString() : formatTimestamp(timestamp);
    return cell;
}

// ── Users and files ──────────────────────────────────────────────────────────

class ListTab : public InvestigationTab
{
public:
    ListTab(Workspace* workspace, ProfileView::Type type, QWidget* parent)
        : InvestigationTab(workspace, type, parent), m_type(type)
    {
        m_model = new TableModel(this);
        if (type == ProfileView::Type::User)
            m_model->setColumns({{qtTrId("common.user")},
                                 {qtTrId("common.events"), true},
                                 {qtTrId("common.last_activity")}});
        else
            m_model->setColumns({{qtTrId("common.file")},
                                 {qtTrId("common.events"), true},
                                 {qtTrId("common.users"), true},
                                 {qtTrId("common.last_access")}});
        m_proxy = createSortProxy(m_model, this);

        m_view = new QTableView(m_left);
        configureTable(m_view);
        m_view->setSelectionMode(QAbstractItemView::SingleSelection);
        m_view->setModel(m_proxy);
        m_view->setSortingEnabled(true);
        m_view->sortByColumn(1, Qt::DescendingOrder);
        m_columns = new ColumnFitter(m_view, m_view->horizontalHeader(), 0);
        if (type == ProfileView::Type::File) {
            m_view->setItemDelegateForColumn(0, new TwoLineDelegate(m_view));
            m_view->verticalHeader()->setDefaultSectionSize(TwoLineDelegate::height(m_view->fontMetrics()));
        }
        m_left->content()->addWidget(m_view, 1);
        m_loading = new LoadingIndicator(m_view);

        connect(m_search, &SearchField::searchChanged, this, [this] { load(); });
        connect(m_view->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
                [this](const QModelIndex& current) {
                    if (current.isValid())
                        choose(current.data(TableModel::KeyRole).toString());
                });
    }

    void focusOn(const QString& value) override
    {
        if (!value.isEmpty() && !m_search->text().isEmpty()) {
            m_search->changeText({});
            InvestigationTab::focusOn(value);
            load();
            return;
        }
        InvestigationTab::focusOn(value);
    }

    void clear() override
    {
        InvestigationTab::clear();
        m_loading->stop();
        m_model->setRows({});
    }

protected:
    void load() override
    {
        cancel(m_token);
        Filters filters = m_workspace->filters();
        const QString search = m_search->text().trimmed();
        const bool users = m_type == ProfileView::Type::User;
        if (!search.isEmpty()) {
            (users ? filters.user : filters.file) = search;
            if ((users ? filters.file : filters.user).isEmpty())
                filters.exact = false;
        }
        m_loading->start();
        auto completed = [this](const QList<TableModel::Row>& rows, int limit) {
            m_token.reset();
            m_loading->stop();
            m_model->setRows(rows);
            m_columns->fitToContent();
            const bool users = m_type == ProfileView::Type::User;
            const int count = static_cast<int>(rows.size());
            QString text = users ? qtTrId("investigation.user_count", count)
                                 : qtTrId("investigation.file_count", count);
            if (rows.size() >= limit)
                text += QStringLiteral(" · ") + qtTrId("investigation.list_truncated");
            m_count->setText(text);
            select();
        };
        const QColor secondary = theme::secondaryText(QApplication::palette());
        if (users) {
            m_token = m_workspace->executor().runCached(
                this, QStringLiteral("users|") + filters.key(),
                [filters](Context& context) { return queries::users(context, filters, USER_LIMIT); },
                [completed, secondary](const QList<queries::UserRow>& list) {
                    QList<TableModel::Row> rows;
                    rows.reserve(list.size());
                    for (const auto& u : list) {
                        TableModel::Row row;
                        row.key = u.name;
                        TableModel::Cell name{u.name.isEmpty() ? qtTrId("common.unknown") : u.name,
                                              u.name.toLower()};
                        name.icon = icon(u.isSystem ? "machine" : "user");
                        if (u.isSystem) {
                            name.color = secondary;
                            name.tooltip = u.name + QLatin1Char('\n') + qtTrId("common.system_or_service_account");
                        }
                        row.cells = {name,
                                     {formatNumber(u.total), u.total},
                                     date(u.last)};
                        rows.append(row);
                    }
                    completed(rows, USER_LIMIT);
                });
        } else {
            m_token = m_workspace->executor().runCached(
                this, QStringLiteral("files|") + filters.key(),
                [filters](Context& context) { return queries::files(context, filters, FILE_LIMIT); },
                [completed](const QList<queries::FileRow>& list) {
                    QList<TableModel::Row> rows;
                    rows.reserve(list.size());
                    for (const auto& f : list) {
                        TableModel::Row row;
                        row.key = f.path;
                        TableModel::Cell path{fileName(f.path), f.path.toLower()};
                        path.detail = parentFolder(f.path);
                        path.icon = dot(theme::categoryColor(f.dominantCategory));
                        path.tooltip = qtTrId("investigation.tooltip_main_action")
                                           .arg(f.path, categoryLabel(f.dominantCategory));
                        row.cells = {path,
                                     {formatNumber(f.total), f.total},
                                     {formatNumber(f.users), f.users},
                                     date(f.last)};
                        rows.append(row);
                    }
                    completed(rows, FILE_LIMIT);
                });
        }
    }

    void select() override
    {
        if (m_selection.isEmpty()) {
            m_view->clearSelection();
            return;
        }
        for (int row = 0; row < m_proxy->rowCount(); ++row) {
            const QModelIndex index = m_proxy->index(row, 0);
            if (index.data(TableModel::KeyRole).toString() == m_selection) {
                const QSignalBlocker blocker(m_view->selectionModel());
                m_view->setCurrentIndex(index);
                m_view->selectRow(row);
                m_view->scrollTo(index, QAbstractItemView::PositionAtCenter);
                return;
            }
        }
        m_view->clearSelection();
    }

private:
    ProfileView::Type m_type;
    TableModel* m_model;
    QSortFilterProxyModel* m_proxy;
    QTableView* m_view;
    ColumnFitter* m_columns;
    LoadingIndicator* m_loading;
};

// ── Folders ──────────────────────────────────────────────────────────────────

struct Node
{
    QString name;
    QString path;
    qint64 total = 0;
    qint64 writes = 0;
    qint64 deletions = 0;
    qint64 permissions = 0;
    std::vector<std::unique_ptr<Node>> children;
    QHash<QString, Node*> index;
};

QStringList splitParts(const QString& path)
{
    QString work = path;
    work.replace(QLatin1Char('/'), QLatin1Char('\\'));
    if (work.startsWith(QLatin1String("\\\\"))) {
        QStringList parts = work.mid(2).split(QLatin1Char('\\'), Qt::SkipEmptyParts);
        if (!parts.isEmpty())
            parts[0] = QStringLiteral("\\\\") + parts[0];
        return parts;
    }
    return work.split(QLatin1Char('\\'), Qt::SkipEmptyParts);
}

std::shared_ptr<Node> buildTree(const QList<queries::FolderRow>& folders)
{
    auto root = std::make_shared<Node>();
    for (const queries::FolderRow& folder : folders) {
        const QStringList parts = splitParts(folder.path);
        Node* node = root.get();
        QString path;
        for (const QString& part : parts) {
            path = path.isEmpty() ? part : path + QLatin1Char('\\') + part;
            Node* child = node->index.value(part.toLower());
            if (!child) {
                auto isNew = std::make_unique<Node>();
                isNew->name = part;
                isNew->path = path;
                child = isNew.get();
                node->index.insert(part.toLower(), child);
                node->children.push_back(std::move(isNew));
            }
            child->total += folder.total;
            child->writes += folder.writes;
            child->deletions += folder.deletions;
            child->permissions += folder.permissions;
            node = child;
        }
    }
    return root;
}

class FoldersTab : public InvestigationTab
{
public:
    FoldersTab(Workspace* workspace, QWidget* parent) : InvestigationTab(workspace, ProfileView::Type::Folder, parent)
    {
        m_model = new QStandardItemModel(this);
        m_model->setHorizontalHeaderLabels(
            {qtTrId("common.folder"), qtTrId("common.events"), qtTrId("common.deletions")});
        m_proxy = createSortProxy(m_model, this);
        m_proxy->setFilterRole(ROLE_PATH);
        m_proxy->setFilterKeyColumn(0);
        m_proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
        m_proxy->setRecursiveFilteringEnabled(true);

        m_tree = new QTreeView(m_left);
        configureTree(m_tree);
        m_tree->setModel(m_proxy);
        m_tree->setSortingEnabled(true);
        m_tree->sortByColumn(1, Qt::DescendingOrder);
        m_columns = new ColumnFitter(m_tree, m_tree->header(), 0);
        m_left->content()->addWidget(m_tree, 1);
        m_loading = new LoadingIndicator(m_tree);

        connect(m_search, &SearchField::searchChanged, this, [this](const QString& text) {
            m_proxy->setFilterFixedString(text);
            if (!text.isEmpty() && m_proxy->rowCount() < 200)
                m_tree->expandAll();
            select();
        });
        connect(m_tree->selectionModel(), &QItemSelectionModel::currentChanged, this,
                [this](const QModelIndex& current) {
                    if (current.isValid())
                        choose(current.siblingAtColumn(0).data(ROLE_PATH).toString());
                });
    }

    void clear() override
    {
        InvestigationTab::clear();
        m_loading->stop();
        m_elements.clear();
        m_model->removeRows(0, m_model->rowCount());
        m_proxy->setFilterFixedString({});
    }

protected:
    void load() override
    {
        cancel(m_token);
        const Filters filters = m_workspace->filters();
        m_loading->start();
        m_token = m_workspace->executor().runCached(
            this, QStringLiteral("folders|") + filters.key(),
            [filters](Context& context) {
                return buildTree(queries::folders(context, filters, FOLDER_LIMIT));
            },
            [this](const std::shared_ptr<Node>& root) {
                m_token.reset();
                m_loading->stop();
                populate(*root);
            });
    }

    void select() override
    {
        QStandardItem* element = m_elements.value(m_selection.toLower());
        if (!element) {
            m_tree->clearSelection();
            return;
        }
        const QModelIndex index = m_proxy->mapFromSource(element->index());
        if (!index.isValid())
            return;
        for (QModelIndex parent = index.parent(); parent.isValid(); parent = parent.parent())
            m_tree->expand(parent);
        const QSignalBlocker blocker(m_tree->selectionModel());
        m_tree->setCurrentIndex(index);
        m_tree->scrollTo(index, QAbstractItemView::PositionAtCenter);
    }

private:
    void populate(const Node& root)
    {
        m_elements.clear();
        m_model->removeRows(0, m_model->rowCount());
        qint64 count = 0;
        const QIcon folderIcon = icon("folder");
        std::function<void(const Node&, QStandardItem*)> add = [&](const Node& node, QStandardItem* parent) {
            for (const auto& child : node.children) {
                auto* name = new QStandardItem(folderIcon, child->name);
                name->setData(child->path, ROLE_PATH);
                name->setData(child->name.toLower(), TableModel::SortRole);
                name->setToolTip(child->path);
                QList<QStandardItem*> row{name};
                for (const qint64 value : {child->total, child->deletions}) {
                    auto* cell = new QStandardItem(formatNumber(value));
                    cell->setData(value, TableModel::SortRole);
                    cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                    row.append(cell);
                }
                for (QStandardItem* cell : row)
                    cell->setEditable(false);
                parent->appendRow(row);
                m_elements.insert(child->path.toLower(), name);
                ++count;
                add(*child, name);
            }
        };
        add(root, m_model->invisibleRootItem());
        m_count->setText(qtTrId("investigation.folder_count", pluralCount(count)));
        for (int row = 0; row < m_proxy->rowCount(); ++row) {
            const QModelIndex index = m_proxy->index(row, 0);
            m_tree->expand(index);
            if (m_proxy->rowCount(index) == 1)
                m_tree->expand(m_proxy->index(0, 0, index));
        }
        m_columns->fitToContent();
        select();
    }

    QStandardItemModel* m_model;
    QSortFilterProxyModel* m_proxy;
    QTreeView* m_tree;
    ColumnFitter* m_columns;
    LoadingIndicator* m_loading;
    QHash<QString, QStandardItem*> m_elements;
};

} // namespace

InvestigationPage::InvestigationPage(Workspace* workspace, QWidget* parent) : Page(workspace, parent)
{
    m_tabs = new QTabWidget(this);
    m_tabs->setDocumentMode(true);
    m_tabs->tabBar()->setDrawBase(false);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_tabs);

    m_views[0] = new ListTab(workspace, ProfileView::Type::User, m_tabs);
    m_views[1] = new ListTab(workspace, ProfileView::Type::File, m_tabs);
    m_views[2] = new FoldersTab(workspace, m_tabs);
    m_tabs->addTab(m_views[0], icon("users"), qtTrId("common.users"));
    m_tabs->addTab(m_views[1], icon("file"), qtTrId("common.files"));
    m_tabs->addTab(m_views[2], icon("folder"), qtTrId("investigation.folders"));
    connect(m_tabs, &QTabWidget::currentChanged, this, [this] {
        refreshCurrentTab();
        emit viewChanged();
    });
    for (InvestigationTab* view : m_views)
        connect(view->profile(), &ProfileView::valueChanged, this, &InvestigationPage::viewChanged);
}

InvestigationPage::Tab InvestigationPage::tab() const
{
    return static_cast<Tab>(std::clamp(m_tabs->currentIndex(), 0, 2));
}

QString InvestigationPage::value() const
{
    return m_views[static_cast<int>(tab())]->profile()->value();
}

void InvestigationPage::refresh()
{
    for (InvestigationTab* view : m_views)
        view->invalidate();
    refreshCurrentTab();
}

void InvestigationPage::clear()
{
    for (InvestigationTab* view : m_views)
        view->clear();
}

void InvestigationPage::refreshCurrentTab()
{
    auto* view = m_views[std::clamp(m_tabs->currentIndex(), 0, 2)];
    if (view->needsRefresh())
        view->refresh();
}

void InvestigationPage::focusOn(Tab tab, const QString& value)
{
    const int index = static_cast<int>(tab);
    m_tabs->setCurrentIndex(index);
    refreshIfNeeded();
    refreshCurrentTab();
    if (!value.isEmpty())
        m_views[index]->focusOn(value);
}

void InvestigationPage::restore(Tab tab, const QString& value)
{
    InvestigationTab* view = m_views[static_cast<int>(tab)];
    const bool change = view->profile()->value() != value;
    focusOn(tab, {});
    if (change)
        view->focusOn(value);
}

} // namespace lh::ui
