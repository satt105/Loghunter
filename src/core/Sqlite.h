// Minimal RAII wrapper around the SQLite C API.
#pragma once

#include <QByteArrayView>
#include <QString>
#include <QVariant>

#include <cstddef>
#include <stdexcept>

struct sqlite3;
struct sqlite3_stmt;

namespace lh::sql {

class Error : public std::runtime_error
{
public:
    Error(int code, const QString& message);
    static Error interruption();
    [[nodiscard]] int code() const noexcept { return m_code; }
    [[nodiscard]] QString message() const;
    [[nodiscard]] bool isInterruption() const noexcept;

private:
    int m_code;
};

class Connection;

class Statement
{
public:
    Statement() = default;
    Statement(sqlite3_stmt* stmt, sqlite3* db) noexcept : m_stmt(stmt), m_db(db) {}
    ~Statement();
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement& bind(int index, std::nullptr_t);
    Statement& bind(int index, qint64 value);
    Statement& bind(int index, int value) { return bind(index, static_cast<qint64>(value)); }
    Statement& bind(int index, double value);
    Statement& bind(int index, QStringView value);
    Statement& bind(int index, const QString& value) { return bind(index, QStringView(value)); }
    Statement& bind(int index, const QVariant& value);
    Statement& bindUtf8(int index, const QByteArray& utf8);
    Statement& bindBlob(int index, const QByteArray& bytes);
    Statement& bindAll(const QVariantList& values, int firstIndex = 1);

    bool step();
    void execute();
    void reset();

    [[nodiscard]] int columnCount() const;
    [[nodiscard]] bool isNull(int column) const;
    [[nodiscard]] qint64 integer(int column) const;
    [[nodiscard]] double real(int column) const;
    [[nodiscard]] QString text(int column) const;
    [[nodiscard]] QByteArrayView bytes(int column) const;
    [[nodiscard]] QByteArray blob(int column) const;
    [[nodiscard]] QVariant value(int column) const;

private:
    void check(int code) const;

    sqlite3_stmt* m_stmt = nullptr;
    sqlite3* m_db = nullptr;
};

class Connection
{
public:
    enum Mode { ReadWrite, Read };

    Connection() = default;
    ~Connection();
    Connection(Connection&& other) noexcept;
    Connection& operator=(Connection&& other) noexcept;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    static Connection open(const QString& path, Mode mode = ReadWrite);

    [[nodiscard]] bool isValid() const noexcept { return m_db != nullptr; }
    [[nodiscard]] sqlite3* handle() const noexcept { return m_db; }
    void close();

    void script(const QString& sql);
    [[nodiscard]] Statement prepare(const QString& sql);

    qint64 integer(const QString& sql, const QVariantList& parameters = {});
    QString text(const QString& sql, const QVariantList& parameters = {});
    void execute(const QString& sql, const QVariantList& parameters = {});

    [[nodiscard]] qint64 lastIdentifier() const;
    [[nodiscard]] qint64 changes() const;
    [[nodiscard]] bool tableExists(const QString& name);

    void interrupt() const noexcept;

private:
    explicit Connection(sqlite3* db) noexcept : m_db(db) {}
    sqlite3* m_db = nullptr;
};

class Transaction
{
public:
    explicit Transaction(Connection& connection, bool immediate = true);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    void commit();

private:
    Connection& m_connection;
    bool m_active = true;
};

QString escapeLike(const QString& text);

} // namespace lh::sql
