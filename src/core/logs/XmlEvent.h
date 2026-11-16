// Windows events written as XML: locating them in a text, decoding and writing.
#pragma once

#include "core/evtx/Extraction.h"

#include <QByteArrayView>
#include <QString>

#include <functional>

namespace lh::logs {

using Callback = std::function<bool(const evtx::DecodedEvent& event)>;

struct ScanResult
{
    qint64 readCount = 0;
    qint64 kept = 0;
    qint64 errors = 0;
    qsizetype start = -1;
    qsizetype end = -1;
};

qsizetype eventStart(QByteArrayView text, qsizetype from);
qsizetype lastEvent(QByteArrayView text);

bool decodeEvent(QByteArrayView fragment, bool xml, evtx::DecodedEvent& output);

ScanResult scanEvents(QByteArrayView text, qsizetype start, qsizetype limit, bool xml, const Callback& callback);

void writeEvent(QString& output, const evtx::DecodedEvent& event);

} // namespace lh::logs
