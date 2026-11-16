// Import of Windows event logs into the working database.
#pragma once

#include "AccessAnalyzer.h"
#include "sigma/Engine.h"

#include <QByteArray>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>

#include <atomic>
#include <functional>

namespace lh {

struct ImportProgress
{
    enum class Phase { Preparation, Analysis, Saving, Finalization, Detection, Finished };
    Phase phase = Phase::Preparation;
    int file = 0;
    int fileCount = 0;
    QString fileName;
    qint64 recordsRead = 0;
    qint64 totalRecords = 0;
    qint64 events = 0;
    qint64 analyzed = 0;
    qint64 toAnalyze = 0;
    double rate = 0;
    double secondsRemaining = -1;
};

struct FileReport
{
    enum class Status { Imported, AlreadyImported, Error, Cancelled };
    QString path;
    Status status = Status::Imported;
    QString message;
    qint64 records = 0;
    qint64 events = 0;
    qint64 duplicates = 0;
    qint64 decodingErrors = 0;
    AnalysisStatistics analysis;
    QString machine;
};

struct ImportReport
{
    QList<FileReport> files;
    bool cancelled = false;
    double seconds = 0;
    QList<QPair<QString, double>> durations;
    sigma::Outcome detection;
    int rulesInError = 0;

    [[nodiscard]] qint64 addedEvents() const;
    [[nodiscard]] int count(FileReport::Status status) const;
};

class Importer
{
public:
    using Callback = std::function<void(const ImportProgress&)>;

    Importer(QString workPath, QByteArray timeZone, sigma::Configuration rules);

    ImportReport importFiles(const QStringList& paths, const Callback& callback, const std::atomic_bool& cancellation);

    static QStringList listLogs(const QString& folder);

private:
    QString m_workPath;
    QByteArray m_timeZone;
    sigma::Configuration m_rules;
};

} // namespace lh
