#include "Content.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QStringDecoder>

#include <algorithm>

namespace lh::logs {

namespace {

constexpr qint64 HEAD_SIZE = 4096 + 65536;
constexpr qint64 TAIL_SIZE = 65536;

QByteArrayView convert(QByteArrayView bytes, QStringConverter::Encoding encoding, QByteArray& converted)
{
    QStringDecoder decoder(encoding);
    const QString text = decoder.decode(bytes);
    converted = text.toUtf8();
    return converted;
}

#ifdef Q_OS_WIN
QByteArrayView convertAnsi(QByteArrayView bytes, QByteArray& converted)
{
    return convert(bytes, QStringConverter::System, converted);
}
#else
constexpr char16_t WINDOWS_1252[32] = {0x20ac, 0x0081, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
                                       0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008d, 0x017d, 0x008f,
                                       0x0090, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
                                       0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0x009d, 0x017e, 0x0178};

QByteArrayView convertAnsi(QByteArrayView bytes, QByteArray& converted)
{
    QString text = QString::fromLatin1(bytes);
    for (QChar& c : text)
        if (c.unicode() >= 0x80 && c.unicode() < 0xa0)
            c = QChar(WINDOWS_1252[c.unicode() - 0x80]);
    converted = text.toUtf8();
    return converted;
}
#endif

bool likelyUtf8(QByteArrayView bytes) noexcept
{
    qsizetype valid = 0;
    qsizetype invalid = 0;
    const qsizetype n = bytes.size();
    for (qsizetype i = 0; i < n;) {
        const auto c = static_cast<uint8_t>(bytes[i]);
        if (c < 0x80) {
            ++i;
            continue;
        }
        const int remainder = c >= 0xf5 ? 0 : c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc2 ? 1 : 0;
        bool isValid = remainder > 0 && i + remainder < n;
        for (int k = 1; isValid && k <= remainder; ++k)
            isValid = (static_cast<uint8_t>(bytes[i + k]) & 0xc0) == 0x80;
        if (isValid) {
            ++valid;
            i += remainder + 1;
        } else {
            ++invalid;
            ++i;
        }
    }
    return valid > invalid;
}

} // namespace

MappedFile::~MappedFile() { close(); }

bool MappedFile::open(const QString& path, QString* error)
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
    m_mapped = m_size > 0 ? m_file.map(0, m_size) : nullptr;
    if (!m_mapped)
        return failure(qtTrId("evtx.load_failed"));
    return true;
}

void MappedFile::close()
{
    if (m_mapped) {
        m_file.unmap(const_cast<uchar*>(m_mapped));
        m_mapped = nullptr;
    }
    if (m_file.isOpen())
        m_file.close();
    m_size = 0;
}

QByteArrayView MappedFile::bytes() const noexcept
{
    return m_mapped ? QByteArrayView(reinterpret_cast<const char*>(m_mapped), m_size) : QByteArrayView();
}

QByteArray MappedFile::fingerprint() const
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    const qint64 size = m_size;
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(&size), sizeof(size)));
    const QByteArrayView content = bytes();
    const qint64 head = std::min(m_size, HEAD_SIZE);
    hash.addData(content.first(head));
    hash.addData(content.sliced(std::max(head, m_size - TAIL_SIZE)));
    return hash.result().toHex();
}

QByteArrayView utf8Text(QByteArrayView bytes, bool check, QByteArray& converted)
{
    const auto byte = [&bytes](qsizetype i) { return static_cast<uint8_t>(bytes[i]); };
    const qsizetype n = bytes.size();
    if (n >= 3 && byte(0) == 0xef && byte(1) == 0xbb && byte(2) == 0xbf)
        return bytes.sliced(3);
    if (n >= 2 && byte(0) == 0xff && byte(1) == 0xfe)
        return convert(bytes.sliced(2), QStringConverter::Utf16LE, converted);
    if (n >= 2 && byte(0) == 0xfe && byte(1) == 0xff)
        return convert(bytes.sliced(2), QStringConverter::Utf16BE, converted);
    if (n >= 4 && byte(0) != 0 && byte(1) == 0 && byte(2) != 0 && byte(3) == 0)
        return convert(bytes, QStringConverter::Utf16LE, converted);
    if (n >= 4 && byte(0) == 0 && byte(1) != 0 && byte(2) == 0 && byte(3) != 0)
        return convert(bytes, QStringConverter::Utf16BE, converted);
    if (check && !bytes.isValidUtf8() && !likelyUtf8(bytes))
        return convertAnsi(bytes, converted);
    return bytes;
}

} // namespace lh::logs
