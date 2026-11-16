#include "Format.h"

#include <QLocale>

#include <algorithm>
#include <climits>
#include <cmath>

namespace lh {

namespace {

const QChar THIN_SPACE(0x202F);

QString decimal(double value, int decimals)
{
    QString text = QString::number(value, 'f', decimals);
    if (decimals > 0 && text.endsWith(QLatin1Char('0'))) {
        while (text.endsWith(QLatin1Char('0')))
            text.chop(1);
        if (text.endsWith(QLatin1Char('.')))
            text.chop(1);
    }
    return text.replace(QLatin1Char('.'), QLocale().decimalPoint());
}

} // namespace

int pluralCount(qint64 value)
{
    return static_cast<int>(std::clamp<qint64>(value, INT_MIN, INT_MAX));
}

QString formatNumber(qint64 value)
{
    return QLocale().toString(value);
}

QString formatEvents(qint64 value)
{
    return qtTrId("common.event_count", pluralCount(value));
}

QString formatShortNumber(qint64 value)
{
    const double magnitude = std::abs(static_cast<double>(value));
    if (magnitude < 10'000)
        return formatNumber(value);
    auto shortened = [&](double divisor, const QString& suffix) {
        const double v = static_cast<double>(value) / divisor;
        return decimal(v, std::abs(v) < 100 ? 1 : 0) + THIN_SPACE + suffix;
    };
    if (magnitude < 1e6)
        return shortened(1e3, qtTrId("format.thousands"));
    if (magnitude < 1e9)
        return shortened(1e6, qtTrId("format.millions"));
    return shortened(1e9, qtTrId("format.billions"));
}

QString formatPercentage(double fraction)
{
    return QString::number(fraction * 100.0, 'f', 1).replace(QLatin1Char('.'), QLocale().decimalPoint()) + THIN_SPACE
           + QLatin1Char('%');
}

QString formatSize(qint64 bytes)
{
    const auto value = static_cast<double>(bytes);
    if (bytes < 1024)
        return qtTrId("format.bytes").arg(bytes);
    if (bytes < 1024 * 1024)
        return qtTrId("format.kib").arg(decimal(value / 1024.0, 1));
    if (bytes < 1024LL * 1024 * 1024)
        return qtTrId("format.mib").arg(decimal(value / (1024.0 * 1024.0), 1));
    return qtTrId("format.gib").arg(decimal(value / (1024.0 * 1024.0 * 1024.0), 2));
}

} // namespace lh
