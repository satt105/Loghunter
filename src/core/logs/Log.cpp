#include "Log.h"

#include "Content.h"
#include "TabularLog.h"
#include "XmlLog.h"
#include "core/Blocks.h"
#include "core/evtx/EvtxFile.h"

#include <QFile>
#include <QFileInfo>

namespace lh::logs {

namespace {

constexpr QByteArrayView EVTX_MAGIC("ElfFile\0", 8);

enum class Format { Unknown, Evtx, Xml, Tabular };

class EvtxLog final : public Log
{
public:
    bool open(const QString& path, QString* error) { return m_file.open(path, error); }

    [[nodiscard]] qint64 size() const override { return m_file.size(); }
    [[nodiscard]] QByteArray fingerprint() const override { return m_file.fingerprint(); }
    [[nodiscard]] qint64 estimateRecords() const override
    {
        return static_cast<qint64>(m_file.estimateRecords());
    }
    [[nodiscard]] int batchCount() const override { return m_file.chunkCount(); }

    Batch decodeBatch(int index, const Callback& callback) const override
    {
        thread_local evtx::NameTable names;
        thread_local evtx::BinXmlDecoder decoder(names);
        thread_local evtx::Extractor extractor(names);
        extractor.setXmlEnabled(false);

        Batch batch;
        const evtx::Chunk chunk = m_file.chunk(index);
        if (!chunk.isValid())
            return batch;
        decoder.beginChunk(chunk);
        bool kept = false;
        const bool complete = chunk.scan([&](const evtx::RawRecord& record) {
            ++batch.readCount;
            if (!extractor.extract(decoder, record)) {
                if (extractor.lastFailed())
                    ++batch.errors;
                return true;
            }
            kept = true;
            return callback(extractor.event());
        });
        if (!complete)
            ++batch.errors;
        if (kept)
            batch.block = blocks::compress(chunk);
        return batch;
    }

private:
    evtx::EvtxFile m_file;
};

Format detect(QByteArrayView head)
{
    if (head.startsWith(EVTX_MAGIC))
        return Format::Evtx;
    QByteArray converted;
    const QByteArrayView text = utf8Text(head, false, converted);
    if (TabularLog::recognize(text))
        return Format::Tabular;
    if (eventStart(text, 0) >= 0)
        return Format::Xml;
    return Format::Unknown;
}

Format detect(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QFileInfo::exists(path) ? qtTrId("evtx.open_failed") : qtTrId("common.file_not_found");
        return Format::Unknown;
    }
    const Format format = detect(file.read(BATCH_SIZE));
    if (format == Format::Unknown && error)
        *error = qtTrId("log.unknown_format");
    return format;
}

template <typename Type, typename... Arguments>
std::unique_ptr<Log> openType(const QString& path, QString* error, Arguments&&... arguments)
{
    auto log = std::make_unique<Type>(std::forward<Arguments>(arguments)...);
    if (!log->open(path, error))
        return nullptr;
    return log;
}

} // namespace

std::unique_ptr<Log> open(const QString& path, const QByteArray& timeZone, QString* error)
{
    switch (detect(path, error)) {
    case Format::Evtx:
        return openType<EvtxLog>(path, error);
    case Format::Xml:
        return openType<XmlLog>(path, error);
    case Format::Tabular:
        return openType<TabularLog>(path, error, timeZone);
    case Format::Unknown:
        break;
    }
    return nullptr;
}

const QStringList& extensions()
{
    static const QStringList EXTENSIONS{QStringLiteral("evtx"), QStringLiteral("xml"), QStringLiteral("csv"),
                                        QStringLiteral("txt")};
    return EXTENSIONS;
}

bool isKnownExtension(const QString& path)
{
    return extensions().contains(QFileInfo(path).suffix(), Qt::CaseInsensitive);
}

bool recognized(const QString& path)
{
    return detect(path, nullptr) != Format::Unknown;
}

} // namespace lh::logs
