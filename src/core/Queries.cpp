#include "Queries.h"

#include "Actions.h"
#include "Blocks.h"
#include "Calendar.h"
#include "Session.h"
#include "DateTime.h"

#include <QHash>

#include <algorithm>
#include <limits>
#include <numeric>

namespace lh::queries {

namespace {

constexpr qint64 HOUR = 3600LL * 1'000'000;
constexpr qsizetype MAX_CANDIDATES = 4096;

struct GroupCount
{
    qint64 count = 0;
    qint64 group = -1;

    void add(qint64 n, qint64 g) noexcept
    {
        count += n;
        if (group < 0)
            group = g;
    }
};

template <typename T>
std::vector<const T*> sortByCount(const Aggregate<T>& aggregate, qsizetype limit)
{
    std::vector<const T*> order;
    order.reserve(aggregate.values().size());
    for (const T& value : aggregate.values())
        order.push_back(&value);
    const auto kept = static_cast<std::ptrdiff_t>(std::min<qsizetype>(limit, static_cast<qsizetype>(order.size())));
    std::partial_sort(order.begin(), order.begin() + kept, order.end(), [](const T* a, const T* b) {
        return a->count != b->count ? a->count > b->count : a->group < b->group;
    });
    order.resize(static_cast<size_t>(kept));
    return order;
}

Series ranking(Context& context, const Aggregate<GroupCount>& aggregate, const QString& column, int limit)
{
    const std::vector<GroupCount>& values = aggregate.values();
    if (values.empty())
        return {};
    std::vector<qsizetype> order(values.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&values](qsizetype a, qsizetype b) {
        const GroupCount& x = values[static_cast<size_t>(a)];
        const GroupCount& y = values[static_cast<size_t>(b)];
        return x.count != y.count ? x.count > y.count : x.group < y.group;
    });
    const qint64 threshold = values[static_cast<size_t>(order[static_cast<size_t>(
                                 std::min<qsizetype>(limit, static_cast<qsizetype>(order.size())) - 1)])]
                                 .count;
    Series series;
    for (const qsizetype index : order) {
        const GroupCount& entry = values[static_cast<size_t>(index)];
        if (entry.count < threshold || series.size() >= MAX_CANDIDATES)
            break;
        series.append({groupText(context.connection, column, entry.group), entry.count});
    }
    std::sort(series.begin(), series.end(), [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : a.first < b.first;
    });
    if (series.size() > limit)
        series.resize(limit);
    return series;
}

QMap<QString, qint64> byCategory(const std::array<qint64, 6>& counts)
{
    QMap<QString, qint64> result;
    const auto& list = categories();
    for (size_t i = 0; i < list.size(); ++i)
        if (counts[i] > 0)
            result.insert(QLatin1String(list[i].key), counts[i]);
    return result;
}

DateSeries fillDays(const QMap<QDate, qint64>& values)
{
    DateSeries series;
    if (values.isEmpty())
        return series;
    const QDate start = values.firstKey();
    const QDate end = values.lastKey();
    if (start.daysTo(end) > 3660) {
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            series.append({it.key(), it.value()});
        return series;
    }
    series.reserve(start.daysTo(end) + 1);
    for (QDate day = start; day <= end; day = day.addDays(1))
        series.append({day, values.value(day, 0)});
    return series;
}

DateSeries daysFromSlots(const GroupCounts& counts)
{
    QMap<QDate, qint64> days;
    for (auto it = counts.bySlot.cbegin(); it != counts.bySlot.cend(); ++it)
        if (it.key() >= 0)
            days[dateFromDay(it.key() / 32)] += it.value();
    return fillDays(days);
}

bool isLocalIp(QByteArrayView ip)
{
    return ip.isEmpty() || ip == QByteArrayView("127.0.0.1") || ip == QByteArrayView("::1")
           || ip == QByteArrayView("0.0.0.0") || ip == QByteArrayView("::");
}

QString sortColumn(Column column)
{
    switch (column) {
    case Column::Level:
        return QStringLiteral(
            "(CASE WHEN level = 1 THEN 3 WHEN level = 2 OR audit = 2 THEN 2 WHEN level = 3 THEN 1 ELSE 0 END)");
    case Column::User:
        return QStringLiteral("lh_text_key(user), user");
    case Column::Action:
        return QStringLiteral("action, event_id");
    case Column::Target:
        return QStringLiteral("lh_text_key(file_path || detail), file_path || detail");
    case Column::Process:
        return QStringLiteral("lh_text_key(lh_file_name(process)), process");
    case Column::Machine:
        return QStringLiteral("lh_text_key(machine), machine");
    case Column::EventId:
        return QStringLiteral("event_id");
    case Column::Ip:
        return QStringLiteral("lh_ip_key(source_ip)");
    case Column::Channel:
        return QStringLiteral("channel");
    case Column::Timestamp:
        break;
    }
    return QStringLiteral("ts_utc");
}

const QString TABLE_COLUMNS = QStringLiteral(
    "id, timestamp, user, is_system, action, action_cat, file_path, process, machine, event_id, level, audit, channel, "
    "source_ip, detail, (SELECT MAX(r.level) FROM matches c JOIN rules r ON r.number = c.rule "
    "WHERE c.event = event_view.id), provider");

TableRow readLine(const sql::Statement& statement)
{
    TableRow row;
    row.id = statement.integer(0);
    row.timestamp = statement.text(1);
    row.user = statement.text(2);
    row.isSystem = statement.integer(3) != 0;
    row.action = statement.text(4);
    row.category = statement.text(5);
    row.file = statement.text(6);
    row.process = statement.text(7);
    row.machine = statement.text(8);
    row.eventId = static_cast<quint32>(statement.integer(9));
    row.level = static_cast<int>(statement.integer(10));
    row.audit = static_cast<int>(statement.integer(11));
    row.channel = statement.text(12);
    row.sourceIp = statement.text(13);
    row.detail = statement.text(14);
    row.alert = statement.isNull(15) ? -1 : static_cast<int>(statement.integer(15));
    row.provider = statement.text(16);
    return row;
}

qint64 histogramStep(qint64 extent, int maxBars)
{
    static constexpr qint64 SECOND = 1'000'000;
    static constexpr qint64 STEPS[] = {SECOND,            5 * SECOND,         10 * SECOND,       30 * SECOND,
                                       60 * SECOND,       5 * 60 * SECOND,    10 * 60 * SECOND,  15 * 60 * SECOND,
                                       30 * 60 * SECOND,  3600 * SECOND,      2 * 3600 * SECOND, 3 * 3600 * SECOND,
                                       6 * 3600 * SECOND, 12 * 3600 * SECOND, 86400 * SECOND,    2 * 86400 * SECOND,
                                       7 * 86400 * SECOND, 14 * 86400 * SECOND, 30 * 86400 * SECOND,
                                       91 * 86400 * SECOND, 365 * 86400 * SECOND};
    for (const qint64 step : STEPS)
        if (extent / step < maxBars)
            return step;
    return STEPS[std::size(STEPS) - 1] * (extent / (STEPS[std::size(STEPS) - 1] * maxBars) + 1);
}

qint64 maxGroup(sql::Connection& connection)
{
    return connection.integer(QStringLiteral("SELECT COALESCE(MAX(group_id), 0) FROM groups"));
}

} // namespace

// ── Summary ──────────────────────────────────────────────────────────────────

Summary summary(Context& context, const Filters& filters)
{
    Summary result;
    const Clause clause = filters.clause();
    const std::shared_ptr<const GroupCounts> counts = context.counts(clause);

    QMap<QDate, qint64> days;
    for (auto it = counts->bySlot.cbegin(); it != counts->bySlot.cend(); ++it) {
        const qint64 count = it.value();
        const bool dayKnown = it.key() >= 0;
        const qint64 hour = dayKnown ? it.key() % 32 - 1 : -2 - it.key();
        const QDate date = dayKnown ? dateFromDay(it.key() / 32) : QDate();
        if (date.isValid())
            days[date] += count;
        if (hour >= 0 && hour < 24) {
            result.byHour[static_cast<size_t>(hour)] += count;
            if (date.isValid())
                result.byDayHour[static_cast<size_t>(date.dayOfWeek() - 1)][static_cast<size_t>(hour)] += count;
        }
    }
    result.total = counts->total;
    result.byDay = fillDays(days);
    if (result.total == 0)
        return result;
    if (counts->date()) {
        TimestampFormatter timestamp(context.connection);
        result.first = timestamp(counts->firstTs);
        result.last = timestamp(counts->lastTs);
    }

    QHash<qint64, qint64> alertsByGroup;
    {
        QString sql = QStringLiteral("SELECT e.group_id FROM (SELECT DISTINCT event FROM matches) c "
                                     "CROSS JOIN events e ON e.id = c.event");
        if (clause.eventBased())
            sql += QStringLiteral(" WHERE (%1)").arg(clause.events);
        sql::Statement statement = context.connection.prepare(sql);
        statement.bindAll(clause.eventParameters);
        qint64 readCount = 0;
        while (statement.step()) {
            if ((++readCount & 0xffff) == 0)
                context.throwIfCancelled();
            const qint64 group = statement.integer(0);
            if (counts->countOf(group) > 0)
                ++alertsByGroup[group];
        }
    }

    struct Event
    {
        qint64 count = 0;
        qint64 group = -1;
        quint32 identifier = 0;
        QByteArray provider;
    };
    std::array<qint64, 6> accountCategories{};
    Aggregate<GroupCount> users, process, channels, ips, suspiciousIps;
    Aggregate<Event> events;
    FingerprintSet files, machines;
    GroupReader groups(context, clause,
                       QStringLiteral("action_cat, user, file_path, machine, process, channel, event_id, provider, "
                                      "audit, source_ip"),
                       counts);
    while (groups.next()) {
        const sql::Statement& row = groups.row();
        const qint64 group = groups.group();
        const qint64 count = groups.count();
        if (const int category = categoryIndex(row.bytes(1)); category >= 0)
            accountCategories[static_cast<size_t>(category)] += count;
        if (const QByteArrayView user = row.bytes(2); !user.isEmpty())
            users.findOrAdd(fingerprint(user)).add(count, group);
        if (const QByteArrayView file = row.bytes(3); !file.isEmpty())
            files.insert(fingerprint(file));
        if (const QByteArrayView machine = row.bytes(4); !machine.isEmpty())
            machines.insert(fingerprint(machine));
        if (const QByteArrayView program = row.bytes(5); !program.isEmpty())
            process.findOrAdd(fingerprint(program)).add(count, group);
        const QByteArrayView channel = row.bytes(6);
        const quint64 channelFingerprint = fingerprint(channel);
        if (!channel.isEmpty())
            channels.findOrAdd(channelFingerprint).add(count, group);
        const auto identifier = static_cast<quint32>(row.integer(7));
        Event& event = events.findOrAdd(mixFingerprint(channelFingerprint, identifier));
        const QByteArrayView provider = row.bytes(8);
        if (event.count == 0 || QByteArrayView(event.provider).compare(provider) > 0)
            event.provider = provider.toByteArray();
        event.count += count;
        event.identifier = identifier;
        if (event.group < 0)
            event.group = group;
        if (const QByteArrayView ip = row.bytes(10); !isLocalIp(ip)) {
            const quint64 ipFingerprint = fingerprint(ip);
            ips.findOrAdd(ipFingerprint).add(count, group);
            const qint64 suspicious = row.integer(9) == 2 ? count : alertsByGroup.value(group);
            if (suspicious > 0)
                suspiciousIps.findOrAdd(ipFingerprint).add(suspicious, group);
        }
    }
    context.throwIfCancelled();

    result.byCategory = byCategory(accountCategories);
    for (const qint64 count : result.byCategory)
        result.fileAccesses += count;
    result.users = users.size();
    result.files = files.size();
    result.machines = machines.size();
    result.topUsers = ranking(context, users, QStringLiteral("user"), 10);
    result.topProcesses = ranking(context, process, QStringLiteral("process"), 10);
    result.byChannel = ranking(context, channels, QStringLiteral("channel"), 8);

    QList<EventRanking> frequentEvents;
    frequentEvents.reserve(events.size());
    for (const Event& event : events.values())
        frequentEvents.append({event.identifier,
                               groupText(context.connection, QStringLiteral("channel"), event.group),
                               QString::fromUtf8(event.provider), event.count});
    std::sort(frequentEvents.begin(), frequentEvents.end(),
              [](const EventRanking& a, const EventRanking& b) {
                  if (a.total != b.total)
                      return a.total > b.total;
                  return a.identifier != b.identifier ? a.identifier < b.identifier : a.channel < b.channel;
              });
    result.topEvents = frequentEvents.mid(0, 10);
    result.topSuspiciousIps = ranking(context, suspiciousIps, QStringLiteral("source_ip"), 10);
    if (result.topSuspiciousIps.isEmpty())
        result.topIp = ranking(context, ips, QStringLiteral("source_ip"), 10);
    return result;
}

// ── Investigation ────────────────────────────────────────────────────────────

QList<UserRow> users(Context& context, const Filters& filters, int limit)
{
    struct User
    {
        qint64 count = 0;
        qint64 group = -1;
        qint64 last = UNKNOWN_TIMESTAMP;
        bool isSystem = false;
    };
    const Clause clause = filters.clause();
    const std::shared_ptr<const GroupCounts> counts = context.counts(clause);
    Aggregate<User> aggregate;
    GroupReader groups(context, clause.andGroupWhere(QStringLiteral("user <> ''")), QStringLiteral("user, is_system"),
                       counts);
    while (groups.next()) {
        const qint64 group = groups.group();
        User& user = aggregate.findOrAdd(fingerprint(groups.row().bytes(1)));
        user.count += groups.count();
        user.isSystem = user.isSystem || groups.row().integer(2) != 0;
        if (user.group < 0)
            user.group = group;
        const qint64 last = counts->last[static_cast<size_t>(group)];
        if (counts->first[static_cast<size_t>(group)] <= last)
            user.last = std::max(user.last, last);
    }
    context.throwIfCancelled();

    QList<UserRow> list;
    TimestampFormatter timestamp(context.connection);
    for (const User* u : sortByCount(aggregate, limit))
        list.append({groupText(context.connection, QStringLiteral("user"), u->group), u->count, u->isSystem,
                     timestamp(u->last)});
    return list;
}

QList<FileRow> files(Context& context, const Filters& filters, int limit)
{
    struct File
    {
        qint64 count = 0;
        qint64 users = 0;
        qint64 group = -1;
        qint64 first = std::numeric_limits<qint64>::max();
        qint64 last = UNKNOWN_TIMESTAMP;
        std::array<qint64, 6> categories{};
    };
    const Clause clause = filters.clause();
    const std::shared_ptr<const GroupCounts> counts = context.counts(clause);
    Aggregate<File> aggregate;
    FingerprintSet pairs;
    GroupReader groups(context, clause.andGroupWhere(QStringLiteral("action_cat <> '' AND file_path <> ''")),
                       QStringLiteral("file_path, user, action_cat"), counts);
    while (groups.next()) {
        const sql::Statement& row = groups.row();
        const qint64 group = groups.group();
        const qint64 count = groups.count();
        const quint64 key = fingerprint(row.bytes(1));
        File& file = aggregate.findOrAdd(key);
        file.count += count;
        if (file.group < 0)
            file.group = group;
        if (const int category = categoryIndex(row.bytes(3)); category >= 0)
            file.categories[static_cast<size_t>(category)] += count;
        if (pairs.insert(mixFingerprint(key, fingerprint(row.bytes(2)))))
            ++file.users;
        const qint64 first = counts->first[static_cast<size_t>(group)];
        const qint64 last = counts->last[static_cast<size_t>(group)];
        if (first <= last) {
            file.first = std::min(file.first, first);
            file.last = std::max(file.last, last);
        }
    }
    context.throwIfCancelled();

    QList<FileRow> list;
    TimestampFormatter timestamp(context.connection);
    const auto& categoryList = categories();
    for (const File* f : sortByCount(aggregate, limit)) {
        FileRow row;
        row.path = groupText(context.connection, QStringLiteral("file_path"), f->group);
        row.total = f->count;
        row.users = f->users;
        row.first = timestamp(f->first);
        row.last = timestamp(f->last);
        row.byCategory = byCategory(f->categories);
        qint64 maximum = -1;
        for (size_t c = 0; c < categoryList.size(); ++c) {
            if (f->categories[c] > maximum) {
                maximum = f->categories[c];
                row.dominantCategory = QLatin1String(categoryList[c].key);
            }
        }
        list.append(row);
    }
    return list;
}

QList<FolderRow> folders(Context& context, const Filters& filters, int limit)
{
    struct Folder
    {
        qint64 count = 0;
        qint64 group = -1;
        qint64 users = 0;
        std::array<qint64, 6> categories{};
    };
    const Clause clause = filters.clause();
    const std::shared_ptr<const GroupCounts> counts = context.counts(clause);
    Aggregate<Folder> aggregate;
    FingerprintSet pairs;
    GroupReader groups(context, clause.andGroupWhere(QStringLiteral("action_cat <> '' AND file_path <> ''")),
                       QStringLiteral("dir_path, user, action_cat"), counts);
    while (groups.next()) {
        const sql::Statement& row = groups.row();
        const qint64 count = groups.count();
        const quint64 key = fingerprint(row.bytes(1));
        Folder& folder = aggregate.findOrAdd(key);
        folder.count += count;
        if (folder.group < 0)
            folder.group = groups.group();
        if (const int category = categoryIndex(row.bytes(3)); category >= 0)
            folder.categories[static_cast<size_t>(category)] += count;
        if (pairs.insert(mixFingerprint(key, fingerprint(row.bytes(2)))))
            ++folder.users;
    }
    context.throwIfCancelled();

    const auto index = [](const char* key) { return static_cast<size_t>(categoryIndex(QByteArrayView(key))); };
    const size_t modification = index(category::Modification);
    const size_t deletion = index(category::Deletion);
    const size_t reader = index(category::Read);
    const size_t permissions = index(category::Permissions);
    QList<FolderRow> list;
    for (const Folder* d : sortByCount(aggregate, limit)) {
        FolderRow row;
        row.path = groupText(context.connection, QStringLiteral("dir_path"), d->group);
        row.total = d->count;
        row.writes = d->categories[modification];
        row.deletions = d->categories[deletion];
        row.reads = d->categories[reader];
        row.permissions = d->categories[permissions];
        row.users = d->users;
        list.append(row);
    }
    return list;
}

Profile profile(Context& context, const Filters& filters)
{
    Profile result;
    const Clause clause = filters.clause();
    const std::shared_ptr<const GroupCounts> counts = context.counts(clause);
    result.total = counts->total;
    if (result.total == 0)
        return result;
    if (counts->date()) {
        TimestampFormatter timestamp(context.connection);
        result.first = timestamp(counts->firstTs);
        result.last = timestamp(counts->lastTs);
    }

    FingerprintSet machines;
    Aggregate<GroupCount> topUsers, topFiles;
    std::array<qint64, 6> accountCategories{};
    GroupReader groups(context, clause, QStringLiteral("user, file_path, machine, action_cat"), counts);
    while (groups.next()) {
        const sql::Statement& row = groups.row();
        const qint64 group = groups.group();
        const qint64 count = groups.count();
        if (const QByteArrayView user = row.bytes(1); !user.isEmpty())
            topUsers.findOrAdd(fingerprint(user)).add(count, group);
        if (const QByteArrayView file = row.bytes(2); !file.isEmpty())
            topFiles.findOrAdd(fingerprint(file)).add(count, group);
        if (const QByteArrayView machine = row.bytes(3); !machine.isEmpty())
            machines.insert(fingerprint(machine));
        if (const int category = categoryIndex(row.bytes(4)); category >= 0)
            accountCategories[static_cast<size_t>(category)] += count;
    }
    context.throwIfCancelled();

    result.files = topFiles.size();
    result.users = topUsers.size();
    result.machines = machines.size();
    result.byCategory = byCategory(accountCategories);
    result.byDay = daysFromSlots(*counts);
    result.topUsers = ranking(context, topUsers, QStringLiteral("user"), 10);
    result.topFiles = ranking(context, topFiles, QStringLiteral("file_path"), 10);
    return result;
}

QStringList distinctValues(sql::Connection& connection, const QString& column, int limit)
{
    static const QStringList ALLOWED{QStringLiteral("user"), QStringLiteral("process"), QStringLiteral("machine"),
                                     QStringLiteral("channel")};
    if (!ALLOWED.contains(column))
        return {};
    QStringList values;
    sql::Statement statement = connection.prepare(
        QStringLiteral("SELECT %1 FROM groups WHERE %1 <> '' GROUP BY %1 ORDER BY SUM(total) DESC LIMIT %2")
            .arg(column)
            .arg(limit));
    while (statement.step())
        values << statement.text(0);
    return values;
}

// ── Event table ──────────────────────────────────────────────────────────────

Identifiers identifiers(Context& context, const Clause& clause, Column sortKey, bool ascending)
{
    const std::shared_ptr<const AdmittedGroups> admitted = context.admitted(clause);
    auto ids = std::make_shared<std::vector<qint64>>();
    ids->reserve(static_cast<size_t>(std::max<qint64>(0, admitted->all ? admitted->events : admitted->estimation)));
    const QString direction = ascending ? QStringLiteral("ASC") : QStringLiteral("DESC");

    if (sortKey == Column::Timestamp) {
        const QString order = admitted->rare ? QStringLiteral("ts_utc %1, id %1").arg(direction)
                                             : QStringLiteral("ts_utc %1").arg(direction);
        EventReader reader(context, clause, admitted, QStringLiteral("id, ts_utc"), order);
        qint64 currentTs = 0;
        size_t seriesStart = 0;
        bool earliest = true;
        auto sortRange = [&](size_t start, size_t end) {
            if (end - start < 2)
                return;
            const auto first = ids->begin() + static_cast<std::ptrdiff_t>(start);
            const auto last = ids->begin() + static_cast<std::ptrdiff_t>(end);
            if (ascending)
                std::sort(first, last);
            else
                std::sort(first, last, std::greater<>());
        };
        while (reader.next()) {
            const sql::Statement& row = reader.row();
            const qint64 ts = row.isNull(2) ? UNKNOWN_TIMESTAMP : row.integer(2);
            if (!earliest && ts != currentTs) {
                sortRange(seriesStart, ids->size());
                seriesStart = ids->size();
            }
            earliest = false;
            currentTs = ts;
            ids->push_back(row.integer(1));
        }
        sortRange(seriesStart, ids->size());
        context.throwIfCancelled();
        return ids;
    }

    const qint64 maximum = maxGroup(context.connection);
    std::vector<qint32> ranks(static_cast<size_t>(maximum) + 1, -1);
    qint32 currentRank = -1;
    {
        const QStringList keys = sortColumn(sortKey).split(QStringLiteral(", "));
        QStringList order;
        for (const QString& key : keys)
            order << key + QLatin1Char(' ') + direction;
        GroupReader groups(context, clause, keys.join(QStringLiteral(", ")), {}, order.join(QStringLiteral(", ")));
        std::vector<QByteArray> previous(static_cast<size_t>(keys.size()));
        while (groups.next()) {
            const sql::Statement& row = groups.row();
            bool differs = currentRank < 0;
            for (int i = 0; i < keys.size() && !differs; ++i)
                differs = row.bytes(i + 1) != QByteArrayView(previous[static_cast<size_t>(i)]);
            if (differs) {
                ++currentRank;
                for (int i = 0; i < keys.size(); ++i)
                    previous[static_cast<size_t>(i)] = row.bytes(i + 1).toByteArray();
            }
            const qint64 group = groups.group();
            if (group >= 0 && group <= maximum)
                ranks[static_cast<size_t>(group)] = currentRank;
        }
    }
    if (currentRank < 0)
        return ids;

    const qint64 maxIdentifier = context.connection.integer(QStringLiteral("SELECT COALESCE(MAX(id), 0) FROM events"));
    const bool compact = maxIdentifier < (qint64{1} << 32);
    EventReader reader(context, clause, admitted, QStringLiteral("id"), QStringLiteral("id %1").arg(direction));
    std::vector<quint64> keys;
    std::vector<std::pair<qint32, qint64>> pairs;
    if (compact)
        keys.reserve(ids->capacity());
    else
        pairs.reserve(ids->capacity());
    while (reader.next()) {
        const qint64 group = reader.group();
        const qint32 rank = group >= 0 && group <= maximum ? ranks[static_cast<size_t>(group)] : -1;
        if (rank < 0)
            continue;
        const qint64 id = reader.row().integer(1);
        if (compact)
            keys.push_back((static_cast<quint64>(rank) << 32)
                           | (ascending ? static_cast<quint64>(id) : ~static_cast<quint64>(id) & 0xffffffffULL));
        else
            pairs.emplace_back(rank, id);
    }
    context.throwIfCancelled();
    if (compact) {
        std::sort(keys.begin(), keys.end());
        for (const quint64 key : keys)
            ids->push_back(static_cast<qint64>(ascending ? key & 0xffffffffULL : ~key & 0xffffffffULL));
    } else {
        std::stable_sort(pairs.begin(), pairs.end(),
                         [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [rank, id] : pairs)
            ids->push_back(id);
    }
    return ids;
}

QList<TableRow> rows(sql::Connection& connection, std::span<const qint64> ids)
{
    QList<TableRow> result;
    if (ids.empty())
        return result;
    QString list;
    list.reserve(static_cast<qsizetype>(ids.size()) * 8);
    for (const qint64 id : ids) {
        if (!list.isEmpty())
            list += QLatin1Char(',');
        list += QString::number(id);
    }
    QHash<qint64, TableRow> byId;
    byId.reserve(static_cast<qsizetype>(ids.size()));
    sql::Statement statement =
        connection.prepare(QStringLiteral("SELECT %1 FROM event_view WHERE id IN (%2)").arg(TABLE_COLUMNS, list));
    while (statement.step()) {
        TableRow row = readLine(statement);
        byId.insert(row.id, std::move(row));
    }
    result.reserve(static_cast<qsizetype>(ids.size()));
    for (const qint64 id : ids) {
        auto found = byId.find(id);
        result.append(found != byId.end() ? std::move(found.value()) : TableRow{id});
    }
    return result;
}

Uniformity uniformity(sql::Connection& connection, const Clause& clause)
{
    Uniformity result;
    static const QString TARGET = QStringLiteral("CASE WHEN file_path <> '' THEN file_path ELSE detail END");
    sql::Statement statement = connection.prepare(
        QStringLiteral("SELECT COUNT(*), MIN(level), MAX(level), MIN(audit), MAX(audit), MIN(user), MAX(user), "
                       "MIN(machine), MAX(machine), MIN(process), MAX(process), MIN(source_ip), MAX(source_ip), "
                       "MIN(%1), MAX(%1), MIN(channel), MAX(channel) FROM groups%2")
            .arg(TARGET, clause.whereGroups()));
    statement.bindAll(clause.groupParameters);
    if (!statement.step() || statement.integer(0) == 0)
        return result;
    TableRow& sample = result.sample;
    sample.level = static_cast<int>(statement.integer(1));
    sample.audit = static_cast<int>(statement.integer(3));
    if (statement.integer(1) == statement.integer(2) && statement.integer(3) == statement.integer(4))
        result.identicalBits |= Uniformity::bit(Column::Level);
    const std::array<std::pair<Column, QString*>, 6> texts{{{Column::User, &sample.user},
                                                            {Column::Machine, &sample.machine},
                                                            {Column::Process, &sample.process},
                                                            {Column::Ip, &sample.sourceIp},
                                                            {Column::Target, &sample.file},
                                                            {Column::Channel, &sample.channel}}};
    int index = 5;
    for (const auto& [column, value] : texts) {
        *value = statement.text(index);
        const QString maximum = statement.text(index + 1);
        if (maximum.isEmpty())
            result.emptyBits |= Uniformity::bit(column);
        else if (*value == maximum)
            result.identicalBits |= Uniformity::bit(column);
        index += 2;
    }
    return result;
}

std::optional<EventDetail> detail(sql::Connection& connection, qint64 id)
{
    sql::Statement statement = connection.prepare(
        QStringLiteral("SELECT %1, ts_utc, dir_path, extension, access, record_id, "
                       "(SELECT s.name FROM sources s WHERE s.id = event_view.source_id), "
                       "(SELECT s.machine FROM sources s WHERE s.id = event_view.source_id), "
                       "destination_ip, block, rank FROM event_view WHERE id = ?")
            .arg(TABLE_COLUMNS));
    statement.bind(1, id);
    if (!statement.step())
        return std::nullopt;
    EventDetail detail;
    detail.row = readLine(statement);
    detail.tsUtcKnown = !statement.isNull(17);
    detail.tsUtc = statement.integer(17);
    detail.folder = statement.text(18);
    detail.extension = statement.text(19);
    detail.access = statement.text(20);
    detail.recordId = statement.integer(21);
    Source source;
    source.name = statement.text(22);
    source.machine = statement.text(23);
    detail.source = source.name.isEmpty() ? QString() : source.label();
    detail.destinationIp = statement.text(24);
    if (const auto event = blocks::event(connection, statement.integer(25), statement.integer(26), false)) {
        detail.xmlAvailable = true;
        for (const evtx::DataField& field : event->data)
            detail.data.append({field.name, field.value});
    }

    sql::Statement alerts = connection.prepare(
        QStringLiteral("SELECT r.id, r.title, r.level, r.tags FROM matches c "
                       "JOIN rules r ON r.number = c.rule WHERE c.event = ? ORDER BY r.level DESC, r.title"));
    alerts.bind(1, id);
    while (alerts.step())
        detail.alerts.append({alerts.text(0), alerts.text(1), static_cast<int>(alerts.integer(2)),
                              alerts.text(3).split(QLatin1Char(' '), Qt::SkipEmptyParts)});
    return detail;
}

QString xml(sql::Connection& connection, qint64 id)
{
    sql::Statement statement = connection.prepare(QStringLiteral("SELECT block, rank FROM events WHERE id = ?"));
    statement.bind(1, id);
    if (!statement.step())
        return {};
    const auto event = blocks::event(connection, statement.integer(0), statement.integer(1), true);
    return event ? event->xml : QString();
}

// ── Grouping ─────────────────────────────────────────────────────────────────

Grouping groups(Context& context, const Filters& filters, int limit)
{
    Grouping result;
    const std::shared_ptr<const GroupCounts> counts = context.counts(filters.clause());
    std::vector<std::pair<qint64, qint64>> kept;
    for (size_t group = 0; group < counts->count.size(); ++group) {
        const qint64 count = counts->count[group];
        if (count <= 0)
            continue;
        ++result.groupCount;
        result.events += count;
        kept.emplace_back(count, static_cast<qint64>(group));
    }
    context.throwIfCancelled();
    const auto keptCount = static_cast<size_t>(std::min<qsizetype>(limit, static_cast<qsizetype>(kept.size())));
    std::partial_sort(kept.begin(), kept.begin() + static_cast<std::ptrdiff_t>(keptCount), kept.end(),
                      [](const auto& a, const auto& b) { return a.first != b.first ? a.first > b.first : a.second < b.second; });
    kept.resize(keptCount);

    TimestampFormatter timestamp(context.connection);
    sql::Statement attributes = context.connection.prepare(
        QStringLiteral("SELECT event_id, channel, user, machine, process, "
                       "CASE WHEN file_path <> '' THEN file_path ELSE detail END, action, provider "
                       "FROM groups WHERE group_id = ?"));
    result.groups.reserve(static_cast<qsizetype>(keptCount));
    for (const auto& [count, identifier] : kept) {
        attributes.reset();
        attributes.bind(1, identifier);
        if (!attributes.step())
            continue;
        Group group;
        group.identifier = identifier;
        group.total = count;
        group.first = timestamp(counts->first[static_cast<size_t>(identifier)]);
        group.last = timestamp(counts->last[static_cast<size_t>(identifier)]);
        group.eventId = static_cast<quint32>(attributes.integer(0));
        group.channel = attributes.text(1);
        group.user = attributes.text(2);
        group.machine = attributes.text(3);
        group.process = attributes.text(4);
        group.target = attributes.text(5);
        group.action = attributes.text(6);
        group.provider = attributes.text(7);
        result.groups.append(std::move(group));
    }
    return result;
}

// ── Timeline ─────────────────────────────────────────────────────────────────

Histogram histogram(Context& context, const Filters& filters, int maxBars, const QByteArray& timeZone)
{
    Histogram result;
    const Clause clause = filters.clause();
    const std::shared_ptr<const GroupCounts> counts = context.counts(clause);
    if (!counts->date())
        return result;
    qint64 minimum = counts->firstTs;
    qint64 maximum = counts->lastTs;
    if (filters.hasExactPeriod()) {
        minimum = filters.startUtc;
        maximum = filters.endUtc - 1;
    }
    const qint64 step = histogramStep(std::max<qint64>(1, maximum - minimum + 1), std::max(10, maxBars));
    TimeZoneConverter converter(timeZone);
    const qint64 shift = static_cast<qint64>(converter.offsetSeconds(minimum)) * 1'000'000;
    qint64 start = step >= HOUR ? ((minimum + shift) / step) * step - shift : (minimum / step) * step;
    if (start > minimum)
        start -= step;
    const auto count = static_cast<size_t>((maximum - start) / step + 1);
    result.start = start;
    result.step = step;
    result.values.assign(count, 0);
    result.alerts.assign(count, 0);
    auto add = [&](std::vector<qint64>& target, qint64 ts, qint64 value) {
        const qint64 bin = (ts - start) / step;
        if (bin >= 0 && static_cast<size_t>(bin) < count)
            target[static_cast<size_t>(bin)] += value;
    };

    const std::shared_ptr<const AdmittedGroups> admitted = context.admitted(clause);
    if (step >= HOUR) {
        for (auto it = counts->byQuarter.cbegin(); it != counts->byQuarter.cend(); ++it)
            add(result.values, it.key() * QUARTER_HOUR, it.value());
    } else {
        EventReader reader(context, clause.andWhere(QStringLiteral("ts_utc IS NOT NULL")), admitted,
                           QStringLiteral("ts_utc"));
        while (reader.next())
            add(result.values, reader.row().integer(1), 1);
    }

    QString sql = QStringLiteral("SELECT e.group_id, e.ts_utc FROM (SELECT DISTINCT event FROM matches) c "
                                 "CROSS JOIN events e ON e.id = c.event WHERE e.ts_utc IS NOT NULL");
    if (clause.eventBased())
        sql += QStringLiteral(" AND (%1)").arg(clause.events);
    sql::Statement alerts = context.connection.prepare(sql);
    alerts.bindAll(clause.eventParameters);
    qint64 readCount = 0;
    while (alerts.step()) {
        if ((++readCount & 0xffff) == 0)
            context.throwIfCancelled();
        if (admitted->admitted(alerts.integer(0)))
            add(result.alerts, alerts.integer(1), 1);
    }
    return result;
}

} // namespace lh::queries
