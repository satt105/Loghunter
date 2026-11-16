#include "EventTable.h"

#include "Components.h"
#include "EventModel.h"
#include "TableModel.h"
#include "core/Actions.h"
#include "core/AccessAnalyzer.h"
#include "core/Catalog.h"
#include "core/Format.h"
#include "core/DateTime.h"
#include "ui/Workspace.h"
#include "ui/Icons.h"
#include "ui/Settings.h"
#include "ui/Theme.h"
#include "ui/dialogs/MessageDialog.h"

#include <QAction>
#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <QStackedWidget>
#include <QTableView>
#include <QToolButton>
#include <QVBoxLayout>

namespace lh::ui {

namespace {

using Column = queries::Column;

constexpr int COPYABLE_ROWS = 100'000;
constexpr int COLUMNS = queries::COLUMN_COUNT;
constexpr std::array<const char*, COLUMNS> COLUMN_KEYS{
    "date", "level", "number", "event", "user", "computer", "program", "ip", "target", "log"};

constexpr int index(Column column)
{
    return static_cast<int>(column);
}

bool autoHideable(int column)
{
    switch (static_cast<Column>(column)) {
    case Column::Level:
    case Column::User:
    case Column::Machine:
    case Column::Process:
    case Column::Ip:
    case Column::Target:
    case Column::Channel:
        return true;
    default:
        return false;
    }
}

quint32 columnBit(int column)
{
    return queries::Uniformity::bit(static_cast<Column>(column));
}

} // namespace

EventTable::EventTable(Workspace* workspace, QWidget* parent) : QWidget(parent), m_workspace(workspace)
{
    m_model = new EventModel(workspace, this);

    m_view = new QTableView(this);
    m_view->setModel(m_model);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setShowGrid(false);
    m_view->setAlternatingRowColors(true);
    m_view->setWordWrap(false);
    m_view->setTextElideMode(Qt::ElideMiddle);
    m_view->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->verticalHeader()->hide();
    m_view->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_view->verticalHeader()->setDefaultSectionSize(m_view->fontMetrics().height() + 10);

    QHeaderView* header = m_view->horizontalHeader();
    header->setHighlightSections(false);
    header->setSectionsMovable(true);
    header->setMinimumSectionSize(48);
    header->setTextElideMode(Qt::ElideRight);
    const QFontMetrics metrics = m_view->fontMetrics();
    auto width = [&metrics](const QString& sample, int margin = 24) {
        return metrics.horizontalAdvance(sample) + margin;
    };
    m_view->setColumnWidth(index(Column::Timestamp),
                           width(formatTimestamp(QStringLiteral("0000-00-00T00:00:00"))));
    auto widest = [&width](std::initializer_list<const char*> identifiers, int margin) {
        int result = 0;
        for (const char* identifier : identifiers)
            result = std::max(result, width(qtTrId(identifier), margin));
        return result;
    };
    m_view->setColumnWidth(index(Column::Level),
                           widest({"level.warning", "level.audit_success", "level.audit_failure"}, 40));
    m_view->setColumnWidth(index(Column::EventId), width(QStringLiteral("00000"), 18));
    m_view->setColumnWidth(index(Column::Action),
                           widest({"event.security_4624", "event.security_5140", "action.attributes"}, 40));
    m_view->setColumnWidth(index(Column::User), width(QStringLiteral("DOMAIN\\name.surname")));
    m_view->setColumnWidth(index(Column::Machine), width(QStringLiteral("SRV-FILESERVER")));
    m_view->setColumnWidth(index(Column::Process), width(QStringLiteral("SearchProtocolHost.exe")));
    m_view->setColumnWidth(index(Column::Ip),
                           std::max(width(QStringLiteral("192.168.100.100")), width(qtTrId("common.source_ip"), 30)));
    m_view->setColumnWidth(index(Column::Channel), width(QStringLiteral("PowerShell")));
    m_fitter = new ColumnFitter(m_view, header, index(Column::Target));
    header->setSortIndicator(index(Column::Timestamp), Qt::DescendingOrder);
    m_view->setSortingEnabled(true);
    header->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(header, &QWidget::customContextMenuRequested, this, [this, header](const QPoint& position) {
        QMenu* menu = createColumnsMenu(this);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(header->mapToGlobal(position));
    });

    auto* copy = new QAction(icon("copy"), qtTrId("common.copy"), m_view);
    copy->setShortcut(QKeySequence::Copy);
    copy->setShortcutContext(Qt::WidgetShortcut);
    connect(copy, &QAction::triggered, this, &EventTable::copySelection);
    m_view->addAction(copy);

    m_empty = new EmptyState("search", qtTrId("events.no_event"), qtTrId("events.none_for_filters"), this);

    m_banner = new QWidget(this);
    auto* bannerLayout = new QHBoxLayout(m_banner);
    bannerLayout->setContentsMargins(8, 4, 8, 6);
    bannerLayout->setSpacing(12);
    m_bannerText = new Label({}, Label::Role::Small, m_banner);
    m_bannerText->setTextFormat(Qt::RichText);
    m_bannerText->setWordWrap(true);
    m_bannerText->setToolTip(qtTrId("events.columns_banner_help"));
    bannerLayout->addWidget(m_bannerText, 1);
    auto* showAll = new LinkButton(qtTrId("events.show_all_columns"), m_banner);
    connect(showAll, &LinkButton::clicked, this, [this] {
        m_hideUseless = false;
        savePreferences();
        applyColumns();
    });
    bannerLayout->addWidget(showAll, 0, Qt::AlignTop);
    m_banner->setVisible(false);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(m_view);
    m_stack->addWidget(m_empty);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_banner);
    layout->addWidget(m_stack, 1);

    m_loading = new LoadingIndicator(m_view);

    connect(m_model, &EventModel::loadingStarted, m_loading, &LoadingIndicator::start);
    connect(m_model, &EventModel::loadingFinished, this, &EventTable::loadingFinished);
    connect(m_model, &QAbstractItemModel::modelReset, this, [this] { emit selectionChanged(0); });
    connect(m_view->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current) { emit selectionChanged(m_model->identifier(current.row())); });
    connect(m_view, &QTableView::doubleClicked, this,
            [this](const QModelIndex& index) { emit eventActivated(m_model->identifier(index.row())); });
    connect(m_view, &QTableView::activated, this, [this](const QModelIndex& index) {
        if (QGuiApplication::mouseButtons() == Qt::NoButton)
            emit eventActivated(m_model->identifier(index.row()));
    });
    connect(m_view, &QWidget::customContextMenuRequested, this, &EventTable::contextMenu);
    connect(workspace, &Workspace::sessionChanged, this, [this] {
        cancel(m_uniformityToken);
        m_uniformity = {};
        applyColumns();
    });

    loadPreferences();
    applyColumns();
}

void EventTable::setFilters(const Filters& filters)
{
    m_model->setFilters(filters);
    cancel(m_uniformityToken);
    const Clause clause = filters.clause();
    m_uniformityToken = m_workspace->executor().runCached(
        this, QStringLiteral("uniformity|") + filters.key(),
        [clause](Context& context) { return queries::uniformity(context.connection, clause); },
        [this](queries::Uniformity uniformity) {
            m_uniformityToken.reset();
            m_uniformity = std::move(uniformity);
            applyColumns();
        },
        [this](const QString&) { m_uniformityToken.reset(); });
}

void EventTable::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    const QSet<int> hidden = m_hidden;
    const bool automatic = m_hideUseless;
    loadPreferences();
    if (hidden != m_hidden || automatic != m_hideUseless)
        applyColumns();
}

void EventTable::loadPreferences()
{
    const QStringList keys =
        settings::read(settings::key::HIDDEN_COLUMNS, QStringList{QStringLiteral("log")}).toStringList();
    m_hidden.clear();
    for (int column = 0; column < COLUMNS; ++column)
        if (column != index(Column::Timestamp)
            && keys.contains(QLatin1String(COLUMN_KEYS[static_cast<size_t>(column)])))
            m_hidden.insert(column);
    m_hideUseless = settings::read(settings::key::HIDE_USELESS_COLUMNS, true).toBool();
}

void EventTable::savePreferences() const
{
    QStringList keys;
    for (const int column : m_hidden)
        keys << QLatin1String(COLUMN_KEYS[static_cast<size_t>(column)]);
    keys.sort();
    settings::write(settings::key::HIDDEN_COLUMNS, keys);
    settings::write(settings::key::HIDE_USELESS_COLUMNS, m_hideUseless);
}

bool EventTable::isUselessColumn(int column) const
{
    return m_hideUseless && autoHideable(column)
           && ((m_uniformity.emptyBits | m_uniformity.identicalBits) & columnBit(column));
}

void EventTable::applyColumns()
{
    QStringList identicalBits;
    QStringList emptyBits;
    for (int column = 0; column < COLUMNS; ++column) {
        const bool chosen = !m_hidden.contains(column);
        const bool useless = isUselessColumn(column);
        m_view->setColumnHidden(column, !chosen || useless);
        if (!chosen || !useless)
            continue;
        const QString title = EventModel::columnTitle(column);
        if (m_uniformity.identicalBits & columnBit(column)) {
            const QString value = EventModel::cellText(m_uniformity.sample, column);
            identicalBits << QStringLiteral("%1 <b>%2</b>")
                                 .arg(title.toHtmlEscaped(),
                                      m_bannerText->fontMetrics()
                                          .elidedText(value, Qt::ElideMiddle, 420)
                                          .toHtmlEscaped());
        } else {
            emptyBits << title.toHtmlEscaped();
        }
    }
    m_model->setAlertOnDate(m_view->isColumnHidden(index(Column::Level)));
    QStringList parts;
    if (!identicalBits.isEmpty())
        parts << qtTrId("events.identical_everywhere").arg(identicalBits.join(QStringLiteral(" &nbsp;·&nbsp; ")));
    if (!emptyBits.isEmpty())
        parts << qtTrId("events.empty_columns").arg(emptyBits.join(QStringLiteral(", ")));
    m_bannerText->setText(parts.join(QStringLiteral(" &nbsp;&nbsp;|&nbsp;&nbsp; ")));
    m_banner->setVisible(!parts.isEmpty());
}

QMenu* EventTable::createColumnsMenu(QWidget* parent)
{
    auto* menu = new QMenu(parent);
    menu->addSection(qtTrId("events.displayed_columns"));
    for (int column = 0; column < COLUMNS; ++column) {
        const QString title = EventModel::columnTitle(column);
        QAction* action = menu->addAction(title);
        action->setCheckable(true);
        if (column == index(Column::Timestamp)) {
            action->setChecked(true);
            action->setEnabled(false);
            continue;
        }
        if (isUselessColumn(column) && !m_hidden.contains(column)) {
            const bool empty = m_uniformity.emptyBits & columnBit(column);
            action->setText(empty ? qtTrId("events.column_empty").arg(title)
                                  : qtTrId("events.column_identical").arg(title));
            action->setChecked(false);
            action->setEnabled(false);
            continue;
        }
        action->setChecked(!m_hidden.contains(column));
        connect(action, &QAction::toggled, this, [this, column](bool visible) {
            if (visible)
                m_hidden.remove(column);
            else
                m_hidden.insert(column);
            savePreferences();
            applyColumns();
        });
    }
    menu->addSeparator();
    QAction* automatic = menu->addAction(qtTrId("events.hide_useless_columns"));
    automatic->setCheckable(true);
    automatic->setChecked(m_hideUseless);
    automatic->setToolTip(qtTrId("events.columns_banner_help"));
    connect(automatic, &QAction::toggled, this, [this](bool active) {
        m_hideUseless = active;
        savePreferences();
        applyColumns();
    });
    menu->addAction(icon("refresh"), qtTrId("events.default_columns"), this, [this] {
        m_hidden = {index(Column::Channel)};
        m_hideUseless = true;
        savePreferences();
        applyColumns();
    });
    menu->setToolTipsVisible(true);
    return menu;
}

QToolButton* EventTable::createColumnsButton(QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setIcon(icon("columns"));
    button->setText(qtTrId("events.columns"));
    button->setToolTip(qtTrId("events.columns_help"));
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setAutoRaise(true);
    connect(button, &QToolButton::clicked, this, [this, button] {
        QMenu* menu = createColumnsMenu(button);
        menu->setAttribute(Qt::WA_DeleteOnClose);
        menu->popup(button->mapToGlobal(QPoint(0, button->height())));
    });
    return button;
}

void EventTable::setSort(queries::Column column, Qt::SortOrder order)
{
    m_model->setSort(column, order);
    m_view->horizontalHeader()->setSortIndicator(index(column), order);
}

void EventTable::setEmptyMessage(const QString& title, const QString& message)
{
    m_empty->changeText(title, message);
}

qint64 EventTable::currentIdentifier() const
{
    return m_model->identifier(m_view->currentIndex().row());
}

void EventTable::loadingFinished(qint64 total)
{
    m_loading->stop();
    m_stack->setCurrentWidget(total > 0 ? static_cast<QWidget*>(m_view) : m_empty);
}

void EventTable::copySelection()
{
    QList<int> rows;
    for (const QModelIndex& index : m_view->selectionModel()->selectedRows())
        rows.append(index.row());
    if (rows.isEmpty() && m_view->currentIndex().isValid())
        rows.append(m_view->currentIndex().row());
    if (rows.isEmpty())
        return;
    std::sort(rows.begin(), rows.end());
    if (rows.size() > COPYABLE_ROWS) {
        MessageDialog::inform(this, qtTrId("events.copy_limited"),
                              qtTrId("events.copy_limited_explanation")
                                  .arg(formatNumber(COPYABLE_ROWS)));
        rows.resize(COPYABLE_ROWS);
    }
    QList<int> columns;
    const QHeaderView* header = m_view->horizontalHeader();
    for (int position = 0; position < header->count(); ++position)
        if (const int column = header->logicalIndex(position); !header->isSectionHidden(column))
            columns.append(column);
    QString text;
    QStringList headers;
    for (const int column : std::as_const(columns))
        headers << EventModel::columnTitle(column);
    text += headers.join(QLatin1Char('\t')) + QLatin1Char('\n');
    for (const int row : rows) {
        const auto l = m_model->row(row);
        if (!l)
            continue;
        QStringList cells;
        for (const int column : std::as_const(columns))
            cells << EventModel::cellText(*l, column);
        text += cells.join(QLatin1Char('\t')) + QLatin1Char('\n');
    }
    QGuiApplication::clipboard()->setText(text);
}

void EventTable::contextMenu(const QPoint& position)
{
    const QModelIndex index = m_view->indexAt(position);
    if (!index.isValid())
        return;
    const auto row = m_model->row(index.row());
    if (!row)
        return;

    QMenu menu(this);
    menu.addAction(icon("copy"), qtTrId("common.copy"), QKeySequence::Copy, this,
                   &EventTable::copySelection);
    const QString value = index.data(Qt::ToolTipRole).toString().section(QLatin1Char('\n'), 0, 0);
    const QString cell = value.isEmpty() ? index.data().toString() : value;
    menu.addAction(
        qtTrId("common.copy_value").arg(QFontMetrics(menu.font()).elidedText(cell, Qt::ElideMiddle, 280)), this,
        [cell] { QGuiApplication::clipboard()->setText(cell); });
    menu.addSeparator();

    auto filter = [this](const std::function<void(Filters&)>& modification) { m_workspace->modifyFilters(modification); };
    auto exactPossible = [this](bool file) {
        const Filters& f = m_workspace->filters();
        return f.exact || (file ? f.user.isEmpty() : f.file.isEmpty());
    };
    QMenu* submenu = menu.addMenu(icon("filters"), qtTrId("events.filter_on"));
    if (!row->user.isEmpty())
        submenu->addAction(icon("user"), qtTrId("events.filter_this_user"), this, [=] {
            filter([&](Filters& f) {
                f.exact = exactPossible(false);
                f.user = row->user;
            });
        });
    const QString number = QString::number(row->eventId);
    submenu->addAction(icon("events"), qtTrId("events.filter_this_id").arg(number), this,
                       [=] { filter([&](Filters& f) { f.events = number; }); });
    submenu->addAction(icon("close"), qtTrId("events.exclude_this_id").arg(number), this, [=] {
        filter([&](Filters& f) {
            const QString existing = f.events.trimmed();
            f.events = existing.isEmpty() ? QString(QLatin1Char('-') + number)
                                          : QString(existing + QStringLiteral(", -") + number);
        });
    });
    if (!row->sourceIp.isEmpty())
        submenu->addAction(icon("machine"), qtTrId("events.filter_this_ip").arg(row->sourceIp), this,
                           [=] { filter([&](Filters& f) { f.ip = row->sourceIp; }); });
    if (!row->channel.isEmpty())
        submenu->addAction(icon("logs"), qtTrId("events.filter_this_log").arg(shortChannelName(row->channel)),
                           this, [=] { filter([&](Filters& f) { f.channel = row->channel; }); });
    if (!row->file.isEmpty())
        submenu->addAction(icon("file"), qtTrId("events.filter_this_file"), this, [=] {
            filter([&](Filters& f) {
                f.exact = exactPossible(true);
                f.file = row->file;
            });
        });
    const QString folder = parentFolder(row->file);
    if (!folder.isEmpty())
        submenu->addAction(icon("folder"), qtTrId("events.filter_this_folder"), this,
                           [=] { filter([&](Filters& f) { f.folder = folder; }); });
    if (!row->process.isEmpty())
        submenu->addAction(icon("process"), qtTrId("events.filter_this_program"), this,
                           [=] { filter([&](Filters& f) { f.process = row->process; }); });
    const QDate day = QDate::fromString(row->timestamp.left(10), Qt::ISODate);
    if (day.isValid())
        submenu->addAction(icon("calendar"), qtTrId("events.filter_this_day").arg(formatDate(day)), this,
                           [=] {
                               filter([&](Filters& f) {
                                   f.since = day;
                                   f.until = day;
                               });
                           });
    if (!row->category.isEmpty())
        submenu->addAction(dot(theme::categoryColor(row->category)),
                           qtTrId("events.filter_this_action").arg(categoryLabel(row->category)), this,
                           [=] { filter([&](Filters& f) { f.category = row->category; }); });

    menu.addSeparator();
    if (!row->user.isEmpty())
        menu.addAction(icon("investigation"), qtTrId("common.user_activity"), this, [=, this] {
            m_workspace->navigate({Navigation::Target::User, row->user});
        });
    if (!row->file.isEmpty())
        menu.addAction(icon("investigation"), qtTrId("common.file_history"), this,
                       [=, this] { m_workspace->navigate({Navigation::Target::File, row->file}); });
    if (!folder.isEmpty())
        menu.addAction(icon("folder"), qtTrId("events.show_folder"), this,
                       [=, this] { m_workspace->navigate({Navigation::Target::Folder, folder}); });
    menu.addSeparator();
    QMenu* columns = createColumnsMenu(&menu);
    columns->setIcon(icon("columns"));
    columns->setTitle(qtTrId("events.columns"));
    menu.addMenu(columns);
    menu.exec(m_view->viewport()->mapToGlobal(position));
}

} // namespace lh::ui
