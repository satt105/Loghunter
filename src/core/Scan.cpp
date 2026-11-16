#include "Scan.h"

#include "Calendar.h"
#include "DateTime.h"

#include <QVariant>

#include <algorithm>

namespace lh {

namespace {

constexpr qsizetype MAX_ADMITTED = 32;
constexpr qint64 MAX_COUNTS_BYTES = 48LL << 20;
constexpr qsizetype MAX_RARE_LIST = 200'000;
constexpr qint64 CHECK_INTERVAL = 0x10000;

QString parametersKey(const QString& sql, const QVariantList& parameters)
{
    QString key = sql;
    for (const QVariant& value : parameters) {
        key += QChar(0x1f);
        key += value.toString();
    }
    return key;
}

} // namespace

// ── Context ──────────────────────────────────────────────────────────────────

void Context::throwIfCancelled() const
{
    if (cancelled())
        throw sql::Error::interruption();
}

// ── Caches ───────────────────────────────────────────────────────────────────

std::shared_ptr<const AdmittedGroups> Caches::admitted(sql::Connection& connection, const Clause& clause,
                                                       const std::atomic_bool* cancellation)
{
    const QString key = parametersKey(clause.groups, clause.groupParameters);
    quint64 generation = 0;
    {
        const std::lock_guard<std::mutex> lock(m_lock);
        for (auto it = m_admitted.begin(); it != m_admitted.end(); ++it) {
            if (it->key == key) {
                m_admitted.splice(m_admitted.begin(), m_admitted, it);
                return m_admitted.front().value;
            }
        }
        generation = m_generation;
    }

    const Context context{connection, *this, cancellation};
    auto result = std::make_shared<AdmittedGroups>();
    result->events = connection.integer(QStringLiteral("SELECT COALESCE(MAX(id), 0) FROM events"));
    if (clause.groups.isEmpty()) {
        result->all = true;
        result->estimation = result->events;
    } else {
        const qint64 maximum = connection.integer(QStringLiteral("SELECT COALESCE(MAX(group_id), 0) FROM groups"));
        result->bits.assign(static_cast<size_t>(maximum) + 1, 0);
        sql::Statement statement =
            connection.prepare(QStringLiteral("SELECT group_id, total FROM groups WHERE %1").arg(clause.groups));
        statement.bindAll(clause.groupParameters);
        qint64 readCount = 0;
        while (statement.step()) {
            if ((++readCount & 0xfff) == 0)
                context.throwIfCancelled();
            const qint64 group = statement.integer(0);
            if (group < 0 || group > maximum)
                continue;
            result->bits[static_cast<size_t>(group)] = 1;
            result->estimation += statement.integer(1);
            result->list.push_back(group);
        }
        result->rare = result->estimation * 4 < result->events
                       && static_cast<qsizetype>(result->list.size()) <= MAX_RARE_LIST;
        if (!result->rare) {
            result->list.clear();
            result->list.shrink_to_fit();
        }
    }
    context.throwIfCancelled();

    const std::lock_guard<std::mutex> lock(m_lock);
    if (generation == m_generation) {
        m_admitted.push_front({key, result});
        while (static_cast<qsizetype>(m_admitted.size()) > MAX_ADMITTED)
            m_admitted.pop_back();
    }
    return result;
}

std::shared_ptr<const GroupCounts> Caches::counts(sql::Connection& connection, const Clause& clause,
                                                  const std::atomic_bool* cancellation)
{
    const QString key = parametersKey(clause.sql, clause.parameters);
    quint64 generation = 0;
    {
        const std::lock_guard<std::mutex> lock(m_lock);
        for (auto it = m_counts.begin(); it != m_counts.end(); ++it) {
            if (it->key == key) {
                m_counts.splice(m_counts.begin(), m_counts, it);
                return m_counts.front().value;
            }
        }
        generation = m_generation;
    }

    Context context{connection, *this, cancellation};
    const std::shared_ptr<const AdmittedGroups> admitted = this->admitted(connection, clause, cancellation);
    auto result = std::make_shared<GroupCounts>();
    const qint64 maximum = connection.integer(QStringLiteral("SELECT COALESCE(MAX(group_id), 0) FROM groups"));
    result->count.assign(static_cast<size_t>(maximum) + 1, 0);
    result->first.assign(static_cast<size_t>(maximum) + 1, std::numeric_limits<qint64>::max());
    result->last.assign(static_cast<size_t>(maximum) + 1, std::numeric_limits<qint64>::min());

    qint64 currentSlot = std::numeric_limits<qint64>::min();
    qint64* slotCounter = nullptr;
    qint64 currentQuarter = std::numeric_limits<qint64>::min();
    qint64* quarterCounter = nullptr;
    EventReader reader(context, clause, admitted, QStringLiteral("ts_utc, day, hour"));
    while (reader.next()) {
        const sql::Statement& row = reader.row();
        const qint64 group = reader.group();
        if (group >= 0 && group <= maximum) {
            ++result->count[static_cast<size_t>(group)];
            if (!row.isNull(1)) {
                const qint64 ts = row.integer(1);
                auto& first = result->first[static_cast<size_t>(group)];
                auto& last = result->last[static_cast<size_t>(group)];
                first = std::min(first, ts);
                last = std::max(last, ts);
            }
        }
        ++result->total;
        if (!row.isNull(1)) {
            const qint64 ts = row.integer(1);
            result->firstTs = std::min(result->firstTs, ts);
            result->lastTs = std::max(result->lastTs, ts);
            const qint64 quarter = floorDivide(ts, QUARTER_HOUR);
            if (quarter != currentQuarter) {
                currentQuarter = quarter;
                quarterCounter = &result->byQuarter[quarter];
            }
            ++*quarterCounter;
        }
        const qint64 hour = std::clamp<qint64>(row.integer(3), -1, 24);
        const qint64 slot = row.isNull(2) ? -2 - hour : row.integer(2) * 32 + hour + 1;
        if (slot != currentSlot) {
            currentSlot = slot;
            slotCounter = &result->bySlot[slot];
        }
        ++*slotCounter;
    }
    context.throwIfCancelled();

    const std::lock_guard<std::mutex> lock(m_lock);
    if (generation == m_generation) {
        m_counts.push_front({key, result});
        qint64 bytes = 0;
        for (auto it = m_counts.begin(); it != m_counts.end();) {
            bytes += it->value->bytes();
            if (bytes > MAX_COUNTS_BYTES && it != m_counts.begin())
                it = m_counts.erase(it);
            else
                ++it;
        }
    }
    return result;
}

void Caches::invalidate()
{
    const std::lock_guard<std::mutex> lock(m_lock);
    ++m_generation;
    m_admitted.clear();
    m_counts.clear();
}

// ── EventReader ──────────────────────────────────────────────────────────────

EventReader::EventReader(Context& context, const Clause& clause,
                         std::shared_ptr<const AdmittedGroups> admitted, const QString& columns,
                         const QString& order)
    : m_context(context), m_admitted(std::move(admitted))
{
    QString sql;
    int firstParameter = 1;
    if (m_admitted->rare) {
        m_json.reserve(static_cast<qsizetype>(m_admitted->list.size()) * 8 + 2);
        m_json += '[';
        for (const qint64 group : m_admitted->list) {
            if (m_json.size() > 1)
                m_json += ',';
            m_json += QByteArray::number(group);
        }
        m_json += ']';
        sql = QStringLiteral("SELECT events.group_id, %1 FROM (SELECT value AS g FROM json_each(?1)) a "
                             "CROSS JOIN events ON events.group_id = a.g")
                  .arg(columns);
        firstParameter = 2;
    } else {
        sql = QStringLiteral("SELECT group_id, %1 FROM events").arg(columns);
        m_filter = !m_admitted->all;
    }
    if (clause.eventBased())
        sql += QStringLiteral(" WHERE (%1)").arg(clause.events);
    if (!order.isEmpty())
        sql += QStringLiteral(" ORDER BY ") + order;
    m_statement = context.connection.prepare(sql);
    if (m_admitted->rare)
        m_statement.bindUtf8(1, m_json);
    if (clause.eventBased())
        m_statement.bindAll(clause.eventParameters, firstParameter);
}

bool EventReader::next()
{
    while (m_statement.step()) {
        if ((++m_readCount & (CHECK_INTERVAL - 1)) == 0)
            m_context.throwIfCancelled();
        if (m_filter && !m_admitted->admitted(m_statement.integer(0)))
            continue;
        return true;
    }
    return false;
}

// ── GroupReader ──────────────────────────────────────────────────────────────

GroupReader::GroupReader(Context& context, const Clause& clause, const QString& columns,
                         std::shared_ptr<const GroupCounts> counts, const QString& order)
    : m_context(context), m_counts(std::move(counts))
{
    QString sql = QStringLiteral("SELECT group_id, %1 FROM groups%2").arg(columns, clause.whereGroups());
    if (!order.isEmpty())
        sql += QStringLiteral(" ORDER BY ") + order;
    m_statement = context.connection.prepare(sql);
    m_statement.bindAll(clause.groupParameters);
}

bool GroupReader::next()
{
    while (m_statement.step()) {
        if ((++m_readCount & 0xfff) == 0)
            m_context.throwIfCancelled();
        m_group = m_statement.integer(0);
        if (m_counts) {
            m_count = m_counts->countOf(m_group);
            if (m_count == 0)
                continue;
        }
        return true;
    }
    return false;
}

// ── TimestampFormatter ───────────────────────────────────────────────────────

TimestampFormatter::TimestampFormatter(sql::Connection& connection)
    : m_statement(connection.prepare(QStringLiteral("SELECT lh_timestamp(?)")))
{
}

QString TimestampFormatter::operator()(qint64 utcMicroseconds)
{
    if (utcMicroseconds == UNKNOWN_TIMESTAMP || utcMicroseconds == std::numeric_limits<qint64>::max())
        return {};
    m_statement.reset();
    m_statement.bind(1, utcMicroseconds);
    return m_statement.step() ? m_statement.text(0) : QString();
}

// ── Fingerprints ─────────────────────────────────────────────────────────────

quint64 fingerprint(QByteArrayView bytes) noexcept
{
    return fingerprint(0xcbf29ce484222325ULL, bytes);
}

quint64 fingerprint(quint64 seed, QByteArrayView bytes) noexcept
{
    quint64 h = seed;
    for (const char c : bytes) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x100000001b3ULL;
    }
    return h;
}

namespace {

quint64 scramble(quint64 h) noexcept
{
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
    return h ^ (h >> 31);
}

} // namespace

quint64 mixFingerprint(quint64 fingerprint, quint64 value) noexcept
{
    return scramble(scramble(fingerprint + 0x9e3779b97f4a7c15ULL) + value);
}

// ── FingerprintTable ─────────────────────────────────────────────────────────

void FingerprintTable::reserve(qsizetype count)
{
    size_t capacity = 64;
    while (capacity < static_cast<size_t>(count) * 2)
        capacity *= 2;
    if (capacity <= m_buckets.size())
        return;
    std::vector<quint64> keys = std::move(m_keys);
    m_keys.clear();
    m_keys.reserve(std::max(static_cast<size_t>(count), keys.size()));
    m_buckets.assign(capacity, -1);
    bool isNew = false;
    for (const quint64 key : keys)
        findOrAdd(key, isNew);
}

void FingerprintTable::grow()
{
    reserve(std::max<qsizetype>(32, static_cast<qsizetype>(m_keys.size()) * 2));
}

qsizetype FingerprintTable::findOrAdd(quint64 key, bool& isNew)
{
    if (m_buckets.empty() || m_keys.size() * 2 >= m_buckets.size())
        grow();
    const size_t mask = m_buckets.size() - 1;
    size_t position = static_cast<size_t>((key * 0x9e3779b97f4a7c15ULL) >> 32) & mask;
    while (true) {
        const qint32 occupant = m_buckets[position];
        if (occupant < 0) {
            const auto index = static_cast<qint32>(m_keys.size());
            m_buckets[position] = index;
            m_keys.push_back(key);
            isNew = true;
            return index;
        }
        if (m_keys[static_cast<size_t>(occupant)] == key) {
            isNew = false;
            return occupant;
        }
        position = (position + 1) & mask;
    }
}

qsizetype FingerprintTable::index(quint64 key) const noexcept
{
    if (m_buckets.empty())
        return -1;
    const size_t mask = m_buckets.size() - 1;
    size_t position = static_cast<size_t>((key * 0x9e3779b97f4a7c15ULL) >> 32) & mask;
    while (true) {
        const qint32 occupant = m_buckets[position];
        if (occupant < 0)
            return -1;
        if (m_keys[static_cast<size_t>(occupant)] == key)
            return occupant;
        position = (position + 1) & mask;
    }
}

// ── Miscellaneous ────────────────────────────────────────────────────────────

QString groupText(sql::Connection& connection, const QString& column, qint64 group)
{
    return connection.text(QStringLiteral("SELECT %1 FROM groups WHERE group_id = ?").arg(column), {group});
}

} // namespace lh
