#include "Alerts.h"

#include "Actions.h"
#include "Catalog.h"
#include "Format.h"
#include "Queries.h"
#include "DateTime.h"

#include <QHash>
#include <QJsonArray>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

namespace lh {

namespace {

constexpr qint64 MAX_TS = std::numeric_limits<qint64>::max();

QJsonArray toJsonArray(const QStringList& list)
{
    QJsonArray array;
    for (const QString& value : list)
        array.append(value);
    return array;
}

QStringList fromJsonArray(const QJsonValue& value, const QStringList& fallback)
{
    if (!value.isArray())
        return fallback;
    QStringList list;
    for (const QJsonValue& element : value.toArray()) {
        QString extension = element.toString().trimmed().toLower();
        if (extension.isEmpty())
            continue;
        if (!extension.startsWith(QLatin1Char('.')))
            extension.prepend(QLatin1Char('.'));
        list << extension;
    }
    return list;
}

Filters explorationOf(const Filters& scope)
{
    Filters filters = scope;
    filters.exact = true;
    return filters;
}

struct Instant
{
    qint64 ts = UNKNOWN_TIMESTAMP;
    qint64 id = 0;
};

bool mostRecent(const Instant& a, const Instant& b) noexcept
{
    return a.ts != b.ts ? a.ts > b.ts : a.id > b.id;
}

class LatestInstants
{
public:
    void add(qint64 ts, qint64 id)
    {
        const Instant instant{ts, id};
        if (static_cast<qsizetype>(m_heap.size()) < Detection::MAX_ROWS) {
            m_heap.push_back(instant);
            std::push_heap(m_heap.begin(), m_heap.end(), mostRecent);
            return;
        }
        if (!mostRecent(instant, m_heap.front()))
            return;
        std::pop_heap(m_heap.begin(), m_heap.end(), mostRecent);
        m_heap.back() = instant;
        std::push_heap(m_heap.begin(), m_heap.end(), mostRecent);
    }

    [[nodiscard]] std::vector<Instant> sorted() const
    {
        std::vector<Instant> result = m_heap;
        std::sort(result.begin(), result.end(), mostRecent);
        return result;
    }

private:
    std::vector<Instant> m_heap;
};

QHash<qint64, QVariantList> readLines(sql::Connection& connection, const QString& columns,
                                      const std::vector<Instant>& instants)
{
    QHash<qint64, QVariantList> result;
    if (instants.empty())
        return result;
    QString list;
    list.reserve(static_cast<qsizetype>(instants.size()) * 8);
    for (const Instant& instant : instants) {
        if (!list.isEmpty())
            list += QLatin1Char(',');
        list += QString::number(instant.id);
    }
    sql::Statement statement =
        connection.prepare(QStringLiteral("SELECT id, %1 FROM event_view WHERE id IN (%2)").arg(columns, list));
    const int readColumns = statement.columnCount();
    while (statement.step()) {
        QVariantList values;
        values.reserve(readColumns - 1);
        for (int i = 1; i < readColumns; ++i)
            values.append(statement.value(i));
        result.insert(statement.integer(0), std::move(values));
    }
    return result;
}

bool isWeekend(qint64 day) noexcept
{
    const qint64 rest = (day % 7 + 11) % 7;
    return rest == 0 || rest == 6;
}

QString isoDate(qint64 day, bool known)
{
    return known ? dateFromDay(day).toString(Qt::ISODate) : QString();
}

// ── Built-in file checks ────────────────────────────────────────────────────

struct DayActivity
{
    qint64 count = 0;
    qint64 files = 0;
    qint64 writes = 0;
    qint64 deletions = 0;
    qint64 first = MAX_TS;
    qint64 last = UNKNOWN_TIMESTAMP;
    qint32 user = -1;
    qint64 day = 0;
    bool dayKnown = false;
};

struct FileAttribute
{
    quint8 category = 255;
    bool ransomware = false;
    bool executable = false;
    qint32 user = -1;
    quint64 file = 0;
};

struct FileChecks
{
    std::vector<QString> names;
    Aggregate<DayActivity> deletions;
    FingerprintSet deletedFiles;
    Aggregate<DayActivity> offHours;
    qint64 permissions = 0;
    LatestInstants latestPermissions;
    qint64 ransomwareFiles = 0;
    LatestInstants latestRansomware;
    qint64 executables = 0;
    LatestInstants latestExecutables;
    std::vector<std::vector<qint64>> instants;
};

Detection deletionsTemplate(const AlertParameters& p)
{
    Detection d;
    d.identifier = QStringLiteral("mass_deletions");
    d.tags = {QStringLiteral("attack.impact"), QStringLiteral("attack.t1485")};
    d.title = qtTrId("alerts.deletions_title");
    d.description = qtTrId("alerts.deletions_description").arg(p.deletionThreshold);
    d.severity = Severity::High;
    d.unit = QT_TRID_N_NOOP("alerts.unit_days");
    d.columns = {{qtTrId("common.user"), ResultColumn::Type::Text},
                 {qtTrId("alerts.column_date"), ResultColumn::Type::Date},
                 {qtTrId("common.deletions"), ResultColumn::Type::Number},
                 {qtTrId("common.files"), ResultColumn::Type::Number},
                 {qtTrId("alerts.column_first"), ResultColumn::Type::Timestamp},
                 {qtTrId("alerts.column_last"), ResultColumn::Type::Timestamp}};
    return d;
}

Detection offHoursTemplate(const AlertParameters& p)
{
    Detection d;
    d.identifier = QStringLiteral("off_hours");
    d.tags = {QStringLiteral("attack.initial-access"), QStringLiteral("attack.t1078")};
    d.title = qtTrId("alerts.out_of_hours_title");
    d.description = (p.weekendOffHours ? qtTrId("alerts.out_of_hours_description_weekend")
                                       : qtTrId("alerts.out_of_hours_description"))
                        .arg(p.openingHour)
                        .arg(p.closingHour);
    d.severity = Severity::Medium;
    d.unit = QT_TRID_N_NOOP("alerts.unit_events");
    d.columns = {{qtTrId("common.user"), ResultColumn::Type::Text},
                 {qtTrId("alerts.column_date"), ResultColumn::Type::Date},
                 {qtTrId("common.events"), ResultColumn::Type::Number},
                 {qtTrId("common.writes"), ResultColumn::Type::Number},
                 {qtTrId("common.deletions"), ResultColumn::Type::Number},
                 {qtTrId("alerts.column_first"), ResultColumn::Type::Timestamp},
                 {qtTrId("alerts.column_last"), ResultColumn::Type::Timestamp}};
    return d;
}

Detection permissionsTemplate()
{
    Detection d;
    d.identifier = QStringLiteral("permissions");
    d.tags = {QStringLiteral("attack.defense-evasion"), QStringLiteral("attack.t1222.001")};
    d.title = qtTrId("alerts.permissions_title");
    d.description = qtTrId("alerts.permissions_description");
    d.severity = Severity::Medium;
    d.unit = QT_TRID_N_NOOP("alerts.unit_events");
    d.columns = {{qtTrId("common.date_time"), ResultColumn::Type::Timestamp},
                 {qtTrId("common.user"), ResultColumn::Type::Text},
                 {qtTrId("common.file"), ResultColumn::Type::Path},
                 {qtTrId("common.program"), ResultColumn::Type::Path},
                 {qtTrId("common.requested_rights"), ResultColumn::Type::Text}};
    return d;
}

Detection extensionsTemplate(bool ransomware)
{
    Detection d;
    if (ransomware) {
        d.identifier = QStringLiteral("ransomware");
        d.tags = {QStringLiteral("attack.impact"), QStringLiteral("attack.t1486")};
        d.title = qtTrId("alerts.ransomware_title");
        d.description = qtTrId("alerts.ransomware_description");
        d.severity = Severity::Critical;
    } else {
        d.identifier = QStringLiteral("executables");
        d.tags = {QStringLiteral("attack.persistence"), QStringLiteral("attack.t1554")};
        d.title = qtTrId("alerts.executables_title");
        d.description = qtTrId("alerts.executables_description");
        d.severity = Severity::Medium;
    }
    d.unit = QT_TRID_N_NOOP("alerts.unit_events");
    d.columns = {{qtTrId("common.date_time"), ResultColumn::Type::Timestamp},
                 {qtTrId("common.user"), ResultColumn::Type::Text},
                 {qtTrId("common.action"), ResultColumn::Type::Category},
                 {qtTrId("common.file"), ResultColumn::Type::Path},
                 {qtTrId("common.program"), ResultColumn::Type::Path}};
    return d;
}

Detection burstsTemplate(const AlertParameters& p)
{
    Detection d;
    d.identifier = QStringLiteral("bursts");
    d.tags = {QStringLiteral("attack.collection"), QStringLiteral("attack.t1119")};
    d.title = qtTrId("alerts.bursts_title");
    d.description = qtTrId("alerts.bursts_description").arg(p.burstActions).arg(p.burstSeconds);
    d.severity = Severity::High;
    d.unit = QT_TRID_N_NOOP("alerts.unit_bursts");
    d.columns = {{qtTrId("common.user"), ResultColumn::Type::Text},
                 {qtTrId("alerts.column_start"), ResultColumn::Type::Timestamp},
                 {qtTrId("alerts.column_end"), ResultColumn::Type::Timestamp},
                 {qtTrId("alerts.column_duration"), ResultColumn::Type::Duration},
                 {qtTrId("alerts.column_actions"), ResultColumn::Type::Number}};
    return d;
}

FileChecks scanFiles(Context& context, const AlertParameters& p, const Clause& base)
{
    FileChecks result;
    const Clause clause = base.andGroupWhere(QStringLiteral("action_cat <> ''"));
    const std::shared_ptr<const AdmittedGroups> admitted = context.admitted(clause);
    const qint64 maximum = context.connection.integer(QStringLiteral("SELECT COALESCE(MAX(group_id), 0) FROM groups"));
    std::vector<FileAttribute> attributes(static_cast<size_t>(maximum) + 1);
    const int deletion = categoryIndex(QByteArrayView(category::Deletion));
    const int modification = categoryIndex(QByteArrayView(category::Modification));
    const int permissions = categoryIndex(QByteArrayView(category::Permissions));

    QSet<QByteArray> ransomwareFiles, executables;
    for (const QString& extension : p.ransomwareExtensions)
        ransomwareFiles.insert(extension.toUtf8());
    for (const QString& extension : p.executableExtensions)
        executables.insert(extension.toUtf8());
    FingerprintTable users;
    GroupReader groups(context, clause, QStringLiteral("action_cat, user, extension, file_path"));
    while (groups.next()) {
        const sql::Statement& row = groups.row();
        const qint64 group = groups.group();
        const int category = categoryIndex(row.bytes(1));
        if (group < 0 || group > maximum || category < 0)
            continue;
        FileAttribute& attribute = attributes[static_cast<size_t>(group)];
        attribute.category = static_cast<quint8>(category);
        const QByteArrayView user = row.bytes(2);
        bool isNew = false;
        attribute.user = static_cast<qint32>(users.findOrAdd(fingerprint(user), isNew));
        if (isNew)
            result.names.push_back(QString::fromUtf8(user));
        const QByteArrayView extension = row.bytes(3);
        if (!extension.isEmpty()) {
            const QByteArray key = extension.toByteArray();
            attribute.ransomware = ransomwareFiles.contains(key);
            attribute.executable = executables.contains(key) && (category == modification || category == deletion);
        }
        attribute.file = fingerprint(row.bytes(4));
    }
    result.instants.resize(result.names.size());

    const int opening = qBound(0, p.openingHour, 24);
    const int closing = qBound(0, p.closingHour, 24);
    auto dayActivity = [](DayActivity& j, const FileAttribute& a, qint64 day, bool dayKnown) {
        if (j.user < 0) {
            j.user = a.user;
            j.day = day;
            j.dayKnown = dayKnown;
        }
    };
    EventReader reader(context, clause, admitted, QStringLiteral("ts_utc, day, hour, id"));
    while (reader.next()) {
        const sql::Statement& row = reader.row();
        const qint64 group = reader.group();
        if (group < 0 || group > maximum)
            continue;
        const FileAttribute& a = attributes[static_cast<size_t>(group)];
        if (a.category == 255)
            continue;
        const bool tsKnown = !row.isNull(1);
        const qint64 ts = tsKnown ? row.integer(1) : UNKNOWN_TIMESTAMP;
        const bool dayKnown = !row.isNull(2);
        const qint64 day = dayKnown ? row.integer(2) : 0;
        const qint64 hour = row.integer(3);
        const qint64 id = row.integer(4);
        const quint64 dayKey = (static_cast<quint64>(static_cast<quint32>(a.user)) << 32)
                               | (dayKnown ? static_cast<quint32>(day) ^ 0x80000000U : 0xffffffffU);

        if (a.category == deletion) {
            DayActivity& j = result.deletions.findOrAdd(dayKey);
            dayActivity(j, a, day, dayKnown);
            ++j.count;
            if (result.deletedFiles.insert(mixFingerprint(dayKey, a.file)))
                ++j.files;
            if (tsKnown) {
                j.first = std::min(j.first, ts);
                j.last = std::max(j.last, ts);
            }
        }
        if (hour >= 0
            && (hour < opening || hour >= closing || (p.weekendOffHours && dayKnown && isWeekend(day)))) {
            DayActivity& j = result.offHours.findOrAdd(dayKey);
            dayActivity(j, a, day, dayKnown);
            ++j.count;
            j.writes += a.category == modification ? 1 : 0;
            j.deletions += a.category == deletion ? 1 : 0;
            if (tsKnown) {
                j.first = std::min(j.first, ts);
                j.last = std::max(j.last, ts);
            }
        }
        if (a.category == permissions) {
            ++result.permissions;
            result.latestPermissions.add(ts, id);
        }
        if (a.ransomware) {
            ++result.ransomwareFiles;
            result.latestRansomware.add(ts, id);
        }
        if (a.executable) {
            ++result.executables;
            result.latestExecutables.add(ts, id);
        }
        if (tsKnown)
            result.instants[static_cast<size_t>(a.user)].push_back(ts);
    }
    context.throwIfCancelled();
    return result;
}

std::vector<const DayActivity*> sortedDays(const Aggregate<DayActivity>& aggregate, const std::vector<QString>& names,
                                           qint64 minimum)
{
    std::vector<const DayActivity*> order;
    for (const DayActivity& j : aggregate.values())
        if (j.count >= minimum)
            order.push_back(&j);
    std::sort(order.begin(), order.end(), [&names](const DayActivity* a, const DayActivity* b) {
        if (a->count != b->count)
            return a->count > b->count;
        const int name = names[static_cast<size_t>(a->user)].compare(names[static_cast<size_t>(b->user)]);
        return name != 0 ? name < 0 : a->day < b->day;
    });
    return order;
}

Detection massDeletions(Context& context, const Filters& filters, const AlertParameters& p,
                        const FileChecks& checks)
{
    Detection d = deletionsTemplate(p);
    TimestampFormatter timestamp(context.connection);
    for (const DayActivity* j : sortedDays(checks.deletions, checks.names, p.deletionThreshold)) {
        ++d.count;
        if (d.rows.size() >= Detection::MAX_ROWS)
            continue;
        const QString user = checks.names[static_cast<size_t>(j->user)];
        const QDate date = j->dayKnown ? dateFromDay(j->day) : QDate();
        d.rows.append({user, isoDate(j->day, j->dayKnown), j->count, j->files, timestamp(j->first),
                       timestamp(j->last)});
        Filters exploration = explorationOf(filters);
        exploration.user = user;
        exploration.category = QLatin1String(category::Deletion);
        exploration.since = exploration.until = date;
        d.explorations.append(exploration);
    }
    if (d.count >= 5 || (!d.rows.isEmpty() && d.rows.first()[2].toLongLong() >= 10LL * p.deletionThreshold))
        d.severity = Severity::Critical;
    return d;
}

Detection offHours(Context& context, const Filters& filters, const AlertParameters& p,
                   const FileChecks& checks)
{
    Detection d = offHoursTemplate(p);
    TimestampFormatter timestamp(context.connection);
    for (const DayActivity* j : sortedDays(checks.offHours, checks.names, 1)) {
        d.count += j->count;
        if (d.rows.size() >= Detection::MAX_ROWS)
            continue;
        const QString user = checks.names[static_cast<size_t>(j->user)];
        const QDate date = j->dayKnown ? dateFromDay(j->day) : QDate();
        d.rows.append({user, isoDate(j->day, j->dayKnown), j->count, j->writes, j->deletions,
                       timestamp(j->first), timestamp(j->last)});
        Filters exploration = explorationOf(filters);
        exploration.user = user;
        exploration.since = exploration.until = date;
        const bool weekend = p.weekendOffHours && j->dayKnown && isWeekend(j->day);
        if (!weekend) {
            exploration.startHour = p.closingHour;
            exploration.endHour = p.openingHour;
        }
        d.explorations.append(exploration);
    }
    return d;
}

Detection permissionChanges(Context& context, const Filters& filters, const FileChecks& checks)
{
    Detection d = permissionsTemplate();
    d.count = checks.permissions;
    if (d.count == 0)
        return d;
    const std::vector<Instant> instants = checks.latestPermissions.sorted();
    const QHash<qint64, QVariantList> rows =
        readLines(context.connection, QStringLiteral("timestamp, user, file_path, process, access"), instants);
    for (const Instant& instant : instants) {
        const auto found = rows.constFind(instant.id);
        if (found == rows.cend())
            continue;
        d.rows.append(*found);
        Filters exploration = explorationOf(filters);
        exploration.file = found->value(2).toString();
        exploration.category = QLatin1String(category::Permissions);
        d.explorations.append(exploration);
    }
    return d;
}

Detection extensions(Context& context, const Filters& filters, bool ransomware, const QStringList& list,
                     const FileChecks& checks)
{
    Detection d = extensionsTemplate(ransomware);
    if (list.isEmpty())
        return d;
    d.count = ransomware ? checks.ransomwareFiles : checks.executables;
    if (d.count == 0)
        return d;
    const std::vector<Instant> instants =
        (ransomware ? checks.latestRansomware : checks.latestExecutables).sorted();
    const QHash<qint64, QVariantList> rows =
        readLines(context.connection, QStringLiteral("timestamp, user, action_cat, file_path, process"), instants);
    for (const Instant& instant : instants) {
        const auto found = rows.constFind(instant.id);
        if (found == rows.cend())
            continue;
        d.rows.append(*found);
        Filters exploration = explorationOf(filters);
        exploration.file = found->value(3).toString();
        d.explorations.append(exploration);
    }
    return d;
}

Detection bursts(Context& context, const Filters& filters, const AlertParameters& p,
                 FileChecks& checks)
{
    Detection d = burstsTemplate(p);
    const auto shift = static_cast<size_t>(std::max(1, p.burstActions - 1));
    const qint64 window = static_cast<qint64>(std::max(1, p.burstSeconds)) * 1'000'000;
    const std::vector<QString>& names = checks.names;

    struct Burst
    {
        QString user;
        qint64 start = 0, end = 0, startRank = 0, endRank = 0;
    };
    QList<Burst> list;
    std::vector<size_t> order(names.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&names](size_t a, size_t b) { return names[a] < names[b]; });
    for (const size_t user : order) {
        std::vector<qint64>& times = checks.instants[user];
        std::sort(times.begin(), times.end());
        Burst current;
        bool isRunning = false;
        for (size_t i = shift; i < times.size(); ++i) {
            const qint64 start = times[i - shift];
            const qint64 end = times[i];
            if (end - start > window)
                continue;
            const auto rank = static_cast<qint64>(i) + 1;
            if (isRunning && start <= current.end) {
                current.end = end;
                current.endRank = rank;
                continue;
            }
            if (isRunning)
                list.append(current);
            current = {names[user], start, end, rank - static_cast<qint64>(shift), rank};
            isRunning = true;
        }
        if (isRunning)
            list.append(current);
        times = {};
        context.throwIfCancelled();
    }

    std::stable_sort(list.begin(), list.end(), [](const Burst& a, const Burst& b) {
        return (a.endRank - a.startRank) > (b.endRank - b.startRank);
    });
    d.count = list.size();
    TimestampFormatter timestamp(context.connection);
    for (const Burst& r : list) {
        if (d.rows.size() >= Detection::MAX_ROWS)
            break;
        const QString textStart = timestamp(r.start);
        const QString textEnd = timestamp(r.end);
        d.rows.append({r.user, textStart, textEnd, static_cast<double>(r.end - r.start) / 1e6,
                       r.endRank - r.startRank + 1});
        Filters exploration = explorationOf(filters);
        exploration.user = r.user;
        exploration.since = QDate::fromString(textStart.left(10), Qt::ISODate);
        exploration.until = QDate::fromString(textEnd.left(10), Qt::ISODate);
        d.explorations.append(exploration);
    }
    return d;
}

// ── Sigma rules ──────────────────────────────────────────────────────────────

QList<Detection> sigmaDetections(Context& context, const Filters& filters)
{
    QList<Detection> detections;
    const Clause clause = filters.clause();
    const std::shared_ptr<const AdmittedGroups> admitted = context.admitted(clause);
    struct Rule
    {
        qint64 count = 0;
        LatestInstants latest;
    };
    QHash<qint64, Rule> byRule;
    {
        QString sql = QStringLiteral("SELECT c.rule, c.event, e.group_id, e.ts_utc FROM matches c "
                                     "CROSS JOIN events e ON e.id = c.event");
        if (clause.eventBased())
            sql += QStringLiteral(" WHERE (%1)").arg(clause.events);
        sql::Statement statement = context.connection.prepare(sql);
        statement.bindAll(clause.eventParameters);
        qint64 readCount = 0;
        while (statement.step()) {
            if ((++readCount & 0xffff) == 0)
                context.throwIfCancelled();
            if (!admitted->admitted(statement.integer(2)))
                continue;
            Rule& rule = byRule[statement.integer(0)];
            ++rule.count;
            rule.latest.add(statement.isNull(3) ? UNKNOWN_TIMESTAMP : statement.integer(3), statement.integer(1));
        }
    }
    if (byRule.isEmpty())
        return detections;

    QList<qint64> numbers;
    sql::Statement rules = context.connection.prepare(
        QStringLiteral("SELECT number, id, title, description, level, tags, author, links FROM rules"));
    while (rules.step()) {
        const auto found = byRule.constFind(rules.integer(0));
        if (found == byRule.cend())
            continue;
        const QString id = rules.text(1);
        Detection d;
        d.identifier = QStringLiteral("sigma:") + id;
        d.sigma = true;
        d.rule = id;
        d.title = rules.text(2);
        d.description = rules.text(3);
        d.severity = severityFromLevel(static_cast<int>(rules.integer(4)));
        d.tags = rules.text(5).split(QLatin1Char(' '), Qt::SkipEmptyParts);
        d.author = rules.text(6);
        d.links = rules.text(7).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        d.count = found->count;
        numbers << found.key();
        d.unit = QT_TRID_N_NOOP("alerts.unit_events");
        d.columns = {{qtTrId("common.date_time"), ResultColumn::Type::Timestamp},
                     {qtTrId("common.computer"), ResultColumn::Type::Text},
                     {qtTrId("common.user"), ResultColumn::Type::Text},
                     {qtTrId("common.event"), ResultColumn::Type::Text},
                     {qtTrId("common.program"), ResultColumn::Type::Path},
                     {qtTrId("common.detail"), ResultColumn::Type::Path}};
        detections.append(std::move(d));
    }
    for (qsizetype i = 0; i < detections.size(); ++i) {
        context.throwIfCancelled();
        Detection& d = detections[i];
        const std::vector<Instant> instants = byRule.value(numbers[i]).latest.sorted();
        const QHash<qint64, QVariantList> rows = readLines(
            context.connection,
            QStringLiteral("timestamp, machine, user, event_id, channel, process, "
                           "CASE WHEN file_path <> '' THEN file_path ELSE detail END, ts_utc"),
            instants);
        for (const Instant& instant : instants) {
            const auto found = rows.constFind(instant.id);
            if (found == rows.cend())
                continue;
            const QVariantList& v = *found;
            d.rows.append({v.value(0), v.value(1), v.value(2),
                           eventLabel(static_cast<quint32>(v.value(3).toLongLong()), v.value(4).toString()),
                           v.value(5), v.value(6)});
            Filters exploration = d.allOccurrences(filters);
            if (!v.value(7).isNull()) {
                exploration.startUtc = v.value(7).toLongLong();
                exploration.endUtc = exploration.startUtc + 1;
            }
            d.explorations.append(exploration);
        }
    }
    return detections;
}

} // namespace

AlertParameters::AlertParameters()
    : executableExtensions(lh::executableExtensions()), ransomwareExtensions(lh::ransomwareExtensions())
{
}

QJsonObject AlertParameters::toJson() const
{
    return {
        {QStringLiteral("deletion_threshold"), deletionThreshold},
        {QStringLiteral("opening_hour"), openingHour},
        {QStringLiteral("closing_hour"), closingHour},
        {QStringLiteral("weekend_off_hours"), weekendOffHours},
        {QStringLiteral("burst_actions"), burstActions},
        {QStringLiteral("burst_seconds"), burstSeconds},
        {QStringLiteral("ignore_system_accounts"), ignoreSystemAccounts},
        {QStringLiteral("executable_extensions"), toJsonArray(executableExtensions)},
        {QStringLiteral("ransomware_extensions"), toJsonArray(ransomwareExtensions)},
        {QStringLiteral("disabled_rules"), toJsonArray(disabledRules)},
        {QStringLiteral("minimum_rule_level"), minimumRuleLevel},
        {QStringLiteral("rule_analysis"), ruleAnalysis},
        {QStringLiteral("file_analysis"), fileAnalysis},
        {QStringLiteral("mitre"), mitre},
    };
}

sigma::Configuration AlertParameters::ruleConfiguration() const
{
    sigma::Configuration configuration;
    configuration.enabled = ruleAnalysis;
    if (ruleAnalysis) {
        configuration.folder = sigma::userFolder();
        configuration.disabled = disabledRules;
    }
    configuration.minimumLevel = minimumRuleLevel;
    return configuration;
}

AlertParameters AlertParameters::fromJson(const QJsonObject& object)
{
    AlertParameters p;
    p.deletionThreshold = qBound(2, object.value(QStringLiteral("deletion_threshold")).toInt(p.deletionThreshold), 100'000);
    p.openingHour = qBound(0, object.value(QStringLiteral("opening_hour")).toInt(p.openingHour), 23);
    p.closingHour = qBound(1, object.value(QStringLiteral("closing_hour")).toInt(p.closingHour), 24);
    p.weekendOffHours = object.value(QStringLiteral("weekend_off_hours")).toBool(p.weekendOffHours);
    p.burstActions = qBound(5, object.value(QStringLiteral("burst_actions")).toInt(p.burstActions), 1'000'000);
    p.burstSeconds = qBound(1, object.value(QStringLiteral("burst_seconds")).toInt(p.burstSeconds), 86'400);
    p.ignoreSystemAccounts = object.value(QStringLiteral("ignore_system_accounts")).toBool(p.ignoreSystemAccounts);
    p.executableExtensions = fromJsonArray(object.value(QStringLiteral("executable_extensions")), p.executableExtensions);
    p.ransomwareExtensions =
        fromJsonArray(object.value(QStringLiteral("ransomware_extensions")), p.ransomwareExtensions);
    for (const QJsonValue& rule : object.value(QStringLiteral("disabled_rules")).toArray())
        if (!rule.toString().isEmpty())
            p.disabledRules << rule.toString();
    p.minimumRuleLevel = qBound(0, object.value(QStringLiteral("minimum_rule_level")).toInt(p.minimumRuleLevel),
                                static_cast<int>(sigma::Level::Critical));
    p.ruleAnalysis = object.value(QStringLiteral("rule_analysis")).toBool(p.ruleAnalysis);
    p.fileAnalysis = object.value(QStringLiteral("file_analysis")).toBool(p.fileAnalysis);
    p.mitre = object.value(QStringLiteral("mitre")).toBool(p.mitre);
    return p;
}

QString formatCell(const QVariant& value, ResultColumn::Type type)
{
    switch (type) {
    case ResultColumn::Type::Number:
        return formatNumber(value.toLongLong());
    case ResultColumn::Type::Timestamp:
        return formatTimestamp(value.toString());
    case ResultColumn::Type::Date:
        return formatDate(value.toString());
    case ResultColumn::Type::Category:
        return categoryLabel(value.toString());
    case ResultColumn::Type::Duration:
        return formatDuration(value.toDouble());
    case ResultColumn::Type::Text:
    case ResultColumn::Type::Path:
        break;
    }
    return value.toString();
}

QString severityLabel(Severity severity)
{
    switch (severity) {
    case Severity::Critical:
        return qtTrId("alerts.severity_critical");
    case Severity::High:
        return qtTrId("alerts.severity_high");
    case Severity::Medium:
        return qtTrId("alerts.severity_medium");
    case Severity::Low:
        break;
    }
    return qtTrId("alerts.severity_low");
}

Severity severityFromLevel(int sigmaLevel)
{
    switch (static_cast<sigma::Level>(sigmaLevel)) {
    case sigma::Level::Critical:
        return Severity::Critical;
    case sigma::Level::High:
        return Severity::High;
    case sigma::Level::Medium:
        return Severity::Medium;
    case sigma::Level::Low:
    case sigma::Level::Information:
        break;
    }
    return Severity::Low;
}

QString Detection::quantity() const
{
    return qtTrId(unit, pluralCount(count));
}

Filters Detection::allOccurrences(const Filters& scope) const
{
    Filters filters = scope;
    filters.startUtc = 0;
    filters.endUtc = 0;
    if (sigma) {
        filters.rule = rule;
        filters.ruleTitle = title;
    }
    return filters;
}

int riskScore(const QList<Detection>& detections)
{
    double rest = 1.0;
    for (const Detection& detection : detections) {
        if (detection.count <= 0)
            continue;
        switch (detection.severity) {
        case Severity::Critical:
            rest *= 0.6;
            break;
        case Severity::High:
            rest *= 0.8;
            break;
        case Severity::Medium:
            rest *= 0.92;
            break;
        case Severity::Low:
            rest *= 0.98;
            break;
        }
    }
    return static_cast<int>(std::lround((1.0 - rest) * 100.0));
}

Severity maxSeverity(const QList<Detection>& detections)
{
    Severity maximum = Severity::Low;
    for (const Detection& detection : detections)
        if (detection.count > 0)
            maximum = std::max(maximum, detection.severity);
    return maximum;
}

int findingCount(const QList<Detection>& detections)
{
    return static_cast<int>(
        std::count_if(detections.cbegin(), detections.cend(), [](const Detection& d) { return d.count > 0; }));
}

QList<Detection> detect(Context& context, const Filters& filters, const AlertParameters& parameters)
{
    Filters scope = filters;
    if (parameters.ignoreSystemAccounts && scope.accounts == AccountFilter::All)
        scope.accounts = AccountFilter::Users;
    const Clause base = scope.clause();

    QList<Detection> detections;
    const bool files =
        parameters.fileAnalysis
        && context.connection.integer(QStringLiteral("SELECT EXISTS (SELECT 1 FROM groups WHERE action_cat <> '')")) != 0;
    if (files) {
        FileChecks checks = scanFiles(context, parameters, base);
        detections << massDeletions(context, scope, parameters, checks);
        detections << extensions(context, scope, true, parameters.ransomwareExtensions, checks);
        detections << bursts(context, scope, parameters, checks);
        detections << offHours(context, scope, parameters, checks);
        detections << permissionChanges(context, scope, checks);
        detections << extensions(context, scope, false, parameters.executableExtensions, checks);
    }
    if (parameters.ruleAnalysis)
        detections << sigmaDetections(context, filters);
    context.throwIfCancelled();
    return detections;
}

} // namespace lh
