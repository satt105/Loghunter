// EVTX file: header, 64 KiB chunks and records.
#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>

#include <cstdint>

namespace lh::evtx {

inline constexpr size_t FILE_HEADER_SIZE = 4096;
inline constexpr size_t CHUNK_SIZE = 65536;
inline constexpr size_t CHUNK_HEADER_SIZE = 512;
inline constexpr size_t RECORD_HEADER_SIZE = 24;

struct RawRecord
{
    uint64_t identifier = 0;
    uint64_t writtenAt = 0;
    const uint8_t* binxml = nullptr;
    uint32_t binxmlSize = 0;
    uint32_t offsetBinxml = 0;
};

class Chunk
{
public:
    explicit Chunk(const uint8_t* data = nullptr) noexcept : m_data(data) {}

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] const uint8_t* data() const noexcept { return m_data; }

    [[nodiscard]] uint64_t firstNumber() const noexcept;
    [[nodiscard]] uint64_t lastNumber() const noexcept;
    [[nodiscard]] uint64_t lastIdentifier() const noexcept;
    [[nodiscard]] uint32_t freeOffset() const noexcept;

    [[nodiscard]] uint64_t recordCount() const noexcept;

    template <typename Callback>
    bool scan(Callback&& callback) const;

private:
    bool nextRecord(size_t& offset, RawRecord& output, bool& corrupt) const noexcept;

    const uint8_t* m_data;
};

class EvtxFile
{
public:
    EvtxFile() = default;
    ~EvtxFile();
    EvtxFile(const EvtxFile&) = delete;
    EvtxFile& operator=(const EvtxFile&) = delete;

    bool open(const QString& path, QString* error = nullptr);
    void close();

    [[nodiscard]] qint64 size() const noexcept { return m_size; }
    [[nodiscard]] int chunkCount() const noexcept { return m_chunkCount; }
    [[nodiscard]] Chunk chunk(int index) const noexcept;

    [[nodiscard]] uint64_t estimateRecords() const noexcept;

    [[nodiscard]] QByteArray fingerprint() const;

private:
    QFile m_file;
    const uint8_t* m_mapped = nullptr;
    qint64 m_size = 0;
    int m_chunkCount = 0;
};

// ── Scan implementation (template) ───────────────────────────────────────────

template <typename Callback>
bool Chunk::scan(Callback&& callback) const
{
    if (!isValid())
        return false;
    size_t offset = CHUNK_HEADER_SIZE;
    RawRecord record;
    bool corrupt = false;
    while (nextRecord(offset, record, corrupt)) {
        if (!callback(record))
            return true;
    }
    return !corrupt;
}

} // namespace lh::evtx
