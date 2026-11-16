// Event blocks kept compressed in the project (EVTX chunks or XML batches) and how they are read back.
#pragma once

#include "Sqlite.h"
#include "evtx/Extraction.h"
#include "evtx/EvtxFile.h"

#include <QByteArray>
#include <QByteArrayView>

#include <functional>
#include <optional>

namespace lh::blocks {

QByteArray compress(const evtx::Chunk& chunk);
QByteArray compressText(QByteArrayView xml);

using Callback = std::function<bool(const evtx::DecodedEvent& event)>;
bool scan(const QByteArray& block, bool xml, const Callback& callback);

std::optional<evtx::DecodedEvent> event(sql::Connection& connection, qint64 block, qint64 rank, bool xml);

} // namespace lh::blocks
