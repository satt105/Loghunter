#include "DateTime.h"

#include "Calendar.h"

#include <QDateTime>

namespace lh {

namespace {

inline void digits(QChar* output, int64_t value, int width) noexcept
{
    for (int i = width - 1; i >= 0; --i) {
        output[i] = QChar(u'0' + static_cast<char16_t>(value % 10));
        value /= 10;
    }
}

QString g_dateFormat;

QString localDate(QStringView iso)
{
    const QString format = displayedDateFormat();
    QString text;
    text.reserve(format.size() + 9);
    for (qsizetype i = 0; i < format.size();) {
        const QChar symbol = format[i];
        qsizetype length = 1;
        while (i + length < format.size() && format[i + length] == symbol)
            ++length;
        if (symbol == u'y') {
            text.append(length > 2 ? iso.left(4) : iso.mid(2, 2));
        } else if (symbol == u'M' || symbol == u'd') {
            const QStringView field = iso.mid(symbol == u'M' ? 5 : 8, 2);
            text.append(length > 1 || field[0] != u'0' ? field : field.mid(1));
        } else {
            text.append(QStringView(format).mid(i, length));
        }
        i += length;
    }
    return text;
}

} // namespace

TimeZoneConverter::TimeZoneConverter(const QByteArray& ianaIdentifier)
{
    m_identifier = ianaIdentifier.isEmpty() ? systemTimeZone() : ianaIdentifier;
    m_timeZone = QTimeZone(m_identifier);
    if (!m_timeZone.isValid()) {
        m_identifier = QByteArrayLiteral("UTC");
        m_timeZone = QTimeZone::UTC;
    }
}

int32_t TimeZoneConverter::offsetSeconds(int64_t utcMicroseconds)
{
    const int64_t bin = floorDivide(utcMicroseconds, 900LL * 1'000'000);
    const auto found = m_cache.find(bin);
    if (found != m_cache.end())
        return found->second;
    const QDateTime instant = QDateTime::fromMSecsSinceEpoch(bin * 900'000, QTimeZone::UTC);
    const int32_t shift = m_timeZone.offsetFromUtc(instant);
    if (m_cache.size() > 200'000)
        m_cache.clear();
    m_cache.emplace(bin, shift);
    return shift;
}

int64_t TimeZoneConverter::toUtc(int64_t localMicroseconds)
{
    const int64_t approximate = localMicroseconds - static_cast<int64_t>(offsetSeconds(localMicroseconds)) * 1'000'000;
    return localMicroseconds - static_cast<int64_t>(offsetSeconds(approximate)) * 1'000'000;
}

LocalTimestamp TimeZoneConverter::convert(int64_t utcMicroseconds)
{
    LocalTimestamp result;
    if (utcMicroseconds == UNKNOWN_TIMESTAMP)
        return result;

    const int64_t local = utcMicroseconds + static_cast<int64_t>(offsetSeconds(utcMicroseconds)) * 1'000'000;
    const int64_t seconds = floorDivide(local, 1'000'000);
    const int64_t micro = local - seconds * 1'000'000;
    const int64_t days = floorDivide(seconds, 86400);
    const int64_t secondOfDay = seconds - days * 86400;
    const CivilDate date = dateFromDays(days);
    if (date.year < 1 || date.year > 9999)
        return result;

    result.timestamp.resize(26);
    QChar* p = result.timestamp.data();
    digits(p, date.year, 4);
    p[4] = u'-';
    digits(p + 5, date.month, 2);
    p[7] = u'-';
    digits(p + 8, date.day, 2);
    p[10] = u'T';
    digits(p + 11, secondOfDay / 3600, 2);
    p[13] = u':';
    digits(p + 14, (secondOfDay / 60) % 60, 2);
    p[16] = u':';
    digits(p + 17, secondOfDay % 60, 2);
    p[19] = u'.';
    digits(p + 20, micro, 6);

    result.date = result.timestamp.left(10);
    result.hour = static_cast<int>(secondOfDay / 3600);
    result.weekday = static_cast<int>(weekday(days));
    result.day = days;
    return result;
}

QDate dateFromDay(qint64 day)
{
    return QDate::fromJulianDay(day + 2440588);
}

qint64 dayFromDate(const QDate& date)
{
    return date.toJulianDay() - 2440588;
}

QByteArray systemTimeZone()
{
    QByteArray identifier = QTimeZone::systemTimeZoneId();
    if (identifier.isEmpty() || !QTimeZone(identifier).isValid())
        identifier = QByteArrayLiteral("UTC");
    return identifier;
}

bool isValidTimeZone(const QByteArray& ianaIdentifier)
{
    return !ianaIdentifier.isEmpty() && QTimeZone(ianaIdentifier).isValid();
}

QStringList dateFormats()
{
    return {QStringLiteral("dd/MM/yyyy"), QStringLiteral("MM/dd/yyyy"), QStringLiteral("dd.MM.yyyy"),
            QStringLiteral("yyyy-MM-dd"), QStringLiteral("yyyy/MM/dd")};
}

void setDateFormat(const QString& format)
{
    g_dateFormat = dateFormats().contains(format) ? format : QString();
}

QString displayedDateFormat()
{
    return g_dateFormat.isEmpty() ? qtTrId("format.date") : g_dateFormat;
}

QString displayedDayFormat(bool withYear)
{
    const QString format = displayedDateFormat();
    const qsizetype start = format.indexOf(u'y');
    const qsizetype end = format.lastIndexOf(u'y') + 1;
    if (start < 0 || (withYear && start == 0))
        return format;
    if (withYear)
        return format.left(start) + QStringLiteral("yy") + format.mid(end);
    if (start == 0)
        return format.mid(end + 1);
    return format.left(start - 1) + format.mid(end);
}

QString formatTimestamp(QStringView iso, bool withSeconds)
{
    if (iso.size() < 16 || iso[4] != u'-' || iso[7] != u'-')
        return iso.toString();
    QString text = localDate(iso);
    text.append(u' ').append(iso.mid(11, withSeconds && iso.size() >= 19 ? 8 : 5));
    return text;
}

QString formatDate(QStringView iso)
{
    if (iso.size() < 10 || iso[4] != u'-')
        return iso.toString();
    return localDate(iso);
}

QString formatDate(const QDate& date)
{
    return formatDate(date.toString(Qt::ISODate));
}

QString formatDateTime(const QDateTime& instant)
{
    return formatTimestamp(instant.toString(Qt::ISODate), false);
}

QString formatUtc(int64_t utcMicroseconds)
{
    if (utcMicroseconds == UNKNOWN_TIMESTAMP)
        return {};
    const int64_t seconds = floorDivide(utcMicroseconds, 1'000'000);
    const int64_t micro = utcMicroseconds - seconds * 1'000'000;
    const int64_t days = floorDivide(seconds, 86400);
    const int64_t secondOfDay = seconds - days * 86400;
    const CivilDate date = dateFromDays(days);
    return QStringLiteral("%1-%2-%3 %4:%5:%6.%7 UTC")
        .arg(date.year, 4, 10, QLatin1Char('0'))
        .arg(date.month, 2, 10, QLatin1Char('0'))
        .arg(date.day, 2, 10, QLatin1Char('0'))
        .arg(secondOfDay / 3600, 2, 10, QLatin1Char('0'))
        .arg((secondOfDay / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(secondOfDay % 60, 2, 10, QLatin1Char('0'))
        .arg(micro, 6, 10, QLatin1Char('0'));
}

QString formatDuration(double seconds)
{
    if (seconds < 0)
        seconds = 0;
    const auto total = static_cast<qint64>(seconds + 0.5);
    if (total < 60)
        return qtTrId("duration.seconds").arg(total);
    if (total < 3600)
        return qtTrId("duration.minutes_seconds").arg(total / 60).arg(total % 60, 2, 10, QLatin1Char('0'));
    return qtTrId("duration.hours_minutes").arg(total / 3600).arg((total / 60) % 60, 2, 10, QLatin1Char('0'));
}

} // namespace lh
