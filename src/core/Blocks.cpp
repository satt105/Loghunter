#include "Blocks.h"

#include "logs/XmlEvent.h"

#include <miniz.h>

#include <cstring>
#include <vector>

namespace lh::blocks {

namespace {

constexpr int COMPRESSION_LEVEL = 3;
constexpr uint32_t CHUNK_HEADER_SIZE = 512;
constexpr uint32_t TEXT_MARKER = 0x80000000;
constexpr qsizetype PREFIX_SIZE = 4;
constexpr uint32_t MAX_COMPRESSION_RATIO = 1100;

QByteArray compressBytes(const unsigned char* data, uint32_t size, uint32_t marker)
{
    mz_ulong compressed = mz_compressBound(size);
    QByteArray output(static_cast<qsizetype>(compressed) + PREFIX_SIZE, Qt::Uninitialized);
    const uint32_t prefix = size | marker;
    for (int i = 0; i < PREFIX_SIZE; ++i)
        output[i] = static_cast<char>((prefix >> (8 * i)) & 0xff);
    if (mz_compress2(reinterpret_cast<unsigned char*>(output.data() + PREFIX_SIZE), &compressed, data, size,
                     COMPRESSION_LEVEL)
        != MZ_OK)
        return {};
    output.resize(static_cast<qsizetype>(compressed) + PREFIX_SIZE);
    return output;
}

bool decompress(const QByteArray& block, unsigned char* output, uint32_t size)
{
    mz_ulong decompressed = size;
    return mz_uncompress(output, &decompressed, reinterpret_cast<const unsigned char*>(block.constData() + PREFIX_SIZE),
                         static_cast<mz_ulong>(block.size() - PREFIX_SIZE))
               == MZ_OK
           && decompressed == size;
}

} // namespace

QByteArray compress(const evtx::Chunk& chunk)
{
    if (!chunk.isValid())
        return {};
    uint32_t size = chunk.freeOffset();
    if (size < CHUNK_HEADER_SIZE || size > evtx::CHUNK_SIZE)
        size = static_cast<uint32_t>(evtx::CHUNK_SIZE);
    return compressBytes(chunk.data(), size, 0);
}

QByteArray compressText(QByteArrayView xml)
{
    if (xml.isEmpty() || xml.size() >= static_cast<qsizetype>(TEXT_MARKER))
        return {};
    return compressBytes(reinterpret_cast<const unsigned char*>(xml.data()), static_cast<uint32_t>(xml.size()),
                         TEXT_MARKER);
}

bool scan(const QByteArray& block, bool xml, const Callback& callback)
{
    if (block.size() <= PREFIX_SIZE)
        return false;
    uint32_t prefix = 0;
    for (int i = 0; i < PREFIX_SIZE; ++i)
        prefix |= static_cast<uint32_t>(static_cast<uint8_t>(block[i])) << (8 * i);

    if (prefix & TEXT_MARKER) {
        const uint32_t size = prefix & ~TEXT_MARKER;
        if (size / MAX_COMPRESSION_RATIO > static_cast<uint32_t>(block.size()))
            return false;
        QByteArray text(static_cast<qsizetype>(size), Qt::Uninitialized);
        if (!decompress(block, reinterpret_cast<unsigned char*>(text.data()), size))
            return false;
        logs::scanEvents(text, 0, text.size(), xml, callback);
        return true;
    }

    const uint32_t size = prefix;
    if (size < CHUNK_HEADER_SIZE || size > evtx::CHUNK_SIZE)
        return false;
    thread_local std::vector<uint8_t> buffer(evtx::CHUNK_SIZE);
    if (!decompress(block, buffer.data(), size))
        return false;
    std::memset(buffer.data() + size, 0, evtx::CHUNK_SIZE - size);

    thread_local evtx::NameTable names;
    thread_local evtx::BinXmlDecoder decoder(names);
    thread_local evtx::Extractor extractor(names);
    extractor.setXmlEnabled(xml);
    const evtx::Chunk chunk(buffer.data());
    if (!chunk.isValid())
        return false;
    decoder.beginChunk(chunk);
    chunk.scan([&](const evtx::RawRecord& record) {
        if (!extractor.extract(decoder, record))
            return true;
        return callback(extractor.event());
    });
    return true;
}

std::optional<evtx::DecodedEvent> event(sql::Connection& connection, qint64 block, qint64 rank, bool xml)
{
    if (block <= 0)
        return std::nullopt;
    sql::Statement statement = connection.prepare(QStringLiteral("SELECT data FROM blocks WHERE id = ?"));
    statement.bind(1, block);
    if (!statement.step())
        return std::nullopt;
    std::optional<evtx::DecodedEvent> found;
    qint64 current = 0;
    scan(statement.blob(0), xml, [&](const evtx::DecodedEvent& e) {
        if (current++ != rank)
            return true;
        found = e;
        return false;
    });
    return found;
}

} // namespace lh::blocks
