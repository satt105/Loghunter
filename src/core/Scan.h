// Shared event scans: admitted groups, counts per group and caches common to all tasks.
#pragma once

#include "Filters.h"
#include "Sqlite.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QHash>
#include <QString>

#include <atomic>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <vector>

namespace lh {

struct AdmittedGroups
{
    bool all = false;
    bool rare = false;
    std::vector<char> bits;
    std::vector<qint64> list;
    qint64 estimation = 0;
    qint64 events = 0;

    [[nodiscard]] bool admitted(qint64 group) const noexcept
    {
        return all
               || (group >= 0 && static_cast<size_t>(group) < bits.size() && bits[static_cast<size_t>(group)] != 0);
    }
};

inline constexpr qint64 QUARTER_HOUR = 900LL * 1'000'000;

struct GroupCounts
{
    std::vector<qint32> count;
    std::vector<qint64> first;
    std::vector<qint64> last;
    qint64 total = 0;
    qint64 firstTs = std::numeric_limits<qint64>::max();
    qint64 lastTs = std::numeric_limits<qint64>::min();
    QHash<qint64, qint64> bySlot;
    QHash<qint64, qint64> byQuarter;

    [[nodiscard]] qint64 countOf(qint64 group) const noexcept
    {
        return group >= 0 && static_cast<size_t>(group) < count.size() ? count[static_cast<size_t>(group)] : 0;
    }
    [[nodiscard]] bool date() const noexcept { return firstTs <= lastTs; }
    [[nodiscard]] qint64 bytes() const noexcept
    {
        return static_cast<qint64>(count.size()) * 20 + (bySlot.size() + byQuarter.size()) * 48;
    }
};

class Caches
{
public:
    std::shared_ptr<const AdmittedGroups> admitted(sql::Connection& connection, const Clause& clause,
                                                   const std::atomic_bool* cancellation);
    std::shared_ptr<const GroupCounts> counts(sql::Connection& connection, const Clause& clause,
                                              const std::atomic_bool* cancellation);
    void invalidate();

private:
    template <typename T>
    struct Entry
    {
        QString key;
        std::shared_ptr<const T> value;
    };

    std::mutex m_lock;
    quint64 m_generation = 0;
    std::list<Entry<AdmittedGroups>> m_admitted;
    std::list<Entry<GroupCounts>> m_counts;
};

struct Context
{
    sql::Connection& connection;
    Caches& caches;
    const std::atomic_bool* cancellation = nullptr;

    [[nodiscard]] bool cancelled() const noexcept { return cancellation && cancellation->load(std::memory_order_relaxed); }
    void throwIfCancelled() const;
    std::shared_ptr<const AdmittedGroups> admitted(const Clause& clause)
    {
        return caches.admitted(connection, clause, cancellation);
    }
    std::shared_ptr<const GroupCounts> counts(const Clause& clause)
    {
        return caches.counts(connection, clause, cancellation);
    }
};

class EventReader
{
public:
    EventReader(Context& context, const Clause& clause, std::shared_ptr<const AdmittedGroups> admitted,
                const QString& columns, const QString& order = {});

    bool next();
    [[nodiscard]] const sql::Statement& row() const noexcept { return m_statement; }
    [[nodiscard]] qint64 group() const { return m_statement.integer(0); }

private:
    Context& m_context;
    std::shared_ptr<const AdmittedGroups> m_admitted;
    QByteArray m_json;
    sql::Statement m_statement;
    bool m_filter = false;
    qint64 m_readCount = 0;
};

class GroupReader
{
public:
    GroupReader(Context& context, const Clause& clause, const QString& columns,
                std::shared_ptr<const GroupCounts> counts = {}, const QString& order = {});

    bool next();
    [[nodiscard]] const sql::Statement& row() const noexcept { return m_statement; }
    [[nodiscard]] qint64 group() const noexcept { return m_group; }
    [[nodiscard]] qint64 count() const noexcept { return m_count; }

private:
    Context& m_context;
    std::shared_ptr<const GroupCounts> m_counts;
    sql::Statement m_statement;
    qint64 m_group = -1;
    qint64 m_count = 0;
    qint64 m_readCount = 0;
};

class TimestampFormatter
{
public:
    explicit TimestampFormatter(sql::Connection& connection);
    QString operator()(qint64 utcMicroseconds);

private:
    sql::Statement m_statement;
};

quint64 fingerprint(QByteArrayView bytes) noexcept;
quint64 fingerprint(quint64 seed, QByteArrayView bytes) noexcept;
quint64 mixFingerprint(quint64 fingerprint, quint64 value) noexcept;

class FingerprintTable
{
public:
    qsizetype findOrAdd(quint64 key, bool& isNew);
    [[nodiscard]] qsizetype index(quint64 key) const noexcept;
    [[nodiscard]] qsizetype size() const noexcept { return static_cast<qsizetype>(m_keys.size()); }
    [[nodiscard]] const std::vector<quint64>& keys() const noexcept { return m_keys; }
    void reserve(qsizetype count);

private:
    void grow();

    std::vector<qint32> m_buckets;
    std::vector<quint64> m_keys;
};

template <typename T>
class Aggregate
{
public:
    T& findOrAdd(quint64 key)
    {
        bool isNew = false;
        const qsizetype index = m_table.findOrAdd(key, isNew);
        if (isNew)
            m_values.emplace_back();
        return m_values[static_cast<size_t>(index)];
    }
    [[nodiscard]] const T* find(quint64 key) const noexcept
    {
        const qsizetype index = m_table.index(key);
        return index < 0 ? nullptr : &m_values[static_cast<size_t>(index)];
    }
    [[nodiscard]] qsizetype size() const noexcept { return m_table.size(); }
    [[nodiscard]] const std::vector<quint64>& keys() const noexcept { return m_table.keys(); }
    [[nodiscard]] std::vector<T>& values() noexcept { return m_values; }
    [[nodiscard]] const std::vector<T>& values() const noexcept { return m_values; }
    void reserve(qsizetype count)
    {
        m_table.reserve(count);
        m_values.reserve(static_cast<size_t>(count));
    }

private:
    FingerprintTable m_table;
    std::vector<T> m_values;
};

class FingerprintSet
{
public:
    bool insert(quint64 key)
    {
        bool isNew = false;
        m_table.findOrAdd(key, isNew);
        return isNew;
    }
    [[nodiscard]] qsizetype size() const noexcept { return m_table.size(); }

private:
    FingerprintTable m_table;
};

QString groupText(sql::Connection& connection, const QString& column, qint64 group);

} // namespace lh
