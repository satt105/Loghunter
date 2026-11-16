// Timestamps: UTC → display time zone conversion and formatting.
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QTimeZone>

#include <cstdint>
#include <limits>
#include <unordered_map>

namespace lh {

inline constexpr int64_t UNKNOWN_TIMESTAMP = std::numeric_limits<int64_t>::min();

struct LocalTimestamp
{
    QString timestamp;
    QString date;
    int hour = -1;
    int weekday = -1;
    qint64 day = 0;
};

class TimeZoneConverter
{
public:
    explicit TimeZoneConverter(const QByteArray& ianaIdentifier = {});

    [[nodiscard]] const QByteArray& identifier() const noexcept { return m_identifier; }
    [[nodiscard]] LocalTimestamp convert(int64_t utcMicroseconds);
    [[nodiscard]] int32_t offsetSeconds(int64_t utcMicroseconds);
    [[nodiscard]] int64_t toUtc(int64_t localMicroseconds);

private:
    QByteArray m_identifier;
    QTimeZone m_timeZone;
    std::unordered_map<int64_t, int32_t> m_cache;
};

QByteArray systemTimeZone();
bool isValidTimeZone(const QByteArray& ianaIdentifier);

QStringList dateFormats();
void setDateFormat(const QString& format);
QString displayedDateFormat();
QString displayedDayFormat(bool withYear);
QString formatTimestamp(QStringView iso, bool withSeconds = true);
QString formatDate(QStringView iso);
QString formatDate(const QDate& date);
QString formatDateTime(const QDateTime& instant);
QString formatUtc(int64_t utcMicroseconds);
QString formatDuration(double seconds);

QDate dateFromDay(qint64 day);
qint64 dayFromDate(const QDate& date);

} // namespace lh
