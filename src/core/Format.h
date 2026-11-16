// Formatting of displayed values, following the conventions of the chosen language.
#pragma once

#include <QString>

namespace lh {

int pluralCount(qint64 value);
QString formatNumber(qint64 value);
QString formatShortNumber(qint64 value);
QString formatEvents(qint64 value);
QString formatPercentage(double fraction);
QString formatSize(qint64 bytes);

} // namespace lh
