#include "Engine.h"

#include "core/Blocks.h"
#include "core/Session.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QLocale>
#include <QLoggingCategory>
#include <QSaveFile>
#include <QSet>
#include <QtConcurrent>

#include <algorithm>

namespace lh::sigma {

namespace {

Q_LOGGING_CATEGORY(log, "loghunter.sigma")

constexpr qint64 BATCH_SIZE = 20'000;
constexpr char KEY_FINGERPRINT[] = "sigma_fingerprint";
constexpr char KEY_LAST_ID[] = "sigma_last_id";
constexpr char KEY_RULES[] = "sigma_rules";
constexpr char RESOURCES[] = ":/rules";

constexpr qint64 MAX_RULE_SIZE = 1024 * 1024;

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity PATH_SENSITIVITY = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity PATH_SENSITIVITY = Qt::CaseSensitive;
#endif

QStringList ruleFiles(const QString& folder)
{
    QStringList paths;
    if (folder.isEmpty() || !QFileInfo(folder).isDir())
        return paths;
    QDirIterator iter(folder, {QStringLiteral("*.yml"), QStringLiteral("*.yaml")}, QDir::Files | QDir::Readable,
                      QDirIterator::Subdirectories);
    while (iter.hasNext())
        paths << iter.next();
    paths.sort(Qt::CaseInsensitive);
    return paths;
}

QString ruleLanguage()
{
    return QLocale().name().section(QLatin1Char('_'), 0, 0);
}

Compilation readRule(const QString& path, const QString& origin, const QString& language,
                     QByteArray* content = nullptr)
{
    Compilation compilation;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        compilation.error = qtTrId("rules.error_open");
        return compilation;
    }
    if (file.size() > MAX_RULE_SIZE) {
        compilation.error = qtTrId("rules.error_too_large");
        return compilation;
    }
    const QByteArray bytes = file.readAll();
    if (content)
        *content = bytes;
    try {
        compilation = compile(yaml::parse(QString::fromUtf8(bytes)), origin, language);
    } catch (const yaml::Error& error) {
        compilation.error = qtTrId("rules.error_line").arg(error.line()).arg(error.message());
    }
    return compilation;
}

Entry::State entryState(Compilation::Status status)
{
    switch (status) {
    case Compilation::Status::Usable:
        return Entry::State::Usable;
    case Compilation::Status::NotApplicable:
        return Entry::State::NotApplicable;
    case Compilation::Status::Deprecated:
        return Entry::State::Deprecated;
    case Compilation::Status::Invalid:
        break;
    }
    return Entry::State::Invalid;
}

QString freePath(const QDir& folder, const QString& source)
{
    const QFileInfo info(source);
    QString name = info.fileName();
    for (int i = 2; folder.exists(name); ++i)
        name = QStringLiteral("%1-%2.%3").arg(info.completeBaseName()).arg(i).arg(info.suffix());
    return folder.filePath(name);
}

bool samePath(const QString& a, const QString& b)
{
    const QString canonical = QFileInfo(a).canonicalFilePath();
    return !canonical.isEmpty() && canonical.compare(QFileInfo(b).canonicalFilePath(), PATH_SENSITIVITY) == 0;
}

} // namespace

QString builtinOrigin()
{
    return QStringLiteral("builtin");
}

RuleSet load(const Configuration& configuration)
{
    RuleSet ruleSet;
    QSet<QString> identifiers;
    const QSet<QString> disabled(configuration.disabled.cbegin(), configuration.disabled.cend());
    QCryptographicHash fingerprint(QCryptographicHash::Sha1);
    const QString language = ruleLanguage();
    fingerprint.addData(language.toUtf8());
    fingerprint.addData(QByteArray::number(configuration.minimumLevel));
    auto loadFile = [&](const QString& path, const QString& origin) {
        QByteArray content;
        Compilation compilation = readRule(path, origin, language, &content);
        fingerprint.addData(content);
        switch (compilation.status) {
        case Compilation::Status::Usable:
            break;
        case Compilation::Status::Deprecated:
            ++ruleSet.discardedCount;
            return;
        case Compilation::Status::NotApplicable:
            ++ruleSet.notApplicableCount;
            return;
        case Compilation::Status::Invalid:
            ruleSet.errors << QStringLiteral("%1: %2").arg(path, compilation.error);
            return;
        }
        Rule& rule = compilation.rule;
        if (identifiers.contains(rule.id))
            return;
        identifiers.insert(rule.id);
        if (disabled.contains(rule.id)) {
            fingerprint.addData(QByteArrayLiteral("disabled:"));
            fingerprint.addData(rule.id.toUtf8());
            ++ruleSet.disabledCount;
            return;
        }
        if (static_cast<int>(rule.level) < configuration.minimumLevel) {
            ++ruleSet.discardedCount;
            return;
        }
        ruleSet.rules.push_back(std::move(rule));
    };
    if (configuration.enabled) {
        for (const QString& path : ruleFiles(configuration.folder.trimmed()))
            loadFile(path, path);
        for (const QString& path : ruleFiles(QLatin1String(RESOURCES)))
            loadFile(path, builtinOrigin());
    }
    ruleSet.fingerprint = fingerprint.result();
    for (const QString& error : std::as_const(ruleSet.errors))
        qCWarning(log).noquote() << "Rule ignored:" << error;
    qCInfo(log).noquote() << QStringLiteral(
                                 "%1 rule(s) loaded, %2 disabled, %3 not applicable, %4 discarded, %5 in error")
                                 .arg(ruleSet.rules.size())
                                 .arg(ruleSet.disabledCount)
                                 .arg(ruleSet.notApplicableCount)
                                 .arg(ruleSet.discardedCount)
                                 .arg(ruleSet.errors.size());
    return ruleSet;
}

// ── Library ──────────────────────────────────────────────────────────────────

QString userFolder()
{
    return dataFolder() + QStringLiteral("/rules");
}

std::vector<Entry> library(const QString& folder)
{
    std::vector<Entry> entries;
    QSet<QString> identifiers;
    const QString language = ruleLanguage();
    auto add = [&](const QString& path, bool builtin) {
        const Compilation compilation = readRule(path, path, language);
        Entry entry;
        entry.id = compilation.rule.id;
        entry.title = compilation.rule.title.isEmpty() ? QFileInfo(path).fileName() : compilation.rule.title;
        entry.description = compilation.rule.description;
        entry.author = compilation.rule.author;
        entry.path = path;
        entry.level = compilation.rule.level;
        entry.state = entryState(compilation.status);
        entry.error = compilation.error;
        entry.builtin = builtin;
        if (entry.state == Entry::State::Usable) {
            if (identifiers.contains(entry.id))
                entry.state = Entry::State::Duplicate;
            identifiers.insert(entry.id);
        }
        entries.push_back(std::move(entry));
    };
    for (const QString& path : ruleFiles(folder.trimmed()))
        add(path, false);
    for (const QString& path : ruleFiles(QLatin1String(RESOURCES)))
        add(path, true);
    return entries;
}

Addition addRules(const QString& folder, const QStringList& paths)
{
    Addition addition;
    QDir().mkpath(folder);
    const QDir target(folder);
    QHash<QString, QString> known;
    for (const QString& path : ruleFiles(folder)) {
        const Compilation compilation = readRule(path, path, QString());
        if (!compilation.rule.id.isEmpty() && !known.contains(compilation.rule.id))
            known.insert(compilation.rule.id, path);
    }
    QStringList sources;
    for (const QString& path : paths) {
        if (QFileInfo(path).isDir())
            sources << ruleFiles(path);
        else
            sources << path;
    }
    for (const QString& source : std::as_const(sources)) {
        QByteArray content;
        const Compilation compilation = readRule(source, source, QString(), &content);
        if (compilation.status != Compilation::Status::Usable) {
            addition.rejected.append({source, entryState(compilation.status), compilation.error});
            continue;
        }
        const QString id = compilation.rule.id;
        const bool update = known.contains(id);
        const QString destination = update ? known.value(id) : freePath(target, source);
        if (update) {
            QFile current(destination);
            if (samePath(source, destination) || (current.open(QIODevice::ReadOnly) && current.readAll() == content)) {
                ++addition.unchanged;
                continue;
            }
        }
        QSaveFile file(destination);
        if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size() || !file.commit()) {
            addition.rejected.append({source, Entry::State::Invalid, qtTrId("rules.error_copy")});
            continue;
        }
        ++(update ? addition.updated : addition.added);
        known.insert(id, destination);
    }
    return addition;
}

bool removeRule(const QString& folder, const QString& path)
{
    const QString root = QDir(folder).canonicalPath();
    const QString file = QFileInfo(path).canonicalFilePath();
    return !root.isEmpty() && file.startsWith(root + QLatin1Char('/'), PATH_SENSITIVITY) && QFile::remove(file);
}

// ── Evaluation ───────────────────────────────────────────────────────────────

Evaluator::Evaluator(const RuleSet& ruleSet) : m_ruleSet(ruleSet)
{
    for (int i = 0; i < static_cast<int>(ruleSet.rules.size()); ++i) {
        const Rule& rule = ruleSet.rules[static_cast<size_t>(i)];
        bool general = rule.targets.empty();
        QSet<QString> channels;
        for (const Target& target : rule.targets) {
            if (target.channel.isEmpty())
                general = true;
            else
                channels.insert(target.channel);
        }
        if (general)
            m_general.push_back(i);
        else
            for (const QString& channel : std::as_const(channels))
                m_byChannel[channel].push_back(i);
    }
}

bool Evaluator::appliesTo(quint32 eventId, const QString& channel) const
{
    const QString lowercase = channel.toLower();
    auto candidate = [&](int r) { return m_ruleSet.rules[static_cast<size_t>(r)].appliesTo(eventId, lowercase); };
    const auto found = m_byChannel.constFind(lowercase);
    if (found != m_byChannel.cend() && std::any_of(found->cbegin(), found->cend(), candidate))
        return true;
    return std::any_of(m_general.cbegin(), m_general.cend(), candidate);
}

void Evaluator::evaluate(quint32 eventId, const QString& channel, const QString& provider, const QString& machine,
                         const std::vector<evtx::DataField>& fields, std::vector<int>& rules) const
{
    rules.clear();
    thread_local std::vector<int> candidates;
    candidates.clear();
    const QString lowercase = channel.toLower();
    auto keep = [&](int r) {
        if (m_ruleSet.rules[static_cast<size_t>(r)].appliesTo(eventId, lowercase))
            candidates.push_back(r);
    };
    const auto found = m_byChannel.constFind(lowercase);
    if (found != m_byChannel.cend())
        for (const int r : found.value())
            keep(r);
    for (const int r : m_general)
        keep(r);
    if (candidates.empty())
        return;
    thread_local Event event;
    event.set(eventId, channel, provider, machine, fields);
    for (const int r : candidates)
        if (m_ruleSet.rules[static_cast<size_t>(r)].evaluate(event))
            rules.push_back(r);
}

// ── Analysis state in the project ────────────────────────────────────────────

bool upToDate(sql::Connection& connection, const RuleSet& ruleSet)
{
    return db::readMeta(connection, QLatin1String(KEY_FINGERPRINT)) == QString::fromLatin1(ruleSet.fingerprint.toHex());
}

std::vector<qint64> storeRules(sql::Connection& connection, const RuleSet& ruleSet, bool complete)
{
    if (complete)
        connection.script(QStringLiteral("DELETE FROM matches; DELETE FROM rules;"));
    sql::Statement insertion = connection.prepare(QStringLiteral(
        "INSERT INTO rules (id, title, description, level, tags, author, links, origin) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?) ON CONFLICT (id) DO UPDATE SET title = excluded.title, "
        "description = excluded.description, level = excluded.level, tags = excluded.tags, "
        "author = excluded.author, links = excluded.links, origin = excluded.origin"));
    sql::Statement number = connection.prepare(QStringLiteral("SELECT number FROM rules WHERE id = ?"));
    std::vector<qint64> numbers;
    numbers.reserve(ruleSet.rules.size());
    for (const Rule& rule : ruleSet.rules) {
        insertion.reset();
        insertion.bind(1, rule.id);
        insertion.bind(2, rule.title);
        insertion.bind(3, rule.description);
        insertion.bind(4, static_cast<int>(rule.level));
        insertion.bind(5, rule.tags.join(QLatin1Char(' ')));
        insertion.bind(6, rule.author);
        insertion.bind(7, rule.links.join(QLatin1Char('\n')));
        insertion.bind(8, rule.origin);
        insertion.execute();
        number.reset();
        number.bind(1, rule.id);
        numbers.push_back(number.step() ? number.integer(0) : 0);
    }
    return numbers;
}

void finalize(sql::Connection& connection, const RuleSet& ruleSet)
{
    db::writeMeta(connection, QLatin1String(KEY_FINGERPRINT), QString::fromLatin1(ruleSet.fingerprint.toHex()));
    db::writeMeta(connection, QLatin1String(KEY_LAST_ID),
                  QString::number(connection.integer(QStringLiteral("SELECT COALESCE(MAX(id), 0) FROM events"))));
    db::writeMeta(connection, QLatin1String(KEY_RULES), QString::number(ruleSet.rules.size()));
}

// ── Analysis of already imported events ──────────────────────────────────────

Outcome analyze(sql::Connection& connection, const RuleSet& ruleSet, bool complete, const ProgressCallback& progress,
                const std::atomic_bool& cancellation)
{
    Outcome outcome;
    outcome.rules = static_cast<int>(ruleSet.rules.size());
    qint64 since = 0;
    if (!complete && upToDate(connection, ruleSet))
        since = db::readMeta(connection, QLatin1String(KEY_LAST_ID)).toLongLong();
    outcome.complete = since == 0;
    sql::Transaction transaction(connection);
    const std::vector<qint64> numbers = storeRules(connection, ruleSet, outcome.complete);
    const Evaluator evaluator(ruleSet);
    connection.script(QStringLiteral("CREATE TEMP TABLE IF NOT EXISTS candidates (group_id INTEGER PRIMARY KEY);"
                                     "DELETE FROM candidates;"));
    {
        sql::Statement groups = connection.prepare(QStringLiteral("SELECT group_id, event_id, channel FROM groups"));
        sql::Statement insert = connection.prepare(QStringLiteral("INSERT INTO candidates (group_id) VALUES (?)"));
        while (groups.step()) {
            if (!evaluator.appliesTo(static_cast<quint32>(groups.integer(1)), groups.text(2)))
                continue;
            insert.reset();
            insert.bind(1, groups.integer(0));
            insert.execute();
        }
    }
    const qint64 total = connection.integer(
        QStringLiteral("SELECT COUNT(*) FROM events WHERE id > ? AND group_id IN (SELECT group_id FROM candidates)"),
        {since});

    struct Row
    {
        qint64 id = 0;
        qint64 block = 0;
        qint64 rank = 0;
        quint32 eventId = 0;
        QString channel;
        QString provider;
        QString machine;
    };
    struct Group
    {
        qint64 block = 0;
        QByteArray data;
        std::vector<size_t> rows;
    };
    sql::Statement reader = connection.prepare(
        QStringLiteral("SELECT e.id, e.block, e.rank, g.event_id, g.channel, g.provider, g.machine "
                       "FROM events e JOIN groups g ON g.group_id = e.group_id "
                       "WHERE e.id > ? AND e.group_id IN (SELECT group_id FROM candidates) ORDER BY e.id LIMIT ?"));
    sql::Statement blockRead = connection.prepare(QStringLiteral("SELECT data FROM blocks WHERE id = ?"));
    sql::Statement insertion =
        connection.prepare(QStringLiteral("INSERT OR IGNORE INTO matches (rule, event) VALUES (?, ?)"));
    qint64 last = since;
    while (!ruleSet.rules.empty()) {
        std::vector<Row> batch;
        batch.reserve(static_cast<size_t>(BATCH_SIZE));
        reader.reset();
        reader.bind(1, last);
        reader.bind(2, BATCH_SIZE);
        while (reader.step())
            batch.push_back({reader.integer(0), reader.integer(1), reader.integer(2),
                             static_cast<quint32>(reader.integer(3)), reader.text(4), reader.text(5), reader.text(6)});
        if (batch.empty())
            break;
        last = batch.back().id;

        std::vector<Group> groups;
        for (size_t i = 0; i < batch.size(); ++i) {
            if (groups.empty() || groups.back().block != batch[i].block || batch[i].block <= 0) {
                Group group;
                group.block = batch[i].block;
                if (group.block > 0) {
                    blockRead.reset();
                    blockRead.bind(1, group.block);
                    if (blockRead.step())
                        group.data = blockRead.blob(0);
                }
                groups.push_back(std::move(group));
            }
            groups.back().rows.push_back(i);
        }

        std::vector<std::vector<int>> results(batch.size());
        QtConcurrent::blockingMap(groups, [&](Group& group) {
            if (cancellation.load(std::memory_order_relaxed))
                return;
            static const std::vector<evtx::DataField> NONE;
            QHash<qint64, size_t> expected;
            for (const size_t i : group.rows)
                expected.insert(batch[i].rank, i);
            if (!group.data.isEmpty()) {
                qint64 rank = 0;
                blocks::scan(group.data, false, [&](const evtx::DecodedEvent& e) {
                    const auto found = expected.constFind(rank++);
                    if (found == expected.cend())
                        return true;
                    const size_t i = found.value();
                    const Row& row = batch[i];
                    evaluator.evaluate(row.eventId, row.channel, row.provider, row.machine, e.data,
                                       results[i]);
                    expected.erase(found);
                    return !expected.isEmpty();
                });
            }
            for (const size_t i : std::as_const(expected)) {
                const Row& row = batch[i];
                evaluator.evaluate(row.eventId, row.channel, row.provider, row.machine, NONE, results[i]);
            }
        });
        if (cancellation.load()) {
            outcome.cancelled = true;
            return outcome;
        }
        for (size_t i = 0; i < batch.size(); ++i) {
            for (const int r : results[i]) {
                insertion.reset();
                insertion.bind(1, numbers[static_cast<size_t>(r)]);
                insertion.bind(2, batch[i].id);
                insertion.execute();
                ++outcome.matches;
            }
        }
        outcome.events += static_cast<qint64>(batch.size());
        if (progress)
            progress(outcome.events, total);
    }
    finalize(connection, ruleSet);
    transaction.commit();
    return outcome;
}

} // namespace lh::sigma
