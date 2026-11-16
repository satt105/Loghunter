#include "EventModel.h"

#include "core/Actions.h"
#include "core/Alerts.h"
#include "core/AccessAnalyzer.h"
#include "core/Catalog.h"
#include "core/DateTime.h"
#include "ui/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QApplication>

#include <limits>

namespace lh::ui {

EventModel::EventModel(Workspace* workspace, QObject* parent) : QAbstractTableModel(parent), m_workspace(workspace)
{
    connect(workspace, &Workspace::sessionChanged, this, &EventModel::clear);
    connect(workspace, &Workspace::dataModified, this, &EventModel::clear);
}

void EventModel::setFilters(const Filters& filters)
{
    m_filters = filters;
    m_filtersSet = true;
    reload();
}

void EventModel::setSort(Column column, Qt::SortOrder order)
{
    if (column == m_sortColumn && order == m_order)
        return;
    m_sortColumn = column;
    m_order = order;
    reload();
}

void EventModel::sort(int column, Qt::SortOrder order)
{
    if (column >= 0 && column < queries::COLUMN_COUNT)
        setSort(static_cast<Column>(column), order);
}

void EventModel::clear()
{
    cancel(m_token);
    beginResetModel();
    m_ids.reset();
    m_pages.clear();
    m_pageOrder.clear();
    endResetModel();
}

void EventModel::reload()
{
    cancel(m_token);
    if (!m_filtersSet)
        return;
    const Clause clause = m_filters.clause();
    const Column sortKey = m_sortColumn;
    const bool ascending = m_order == Qt::AscendingOrder;
    emit loadingStarted();
    const QString key = QStringLiteral("identifiers|%1|%2|%3")
                            .arg(m_filters.key())
                            .arg(static_cast<int>(m_sortColumn))
                            .arg(ascending);
    m_token = m_workspace->executor().runCached(
        this, key,
        [clause, sortKey, ascending](Context& context) { return queries::identifiers(context, clause, sortKey, ascending); },
        [this](queries::Identifiers ids) {
            m_token.reset();
            beginResetModel();
            m_ids = std::move(ids);
            m_pages.clear();
            m_pageOrder.clear();
            endResetModel();
            emit loadingFinished(total());
        },
        [this](const QString& message) {
            m_token.reset();
            clear();
            emit loadingFinished(0);
            emit m_workspace->executor().error(message);
        });
}

void EventModel::setAlertOnDate(bool active)
{
    if (active == m_alertOnDate)
        return;
    m_alertOnDate = active;
    if (rowCount() > 0)
        emit dataChanged(index(0, 0), index(rowCount() - 1, 0), {Qt::DecorationRole});
}

qint64 EventModel::identifier(int row) const
{
    return m_ids && row >= 0 && static_cast<size_t>(row) < m_ids->size() ? (*m_ids)[static_cast<size_t>(row)] : 0;
}

std::optional<queries::TableRow> EventModel::row(int row) const
{
    const queries::TableRow* result = loadedRow(row);
    return result ? std::optional(*result) : std::nullopt;
}

const queries::TableRow* EventModel::loadedRow(int row) const
{
    if (!m_ids || row < 0 || static_cast<size_t>(row) >= m_ids->size())
        return nullptr;
    const int page = row / ROWS_PER_PAGE;
    auto it = m_pages.find(page);
    if (it == m_pages.end()) {
        const size_t start = static_cast<size_t>(page) * ROWS_PER_PAGE;
        const size_t count = std::min<size_t>(ROWS_PER_PAGE, m_ids->size() - start);
        QList<queries::TableRow> rows;
        try {
            rows = queries::rows(m_workspace->session().mainConnection(), std::span<const qint64>(m_ids->data() + start, count));
        } catch (const std::exception&) {
            rows.clear();
        }
        if (m_pages.size() >= CACHED_PAGES && !m_pageOrder.isEmpty())
            m_pages.remove(m_pageOrder.takeFirst());
        it = m_pages.insert(page, std::move(rows));
        m_pageOrder.append(page);
    } else if (m_pageOrder.isEmpty() || m_pageOrder.last() != page) {
        m_pageOrder.removeOne(page);
        m_pageOrder.append(page);
    }
    const int position = row - page * ROWS_PER_PAGE;
    const auto& rows = it.value();
    return position < rows.size() && rows[position].id != 0 ? &rows[position] : nullptr;
}

int EventModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() || !m_ids
               ? 0
               : static_cast<int>(std::min<size_t>(m_ids->size(), std::numeric_limits<int>::max()));
}

int EventModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : queries::COLUMN_COUNT;
}

const QIcon& EventModel::colorDot(const QColor& color) const
{
    auto it = m_dots.constFind(color.rgba());
    if (it == m_dots.cend())
        it = m_dots.insert(color.rgba(), dot(color));
    return *it;
}

const QIcon& EventModel::alertIcon(int level) const
{
    auto it = m_alertIcons.constFind(level);
    if (it == m_alertIcons.cend())
        it = m_alertIcons.insert(level, icon("alerts", theme::severityColor(severityFromLevel(level))));
    return *it;
}

QString EventModel::cellText(const queries::TableRow& l, int column)
{
    switch (static_cast<Column>(column)) {
    case Column::Timestamp:
        return formatTimestamp(l.timestamp);
    case Column::Level:
        return levelLabel(l.level, l.audit);
    case Column::EventId:
        return QString::number(l.eventId);
    case Column::Action: {
        if (!l.action.isEmpty())
            return actionLabel(l.action);
        const QString description = eventDescription(l.eventId, l.channel);
        return description.isEmpty() ? l.provider : description;
    }
    case Column::User:
        return l.user;
    case Column::Machine:
        return l.machine.section(QLatin1Char('.'), 0, 0);
    case Column::Process:
        return fileName(l.process);
    case Column::Ip:
        return l.sourceIp;
    case Column::Target:
        return l.file.isEmpty() ? l.detail : l.file;
    case Column::Channel:
        return shortChannelName(l.channel);
    }
    return {};
}

QString EventModel::columnTitle(int column)
{
    switch (static_cast<Column>(column)) {
    case Column::Timestamp:
        return qtTrId("common.date_time");
    case Column::Level:
        return qtTrId("common.level");
    case Column::EventId:
        return qtTrId("events.column_id");
    case Column::Action:
        return qtTrId("common.event");
    case Column::User:
        return qtTrId("common.user");
    case Column::Machine:
        return qtTrId("common.computer");
    case Column::Process:
        return qtTrId("common.program");
    case Column::Ip:
        return qtTrId("common.source_ip");
    case Column::Target:
        return qtTrId("common.target");
    case Column::Channel:
        return qtTrId("common.windows_log");
    }
    return {};
}

QVariant EventModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid())
        return {};
    if (role == IdentifierRole)
        return identifier(index.row());
    const queries::TableRow* l = loadedRow(index.row());
    if (!l)
        return role == Qt::DisplayRole && index.column() == 0 ? QStringLiteral("…") : QVariant();
    const auto column = static_cast<Column>(index.column());

    switch (role) {
    case Qt::DisplayRole:
        return cellText(*l, index.column());
    case Qt::ToolTipRole:
        switch (column) {
        case Column::Level:
            return l->alert >= 0 ? qtTrId("events.tooltip_alert")
                                       .arg(levelLabel(l->level, l->audit),
                                            severityLabel(severityFromLevel(l->alert)).toLower())
                                 : levelLabel(l->level, l->audit);
        case Column::User:
            return l->isSystem ? l->user + QLatin1Char('\n') + qtTrId("common.system_or_service_account")
                               : l->user;
        case Column::Action:
            if (l->action.isEmpty())
                return eventLabel(l->eventId, l->channel, l->provider);
            return qtTrId("events.tooltip_action").arg(actionLabel(l->action), categoryLabel(l->category));
        case Column::Target:
            return l->file.isEmpty() ? l->detail : l->file;
        case Column::Process:
            return l->process;
        case Column::Machine:
            return l->machine;
        case Column::Channel:
            return l->channel;
        default:
            return {};
        }
    case Qt::DecorationRole:
        if (column == Column::Timestamp && m_alertOnDate && l->alert >= 0)
            return alertIcon(l->alert);
        if (column == Column::Level)
            return l->alert >= 0 ? alertIcon(l->alert)
                                 : colorDot(theme::levelColor(levelRank(l->level, l->audit)));
        if (column == Column::Action && !l->category.isEmpty())
            return colorDot(theme::categoryColor(l->category));
        return {};
    case Qt::ForegroundRole:
        if (column == Column::User && l->isSystem)
            return theme::secondaryText(QApplication::palette());
        return {};
    case Qt::TextAlignmentRole:
        if (column == Column::EventId)
            return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
        return {};
    default:
        return {};
    }
}

QVariant EventModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal)
        return {};
    if (role == Qt::DisplayRole)
        return columnTitle(section);
    if (role == Qt::ToolTipRole) {
        switch (static_cast<Column>(section)) {
        case Column::EventId:
            return qtTrId("events.column_id_help");
        case Column::Target:
            return qtTrId("events.column_target_help");
        case Column::Level:
            return qtTrId("events.column_level_help");
        default:
            return {};
        }
    }
    if (role == Qt::TextAlignmentRole && static_cast<Column>(section) == Column::EventId)
        return QVariant::fromValue(Qt::AlignRight | Qt::AlignVCenter);
    return {};
}

} // namespace lh::ui
