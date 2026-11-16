// Fast calendar computations (proleptic Gregorian), without QDateTime.
#pragma once

#include <cstdint>

namespace lh {

struct CivilDate
{
    int64_t year;
    unsigned month;
    unsigned day;
};

constexpr CivilDate dateFromDays(int64_t days) noexcept
{
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = static_cast<unsigned>(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int64_t year = static_cast<int64_t>(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned day = doy - (153 * mp + 2) / 5 + 1;
    const unsigned month = mp < 10 ? mp + 3 : mp - 9;
    return {year + (month <= 2 ? 1 : 0), month, day};
}

constexpr int64_t daysFromDate(int64_t year, unsigned month, unsigned day) noexcept
{
    year -= month <= 2 ? 1 : 0;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

constexpr int64_t floorDivide(int64_t a, int64_t b) noexcept
{
    const int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

constexpr unsigned weekday(int64_t days) noexcept
{
    return static_cast<unsigned>(days >= -4 ? (days + 4) % 7 : (days + 5) % 7 + 6);
}

static_assert(daysFromDate(1970, 1, 1) == 0);
static_assert(dateFromDays(19723).year == 2024 && dateFromDays(19723).month == 1
              && dateFromDays(19723).day == 1);
static_assert(weekday(0) == 4);

} // namespace lh
