// Export of filtered events and alerts: CSV, JSON and Excel (.xlsx).
#pragma once

#include "Alerts.h"
#include "Filters.h"
#include "Sqlite.h"

#include <QString>

#include <atomic>
#include <functional>

namespace lh::exporter {

enum class Format { Csv, Json, Excel };

struct Result
{
    qint64 rows = 0;
    int sheets = 0;
    bool cancelled = false;
};

using ProgressCallback = std::function<void(qint64 rows, qint64 total)>;

Result exportEvents(sql::Connection& connection, const Filters& filters, Format format, const QString& path,
                    const ProgressCallback& progress = {}, const std::atomic_bool* cancellation = nullptr);

Result exportAlerts(const QList<Detection>& detections, const QString& scope, Format format,
                    const QString& path);

QString neutralizeFormula(const QString& value);

inline constexpr qint64 ROWS_PER_SHEET = 1'048'575;

} // namespace lh::exporter
