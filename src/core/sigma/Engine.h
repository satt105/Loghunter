// Library of Sigma rules, loading and search for matches in the events of a project.
#pragma once

#include "Rule.h"
#include "core/Sqlite.h"

#include <QByteArray>
#include <QHash>
#include <QStringList>

#include <atomic>
#include <functional>
#include <vector>

namespace lh::sigma {

struct Configuration
{
    bool enabled = true;
    QString folder;
    QStringList disabled;
    int minimumLevel = static_cast<int>(Level::Low);

    bool operator==(const Configuration&) const = default;
};

struct RuleSet
{
    std::vector<Rule> rules;
    QStringList errors;
    int notApplicableCount = 0;
    int discardedCount = 0;
    int disabledCount = 0;
    QByteArray fingerprint;
};

RuleSet load(const Configuration& configuration);

// ── Library: rules shipped with LogHunter and rules added by the user ────────

struct Entry
{
    enum class State { Usable, NotApplicable, Deprecated, Invalid, Duplicate };

    QString id;
    QString title;
    QString description;
    QString author;
    QString path;
    Level level = Level::Medium;
    State state = State::Usable;
    QString error;
    bool builtin = false;
};

struct RejectedFile
{
    QString path;
    Entry::State state = Entry::State::Invalid;
    QString error;
};

struct Addition
{
    int added = 0;
    int updated = 0;
    int unchanged = 0;
    QList<RejectedFile> rejected;
};

QString userFolder();
std::vector<Entry> library(const QString& folder);
Addition addRules(const QString& folder, const QStringList& paths);
bool removeRule(const QString& folder, const QString& path);

struct Outcome
{
    int rules = 0;
    qint64 events = 0;
    qint64 matches = 0;
    bool complete = false;
    bool cancelled = false;
};

class Evaluator
{
public:
    explicit Evaluator(const RuleSet& ruleSet);

    [[nodiscard]] bool appliesTo(quint32 eventId, const QString& channel) const;

    void evaluate(quint32 eventId, const QString& channel, const QString& provider, const QString& machine,
                  const std::vector<evtx::DataField>& fields, std::vector<int>& rules) const;

private:
    const RuleSet& m_ruleSet;
    QHash<QString, std::vector<int>> m_byChannel;
    std::vector<int> m_general;
};

bool upToDate(sql::Connection& connection, const RuleSet& ruleSet);
std::vector<qint64> storeRules(sql::Connection& connection, const RuleSet& ruleSet, bool complete);
void finalize(sql::Connection& connection, const RuleSet& ruleSet);

using ProgressCallback = std::function<void(qint64 readCount, qint64 total)>;

Outcome analyze(sql::Connection& connection, const RuleSet& ruleSet, bool complete, const ProgressCallback& progress,
                const std::atomic_bool& cancellation);

QString builtinOrigin();

} // namespace lh::sigma
