// CSV or tab-separated export of Event Viewer: level, date, source, number, category, message.
#pragma once

#include "Content.h"
#include "Log.h"

#include <vector>

namespace lh::logs {

class TabularLog final : public Log
{
public:
    explicit TabularLog(QByteArray timeZone) : m_timeZone(std::move(timeZone)) {}

    static bool recognize(QByteArrayView text);

    bool open(const QString& path, QString* error = nullptr);

    [[nodiscard]] qint64 size() const override { return m_file.size(); }
    [[nodiscard]] QByteArray fingerprint() const override { return m_file.fingerprint(); }
    [[nodiscard]] qint64 estimateRecords() const override { return m_records; }
    [[nodiscard]] int batchCount() const override { return static_cast<int>(m_batches.size()); }
    [[nodiscard]] bool reduced() const override { return true; }
    [[nodiscard]] bool descending() const override { return m_descending; }

    Batch decodeBatch(int index, const Callback& callback) const override;

private:
    MappedFile m_file;
    QByteArray m_converted;
    QByteArrayView m_text;
    QByteArray m_timeZone;
    std::vector<qsizetype> m_batches;
    qint64 m_records = 0;
    char m_delimiter = ',';
    bool m_monthFirst = false;
    bool m_descending = false;
};

} // namespace lh::logs
