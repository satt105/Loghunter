// Filter criteria and generation of parameterized SQL clauses.
#pragma once

#include <QByteArray>
#include <QDate>
#include <QJsonObject>
#include <QString>
#include <QVariantList>

namespace lh {

struct Clause
{
    QString sql;
    QVariantList parameters;
    QString groups;
    QVariantList groupParameters;
    QString events;
    QVariantList eventParameters;

    [[nodiscard]] bool eventBased() const { return !events.isEmpty(); }
    [[nodiscard]] QString where() const { return sql.isEmpty() ? QString() : QStringLiteral(" WHERE ") + sql; }
    [[nodiscard]] QString whereGroups() const
    {
        return groups.isEmpty() ? QString() : QStringLiteral(" WHERE ") + groups;
    }
    [[nodiscard]] Clause andWhere(const QString& condition, const QVariantList& values = {}) const;
    [[nodiscard]] Clause andGroupWhere(const QString& condition, const QVariantList& values = {}) const;

private:
    void recompose();
};

enum class AccountFilter { All, Users, System };

struct Filters
{
    QString text;
    QString user;
    QString category;
    QString file;
    QString folder;
    QString process;
    QString machine;
    QString events;
    int level = 0;
    QString channel;
    QString ip;
    QString rule;
    QString ruleTitle;
    qint64 group = 0;
    QString groupLabel;
    qint64 sourceId = 0;
    QDate since;
    QDate until;
    qint64 startUtc = 0;
    qint64 endUtc = 0;
    int startHour = -1;
    int endHour = -1;
    bool includeHandle = false;
    AccountFilter accounts = AccountFilter::All;
    bool exact = false;

    [[nodiscard]] Clause clause() const;
    [[nodiscard]] int activeCount() const;
    [[nodiscard]] bool isEmpty() const { return activeCount() == 0; }
    [[nodiscard]] bool hasExactPeriod() const { return endUtc > startUtc; }
    [[nodiscard]] bool hourRange() const { return startHour >= 0 && endHour >= 0 && startHour != endHour; }
    [[nodiscard]] QString description(const QByteArray& timeZone = {}) const;

    [[nodiscard]] QJsonObject toJson() const;
    [[nodiscard]] QString key() const;
    static Filters fromJson(const QJsonObject& object);

    bool operator==(const Filters&) const = default;
};

QString searchQuery(const QString& text);
QString periodDescription(qint64 startUtc, qint64 endUtc, const QByteArray& timeZone);
bool validEvents(const QString& text);

} // namespace lh
