#include "Import.h"

#include "Schema.h"
#include "Session.h"
#include "logs/Log.h"

#include <QDateTime>
#include <QDebug>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFuture>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent>

#include <algorithm>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <vector>

namespace lh {

namespace {

constexpr int BATCHES_PER_WINDOW = 64;
constexpr qint64 INDEX_DROP_THRESHOLD = 200'000;

struct PreparedRow
{
    EventRow row;
    bool file = false;
    std::vector<int> rules;
};

struct BatchResult
{
    std::vector<PreparedRow> rows;
    QByteArray block;
    qint64 readCount = 0;
    qint64 errors = 0;
};

BatchResult decodeBatch(const logs::Log& log, int index, const QByteArray& timeZone,
                        const sigma::Evaluator* evaluator)
{
    thread_local QByteArray currentTimeZone;
    thread_local std::unique_ptr<TimeZoneConverter> converter;
    if (!converter || currentTimeZone != timeZone) {
        converter = std::make_unique<TimeZoneConverter>(timeZone);
        currentTimeZone = timeZone;
    }

    BatchResult result;
    logs::Batch batch = log.decodeBatch(index, [&](const evtx::DecodedEvent& event) {
        PreparedRow preparedRow;
        preparedRow.file = AccessAnalyzer::prepare(event, *converter, preparedRow.row)
                           == AccessAnalyzer::Preparation::File;
        preparedRow.row.signature = eventSignature(preparedRow.row);
        if (evaluator)
            evaluator->evaluate(event.eventId, event.channel, event.provider, event.machine,
                                event.data, preparedRow.rules);
        result.rows.push_back(std::move(preparedRow));
        return true;
    });
    result.readCount = batch.readCount;
    result.errors = batch.errors;
    result.block = std::move(batch.block);
    return result;
}

void insertPairs(sql::Connection& connection, const QString& sql, const QHash<QString, QString>& pairs)
{
    sql::Statement insert = connection.prepare(sql);
    for (auto it = pairs.cbegin(); it != pairs.cend(); ++it) {
        insert.reset();
        insert.bind(1, it.key());
        insert.bind(2, it.value());
        insert.execute();
    }
}

void reindexSearch(sql::Connection& connection, const QString& groupTable)
{
    connection.script(QStringLiteral("DELETE FROM search WHERE rowid IN (SELECT group_id FROM %1);"
                                     "INSERT INTO search (rowid, text) SELECT group_id, %2 FROM groups "
                                     "WHERE group_id IN (SELECT group_id FROM %1);"
                                     "DROP TABLE %1;")
                          .arg(groupTable, schema::SQL_SEARCH_TEXT));
}

void applyVolumes(sql::Connection& connection, const AccessAnalyzer::Volumes& volumes)
{
    sql::Transaction transaction(connection);
    insertPairs(connection, QStringLiteral("INSERT OR IGNORE INTO volumes (key, letter) VALUES (?, ?)"), volumes);
    std::vector<std::pair<qint64, QString>> renames;
    {
        sql::Statement reader = connection.prepare(QStringLiteral(
            "SELECT group_id, machine, file_path FROM groups WHERE file_path LIKE '\\Device\\HarddiskVolume%'"));
        while (reader.step())
            if (auto path = AccessAnalyzer::convertVolume(volumes, reader.text(1), reader.text(2)))
                renames.emplace_back(reader.integer(0), std::move(*path));
    }
    if (!renames.empty()) {
        connection.script(QStringLiteral("CREATE TEMP TABLE volume_groups (group_id INTEGER PRIMARY KEY);"));
        {
            sql::Statement updatePath =
                connection.prepare(QStringLiteral("UPDATE groups SET file_path = ?, dir_path = ? WHERE group_id = ?"));
            sql::Statement markGroup = connection.prepare(QStringLiteral("INSERT INTO volume_groups VALUES (?)"));
            for (const auto& [group, path] : renames) {
                updatePath.reset();
                updatePath.bind(1, path);
                updatePath.bind(2, parentFolder(path));
                updatePath.bind(3, group);
                updatePath.execute();
                markGroup.reset();
                markGroup.bind(1, group);
                markGroup.execute();
            }
        }
        reindexSearch(connection, QStringLiteral("volume_groups"));
    }
    transaction.commit();
}

void applyAccountNames(sql::Connection& connection, const QHash<QString, QString>& namesBySid)
{
    sql::Transaction transaction(connection);
    connection.script(QStringLiteral("CREATE TEMP TABLE sid_names (sid TEXT PRIMARY KEY, name TEXT);"));
    insertPairs(connection, QStringLiteral("INSERT OR IGNORE INTO sid_names VALUES (?, ?)"), namesBySid);
    connection.script(QStringLiteral("CREATE TEMP TABLE sid_groups AS SELECT group_id FROM groups "
                                     "WHERE user LIKE 'S-1-5-21-%' AND user IN (SELECT sid FROM sid_names);"
                                     "UPDATE groups SET user = (SELECT name FROM sid_names WHERE sid = groups.user) "
                                     "WHERE group_id IN (SELECT group_id FROM sid_groups);"));
    reindexSearch(connection, QStringLiteral("sid_groups"));
    connection.script(QStringLiteral("DROP TABLE sid_names;"));
    transaction.commit();
}

struct Window
{
    std::vector<int> indices;
    std::vector<BatchResult> results;
    QFuture<void> tasks;
};

struct Tally
{
    qint64 count = 0;
    qint64 first = std::numeric_limits<qint64>::max();
    qint64 last = UNKNOWN_TIMESTAMP;
};

struct PreparedFile
{
    QString path;
    QString fingerprint;
    qint64 size = 0;
    qint64 estimation = 0;
    QString error;
    bool duplicate = false;
};

class Writer
{
public:
    explicit Writer(sql::Connection& connection) : m_connection(connection)
    {
        sql::Statement groups = connection.prepare(QStringLiteral("SELECT signature, group_id FROM groups"));
        while (groups.step())
            m_groups.insert(groups.integer(0), groups.integer(1));
        sql::Statement logs = connection.prepare(QStringLiteral("SELECT machine, channel, id FROM logs"));
        while (logs.step())
            m_logs.insert({logs.text(0), logs.text(1)}, logs.integer(2));
        m_nextGroup = connection.integer(QStringLiteral("SELECT COALESCE(MAX(group_id), 0) + 1 FROM groups"));
        m_event = connection.prepare(schema::SQL_INSERT_EVENT);
        m_group = connection.prepare(schema::SQL_INSERT_GROUP);
        m_search = connection.prepare(schema::SQL_INDEX_SEARCH);
        m_block = connection.prepare(schema::SQL_INSERT_BLOCK);
        m_log = connection.prepare(QStringLiteral("INSERT INTO logs (machine, channel) VALUES (?, ?)"));
        m_match =
            connection.prepare(QStringLiteral("INSERT OR IGNORE INTO matches (rule, event) VALUES (?, ?)"));
        m_total = connection.prepare(QStringLiteral(
            "UPDATE groups SET total = total + ?1, "
            "first_ts = CASE WHEN ?2 IS NULL THEN first_ts WHEN first_ts IS NULL OR ?2 < first_ts THEN ?2 ELSE first_ts END, "
            "last_ts = CASE WHEN ?3 IS NULL THEN last_ts WHEN last_ts IS NULL OR ?3 > last_ts THEN ?3 ELSE last_ts END "
            "WHERE group_id = ?4"));
    }

    void beginFile()
    {
        m_nextBlock = m_connection.integer(QStringLiteral("SELECT COALESCE(MAX(id), 0) + 1 FROM blocks"));
        m_nextGroupStart = m_nextGroup;
        m_newSignatures.clear();
        m_newLogs.clear();
        m_totals.clear();
    }

    void abandonFile()
    {
        for (const qint64 signature : std::as_const(m_newSignatures))
            m_groups.remove(signature);
        for (const LogKey& key : std::as_const(m_newLogs))
            m_logs.remove(key);
        m_lastLog = 0;
        m_nextGroup = m_nextGroupStart;
    }

    void finishFile()
    {
        if (m_nextGroup > m_nextGroupStart) {
            m_search.reset();
            m_search.bind(1, m_nextGroupStart);
            m_search.execute();
        }
        std::vector<qint64> groups;
        groups.reserve(static_cast<size_t>(m_totals.size()));
        for (auto it = m_totals.cbegin(); it != m_totals.cend(); ++it)
            groups.push_back(it.key());
        std::sort(groups.begin(), groups.end());
        for (const qint64 group : groups) {
            const Tally tally = m_totals.value(group);
            const bool date = tally.first <= tally.last;
            m_total.reset();
            m_total.bind(1, tally.count);
            if (date) {
                m_total.bind(2, tally.first);
                m_total.bind(3, tally.last);
            } else {
                m_total.bind(2, nullptr);
                m_total.bind(3, nullptr);
            }
            m_total.bind(4, group);
            m_total.execute();
        }
    }

    qint64 nextBlock() const { return m_nextBlock; }

    void writeBlock(qint64 block, qint64 source, const QByteArray& data)
    {
        m_block.reset();
        m_block.bind(1, block);
        m_block.bind(2, source);
        m_block.bindBlob(3, data);
        m_block.execute();
        ++m_nextBlock;
    }

    std::optional<qint64> write(const EventRow& row, qint64 source, qint64 block, qint64 rank)
    {
        const auto found = m_groups.constFind(row.signature);
        const bool isNew = found == m_groups.cend();
        const qint64 group = isNew ? m_nextGroup : found.value();
        m_event.reset();
        m_event.bind(1, group);
        if (row.tsUtc == UNKNOWN_TIMESTAMP || row.local.hour < 0) {
            m_event.bind(2, nullptr);
            m_event.bind(3, nullptr);
        } else {
            m_event.bind(2, static_cast<qint64>(row.tsUtc));
            m_event.bind(3, row.local.day);
        }
        m_event.bind(4, row.local.hour);
        m_event.bind(5, source);
        m_event.bind(6, log(row.machine, row.channel));
        m_event.bind(7, static_cast<qint64>(row.recordId));
        m_event.bind(8, block);
        m_event.bind(9, rank);
        m_event.execute();
        if (m_connection.changes() == 0)
            return std::nullopt;
        const qint64 identifier = m_connection.lastIdentifier();
        if (isNew)
            createGroup(group, row);
        Tally& tally = m_totals[group];
        ++tally.count;
        if (row.tsUtc != UNKNOWN_TIMESTAMP && row.local.hour >= 0) {
            tally.first = std::min<qint64>(tally.first, row.tsUtc);
            tally.last = std::max<qint64>(tally.last, row.tsUtc);
        }
        return identifier;
    }

    void writeMatch(qint64 rule, qint64 event)
    {
        m_match.reset();
        m_match.bind(1, rule);
        m_match.bind(2, event);
        m_match.execute();
    }

    void release()
    {
        m_event = sql::Statement();
        m_group = sql::Statement();
        m_search = sql::Statement();
        m_block = sql::Statement();
        m_log = sql::Statement();
        m_match = sql::Statement();
        m_total = sql::Statement();
    }

private:
    using LogKey = QPair<QString, QString>;

    qint64 log(const QString& machine, const QString& channel)
    {
        if (m_lastLog > 0 && machine == m_lastLogKey.first && channel == m_lastLogKey.second)
            return m_lastLog;
        m_lastLogKey = {machine, channel};
        const auto found = m_logs.constFind(m_lastLogKey);
        if (found != m_logs.cend()) {
            m_lastLog = found.value();
            return m_lastLog;
        }
        m_log.reset();
        m_log.bind(1, machine);
        m_log.bind(2, channel);
        m_log.execute();
        m_lastLog = m_connection.lastIdentifier();
        m_logs.insert(m_lastLogKey, m_lastLog);
        m_newLogs << m_lastLogKey;
        return m_lastLog;
    }

    void createGroup(qint64 group, const EventRow& l)
    {
        m_group.reset();
        m_group.bind(1, group);
        m_group.bind(2, l.signature);
        m_group.bind(3, static_cast<qint64>(l.eventId));
        m_group.bind(4, l.level);
        m_group.bind(5, l.audit);
        m_group.bind(6, l.channel);
        m_group.bind(7, l.provider);
        m_group.bind(8, l.machine);
        m_group.bind(9, l.user);
        m_group.bind(10, l.isSystem ? 1 : 0);
        m_group.bind(11, l.process);
        m_group.bind(12, l.action);
        m_group.bind(13, QString::fromLatin1(l.category));
        m_group.bind(14, l.file);
        m_group.bind(15, l.folder);
        m_group.bind(16, l.extension);
        m_group.bind(17, l.access);
        m_group.bind(18, l.sourceIp);
        m_group.bind(19, l.destinationIp);
        m_group.bind(20, l.detail);
        m_group.execute();
        m_groups.insert(l.signature, group);
        m_newSignatures << l.signature;
        ++m_nextGroup;
    }

    sql::Connection& m_connection;
    QHash<qint64, qint64> m_groups;
    QHash<LogKey, qint64> m_logs;
    QHash<qint64, Tally> m_totals;
    QList<qint64> m_newSignatures;
    QList<LogKey> m_newLogs;
    LogKey m_lastLogKey;
    qint64 m_lastLog = 0;
    qint64 m_nextGroup = 1;
    qint64 m_nextGroupStart = 1;
    qint64 m_nextBlock = 1;
    sql::Statement m_event;
    sql::Statement m_group;
    sql::Statement m_search;
    sql::Statement m_block;
    sql::Statement m_log;
    sql::Statement m_match;
    sql::Statement m_total;
};

} // namespace

qint64 ImportReport::addedEvents() const
{
    return std::accumulate(files.cbegin(), files.cend(), qint64{0},
                           [](qint64 total, const FileReport& f) { return total + f.events; });
}

int ImportReport::count(FileReport::Status status) const
{
    return static_cast<int>(
        std::count_if(files.cbegin(), files.cend(), [status](const FileReport& f) { return f.status == status; }));
}

Importer::Importer(QString workPath, QByteArray timeZone, sigma::Configuration rules)
    : m_workPath(std::move(workPath)), m_timeZone(std::move(timeZone)), m_rules(std::move(rules))
{
}

QStringList Importer::listLogs(const QString& folder)
{
    QStringList filters;
    for (const QString& extension : logs::extensions())
        filters << QStringLiteral("*.") + extension;
    QStringList paths;
    QDirIterator iter(folder, filters, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (iter.hasNext()) {
        const QString path = iter.next();
        const QString extension = iter.fileInfo().suffix().toLower();
        if (extension == QLatin1String("evtx") || logs::recognized(path))
            paths << path;
    }
    paths.sort(Qt::CaseInsensitive);
    return paths;
}

ImportReport Importer::importFiles(const QStringList& paths, const Callback& callback, const std::atomic_bool& cancellation)
{
    QElapsedTimer chrono;
    chrono.start();
    ImportReport report;
    ImportProgress progress;
    progress.fileCount = static_cast<int>(paths.size());

    QElapsedTimer lastNotification;
    lastNotification.start();
    auto notify = [&](bool force) {
        if (!callback || (!force && lastNotification.elapsed() < 100))
            return;
        lastNotification.restart();
        const double seconds = static_cast<double>(chrono.nsecsElapsed()) / 1e9;
        progress.rate = seconds > 0.2 ? static_cast<double>(progress.recordsRead) / seconds : 0;
        const qint64 remaining = progress.totalRecords - progress.recordsRead;
        progress.secondsRemaining =
            progress.rate > 0 && remaining > 0 ? static_cast<double>(remaining) / progress.rate : -1;
        callback(progress);
    };

    sql::Connection connection = db::open(m_workPath, sql::Connection::ReadWrite);
    bool exclusive = false;
    try {
        exclusive = connection.text(QStringLiteral("PRAGMA journal_mode = DELETE")) == QLatin1String("delete");
    } catch (const sql::Error& e) {
        qWarning().noquote() << "Import without exclusive access:" << e.message();
    }
    if (exclusive)
        connection.script(QStringLiteral("PRAGMA locking_mode = EXCLUSIVE;"));

    // ── Preparation: fingerprints, duplicates, volume estimate ───────────────
    progress.phase = ImportProgress::Phase::Preparation;
    notify(true);
    std::vector<PreparedFile> preparedFiles;
    QSet<QString> seenFingerprints;
    for (const QString& path : paths) {
        PreparedFile preparedFile;
        preparedFile.path = path;
        QString error;
        const std::unique_ptr<logs::Log> log = logs::open(path, m_timeZone, &error);
        if (!log) {
            preparedFile.error = error;
        } else {
            preparedFile.fingerprint = QString::fromLatin1(log->fingerprint());
            preparedFile.size = log->size();
            preparedFile.duplicate = seenFingerprints.contains(preparedFile.fingerprint) || db::fingerprintKnown(connection, preparedFile.fingerprint);
            if (!preparedFile.duplicate)
                preparedFile.estimation = log->estimateRecords();
            seenFingerprints.insert(preparedFile.fingerprint);
        }
        progress.totalRecords += preparedFile.estimation;
        preparedFiles.push_back(std::move(preparedFile));
        if (cancellation.load())
            break;
    }

    // ── Detection rules ─────────────────────────────────────────────────────
    const sigma::RuleSet rules = sigma::load(m_rules);
    report.rulesInError = static_cast<int>(rules.errors.size());
    const bool emptyDatabase = connection.integer(QStringLiteral("SELECT NOT EXISTS (SELECT 1 FROM events)")) != 0;
    const bool directDetection = emptyDatabase || sigma::upToDate(connection, rules);
    std::vector<qint64> numbers;
    if (directDetection) {
        sql::Transaction transaction(connection);
        numbers = sigma::storeRules(connection, rules, emptyDatabase);
        transaction.commit();
    }
    std::optional<sigma::Evaluator> evaluator;
    if (directDetection && !rules.rules.empty())
        evaluator.emplace(rules);
    const sigma::Evaluator* evaluation = evaluator ? &*evaluator : nullptr;

    const bool reindex = progress.totalRecords >= INDEX_DROP_THRESHOLD;
    qint64 waitNs = 0, insertNs = 0, totalsNs = 0, writeNs = 0;
    auto duration = [&report](const char* step, qint64 nanoseconds) {
        report.durations.append({QString::fromUtf8(step), static_cast<double>(nanoseconds) / 1e9});
    };
    auto measure = [&duration](const char* step, const std::function<void()>& work) {
        QElapsedTimer stopwatch;
        stopwatch.start();
        work();
        duration(step, stopwatch.nsecsElapsed());
    };
    if (reindex)
        db::dropSecondaryIndexes(connection);

    QThreadPool pool;
    pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));
    Writer writer(connection);
    QHash<QString, QString> namesBySid;
    AccessAnalyzer::Volumes volumes;
    {
        sql::Statement reader = connection.prepare(QStringLiteral("SELECT key, letter FROM volumes"));
        while (reader.step())
            volumes.insert(reader.text(0), reader.text(1));
    }
    const qsizetype knownVolumes = volumes.size();
    duration("preparation", chrono.nsecsElapsed());

    for (size_t fileIndex = 0; fileIndex < preparedFiles.size(); ++fileIndex) {
        const PreparedFile& preparedFile = preparedFiles[fileIndex];
        FileReport result;
        result.path = preparedFile.path;
        progress.phase = ImportProgress::Phase::Analysis;
        progress.file = static_cast<int>(fileIndex);
        progress.fileName = QFileInfo(preparedFile.path).fileName();
        notify(true);

        if (cancellation.load()) {
            result.status = FileReport::Status::Cancelled;
            report.files << result;
            continue;
        }
        if (!preparedFile.error.isEmpty()) {
            result.status = FileReport::Status::Error;
            result.message = preparedFile.error;
            report.files << result;
            continue;
        }
        if (preparedFile.duplicate) {
            result.status = FileReport::Status::AlreadyImported;
            result.message = qtTrId("import.already_present_explanation");
            report.files << result;
            continue;
        }

        QString error;
        const std::unique_ptr<logs::Log> log = logs::open(preparedFile.path, m_timeZone, &error);
        if (!log) {
            result.status = FileReport::Status::Error;
            result.message = error;
            report.files << result;
            continue;
        }

        const qint64 readBefore = progress.recordsRead;
        const qint64 eventsBefore = progress.events;
        qint64 matches = 0;
        std::unique_ptr<Window> isRunning;
        try {
            sql::Transaction transaction(connection);
            writer.beginFile();
            connection.execute(QStringLiteral("INSERT INTO sources (path, name, fingerprint, size, imported_at) "
                                              "VALUES (?, ?, ?, ?, ?)"),
                               {QFileInfo(preparedFile.path).absoluteFilePath(), progress.fileName,
                                preparedFile.fingerprint, preparedFile.size,
                                QDateTime::currentDateTimeUtc().toString(Qt::ISODate)});
            const qint64 sourceId = connection.lastIdentifier();

            AccessAnalyzer analyzer;
            analyzer.setVolumes(volumes);
            const int batchCount = log->batchCount();
            const bool inverse = log->descending();
            auto run = [&](int start) {
                auto window = std::make_unique<Window>();
                const auto count = static_cast<size_t>(std::min(BATCHES_PER_WINDOW, batchCount - start));
                window->indices.resize(count);
                std::iota(window->indices.begin(), window->indices.end(), start);
                window->results.resize(count);
                Window* target = window.get();
                window->tasks = QtConcurrent::map(
                    &pool, window->indices, [&log, target, start, batchCount, inverse, evaluation, this](int& index) {
                        target->results[static_cast<size_t>(index - start)] =
                            decodeBatch(*log, inverse ? batchCount - 1 - index : index, m_timeZone, evaluation);
                    });
                return window;
            };

            bool interrupted = false;
            if (batchCount > 0)
                isRunning = run(0);
            QElapsedTimer stopwatch;
            for (int start = 0; start < batchCount; start += BATCHES_PER_WINDOW) {
                stopwatch.start();
                isRunning->tasks.waitForFinished();
                std::vector<BatchResult> results = std::move(isRunning->results);
                waitNs += stopwatch.nsecsElapsed();
                if (start + BATCHES_PER_WINDOW < batchCount && !cancellation.load())
                    isRunning = run(start + BATCHES_PER_WINDOW);
                else
                    isRunning.reset();

                stopwatch.start();
                for (BatchResult& batch : results) {
                    result.records += batch.readCount;
                    result.decodingErrors += batch.errors;
                    progress.recordsRead += batch.readCount;
                    const qint64 block = batch.block.isEmpty() ? 0 : writer.nextBlock();
                    bool usedBlock = false;
                    const auto count = static_cast<qint64>(batch.rows.size());
                    for (qint64 i = 0; i < count; ++i) {
                        const qint64 rank = inverse ? count - 1 - i : i;
                        PreparedRow& preparedRow = batch.rows[static_cast<size_t>(rank)];
                        EventRow& row = preparedRow.row;
                        if (preparedRow.file) {
                            const QString previousPath = row.file;
                            const QString actionBefore = row.action;
                            if (!analyzer.correlate(row)) {
                                row.action.clear();
                                row.category = "";
                            }
                            if (row.file != previousPath || row.action != actionBefore)
                                row.signature = eventSignature(row);
                        }
                        if (!row.userSid.isEmpty() && !namesBySid.contains(row.userSid))
                            namesBySid.insert(row.userSid, row.user);
                        if (result.machine.isEmpty())
                            result.machine = row.machine;
                        if (const auto identifier = writer.write(row, sourceId, block, rank)) {
                            usedBlock = true;
                            for (const int r : preparedRow.rules) {
                                writer.writeMatch(numbers[static_cast<size_t>(r)], *identifier);
                                ++matches;
                            }
                            ++result.events;
                            ++progress.events;
                        } else {
                            ++result.duplicates;
                        }
                    }
                    if (usedBlock && block > 0)
                        writer.writeBlock(block, sourceId, batch.block);
                }
                (void)QtConcurrent::run(&pool, [released = std::move(results)]() mutable { released.clear(); });
                insertNs += stopwatch.nsecsElapsed();
                notify(false);
                if (cancellation.load()) {
                    interrupted = true;
                    break;
                }
            }
            if (isRunning)
                isRunning->tasks.waitForFinished();

            if (interrupted) {
                writer.abandonFile();
                result.status = FileReport::Status::Cancelled;
                result.events = 0;
                progress.events = eventsBefore;
                progress.recordsRead = readBefore;
                report.files << result;
                continue;
            }

            progress.phase = ImportProgress::Phase::Saving;
            notify(true);
            stopwatch.start();
            writer.finishFile();
            totalsNs += stopwatch.nsecsElapsed();
            result.analysis = analyzer.statistics();
            volumes = analyzer.volumes();
            connection.execute(QStringLiteral("UPDATE sources SET record_count = ?, event_count = ?, "
                                              "duplicate_count = ?, machine = ? WHERE id = ?"),
                               {result.records, result.events, result.duplicates, result.machine,
                                sourceId});
            stopwatch.start();
            transaction.commit();
            writeNs += stopwatch.nsecsElapsed();
            report.detection.matches += matches;
            result.status = FileReport::Status::Imported;
            if (result.events == 0 && result.duplicates == 0)
                result.message = qtTrId("import.no_event");
            else if (log->reduced())
                result.message = qtTrId("import.message_only_explanation");
        } catch (const std::exception& exception) {
            if (isRunning)
                isRunning->tasks.waitForFinished();
            writer.abandonFile();
            result.status = FileReport::Status::Error;
            result.message = QString::fromUtf8(exception.what());
            result.events = 0;
            progress.events = eventsBefore;
        }
        report.files << result;
    }

    report.cancelled = cancellation.load();
    writer.release();
    duration("decoding", waitNs);
    duration("insertion", insertNs);
    duration("totals", totalsNs);
    duration("writing", writeNs);
    const bool imported = report.count(FileReport::Status::Imported) > 0;
    if (reindex || imported) {
        progress.phase = ImportProgress::Phase::Finalization;
        notify(true);
    }
    if (volumes.size() > knownVolumes && imported)
        measure("volumes", [&] { applyVolumes(connection, volumes); });
    if (!namesBySid.isEmpty() && imported)
        measure("accounts", [&] { applyAccountNames(connection, namesBySid); });
    if (reindex || imported)
        measure("indexing", [&] { db::createSecondaryIndexes(connection); });
    measure("checkpoint", [&] {
        connection.script(exclusive ? QStringLiteral("PRAGMA locking_mode = NORMAL; PRAGMA journal_mode = WAL;")
                                    : QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE);"));
    });

    report.detection.rules = static_cast<int>(rules.rules.size());
    if (directDetection && imported) {
        sigma::finalize(connection, rules);
        report.detection.events = report.addedEvents();
        report.detection.complete = emptyDatabase;
    } else if (!report.cancelled && imported) {
        progress.phase = ImportProgress::Phase::Detection;
        notify(true);
        measure("detection", [&] {
            try {
                report.detection = sigma::analyze(
                    connection, rules, false,
                    [&](qint64 readCount, qint64 total) {
                        progress.analyzed = readCount;
                        progress.toAnalyze = total;
                        notify(false);
                    },
                    cancellation);
            } catch (const std::exception& exception) {
                qWarning().noquote() << "Detection interrupted:" << exception.what();
            }
        });
    }

    progress.phase = ImportProgress::Phase::Finished;
    notify(true);
    report.seconds = static_cast<double>(chrono.nsecsElapsed()) / 1e9;
    return report;
}

} // namespace lh
