// Importable Windows event logs: EVTX, XML, CSV and tab-separated text.
#pragma once

#include "XmlEvent.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <memory>

namespace lh::logs {

struct Batch
{
    qint64 readCount = 0;
    qint64 errors = 0;
    QByteArray block;
};

class Log
{
public:
    virtual ~Log() = default;

    [[nodiscard]] virtual qint64 size() const = 0;
    [[nodiscard]] virtual QByteArray fingerprint() const = 0;
    [[nodiscard]] virtual qint64 estimateRecords() const = 0;
    [[nodiscard]] virtual int batchCount() const = 0;
    [[nodiscard]] virtual bool reduced() const { return false; }
    [[nodiscard]] virtual bool descending() const { return false; }

    virtual Batch decodeBatch(int index, const Callback& callback) const = 0;
};

std::unique_ptr<Log> open(const QString& path, const QByteArray& timeZone, QString* error = nullptr);

const QStringList& extensions();
bool isKnownExtension(const QString& path);
bool recognized(const QString& path);

} // namespace lh::logs
