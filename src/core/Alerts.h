// Detection of suspicious activity: built-in checks and Sigma rules.
#pragma once

#include "Filters.h"
#include "Scan.h"
#include "Sqlite.h"
#include "sigma/Engine.h"

#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <atomic>

namespace lh {

struct AlertParameters
{
    int deletionThreshold = 20;
    int openingHour = 7;
    int closingHour = 20;
    bool weekendOffHours = true;
    int burstActions = 100;
    int burstSeconds = 60;
    bool ignoreSystemAccounts = true;
    QStringList executableExtensions;
    QStringList ransomwareExtensions;
    QStringList disabledRules;
    int minimumRuleLevel = static_cast<int>(sigma::Level::Low);
    bool ruleAnalysis = true;
    bool fileAnalysis = true;
    bool mitre = true;

    AlertParameters();
    [[nodiscard]] sigma::Configuration ruleConfiguration() const;
    [[nodiscard]] QJsonObject toJson() const;
    static AlertParameters fromJson(const QJsonObject& object);
    bool operator==(const AlertParameters&) const = default;
};

enum class Severity { Low, Medium, High, Critical };
QString severityLabel(Severity severity);
Severity severityFromLevel(int sigmaLevel);

struct ResultColumn
{
    enum class Type { Text, Number, Timestamp, Date, Category, Path, Duration };
    QString title;
    Type type = Type::Text;
};
QString formatCell(const QVariant& value, ResultColumn::Type type);

struct Detection
{
    QString identifier;
    QString title;
    QString description;
    Severity severity = Severity::Medium;
    const char* unit = "";
    qint64 count = 0;
    QList<ResultColumn> columns;
    QList<QVariantList> rows;
    QList<Filters> explorations;
    QStringList tags;
    bool sigma = false;
    QString rule;
    QString author;
    QStringList links;

    static constexpr int MAX_ROWS = 1000;

    [[nodiscard]] QString quantity() const;
    [[nodiscard]] Filters allOccurrences(const Filters& scope) const;
};

QList<Detection> detect(Context& context, const Filters& filters, const AlertParameters& parameters);

int riskScore(const QList<Detection>& detections);
Severity maxSeverity(const QList<Detection>& detections);
int findingCount(const QList<Detection>& detections);

} // namespace lh
