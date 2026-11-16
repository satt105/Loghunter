// Normalization of decoded events: fields common to all Windows logs.
#pragma once

#include "DateTime.h"
#include "evtx/Extraction.h"

#include <QString>

namespace lh {

namespace level {
inline constexpr int Critical = 1;
inline constexpr int Error = 2;
inline constexpr int Warning = 3;
inline constexpr int Information = 4;
inline constexpr int Verbose = 5;
} // namespace level

namespace audit {
inline constexpr int None = 0;
inline constexpr int Success = 1;
inline constexpr int Failure = 2;
} // namespace audit

struct EventRow
{
    int64_t tsUtc = UNKNOWN_TIMESTAMP;
    LocalTimestamp local;
    QString user;
    bool isSystem = false;
    QString action;
    const char* category = "";
    QString file;
    QString folder;
    QString extension;
    QString process;
    QString access;
    uint32_t eventId = 0;
    uint64_t recordId = 0;
    QString machine;
    quint64 processId = 0;
    quint64 handle = 0;
    int level = level::Information;
    int audit = audit::None;
    QString channel;
    QString provider;
    QString sourceIp;
    QString destinationIp;
    QString detail;
    QString userSid;
    qint64 signature = 0;
};

void normalize(const evtx::DecodedEvent& event, TimeZoneConverter& timeZone, EventRow& row);
qint64 eventSignature(const EventRow& row);

QString indentXml(const QString& xml);
QString xmlToJson(const QString& xml);

QString cleanValue(const QString& value);
QString cleanIp(const QString& value);
bool isSecurityChannel(QStringView channel);

} // namespace lh
