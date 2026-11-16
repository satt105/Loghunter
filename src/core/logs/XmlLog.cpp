#include "XmlLog.h"

#include "core/Blocks.h"

#include <algorithm>

namespace lh::logs {

namespace {

constexpr int SAMPLED_BATCHES = 64;

uint64_t eventDate(QByteArrayView text, qsizetype start)
{
    uint64_t date = 0;
    if (start >= 0)
        scanEvents(text, start, text.size(), false, [&date](const evtx::DecodedEvent& event) {
            date = event.fileTime;
            return false;
        });
    return date;
}

} // namespace

bool XmlLog::open(const QString& path, QString* error)
{
    if (!m_file.open(path, error))
        return false;
    m_text = utf8Text(m_file.bytes(), true, m_converted);
    const uint64_t earliest = eventDate(m_text, 0);
    const uint64_t latest = eventDate(m_text, lastEvent(m_text));
    m_descending = latest != 0 && earliest > latest;
    return true;
}

int XmlLog::batchCount() const
{
    return static_cast<int>((m_text.size() + BATCH_SIZE - 1) / BATCH_SIZE);
}

qint64 XmlLog::estimateRecords() const
{
    const int batches = batchCount();
    if (batches <= 0)
        return 0;
    const int samples = std::min(batches, SAMPLED_BATCHES);
    qint64 total = 0;
    for (int i = 0; i < samples; ++i) {
        const qsizetype start = static_cast<qsizetype>(static_cast<qint64>(i) * batches / samples) * BATCH_SIZE;
        const qsizetype limit = std::min(start + BATCH_SIZE, m_text.size());
        for (qsizetype p = eventStart(m_text, start); p >= 0 && p < limit; p = eventStart(m_text, p + 1))
            ++total;
    }
    return total * batches / samples;
}

Batch XmlLog::decodeBatch(int index, const Callback& callback) const
{
    Batch batch;
    const qsizetype start = static_cast<qsizetype>(index) * BATCH_SIZE;
    const ScanResult scan =
        scanEvents(m_text, start, std::min(start + BATCH_SIZE, m_text.size()), false, callback);
    batch.readCount = scan.readCount;
    batch.errors = scan.errors;
    if (scan.kept > 0)
        batch.block = blocks::compressText(m_text.sliced(scan.start, scan.end - scan.start));
    return batch;
}

} // namespace lh::logs
