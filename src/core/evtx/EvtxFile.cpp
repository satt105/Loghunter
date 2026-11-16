#include "EvtxFile.h"

#include "Bytes.h"

#include <QCryptographicHash>
#include <QFileInfo>

#include <algorithm>
#include <cstring>
#include <limits>

namespace lh::evtx {

namespace {

constexpr char FILE_MAGIC[8] = {'E', 'l', 'f', 'F', 'i', 'l', 'e', '\0'};
constexpr char CHUNK_MAGIC[8] = {'E', 'l', 'f', 'C', 'h', 'n', 'k', '\0'};
constexpr uint32_t RECORD_MAGIC = 0x00002a2a;
constexpr size_t SCAN_END = std::numeric_limits<size_t>::max();
constexpr int SAMPLED_CHUNKS = 256;

int mostRecentChunk(const EvtxFile& file) noexcept
{
    const Chunk first = file.chunk(0);
    if (!first.isValid())
        return -1;
    const uint64_t reference = first.firstNumber();
    int bottom = 0;
    int top = file.chunkCount() - 1;
    while (bottom < top) {
        const int middle = bottom + (top - bottom + 1) / 2;
        const Chunk candidate = file.chunk(middle);
        if (candidate.isValid() && candidate.firstNumber() >= reference)
            bottom = middle;
        else
            top = middle - 1;
    }
    return bottom;
}

} // namespace

// ── Chunk ────────────────────────────────────────────────────────────────────

bool Chunk::isValid() const noexcept
{
    return m_data != nullptr && std::memcmp(m_data, CHUNK_MAGIC, 8) == 0;
}

uint64_t Chunk::firstNumber() const noexcept { return read64(m_data + 8); }
uint64_t Chunk::lastNumber() const noexcept { return read64(m_data + 16); }
uint64_t Chunk::lastIdentifier() const noexcept { return read64(m_data + 32); }
uint32_t Chunk::freeOffset() const noexcept { return read32(m_data + 48); }

uint64_t Chunk::recordCount() const noexcept
{
    if (!isValid())
        return 0;
    const uint64_t first = firstNumber();
    const uint64_t last = lastNumber();
    if (last < first || last - first > CHUNK_SIZE / 28)
        return 0;
    return last - first + 1;
}

bool Chunk::nextRecord(size_t& offset, RawRecord& output, bool& corrupt) const noexcept
{
    const size_t limit = std::min<size_t>(freeOffset(), CHUNK_SIZE);
    if (offset == SCAN_END || offset >= limit || limit - offset < 4)
        return false;

    const uint8_t* p = m_data + offset;
    const uint32_t magic = read32(p);
    if (magic != RECORD_MAGIC) {
        corrupt = magic != 0;
        return false;
    }
    if (limit - offset < RECORD_HEADER_SIZE + 4) {
        corrupt = true;
        return false;
    }
    const uint32_t size = read32(p + 4);
    if (size < RECORD_HEADER_SIZE + 4 || size > CHUNK_SIZE - offset) {
        corrupt = true;
        return false;
    }

    output.identifier = read64(p + 8);
    output.writtenAt = read64(p + 16);
    output.binxml = p + RECORD_HEADER_SIZE;
    output.binxmlSize = size - RECORD_HEADER_SIZE - 4;
    output.offsetBinxml = static_cast<uint32_t>(offset + RECORD_HEADER_SIZE);

    offset += size;
    if (output.identifier == lastIdentifier())
        offset = SCAN_END;
    return true;
}

// ── EvtxFile ─────────────────────────────────────────────────────────────────

EvtxFile::~EvtxFile() { close(); }

bool EvtxFile::open(const QString& path, QString* error)
{
    close();
    auto failure = [&](const QString& message) {
        if (error)
            *error = message;
        close();
        return false;
    };

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::ReadOnly)) {
        if (QFileInfo::exists(path))
            return failure(qtTrId("evtx.open_failed"));
        return failure(qtTrId("common.file_not_found"));
    }
    m_size = m_file.size();
    if (m_size < static_cast<qint64>(FILE_HEADER_SIZE))
        return failure(qtTrId("evtx.too_small"));

    m_mapped = m_file.map(0, m_size);
    if (!m_mapped)
        return failure(qtTrId("evtx.load_failed"));

    if (std::memcmp(m_mapped, FILE_MAGIC, 8) != 0)
        return failure(qtTrId("evtx.signature_missing"));

    m_chunkCount = static_cast<int>((m_size - static_cast<qint64>(FILE_HEADER_SIZE))
                                    / static_cast<qint64>(CHUNK_SIZE));
    return true;
}

void EvtxFile::close()
{
    if (m_mapped) {
        m_file.unmap(const_cast<uchar*>(m_mapped));
        m_mapped = nullptr;
    }
    if (m_file.isOpen())
        m_file.close();
    m_size = 0;
    m_chunkCount = 0;
}

Chunk EvtxFile::chunk(int index) const noexcept
{
    if (!m_mapped || index < 0 || index >= m_chunkCount)
        return Chunk();
    return Chunk(m_mapped + FILE_HEADER_SIZE + static_cast<size_t>(index) * CHUNK_SIZE);
}

uint64_t EvtxFile::estimateRecords() const noexcept
{
    if (m_chunkCount <= 0)
        return 0;
    const int samples = std::min(m_chunkCount, SAMPLED_CHUNKS);
    uint64_t total = 0;
    for (int i = 0; i < samples; ++i)
        total += chunk(static_cast<int>(static_cast<qint64>(i) * m_chunkCount / samples)).recordCount();
    return total * static_cast<uint64_t>(m_chunkCount) / static_cast<uint64_t>(samples);
}

QByteArray EvtxFile::fingerprint() const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const qint64 size = m_size;
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(&size), sizeof(size)));
    if (m_mapped) {
        const qint64 start = std::min<qint64>(m_size, FILE_HEADER_SIZE + CHUNK_SIZE);
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(m_mapped), start));
        const qint64 tailStart = std::max<qint64>(start, m_size - static_cast<qint64>(CHUNK_SIZE));
        hash.addData(QByteArrayView(reinterpret_cast<const char*>(m_mapped + tailStart), m_size - tailStart));
        const int recent = mostRecentChunk(*this);
        if (recent > 0)
            hash.addData(QByteArrayView(reinterpret_cast<const char*>(chunk(recent).data()), CHUNK_SIZE));
    }
    return hash.result().toHex();
}

} // namespace lh::evtx
