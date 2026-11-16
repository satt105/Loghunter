// Working session and LogHunter projects.
#pragma once

#include "Sqlite.h"

#include <QDateTime>
#include <QList>
#include <QLockFile>
#include <QString>

#include <functional>
#include <memory>

namespace lh {

QString dataFolder();

struct Source
{
    qint64 id = 0;
    QString path;
    QString name;
    QString fingerprint;
    qint64 size = 0;
    QDateTime importedAt;
    qint64 records = 0;
    qint64 events = 0;
    qint64 duplicates = 0;
    QString machine;

    [[nodiscard]] QString label() const;
};

class Session
{
public:
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    static std::unique_ptr<Session> create(const QByteArray& timeZone);
    static std::unique_ptr<Session> openProject(const QString& path,
                                                const std::function<void(double)>& progress = {});
    static void saveProject(const QString& workPath, const QString& destination);

    static void cleanOrphanSessions();

    [[nodiscard]] const QString& workPath() const noexcept { return m_workPath; }
    [[nodiscard]] const QString& projectPath() const noexcept { return m_projectPath; }
    void setProjectPath(const QString& path) { m_projectPath = path; }
    [[nodiscard]] bool isModified() const noexcept { return m_modified; }
    void markModified(bool modified = true) { m_modified = modified; }

    [[nodiscard]] QByteArray timeZone();
    void setTimeZone(const QByteArray& timeZone);

    [[nodiscard]] sql::Connection& mainConnection() noexcept { return m_main; }
    void closeConnection();
    void reopenConnection();

private:
    Session();
    static QString sessionsFolder();

    QString m_workPath;
    QString m_projectPath;
    std::unique_ptr<QLockFile> m_lock;
    sql::Connection m_main;
    bool m_modified = false;
};

// ── Database access ──────────────────────────────────────────────────────────
namespace db {

sql::Connection open(const QString& path, sql::Connection::Mode mode = sql::Connection::Read);
void configure(sql::Connection& connection, bool readOnly);
void initializeSchema(sql::Connection& connection);
QString readMeta(sql::Connection& connection, const QString& key, const QString& fallback = {});
void writeMeta(sql::Connection& connection, const QString& key, const QString& value);

QList<Source> sources(sql::Connection& connection);
bool fingerprintKnown(sql::Connection& connection, const QString& fingerprint);
void removeSource(sql::Connection& connection, qint64 identifier);

void dropSecondaryIndexes(sql::Connection& connection);
void createSecondaryIndexes(sql::Connection& connection);
qint64 eventCount(sql::Connection& connection);

void recomputeTimeZone(sql::Connection& connection, const QByteArray& timeZone);

} // namespace db

} // namespace lh
