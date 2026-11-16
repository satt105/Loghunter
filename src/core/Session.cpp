#include "Session.h"

#include "Schema.h"
#include "DateTime.h"
#include "loghunter_version.h"

#include <sqlite3.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <thread>

namespace lh {

namespace {

constexpr char KEY_VERSION[] = "schema_version";
constexpr char KEY_TIME_ZONE[] = "time_zone";

std::atomic_int g_timeZoneGeneration{0};

void textResult(sqlite3_context* context, const QString& text)
{
    const QByteArray utf8 = text.toUtf8();
    sqlite3_result_text(context, utf8.constData(), static_cast<int>(utf8.size()), SQLITE_TRANSIENT);
}

// ── SQL functions: local time and time zone recomputation ────────────────────

void dayFunction(sqlite3_context* context, int, sqlite3_value** arguments)
{
    auto* converter = static_cast<TimeZoneConverter*>(sqlite3_user_data(context));
    const LocalTimestamp local = converter->convert(sqlite3_value_int64(arguments[0]));
    if (local.hour < 0)
        sqlite3_result_null(context);
    else
        sqlite3_result_int64(context, local.day);
}

void hourFunction(sqlite3_context* context, int, sqlite3_value** arguments)
{
    auto* converter = static_cast<TimeZoneConverter*>(sqlite3_user_data(context));
    sqlite3_result_int(context, converter->convert(sqlite3_value_int64(arguments[0])).hour);
}

struct TimestampState
{
    int generation = -1;
    std::unique_ptr<TimeZoneConverter> converter;
};

void timestampFunction(sqlite3_context* context, int, sqlite3_value** arguments)
{
    if (sqlite3_value_type(arguments[0]) == SQLITE_NULL) {
        sqlite3_result_text(context, "", 0, SQLITE_STATIC);
        return;
    }
    auto* state = static_cast<TimestampState*>(sqlite3_user_data(context));
    const int generation = g_timeZoneGeneration.load(std::memory_order_acquire);
    if (!state->converter || state->generation != generation) {
        QByteArray timeZone;
        sqlite3_stmt* reader = nullptr;
        if (sqlite3_prepare_v2(sqlite3_context_db_handle(context), "SELECT value FROM meta WHERE key = 'time_zone'", -1,
                               &reader, nullptr)
            == SQLITE_OK) {
            if (sqlite3_step(reader) == SQLITE_ROW)
                timeZone = QByteArray(reinterpret_cast<const char*>(sqlite3_column_text(reader, 0)));
            sqlite3_finalize(reader);
        }
        state->converter = std::make_unique<TimeZoneConverter>(isValidTimeZone(timeZone) ? timeZone : systemTimeZone());
        state->generation = generation;
    }
    textResult(context, state->converter->convert(sqlite3_value_int64(arguments[0])).timestamp);
}

void ipKeyFunction(sqlite3_context* context, int, sqlite3_value** arguments)
{
    const auto* text = sqlite3_value_text(arguments[0]);
    const int size = sqlite3_value_bytes(arguments[0]);
    if (!text || size == 0) {
        sqlite3_result_null(context);
        return;
    }
    unsigned char key[5] = {4, 0, 0, 0, 0};
    int byte = 1;
    int value = -1;
    bool ipv4 = true;
    for (int i = 0; i < size && ipv4; ++i) {
        const unsigned char c = text[i];
        if (c >= '0' && c <= '9') {
            value = (value < 0 ? 0 : value * 10) + (c - '0');
            ipv4 = value <= 255;
        } else if (c == '.' && value >= 0 && byte < 4) {
            key[byte++] = static_cast<unsigned char>(value);
            value = -1;
        } else {
            ipv4 = false;
        }
    }
    if (ipv4 && byte == 4 && value >= 0) {
        key[4] = static_cast<unsigned char>(value);
        sqlite3_result_blob(context, key, sizeof(key), SQLITE_TRANSIENT);
        return;
    }
    QByteArray other(1, '\x06');
    other.append(reinterpret_cast<const char*>(text), size);
    sqlite3_result_blob(context, other.constData(), static_cast<int>(other.size()), SQLITE_TRANSIENT);
}

void textKeyFunction(sqlite3_context* context, int, sqlite3_value** arguments)
{
    const auto* text = reinterpret_cast<const char*>(sqlite3_value_text(arguments[0]));
    if (!text) {
        sqlite3_result_null(context);
        return;
    }
    const QByteArrayView bytes(text, sqlite3_value_bytes(arguments[0]));
    QByteArray key;
    if (std::all_of(bytes.begin(), bytes.end(), [](char c) { return static_cast<unsigned char>(c) < 0x80; })) {
        key = bytes.toByteArray().toLower();
    } else {
        QString folded = QString::fromUtf8(bytes).toCaseFolded().normalized(QString::NormalizationForm_KD);
        folded.removeIf([](QChar c) { return c.category() == QChar::Mark_NonSpacing; });
        key = folded.toUtf8();
    }
    sqlite3_result_text(context, key.constData(), static_cast<int>(key.size()), SQLITE_TRANSIENT);
}

void fileNameFunction(sqlite3_context* context, int, sqlite3_value** arguments)
{
    const auto* text = reinterpret_cast<const char*>(sqlite3_value_text(arguments[0]));
    if (!text) {
        sqlite3_result_null(context);
        return;
    }
    const int size = sqlite3_value_bytes(arguments[0]);
    int start = size;
    while (start > 0 && text[start - 1] != '\\' && text[start - 1] != '/')
        --start;
    sqlite3_result_text(context, text + start, size - start, SQLITE_TRANSIENT);
}

std::filesystem::path nativePath(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(QFile::encodeName(path).toStdString());
#endif
}

bool isSqliteFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    return file.read(16) == QByteArray("SQLite format 3\0", 16);
}

void removeDatabaseFiles(const QString& path)
{
    for (const char* suffix : {"", "-wal", "-shm", "-journal"})
        QFile::remove(path + QLatin1String(suffix));
}

QString newWorkPath(const QString& folder)
{
    return QStringLiteral("%1/session-%2-%3-%4.db")
        .arg(folder, QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss")))
        .arg(QCoreApplication::applicationPid())
        .arg(QRandomGenerator::global()->generate() & 0xffff, 4, 16, QLatin1Char('0'));
}

void copyDatabase(const QString& source, const QString& destination, const std::function<void(double)>& progress)
{
    sql::Connection origin;
    try {
        origin = sql::Connection::open(source, sql::Connection::Read);
    } catch (const sql::Error&) {
        origin = sql::Connection::open(QUrl::fromLocalFile(source).toString() + QStringLiteral("?immutable=1"),
                                       sql::Connection::Read);
    }
    sql::Connection target = sql::Connection::open(destination);
    sqlite3_backup* copy = sqlite3_backup_init(target.handle(), "main", origin.handle(), "main");
    if (!copy)
        throw sql::Error(sqlite3_errcode(target.handle()), QString::fromUtf8(sqlite3_errmsg(target.handle())));
    int code = SQLITE_OK;
    while ((code = sqlite3_backup_step(copy, 2048)) == SQLITE_OK || code == SQLITE_BUSY || code == SQLITE_LOCKED) {
        const int total = sqlite3_backup_pagecount(copy);
        if (progress && total > 0)
            progress(1.0 - static_cast<double>(sqlite3_backup_remaining(copy)) / total);
    }
    sqlite3_backup_finish(copy);
    if (code != SQLITE_DONE)
        throw sql::Error(code, QString::fromUtf8(sqlite3_errstr(code)));
}

} // namespace

QString dataFolder()
{
    QString folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (folder.isEmpty())
        folder = QDir::tempPath() + QStringLiteral("/LogHunter");
    QDir().mkpath(folder);
    return folder;
}

// ── Source ───────────────────────────────────────────────────────────────────

QString Source::label() const
{
    return machine.isEmpty() ? name : QStringLiteral("%1 (%2)").arg(name, machine);
}

// ── Session ──────────────────────────────────────────────────────────────────

Session::Session() : m_workPath(newWorkPath(sessionsFolder()))
{
    m_lock = std::make_unique<QLockFile>(m_workPath + QStringLiteral(".lock"));
    m_lock->tryLock(0);
}

Session::~Session()
{
    m_main.close();
    removeDatabaseFiles(m_workPath);
    m_lock->unlock();
}

QString Session::sessionsFolder()
{
    const QString folder = dataFolder() + QStringLiteral("/sessions");
    QDir().mkpath(folder);
    return folder;
}

void Session::cleanOrphanSessions()
{
    const QDir folder(sessionsFolder());
    const QStringList databases = folder.entryList({QStringLiteral("session-*.db")}, QDir::Files);
    for (const QString& name : databases) {
        const QString path = folder.filePath(name);
        QLockFile lock(path + QStringLiteral(".lock"));
        if (lock.tryLock(0)) {
            removeDatabaseFiles(path);
            lock.unlock();
        }
    }
}

std::unique_ptr<Session> Session::create(const QByteArray& timeZone)
{
    std::unique_ptr<Session> session(new Session);
    sql::Connection& connection = session->m_main;
    connection = sql::Connection::open(session->m_workPath);
    connection.script(QStringLiteral("PRAGMA page_size = 16384;"));
    db::configure(connection, false);
    db::initializeSchema(connection);
    db::createSecondaryIndexes(connection);
    db::writeMeta(connection, QLatin1String(KEY_VERSION), QString::number(schema::VERSION));
    db::writeMeta(connection, QLatin1String(KEY_TIME_ZONE),
                  QString::fromLatin1(isValidTimeZone(timeZone) ? timeZone : systemTimeZone()));
    db::writeMeta(connection, QStringLiteral("application"), QStringLiteral(LOGHUNTER_NAME_VERSION));
    db::writeMeta(connection, QStringLiteral("created_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return session;
}

void Session::closeConnection()
{
    m_main.close();
}

void Session::reopenConnection()
{
    m_main = db::open(m_workPath, sql::Connection::ReadWrite);
}

std::unique_ptr<Session> Session::openProject(const QString& path, const std::function<void(double)>& progress)
{
    if (!QFileInfo::exists(path))
        throw std::runtime_error(qtTrId("common.file_not_found").toStdString());
    if (!isSqliteFile(path))
        throw std::runtime_error(qtTrId("project.error_not_sqlite").toStdString());

    std::unique_ptr<Session> session(new Session);
    copyDatabase(path, session->m_workPath, progress);

    sql::Connection& connection = session->m_main;
    connection = db::open(session->m_workPath, sql::Connection::ReadWrite);
    if (connection.text(QStringLiteral("PRAGMA quick_check(1)")) != QLatin1String("ok"))
        throw std::runtime_error(qtTrId("project.error_integrity").toStdString());
    if (!connection.tableExists(QStringLiteral("events")) || !connection.tableExists(QStringLiteral("meta")))
        throw std::runtime_error(qtTrId("project.error_table_missing").toStdString());
    const int version = db::readMeta(connection, QLatin1String(KEY_VERSION), QStringLiteral("0")).toInt();
    if (version > schema::VERSION)
        throw std::runtime_error(qtTrId("project.error_newer_version").toStdString());
    if (version < schema::VERSION)
        throw std::runtime_error(qtTrId("project.error_table_missing").toStdString());

    db::createSecondaryIndexes(connection);
    session->m_projectPath = QFileInfo(path).absoluteFilePath();
    return session;
}

void Session::saveProject(const QString& workPath, const QString& destination)
{
    const QString temporary = QStringLiteral("%1.tmp-%2").arg(destination).arg(QRandomGenerator::global()->generate(), 8,
                                                                                 16, QLatin1Char('0'));
    removeDatabaseFiles(temporary);
    try {
        {
            sql::Connection connection = sql::Connection::open(workPath);
            connection.execute(QStringLiteral("VACUUM INTO ?"), {temporary});
        }
        {
            sql::Connection copy = sql::Connection::open(temporary);
            copy.script(QStringLiteral("PRAGMA journal_mode = DELETE;"));
        }
        std::error_code error;
        std::filesystem::rename(nativePath(temporary), nativePath(destination), error);
        if (error)
            throw std::runtime_error(qtTrId("project.error_replace").toStdString());
    } catch (...) {
        removeDatabaseFiles(temporary);
        throw;
    }
}

QByteArray Session::timeZone()
{
    return db::readMeta(m_main, QLatin1String(KEY_TIME_ZONE), QString::fromLatin1(systemTimeZone())).toLatin1();
}

void Session::setTimeZone(const QByteArray& timeZone)
{
    db::writeMeta(m_main, QLatin1String(KEY_TIME_ZONE), QString::fromLatin1(timeZone));
    g_timeZoneGeneration.fetch_add(1, std::memory_order_release);
}

// ── db:: ───────────────────────────────────────────────────────────────────

namespace db {

sql::Connection open(const QString& path, sql::Connection::Mode mode)
{
    sql::Connection connection = sql::Connection::open(path, mode);
    configure(connection, mode == sql::Connection::Read);
    return connection;
}

void configure(sql::Connection& connection, bool readOnly)
{
    QString pragmas = QStringLiteral(
        "PRAGMA temp_store = MEMORY;"
        "PRAGMA mmap_size = 1073741824;"
        "PRAGMA foreign_keys = OFF;");
    if (readOnly) {
        pragmas += QStringLiteral("PRAGMA cache_size = -65536; PRAGMA query_only = ON;");
    } else {
        pragmas += QStringLiteral(
            "PRAGMA journal_mode = WAL;"
            "PRAGMA synchronous = OFF;"
            "PRAGMA cache_size = -262144;"
            "PRAGMA threads = %1;")
                       .arg(std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, 8));
    }
    connection.script(pragmas);
    sqlite3_create_function_v2(connection.handle(), "lh_timestamp", 1, SQLITE_UTF8 | SQLITE_INNOCUOUS,
                               new TimestampState, timestampFunction, nullptr, nullptr,
                               [](void* state) { delete static_cast<TimestampState*>(state); });
    constexpr int PURE = SQLITE_UTF8 | SQLITE_DETERMINISTIC | SQLITE_INNOCUOUS;
    sqlite3_create_function_v2(connection.handle(), "lh_ip_key", 1, PURE, nullptr, ipKeyFunction, nullptr, nullptr,
                               nullptr);
    sqlite3_create_function_v2(connection.handle(), "lh_text_key", 1, PURE, nullptr, textKeyFunction, nullptr,
                               nullptr, nullptr);
    sqlite3_create_function_v2(connection.handle(), "lh_file_name", 1, PURE, nullptr, fileNameFunction, nullptr,
                               nullptr, nullptr);
}

void initializeSchema(sql::Connection& connection)
{
    connection.script(schema::SQL_TABLES);
    connection.script(schema::SQL_VIEWS);
}

QString readMeta(sql::Connection& connection, const QString& key, const QString& fallback)
{
    sql::Statement statement = connection.prepare(QStringLiteral("SELECT value FROM meta WHERE key = ?"));
    statement.bind(1, key);
    return statement.step() ? statement.text(0) : fallback;
}

void writeMeta(sql::Connection& connection, const QString& key, const QString& value)
{
    connection.execute(QStringLiteral("INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)"), {key, value});
}

QList<Source> sources(sql::Connection& connection)
{
    QList<Source> list;
    sql::Statement statement = connection.prepare(QStringLiteral(
        "SELECT id, path, name, fingerprint, size, imported_at, record_count, event_count, duplicate_count, "
        "machine FROM sources ORDER BY imported_at, id"));
    while (statement.step()) {
        Source source;
        source.id = statement.integer(0);
        source.path = statement.text(1);
        source.name = statement.text(2);
        source.fingerprint = statement.text(3);
        source.size = statement.integer(4);
        source.importedAt = QDateTime::fromString(statement.text(5), Qt::ISODate);
        source.records = statement.integer(6);
        source.events = statement.integer(7);
        source.duplicates = statement.integer(8);
        source.machine = statement.text(9);
        list << source;
    }
    return list;
}

bool fingerprintKnown(sql::Connection& connection, const QString& fingerprint)
{
    return connection.integer(QStringLiteral("SELECT COUNT(*) FROM sources WHERE fingerprint = ?"), {fingerprint}) > 0;
}

void removeSource(sql::Connection& connection, qint64 identifier)
{
    sql::Transaction transaction(connection);
    connection.execute(QStringLiteral("DELETE FROM matches WHERE event IN "
                                      "(SELECT id FROM events WHERE source_id = ?)"),
                       {identifier});
    connection.script(QStringLiteral("DROP TABLE IF EXISTS temp.removed;"));
    connection.execute(QStringLiteral("CREATE TEMP TABLE removed AS "
                                      "SELECT group_id, COUNT(*) AS n FROM events WHERE source_id = ? GROUP BY group_id"),
                       {identifier});
    connection.execute(QStringLiteral("DELETE FROM events WHERE source_id = ?"), {identifier});
    connection.execute(QStringLiteral("DELETE FROM blocks WHERE source_id = ?"), {identifier});
    connection.execute(QStringLiteral("DELETE FROM sources WHERE id = ?"), {identifier});
    connection.script(QStringLiteral(
        "UPDATE groups SET total = total - r.n FROM removed r WHERE r.group_id = groups.group_id;"
        "DELETE FROM search WHERE rowid IN (SELECT group_id FROM groups WHERE total <= 0);"
        "DELETE FROM groups WHERE total <= 0;"
        "UPDATE groups SET first_ts = (SELECT MIN(ts_utc) FROM events e WHERE e.group_id = groups.group_id), "
        "last_ts = (SELECT MAX(ts_utc) FROM events e WHERE e.group_id = groups.group_id) "
        "WHERE group_id IN (SELECT group_id FROM removed);"
        "DROP TABLE removed;"));
    transaction.commit();
}

void dropSecondaryIndexes(sql::Connection& connection)
{
    QString script;
    for (const auto& index : schema::SECONDARY_INDEXES)
        script += QStringLiteral("DROP INDEX IF EXISTS %1;").arg(QLatin1String(index.name));
    connection.script(script);
}

void createSecondaryIndexes(sql::Connection& connection)
{
    QString script;
    for (const auto& index : schema::SECONDARY_INDEXES)
        script += QStringLiteral("CREATE %1INDEX IF NOT EXISTS %2 ON %3;")
                      .arg(index.unique ? QStringLiteral("UNIQUE ") : QString(), QLatin1String(index.name),
                           QLatin1String(index.target));
    connection.script(script + QStringLiteral("PRAGMA optimize;"));
}

qint64 eventCount(sql::Connection& connection)
{
    return connection.integer(QStringLiteral("SELECT COUNT(*) FROM events"));
}

void recomputeTimeZone(sql::Connection& connection, const QByteArray& timeZone)
{
    TimeZoneConverter converter(timeZone);
    sqlite3_create_function_v2(connection.handle(), "lh_day", 1, SQLITE_UTF8, &converter, dayFunction, nullptr,
                               nullptr, nullptr);
    sqlite3_create_function_v2(connection.handle(), "lh_hour", 1, SQLITE_UTF8, &converter, hourFunction, nullptr,
                               nullptr, nullptr);
    sql::Transaction transaction(connection);
    connection.script(QStringLiteral(
        "UPDATE events SET day = lh_day(ts_utc), hour = lh_hour(ts_utc) WHERE ts_utc IS NOT NULL;"));
    writeMeta(connection, QLatin1String(KEY_TIME_ZONE), QString::fromLatin1(converter.identifier()));
    transaction.commit();
    g_timeZoneGeneration.fetch_add(1, std::memory_order_release);
    for (const char* name : {"lh_day", "lh_hour"})
        sqlite3_create_function_v2(connection.handle(), name, 1, SQLITE_UTF8, nullptr, nullptr, nullptr, nullptr,
                                   nullptr);
}

} // namespace db

} // namespace lh
