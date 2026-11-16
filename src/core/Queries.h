// Analysis queries (dashboard, investigation, event table, timeline).
#pragma once

#include "Filters.h"
#include "Scan.h"
#include "Sqlite.h"

#include <QDate>
#include <QList>
#include <QMap>
#include <QPair>
#include <QString>

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace lh::queries {

using Series = QList<QPair<QString, qint64>>;
using DateSeries = QList<QPair<QDate, qint64>>;

struct EventRanking
{
    quint32 identifier = 0;
    QString channel;
    QString provider;
    qint64 total = 0;
};

struct Summary
{
    qint64 total = 0;
    qint64 users = 0;
    qint64 files = 0;
    qint64 machines = 0;
    qint64 fileAccesses = 0;
    QString first;
    QString last;
    QMap<QString, qint64> byCategory;
    DateSeries byDay;
    std::array<qint64, 24> byHour{};
    std::array<std::array<qint64, 24>, 7> byDayHour{};
    Series topUsers;
    Series topProcesses;
    Series byChannel;
    QList<EventRanking> topEvents;
    Series topSuspiciousIps;
    Series topIp;
};
Summary summary(Context& context, const Filters& filters);

// ── Investigation ────────────────────────────────────────────────────────────

struct UserRow
{
    QString name;
    qint64 total = 0;
    bool isSystem = false;
    QString last;
};
QList<UserRow> users(Context& context, const Filters& filters, int limit = 10'000);

struct FileRow
{
    QString path;
    qint64 total = 0;
    qint64 users = 0;
    QString first;
    QString last;
    QString dominantCategory;
    QMap<QString, qint64> byCategory;
};
QList<FileRow> files(Context& context, const Filters& filters, int limit = 5'000);

struct FolderRow
{
    QString path;
    qint64 total = 0;
    qint64 writes = 0;
    qint64 deletions = 0;
    qint64 reads = 0;
    qint64 permissions = 0;
    qint64 users = 0;
};
QList<FolderRow> folders(Context& context, const Filters& filters, int limit = 20'000);

struct Profile
{
    qint64 total = 0;
    QString first;
    QString last;
    qint64 files = 0;
    qint64 users = 0;
    qint64 machines = 0;
    QMap<QString, qint64> byCategory;
    DateSeries byDay;
    Series topUsers;
    Series topFiles;
};
Profile profile(Context& context, const Filters& filters);

QStringList distinctValues(sql::Connection& connection, const QString& column, int limit = 5'000);

// ── Event table (paging by identifiers) ─────────────────────────────────────

enum class Column { Timestamp, Level, EventId, Action, User, Machine, Process, Ip, Target, Channel };
inline constexpr int COLUMN_COUNT = 10;

using Identifiers = std::shared_ptr<const std::vector<qint64>>;
Identifiers identifiers(Context& context, const Clause& clause, Column sortKey, bool ascending);

struct TableRow
{
    qint64 id = 0;
    QString timestamp;
    QString user;
    bool isSystem = false;
    QString action;
    QString category;
    QString file;
    QString process;
    QString machine;
    quint32 eventId = 0;
    int level = 4;
    int audit = 0;
    QString channel;
    QString sourceIp;
    QString detail;
    int alert = -1;
    QString provider;
};
QList<TableRow> rows(sql::Connection& connection, std::span<const qint64> ids);

struct Uniformity
{
    quint32 emptyBits = 0;
    quint32 identicalBits = 0;
    TableRow sample;
    [[nodiscard]] static constexpr quint32 bit(Column column) { return 1U << static_cast<int>(column); }
};
Uniformity uniformity(sql::Connection& connection, const Clause& clause);

struct EventAlert
{
    QString rule;
    QString title;
    int level = 2;
    QStringList tags;
};

struct EventDetail
{
    TableRow row;
    qint64 tsUtc = 0;
    bool tsUtcKnown = false;
    QString folder;
    QString extension;
    QString access;
    qint64 recordId = 0;
    QString source;
    QString destinationIp;
    QList<QPair<QString, QString>> data;
    bool xmlAvailable = false;
    QList<EventAlert> alerts;
};
std::optional<EventDetail> detail(sql::Connection& connection, qint64 id);
QString xml(sql::Connection& connection, qint64 id);

// ── Grouping of identical events ────────────────────────────────────────────

struct Group
{
    qint64 identifier = 0;
    qint64 total = 0;
    QString first;
    QString last;
    quint32 eventId = 0;
    QString channel;
    QString provider;
    QString user;
    QString machine;
    QString process;
    QString target;
    QString action;
};
struct Grouping
{
    QList<Group> groups;
    qint64 groupCount = 0;
    qint64 events = 0;
};
Grouping groups(Context& context, const Filters& filters, int limit = 20'000);

// ── Timeline ────────────────────────────────────────────────────────────────

struct Histogram
{
    qint64 start = 0;
    qint64 step = 0;
    std::vector<qint64> values;
    std::vector<qint64> alerts;
};
Histogram histogram(Context& context, const Filters& filters, int maxBars, const QByteArray& timeZone);

} // namespace lh::queries
