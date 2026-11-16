// Summary report as standalone HTML or as PDF.
#pragma once

#include "core/Alerts.h"
#include "core/Filters.h"
#include "core/Queries.h"
#include "core/Session.h"

#include <QDateTime>
#include <QString>

#include <atomic>

namespace lh::report {

struct Options
{
    enum class Format { Html, Pdf };
    Format format = Format::Pdf;
    QString title = qtTrId("report.default_title");
    QString project;
    QString author;
    bool charts = true;
    bool rankings = true;
    bool alerts = true;
    bool logs = true;
    int rowsPerAlert = 25;
};

struct Data
{
    Filters filters;
    AlertParameters parameters;
    QByteArray timeZone;
    QDateTime generation;
    queries::Summary summary;
    QList<Detection> detections;
    QList<queries::UserRow> users;
    QList<queries::FileRow> files;
    QList<queries::FolderRow> folders;
    QList<Source> sources;
};

Data collect(Context& context, const Filters& filters, const AlertParameters& parameters,
             const QByteArray& timeZone);

QString html(const Data& data, const Options& options);

void write(const Data& data, const Options& options, const QString& path);

} // namespace lh::report
