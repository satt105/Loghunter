// XML export of Event Viewer or wevtutil: a sequence of <Event> elements.
#pragma once

#include "Content.h"
#include "Log.h"

namespace lh::logs {

class XmlLog final : public Log
{
public:
    bool open(const QString& path, QString* error = nullptr);

    [[nodiscard]] qint64 size() const override { return m_file.size(); }
    [[nodiscard]] QByteArray fingerprint() const override { return m_file.fingerprint(); }
    [[nodiscard]] qint64 estimateRecords() const override;
    [[nodiscard]] int batchCount() const override;
    [[nodiscard]] bool descending() const override { return m_descending; }

    Batch decodeBatch(int index, const Callback& callback) const override;

private:
    MappedFile m_file;
    QByteArray m_converted;
    QByteArrayView m_text;
    bool m_descending = false;
};

} // namespace lh::logs
