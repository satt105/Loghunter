// Memory-mapped file and reading of its text as UTF-8.
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QFile>
#include <QString>

namespace lh::logs {

inline constexpr qsizetype BATCH_SIZE = 65536;

class MappedFile
{
public:
    MappedFile() = default;
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    bool open(const QString& path, QString* error = nullptr);
    void close();

    [[nodiscard]] qint64 size() const noexcept { return m_size; }
    [[nodiscard]] QByteArrayView bytes() const noexcept;
    [[nodiscard]] QByteArray fingerprint() const;

private:
    QFile m_file;
    const uchar* m_mapped = nullptr;
    qint64 m_size = 0;
};

QByteArrayView utf8Text(QByteArrayView bytes, bool check, QByteArray& converted);

} // namespace lh::logs
