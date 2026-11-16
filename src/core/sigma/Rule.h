// Sigma rules: compilation from YAML and evaluation against an event.
#pragma once

#include "Yaml.h"
#include "core/evtx/Extraction.h"

#include <QByteArray>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <optional>
#include <vector>

namespace lh::sigma {

enum class Level { Information, Low, Medium, High, Critical };
Level levelFromText(QStringView text);

class Event
{
public:
    void set(quint32 eventId, const QString& channel, const QString& provider, const QString& machine,
             const std::vector<evtx::DataField>& fields);

    [[nodiscard]] quint32 eventId() const noexcept { return m_eventId; }
    [[nodiscard]] const QString& lowerChannel() const noexcept { return m_channel; }
    [[nodiscard]] bool isSecurity() const noexcept { return m_security; }
    [[nodiscard]] int count() const noexcept { return static_cast<int>(m_count); }
    [[nodiscard]] int index(QStringView name) const;
    [[nodiscard]] const QString& value(int index) const { return m_values[static_cast<size_t>(index)]; }
    [[nodiscard]] const QString& lowerValue(int index) const;

private:
    void add(const QString& name, const QString& value);

    quint32 m_eventId = 0;
    QString m_channel;
    bool m_security = false;
    size_t m_count = 0;
    std::vector<QString> m_names;
    std::vector<QString> m_values;
    mutable std::vector<QString> m_lowercase;
    mutable std::vector<char> m_computed;
};

struct Pattern
{
    enum class Type {
        Equals,
        Contains,
        StartsWith,
        EndsWith,
        Wildcard,
        Regex,
        Cidr,
        Null,
        Exists,
        Comparison,
        Reference,
        Always
    };

    Type type = Type::Equals;
    QString text;
    bool caseSensitive = false;
    QRegularExpression expression;
    QByteArray network;
    int prefix = 0;
    int op = 0;
    double number = 0;
    bool expected = true;

    [[nodiscard]] bool matches(const QString& value, const QString& lowercase) const;
};

struct Condition
{
    QString field;
    bool all = false;
    std::vector<Pattern> patterns;
};

struct Group
{
    std::vector<Condition> conditions;
};

struct Selection
{
    QString name;
    std::vector<Group> groups;
};

struct Expression
{
    enum class Type { Selection, And, Or, Not, AtLeast };

    Type type = Type::Selection;
    int selection = -1;
    int minimum = 1;
    std::vector<Expression> children;
};

struct Target
{
    QString channel;
    std::vector<quint32> identifiers;
};

enum class Category { Other, ProcessCreation, ProcessTermination, NetworkConnection };

class Rule
{
public:
    QString id;
    QString title;
    QString description;
    QString author;
    QString origin;
    QStringList tags;
    QStringList links;
    Level level = Level::Medium;
    std::vector<Target> targets;
    std::optional<std::vector<quint32>> requiredIdentifiers;
    Category category = Category::Other;
    std::vector<Selection> selections;
    Expression condition;

    [[nodiscard]] bool appliesTo(quint32 eventId, const QString& lowerChannel) const;
    [[nodiscard]] bool evaluate(const Event& event) const;

private:
    [[nodiscard]] int fieldIndex(const Event& event, const QString& field) const;
    [[nodiscard]] bool evaluate(const Expression& expression, const Event& event) const;
    [[nodiscard]] bool evaluate(const Selection& selection, const Event& event) const;
    [[nodiscard]] bool evaluate(const Condition& criterion, const Event& event) const;
};

struct Compilation
{
    enum class Status { Usable, NotApplicable, Deprecated, Invalid };

    Rule rule;
    Status status = Status::Invalid;
    QString error;
};

Compilation compile(const yaml::Node& document, const QString& origin, const QString& language);

} // namespace lh::sigma
