#include "Sqlite.h"

#include <sqlite3.h>

#include <QByteArray>

#include <utility>

namespace lh::sql {

// ── Error ────────────────────────────────────────────────────────────────────

Error::Error(int code, const QString& message) : std::runtime_error(message.toStdString()), m_code(code) {}

Error Error::interruption()
{
    return Error(SQLITE_INTERRUPT, QString::fromUtf8(sqlite3_errstr(SQLITE_INTERRUPT)));
}

QString Error::message() const
{
    return QString::fromUtf8(what());
}

bool Error::isInterruption() const noexcept
{
    return m_code == SQLITE_INTERRUPT;
}

namespace {

[[noreturn]] void throwError(sqlite3* db, int code)
{
    const char* detail = db ? sqlite3_errmsg(db) : sqlite3_errstr(code);
    throw Error(code, QString::fromUtf8(detail));
}

} // namespace

// ── Statement ────────────────────────────────────────────────────────────────

Statement::~Statement()
{
    if (m_stmt)
        sqlite3_finalize(m_stmt);
}

Statement::Statement(Statement&& other) noexcept
    : m_stmt(std::exchange(other.m_stmt, nullptr)), m_db(other.m_db)
{
}

Statement& Statement::operator=(Statement&& other) noexcept
{
    if (this != &other) {
        if (m_stmt)
            sqlite3_finalize(m_stmt);
        m_stmt = std::exchange(other.m_stmt, nullptr);
        m_db = other.m_db;
    }
    return *this;
}

void Statement::check(int code) const
{
    if (code != SQLITE_OK)
        throwError(m_db, code);
}

Statement& Statement::bind(int index, std::nullptr_t)
{
    check(sqlite3_bind_null(m_stmt, index));
    return *this;
}

Statement& Statement::bind(int index, qint64 value)
{
    check(sqlite3_bind_int64(m_stmt, index, value));
    return *this;
}

Statement& Statement::bind(int index, double value)
{
    check(sqlite3_bind_double(m_stmt, index, value));
    return *this;
}

Statement& Statement::bind(int index, QStringView value)
{
    static constexpr char16_t EMPTY[] = u"";
    const void* data = value.isNull() ? static_cast<const void*>(EMPTY) : static_cast<const void*>(value.utf16());
    check(
        sqlite3_bind_text16(m_stmt, index, data, static_cast<int>(value.size() * 2), SQLITE_TRANSIENT));
    return *this;
}

Statement& Statement::bindUtf8(int index, const QByteArray& utf8)
{
    check(sqlite3_bind_text(m_stmt, index, utf8.constData(), static_cast<int>(utf8.size()), SQLITE_STATIC));
    return *this;
}

Statement& Statement::bindBlob(int index, const QByteArray& bytes)
{
    if (bytes.isEmpty())
        return bind(index, nullptr);
    check(sqlite3_bind_blob(m_stmt, index, bytes.constData(), static_cast<int>(bytes.size()),
                            SQLITE_STATIC));
    return *this;
}

Statement& Statement::bind(int index, const QVariant& value)
{
    if (value.isNull())
        return bind(index, nullptr);
    switch (value.typeId()) {
    case QMetaType::Bool:
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Long:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        return bind(index, value.toLongLong());
    case QMetaType::Double:
    case QMetaType::Float:
        return bind(index, value.toDouble());
    default:
        return bind(index, value.toString());
    }
}

Statement& Statement::bindAll(const QVariantList& values, int firstIndex)
{
    for (qsizetype i = 0; i < values.size(); ++i)
        bind(firstIndex + static_cast<int>(i), values[i]);
    return *this;
}

bool Statement::step()
{
    const int code = sqlite3_step(m_stmt);
    if (code == SQLITE_ROW)
        return true;
    if (code == SQLITE_DONE)
        return false;
    throwError(m_db, code);
}

void Statement::execute()
{
    while (step()) {
    }
}

void Statement::reset()
{
    sqlite3_reset(m_stmt);
}

int Statement::columnCount() const
{
    return sqlite3_column_count(m_stmt);
}

bool Statement::isNull(int column) const
{
    return sqlite3_column_type(m_stmt, column) == SQLITE_NULL;
}

qint64 Statement::integer(int column) const
{
    return sqlite3_column_int64(m_stmt, column);
}

double Statement::real(int column) const
{
    return sqlite3_column_double(m_stmt, column);
}

QString Statement::text(int column) const
{
    const auto* data = reinterpret_cast<const char*>(sqlite3_column_text(m_stmt, column));
    if (!data)
        return {};
    return QString::fromUtf8(data, sqlite3_column_bytes(m_stmt, column));
}

QByteArrayView Statement::bytes(int column) const
{
    const auto* data = reinterpret_cast<const char*>(sqlite3_column_text(m_stmt, column));
    return data ? QByteArrayView(data, sqlite3_column_bytes(m_stmt, column)) : QByteArrayView();
}

QByteArray Statement::blob(int column) const
{
    const auto* data = static_cast<const char*>(sqlite3_column_blob(m_stmt, column));
    if (!data)
        return {};
    return QByteArray(data, sqlite3_column_bytes(m_stmt, column));
}

QVariant Statement::value(int column) const
{
    switch (sqlite3_column_type(m_stmt, column)) {
    case SQLITE_INTEGER:
        return QVariant::fromValue<qint64>(integer(column));
    case SQLITE_FLOAT:
        return real(column);
    case SQLITE_NULL:
        return {};
    default:
        return text(column);
    }
}

// ── Connection ───────────────────────────────────────────────────────────────

Connection::~Connection()
{
    close();
}

Connection::Connection(Connection&& other) noexcept : m_db(std::exchange(other.m_db, nullptr)) {}

Connection& Connection::operator=(Connection&& other) noexcept
{
    if (this != &other) {
        close();
        m_db = std::exchange(other.m_db, nullptr);
    }
    return *this;
}

Connection Connection::open(const QString& path, Mode mode)
{
    sqlite3* db = nullptr;
    const int flags = (mode == Read ? SQLITE_OPEN_READONLY : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE))
                      | SQLITE_OPEN_NOMUTEX | SQLITE_OPEN_EXRESCODE | SQLITE_OPEN_URI;
    const QByteArray utf8Path = path.toUtf8();
    const int code = sqlite3_open_v2(utf8Path.constData(), &db, flags, nullptr);
    if (code != SQLITE_OK) {
        const QString message = QString::fromUtf8(db ? sqlite3_errmsg(db) : sqlite3_errstr(code));
        sqlite3_close_v2(db);
        throw Error(code, message);
    }
    Connection connection(db);
    sqlite3_db_config(db, SQLITE_DBCONFIG_TRUSTED_SCHEMA, 0, nullptr);
    sqlite3_db_config(db, SQLITE_DBCONFIG_DEFENSIVE, 1, nullptr);
    sqlite3_busy_timeout(db, 10'000);
    return connection;
}

void Connection::close()
{
    if (m_db) {
        sqlite3_close_v2(m_db);
        m_db = nullptr;
    }
}

void Connection::script(const QString& sql)
{
    if (!m_db)
        throwError(nullptr, SQLITE_MISUSE);
    char* error = nullptr;
    const QByteArray utf8 = sql.toUtf8();
    const int code = sqlite3_exec(m_db, utf8.constData(), nullptr, nullptr, &error);
    if (code != SQLITE_OK) {
        const QString message = QString::fromUtf8(error ? error : sqlite3_errstr(code));
        sqlite3_free(error);
        throw Error(code, message);
    }
}

Statement Connection::prepare(const QString& sql)
{
    if (!m_db)
        throwError(nullptr, SQLITE_MISUSE);
    sqlite3_stmt* stmt = nullptr;
    const int code = sqlite3_prepare16_v3(m_db, sql.utf16(), static_cast<int>(sql.size() * 2), 0, &stmt, nullptr);
    if (code != SQLITE_OK)
        throwError(m_db, code);
    return Statement(stmt, m_db);
}

qint64 Connection::integer(const QString& sql, const QVariantList& parameters)
{
    Statement statement = prepare(sql);
    statement.bindAll(parameters);
    return statement.step() ? statement.integer(0) : 0;
}

QString Connection::text(const QString& sql, const QVariantList& parameters)
{
    Statement statement = prepare(sql);
    statement.bindAll(parameters);
    return statement.step() ? statement.text(0) : QString();
}

void Connection::execute(const QString& sql, const QVariantList& parameters)
{
    Statement statement = prepare(sql);
    statement.bindAll(parameters);
    statement.execute();
}

qint64 Connection::lastIdentifier() const
{
    return sqlite3_last_insert_rowid(m_db);
}

qint64 Connection::changes() const
{
    return sqlite3_changes64(m_db);
}

bool Connection::tableExists(const QString& name)
{
    return integer(QStringLiteral("SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = ?"), {name}) > 0;
}

void Connection::interrupt() const noexcept
{
    if (m_db)
        sqlite3_interrupt(m_db);
}

// ── Transaction ──────────────────────────────────────────────────────────────

Transaction::Transaction(Connection& connection, bool immediate) : m_connection(connection)
{
    m_connection.script(immediate ? QStringLiteral("BEGIN IMMEDIATE") : QStringLiteral("BEGIN"));
}

Transaction::~Transaction()
{
    if (m_active) {
        try {
            m_connection.script(QStringLiteral("ROLLBACK"));
        } catch (...) {
        }
    }
}

void Transaction::commit()
{
    m_connection.script(QStringLiteral("COMMIT"));
    m_active = false;
}

QString escapeLike(const QString& text)
{
    QString result;
    result.reserve(text.size() + 4);
    for (const QChar c : text) {
        if (c == u'\\' || c == u'%' || c == u'_')
            result.append(u'\\');
        result.append(c);
    }
    return result;
}

} // namespace lh::sql
