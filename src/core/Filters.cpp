#include "Filters.h"

#include "Actions.h"
#include "Catalog.h"
#include "Sqlite.h"
#include "DateTime.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace lh {

namespace {

QString pattern(const QString& fragment)
{
    return QLatin1Char('%') + sql::escapeLike(fragment) + QLatin1Char('%');
}

struct EventRange
{
    quint32 start = 0;
    quint32 end = 0;
    bool exclusion = false;
};

bool parseEvents(const QString& text, QList<EventRange>& ranges)
{
    static const QRegularExpression SEPARATORS(QStringLiteral("[\\s,;]+"));
    for (QString token : text.split(SEPARATORS, Qt::SkipEmptyParts)) {
        EventRange range;
        if (token.startsWith(QLatin1Char('-')) || token.startsWith(QLatin1Char('!'))) {
            range.exclusion = true;
            token.remove(0, 1);
        }
        bool ok = false;
        const qsizetype dash = token.indexOf(QLatin1Char('-'));
        if (dash > 0) {
            bool okEnd = false;
            range.start = token.left(dash).toUInt(&ok);
            range.end = token.mid(dash + 1).toUInt(&okEnd);
            ok = ok && okEnd;
            if (range.start > range.end)
                std::swap(range.start, range.end);
        } else {
            range.start = range.end = token.toUInt(&ok);
        }
        if (!ok)
            return false;
        ranges << range;
    }
    return !ranges.isEmpty();
}

} // namespace

QString searchQuery(const QString& text)
{
    static const QRegularExpression SPACES(QStringLiteral("\\s+"));
    QStringList terms;
    for (const QString& word : text.split(SPACES, Qt::SkipEmptyParts)) {
        if (std::none_of(word.begin(), word.end(), [](QChar c) { return c.isLetterOrNumber(); }))
            continue;
        QString escaped = word;
        escaped.replace(QLatin1Char('"'), QLatin1String("\"\""));
        terms << QLatin1Char('"') + escaped + QLatin1String("\"*");
    }
    return terms.join(QLatin1Char(' '));
}

bool validEvents(const QString& text)
{
    QList<EventRange> ranges;
    return text.trimmed().isEmpty() || parseEvents(text, ranges);
}

QString periodDescription(qint64 startUtc, qint64 endUtc, const QByteArray& timeZone)
{
    TimeZoneConverter converter(timeZone);
    const QString start = converter.convert(startUtc).timestamp;
    const QString end = converter.convert(std::max(startUtc, endUtc - 1)).timestamp;
    if (start.size() >= 19 && end.size() >= 19 && QStringView(start).left(10) == QStringView(end).left(10))
        return qtTrId("common.period_on_from_to").arg(formatDate(start), start.mid(11, 8), end.mid(11, 8));
    return qtTrId("common.period_from_to").arg(formatTimestamp(start), formatTimestamp(end));
}

Clause Clause::andWhere(const QString& condition, const QVariantList& values) const
{
    Clause result = *this;
    result.events = events.isEmpty() ? condition : QStringLiteral("(%1) AND %2").arg(events, condition);
    result.eventParameters += values;
    result.recompose();
    return result;
}

Clause Clause::andGroupWhere(const QString& condition, const QVariantList& values) const
{
    Clause result = *this;
    result.groups = groups.isEmpty() ? condition : QStringLiteral("(%1) AND %2").arg(groups, condition);
    result.groupParameters += values;
    result.recompose();
    return result;
}

void Clause::recompose()
{
    QStringList parts;
    if (!events.isEmpty())
        parts << QStringLiteral("(%1)").arg(events);
    if (!groups.isEmpty())
        parts << QStringLiteral("group_id IN (SELECT group_id FROM groups WHERE %1)").arg(groups);
    sql = parts.join(QStringLiteral(" AND "));
    parameters = eventParameters + groupParameters;
}

Clause Filters::clause() const
{
    QStringList conditions;
    QVariantList parameters;
    QStringList eventsSql;
    QVariantList eventsParameters;
    const QString like = QStringLiteral(" LIKE ? ESCAPE '\\'");

    if (!text.trimmed().isEmpty()) {
        const QString statement = searchQuery(text);
        if (!statement.isEmpty()) {
            conditions << QStringLiteral("group_id IN (SELECT rowid FROM search WHERE search MATCH ?)");
            parameters << statement;
        } else {
            const QString m = pattern(text.trimmed());
            conditions << QStringLiteral("(user%1 OR file_path%1 OR process%1 OR detail%1)").arg(like);
            parameters << m << m << m << m;
        }
    }
    if (!user.trimmed().isEmpty()) {
        conditions << (exact ? QStringLiteral("user = ?") : QStringLiteral("user") + like);
        parameters << (exact ? user : pattern(user.trimmed()));
    }
    if (!category.isEmpty()) {
        conditions << QStringLiteral("action_cat = ?");
        parameters << category;
    }
    if (!file.trimmed().isEmpty()) {
        conditions << (exact ? QStringLiteral("file_path = ?") : QStringLiteral("file_path") + like);
        parameters << (exact ? file : pattern(file.trimmed()));
    }
    if (!folder.isEmpty()) {
        conditions << QStringLiteral("(dir_path = ? OR (dir_path >= ? AND dir_path < ?))");
        parameters << folder << QString(folder + QLatin1Char('\\')) << QString(folder + QLatin1Char(']'));
    }
    if (!process.trimmed().isEmpty()) {
        conditions << QStringLiteral("process") + like;
        parameters << pattern(process.trimmed());
    }
    if (!machine.trimmed().isEmpty()) {
        conditions << QStringLiteral("machine") + like;
        parameters << pattern(machine.trimmed());
    }
    QList<EventRange> ranges;
    if (!events.trimmed().isEmpty() && parseEvents(events, ranges)) {
        QStringList included;
        for (const EventRange& range : std::as_const(ranges)) {
            const QString condition =
                range.start == range.end ? QStringLiteral("event_id = %1").arg(range.start)
                                         : QStringLiteral("event_id BETWEEN %1 AND %2").arg(range.start).arg(range.end);
            if (range.exclusion)
                conditions << QStringLiteral("NOT (%1)").arg(condition);
            else
                included << condition;
        }
        if (!included.isEmpty())
            conditions << QStringLiteral("(%1)").arg(included.join(QStringLiteral(" OR ")));
    }
    switch (level) {
    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        conditions << QStringLiteral("level = %1").arg(level);
        break;
    case 6:
        conditions << QStringLiteral("audit = 2");
        break;
    case 7:
        conditions << QStringLiteral("audit = 1");
        break;
    case 8:
        conditions << QStringLiteral("level BETWEEN 1 AND 3");
        break;
    default:
        break;
    }
    if (!channel.isEmpty()) {
        conditions << QStringLiteral("channel = ?");
        parameters << channel;
    }
    if (!ip.trimmed().isEmpty()) {
        if (exact) {
            conditions << QStringLiteral("(source_ip = ? OR destination_ip = ?)");
            parameters << ip.trimmed() << ip.trimmed();
        } else {
            conditions << QStringLiteral("(source_ip%1 OR destination_ip%1)").arg(like);
            parameters << pattern(ip.trimmed()) << pattern(ip.trimmed());
        }
    }
    if (group != 0) {
        conditions << QStringLiteral("group_id = ?");
        parameters << group;
    }
    if (!includeHandle)
        conditions << QStringLiteral("event_id NOT IN (%1, %2, %3)")
                          .arg(EVT_HANDLE)
                          .arg(EVT_CLOSE)
                          .arg(EVT_DUPLICATION);
    if (accounts == AccountFilter::Users)
        conditions << QStringLiteral("is_system = 0");
    else if (accounts == AccountFilter::System)
        conditions << QStringLiteral("is_system = 1");

    if (!rule.isEmpty()) {
        eventsSql << QStringLiteral("id IN (SELECT c.event FROM matches c "
                                    "JOIN rules r ON r.number = c.rule WHERE r.id = ?)");
        eventsParameters << rule;
    }
    if (sourceId > 0) {
        eventsSql << QStringLiteral("source_id = ?");
        eventsParameters << sourceId;
    }
    static constexpr qint64 DAY = 86'400'000'000LL;
    if (since.isValid()) {
        const qint64 day = dayFromDate(since);
        eventsSql << QStringLiteral("ts_utc >= ? AND day >= ?");
        eventsParameters << (day - 1) * DAY << day;
    }
    if (until.isValid()) {
        const qint64 day = dayFromDate(until);
        eventsSql << QStringLiteral("ts_utc < ? AND day <= ?");
        eventsParameters << (day + 2) * DAY << day;
    }
    if (hasExactPeriod()) {
        eventsSql << QStringLiteral("ts_utc >= ? AND ts_utc < ?");
        eventsParameters << startUtc << endUtc;
    }
    if (hourRange()) {
        if (startHour < endHour)
            eventsSql << QStringLiteral("(hour >= ? AND hour < ?)");
        else
            eventsSql << QStringLiteral("(hour >= ? OR (hour >= 0 AND hour < ?))");
        eventsParameters << startHour << endHour;
    }

    Clause result;
    if (!eventsSql.isEmpty())
        result = result.andWhere(eventsSql.join(QStringLiteral(" AND ")), eventsParameters);
    if (!conditions.isEmpty())
        result = result.andGroupWhere(conditions.join(QStringLiteral(" AND ")), parameters);
    return result;
}

int Filters::activeCount() const
{
    int count = 0;
    for (const QString* field :
         {&text, &user, &category, &file, &folder, &process, &machine, &events, &channel, &ip, &rule})
        count += field->trimmed().isEmpty() ? 0 : 1;
    count += level != 0 ? 1 : 0;
    count += group != 0 ? 1 : 0;
    count += sourceId > 0 ? 1 : 0;
    count += (since.isValid() || until.isValid()) ? 1 : 0;
    count += hasExactPeriod() ? 1 : 0;
    count += hourRange() ? 1 : 0;
    count += includeHandle ? 1 : 0;
    count += accounts != AccountFilter::All ? 1 : 0;
    return count;
}

QString Filters::description(const QByteArray& timeZone) const
{
    QStringList parts;
    auto quotes = [](const QString& value) { return qtTrId("filters.quotes").arg(value.trimmed()); };
    if (!text.trimmed().isEmpty())
        parts << qtTrId("filters.description_search").arg(quotes(text));
    if (!rule.isEmpty())
        parts << qtTrId("filters.description_rule").arg(quotes(ruleTitle.isEmpty() ? rule : ruleTitle));
    if (group != 0)
        parts << qtTrId("filters.description_group");
    if (!events.trimmed().isEmpty())
        parts << qtTrId("filters.description_events").arg(events.trimmed());
    if (level != 0)
        parts << qtTrId("filters.description_level").arg(levelFilterLabel(level));
    if (!channel.isEmpty())
        parts << qtTrId("filters.description_windows_log").arg(shortChannelName(channel));
    if (!user.trimmed().isEmpty())
        parts << qtTrId("filters.description_user").arg(quotes(user));
    if (!category.isEmpty())
        parts << qtTrId("filters.description_action").arg(categoryLabel(category));
    if (!file.trimmed().isEmpty())
        parts << qtTrId("filters.description_file").arg(quotes(file));
    if (!folder.isEmpty())
        parts << qtTrId("filters.description_folder").arg(quotes(folder));
    if (!process.trimmed().isEmpty())
        parts << qtTrId("filters.description_program").arg(quotes(process));
    if (!machine.trimmed().isEmpty())
        parts << qtTrId("common.computer_name").arg(quotes(machine));
    if (!ip.trimmed().isEmpty())
        parts << qtTrId("filters.description_ip").arg(quotes(ip));
    if (sourceId > 0)
        parts << qtTrId("filters.description_log").arg(sourceId);
    if (since.isValid() && until.isValid())
        parts << qtTrId("common.period_from_to").arg(formatDate(since), formatDate(until));
    else if (since.isValid())
        parts << qtTrId("filters.description_from").arg(formatDate(since));
    else if (until.isValid())
        parts << qtTrId("filters.description_until").arg(formatDate(until));
    if (hasExactPeriod())
        parts << periodDescription(startUtc, endUtc, timeZone);
    if (hourRange())
        parts << qtTrId("filters.description_hours").arg(startHour).arg(endHour);
    if (accounts == AccountFilter::Users)
        parts << qtTrId("filters.description_user_accounts");
    else if (accounts == AccountFilter::System)
        parts << qtTrId("filters.description_system_accounts");
    if (includeHandle)
        parts << qtTrId("filters.description_simple_opens");
    return parts.isEmpty() ? qtTrId("filters.description_no_filter") : parts.join(QStringLiteral(" · "));
}

QJsonObject Filters::toJson() const
{
    QJsonObject object;
    auto add = [&](const char* key, const QString& value) {
        if (!value.isEmpty())
            object.insert(QLatin1String(key), value);
    };
    add("text", text);
    add("user", user);
    add("category", category);
    add("file", file);
    add("folder", folder);
    add("process", process);
    add("machine", machine);
    add("events", events);
    add("channel", channel);
    add("ip", ip);
    add("rule", rule);
    add("rule_title", ruleTitle);
    add("group_label", groupLabel);
    if (group != 0)
        object.insert(QStringLiteral("group"), QString::number(group));
    if (level != 0)
        object.insert(QStringLiteral("level"), level);
    if (since.isValid())
        object.insert(QStringLiteral("since"), since.toString(Qt::ISODate));
    if (until.isValid())
        object.insert(QStringLiteral("until"), until.toString(Qt::ISODate));
    if (hasExactPeriod()) {
        object.insert(QStringLiteral("start_utc"), QString::number(startUtc));
        object.insert(QStringLiteral("end_utc"), QString::number(endUtc));
    }
    if (startHour >= 0 && endHour >= 0) {
        object.insert(QStringLiteral("start_hour"), startHour);
        object.insert(QStringLiteral("end_hour"), endHour);
    }
    object.insert(QStringLiteral("include_handle"), includeHandle);
    object.insert(QStringLiteral("accounts"), static_cast<int>(accounts));
    object.insert(QStringLiteral("exact"), exact);
    return object;
}

QString Filters::key() const
{
    return QString::fromUtf8(QJsonDocument(toJson()).toJson(QJsonDocument::Compact));
}

Filters Filters::fromJson(const QJsonObject& object)
{
    Filters filters;
    auto text = [&](const char* key) { return object.value(QLatin1String(key)).toString(); };
    filters.text = text("text");
    filters.user = text("user");
    filters.category = text("category");
    if (!filters.category.isEmpty() && !findCategory(filters.category))
        filters.category.clear();
    filters.file = text("file");
    filters.folder = text("folder");
    filters.process = text("process");
    filters.machine = text("machine");
    filters.events = text("events");
    if (!validEvents(filters.events))
        filters.events.clear();
    filters.channel = text("channel");
    filters.ip = text("ip");
    filters.rule = text("rule");
    filters.ruleTitle = text("rule_title");
    filters.group = text("group").toLongLong();
    filters.groupLabel = text("group_label");
    filters.level = qBound(0, object.value(QStringLiteral("level")).toInt(0), 8);
    filters.since = QDate::fromString(text("since"), Qt::ISODate);
    filters.until = QDate::fromString(text("until"), Qt::ISODate);
    filters.startUtc = text("start_utc").toLongLong();
    filters.endUtc = text("end_utc").toLongLong();
    filters.startHour = qBound(-1, object.value(QStringLiteral("start_hour")).toInt(-1), 24);
    filters.endHour = qBound(-1, object.value(QStringLiteral("end_hour")).toInt(-1), 24);
    filters.includeHandle = object.value(QStringLiteral("include_handle")).toBool(false);
    filters.accounts = static_cast<AccountFilter>(qBound(0, object.value(QStringLiteral("accounts")).toInt(0), 2));
    filters.exact = object.value(QStringLiteral("exact")).toBool(false);
    return filters;
}

} // namespace lh
