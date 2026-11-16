#include "BinXml.h"

#include "core/Calendar.h"

#include <QStringList>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>

namespace lh::evtx {

namespace {

constexpr int MAX_DEPTH = 16;
constexpr uint32_t MAX_VALUE_COUNT = 4096;

constexpr int64_t EPOCH_GAP_SECONDS = 11'644'473'600LL;

namespace token {
constexpr uint8_t EndOfStream = 0x00;
constexpr uint8_t OpenElement = 0x01;
constexpr uint8_t EndOpen = 0x02;
constexpr uint8_t CloseEmpty = 0x03;
constexpr uint8_t CloseElement = 0x04;
constexpr uint8_t Value = 0x05;
constexpr uint8_t Attribute = 0x06;
constexpr uint8_t CData = 0x07;
constexpr uint8_t CharRef = 0x08;
constexpr uint8_t EntityRef = 0x09;
constexpr uint8_t PiTarget = 0x0a;
constexpr uint8_t PiData = 0x0b;
constexpr uint8_t InstanceTemplate = 0x0c;
constexpr uint8_t Substitution = 0x0d;
constexpr uint8_t OptionalSubstitution = 0x0e;
constexpr uint8_t FragmentHeader = 0x0f;
constexpr uint8_t MoreFlag = 0x40;
} // namespace token

constexpr uint32_t fixedSize(uint8_t t) noexcept
{
    switch (t) {
    case type::Int8:
    case type::UInt8:
        return 1;
    case type::Int16:
    case type::UInt16:
        return 2;
    case type::Int32:
    case type::UInt32:
    case type::Real32:
    case type::Boolean:
    case type::HexInt32:
        return 4;
    case type::Int64:
    case type::UInt64:
    case type::Real64:
    case type::FileTime:
    case type::HexInt64:
        return 8;
    case type::Guid:
    case type::SysTime:
        return 16;
    default:
        return 0;
    }
}

constexpr char16_t ENTITY_AMP[] = u"&";
constexpr char16_t ENTITY_LT[] = u"<";
constexpr char16_t ENTITY_GT[] = u">";
constexpr char16_t ENTITY_QUOT[] = u"\"";
constexpr char16_t ENTITY_APOS[] = u"'";

QString formatIso(int64_t unixMicroseconds)
{
    const int64_t seconds = floorDivide(unixMicroseconds, 1'000'000);
    const int64_t micro = unixMicroseconds - seconds * 1'000'000;
    const int64_t days = floorDivide(seconds, 86400);
    const int64_t rest = seconds - days * 86400;
    const CivilDate d = dateFromDays(days);
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%04" PRId64 "-%02u-%02uT%02d:%02d:%02d.%06dZ", d.year, d.month, d.day,
                  static_cast<int>(rest / 3600), static_cast<int>((rest / 60) % 60), static_cast<int>(rest % 60),
                  static_cast<int>(micro));
    return QString::fromLatin1(buffer);
}

uint64_t sysTimeToFileTime(const uint8_t* p) noexcept
{
    const unsigned year = read16(p);
    const unsigned month = read16(p + 2);
    const unsigned day = read16(p + 6);
    const unsigned hour = read16(p + 8);
    const unsigned minute = read16(p + 10);
    const unsigned second = read16(p + 12);
    const unsigned milli = read16(p + 14);
    if (year < 1601 || month < 1 || month > 12 || day < 1 || day > 31)
        return 0;
    const int64_t days = daysFromDate(year, month, day);
    const int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second;
    const int64_t fileTime = (seconds + EPOCH_GAP_SECONDS) * 10'000'000LL + milli * 10'000LL;
    return fileTime > 0 ? static_cast<uint64_t>(fileTime) : 0;
}

QString formatHex(const uint8_t* p, uint32_t n)
{
    static constexpr char DIGITS[] = "0123456789ABCDEF";
    QString text(static_cast<qsizetype>(n) * 2, Qt::Uninitialized);
    QChar* output = text.data();
    for (uint32_t i = 0; i < n; ++i) {
        *output++ = QLatin1Char(DIGITS[p[i] >> 4]);
        *output++ = QLatin1Char(DIGITS[p[i] & 0x0f]);
    }
    return text;
}

QString formatGuid(const uint8_t* p)
{
    static constexpr std::array<int, 16> ORDER{3, 2, 1, 0, 5, 4, 7, 6, 8, 9, 10, 11, 12, 13, 14, 15};
    static constexpr char DIGITS[] = "0123456789ABCDEF";
    QString text;
    text.reserve(36);
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10)
            text += QLatin1Char('-');
        const uint8_t byte = p[ORDER[i]];
        text += QLatin1Char(DIGITS[byte >> 4]);
        text += QLatin1Char(DIGITS[byte & 0x0f]);
    }
    return text;
}

QString formatSid(const uint8_t* p, uint32_t n)
{
    if (n < 8)
        return QStringLiteral("S-?");
    uint64_t authority = 0;
    for (int i = 2; i < 8; ++i)
        authority = (authority << 8) | p[i];
    QString text = QStringLiteral("S-%1-%2").arg(p[0]).arg(authority);
    const unsigned count = p[1];
    uint32_t offset = 8;
    for (unsigned i = 0; i < count && offset + 4 <= n; ++i, offset += 4)
        text += QLatin1Char('-') + QString::number(read32(p + offset));
    return text;
}

QString formatHexInteger(uint64_t value)
{
    return QStringLiteral("0x") + QString::number(value, 16);
}

QString scalarToText(uint8_t t, const uint8_t* p, uint32_t n)
{
    switch (t) {
    case type::Null:
        return {};
    case type::String:
        return utf16ToText(p, n / 2);
    case type::AnsiString: {
        QByteArray bytes;
        bytes.reserve(n);
        for (uint32_t i = 0; i < n; ++i)
            if (p[i] != 0)
                bytes.append(static_cast<char>(p[i]));
        return QString::fromLatin1(bytes);
    }
    case type::Int8:
        return n >= 1 ? QString::number(static_cast<int8_t>(p[0])) : QString();
    case type::UInt8:
        return n >= 1 ? QString::number(p[0]) : QString();
    case type::Int16:
        return n >= 2 ? QString::number(static_cast<int16_t>(read16(p))) : QString();
    case type::UInt16:
        return n >= 2 ? QString::number(read16(p)) : QString();
    case type::Int32:
        return n >= 4 ? QString::number(static_cast<int32_t>(read32(p))) : QString();
    case type::UInt32:
        return n >= 4 ? QString::number(read32(p)) : QString();
    case type::Int64:
        return n >= 8 ? QString::number(static_cast<qint64>(read64(p))) : QString();
    case type::UInt64:
        return n >= 8 ? QString::number(static_cast<quint64>(read64(p))) : QString();
    case type::Real32:
        return n >= 4 ? QString::number(readRaw<float>(p)) : QString();
    case type::Real64:
        return n >= 8 ? QString::number(readRaw<double>(p), 'g', 17) : QString();
    case type::Boolean:
        return n >= 4 ? (read32(p) != 0 ? QStringLiteral("true") : QStringLiteral("false")) : QString();
    case type::Binary:
        return formatHex(p, n);
    case type::Guid:
        return n >= 16 ? formatGuid(p) : QString();
    case type::SizeT:
        if (n == 4)
            return formatHexInteger(read32(p));
        if (n == 8)
            return formatHexInteger(read64(p));
        return formatHex(p, n);
    case type::FileTime:
        return n >= 8 ? fileTimeToIso(read64(p)) : QString();
    case type::SysTime:
        return n >= 16 ? fileTimeToIso(sysTimeToFileTime(p)) : QString();
    case type::Sid:
        return formatSid(p, n);
    case type::HexInt32:
        return n >= 4 ? formatHexInteger(read32(p)) : QString();
    case type::HexInt64:
        return n >= 8 ? formatHexInteger(read64(p)) : QString();
    default:
        return formatHex(p, n);
    }
}

} // namespace

// ── Public conversions ───────────────────────────────────────────────────────

uint64_t isoToFileTime(const QString& text)
{
    const QString t = text.trimmed();
    if (t.size() < 19 || t[4] != u'-' || t[7] != u'-' || (t[10] != u'T' && t[10] != u' ') || t[13] != u':')
        return 0;
    auto count = [&](qsizetype start, qsizetype length, bool& ok) {
        return t.mid(start, length).toUInt(&ok);
    };
    bool ok[6];
    const unsigned year = count(0, 4, ok[0]);
    const unsigned month = count(5, 2, ok[1]);
    const unsigned day = count(8, 2, ok[2]);
    const unsigned hour = count(11, 2, ok[3]);
    const unsigned minute = count(14, 2, ok[4]);
    const unsigned second = count(17, 2, ok[5]);
    for (bool b : ok)
        if (!b)
            return 0;
    if (year < 1601 || month < 1 || month > 12 || day < 1 || day > 31 || hour > 23 || minute > 59 || second > 60)
        return 0;

    qsizetype position = 19;
    int64_t hundredNanos = 0;
    if (position < t.size() && t[position] == u'.') {
        ++position;
        int digitsRead = 0;
        while (position < t.size() && t[position].isDigit()) {
            if (digitsRead < 7) {
                hundredNanos = hundredNanos * 10 + t[position].digitValue();
                ++digitsRead;
            }
            ++position;
        }
        for (; digitsRead < 7; ++digitsRead)
            hundredNanos *= 10;
    }
    int64_t shift = 0;
    if (position < t.size() && (t[position] == u'+' || t[position] == u'-') && position + 6 <= t.size()) {
        const int sign = t[position] == u'+' ? 1 : -1;
        bool okH = false, okM = false;
        const unsigned h = t.mid(position + 1, 2).toUInt(&okH);
        const unsigned m = t.mid(position + 4, 2).toUInt(&okM);
        if (okH && okM)
            shift = sign * static_cast<int64_t>(h * 3600 + m * 60);
    }

    const int64_t seconds = daysFromDate(year, month, day) * 86400 + hour * 3600 + minute * 60 + second - shift;
    const int64_t fileTime = (seconds + EPOCH_GAP_SECONDS) * 10'000'000LL + hundredNanos;
    return fileTime > 0 ? static_cast<uint64_t>(fileTime) : 0;
}

bool parseIntegerText(const QString& text, uint64_t& output)
{
    const QString t = text.trimmed();
    bool ok = false;
    if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        output = t.mid(2).toULongLong(&ok, 16);
    else
        output = t.toULongLong(&ok, 10);
    return ok;
}

QString utf16ToText(const uint8_t* data, size_t characterCount)
{
    for (size_t i = 0; i < characterCount; ++i) {
        if (read16(data + 2 * i) == 0) {
            characterCount = i;
            break;
        }
    }
    QString text(static_cast<qsizetype>(characterCount), Qt::Uninitialized);
    std::memcpy(text.data(), data, characterCount * 2);
    return text;
}

int64_t fileTimeToUnixMicroseconds(uint64_t fileTime) noexcept
{
    return static_cast<int64_t>(fileTime / 10) - EPOCH_GAP_SECONDS * 1'000'000LL;
}

uint64_t unixMicrosecondsToFileTime(int64_t microseconds) noexcept
{
    const int64_t fileTime = (microseconds + EPOCH_GAP_SECONDS * 1'000'000LL) * 10;
    return fileTime > 0 ? static_cast<uint64_t>(fileTime) : 0;
}

QString fileTimeToIso(uint64_t fileTime)
{
    return formatIso(fileTimeToUnixMicroseconds(fileTime));
}

QString valueToText(const Value& value)
{
    if (value.data == nullptr)
        return {};
    if ((value.type & type::Array) == 0)
        return scalarToText(value.type, value.data, value.size);

    const uint8_t base = value.type & ~type::Array;
    QStringList elements;
    if (base == type::String) {
        const uint32_t count = value.size / 2;
        uint32_t start = 0;
        for (uint32_t i = 0; i < count; ++i) {
            if (read16(value.data + 2 * i) == 0) {
                if (i > start)
                    elements << utf16ToText(value.data + 2 * start, i - start);
                start = i + 1;
            }
        }
        if (start < count)
            elements << utf16ToText(value.data + 2 * start, count - start);
    } else if (base == type::Sid) {
        uint32_t offset = 0;
        while (offset + 8 <= value.size) {
            const uint32_t size = 8 + 4u * value.data[offset + 1];
            elements << formatSid(value.data + offset, std::min(size, value.size - offset));
            offset += size;
        }
    } else {
        const uint32_t width = fixedSize(base);
        if (width == 0)
            return formatHex(value.data, value.size);
        for (uint32_t offset = 0; offset + width <= value.size; offset += width)
            elements << scalarToText(base, value.data + offset, width);
    }
    return elements.join(QLatin1Char(','));
}

bool valueToInteger(const Value& value, uint64_t& output)
{
    const uint8_t* p = value.data;
    const uint32_t n = value.size;
    if (p == nullptr)
        return false;
    switch (value.type) {
    case type::Int8:
    case type::UInt8:
        if (n < 1)
            return false;
        output = p[0];
        return true;
    case type::Int16:
    case type::UInt16:
        if (n < 2)
            return false;
        output = read16(p);
        return true;
    case type::Int32:
    case type::UInt32:
    case type::HexInt32:
        if (n < 4)
            return false;
        output = read32(p);
        return true;
    case type::Int64:
    case type::UInt64:
    case type::HexInt64:
        if (n < 8)
            return false;
        output = read64(p);
        return true;
    case type::SizeT:
        if (n == 4) {
            output = read32(p);
            return true;
        }
        if (n == 8) {
            output = read64(p);
            return true;
        }
        return false;
    case type::String:
    case type::AnsiString:
        return parseIntegerText(valueToText(value), output);
    default:
        return false;
    }
}

uint64_t valueToFileTime(const Value& value)
{
    if (value.data == nullptr)
        return 0;
    if (value.type == type::FileTime && value.size >= 8)
        return read64(value.data);
    if (value.type == type::SysTime && value.size >= 16)
        return sysTimeToFileTime(value.data);
    if (value.type == type::String || value.type == type::AnsiString)
        return isoToFileTime(valueToText(value));
    return 0;
}

// ── NameTable ────────────────────────────────────────────────────────────────

NameTable::NameTable()
{
    static const char* const KNOWN[] = {"Event",      "System",        "Provider", "EventID",  "TimeCreated",
                                        "SystemTime", "EventRecordID", "Channel",  "Computer", "EventData",
                                        "UserData",   "Data",          "Name",     "Level",    "Keywords",
                                        "Security",   "UserID"};
    static_assert(std::size(KNOWN) == name::KnownCount);
    for (const char* text : KNOWN)
        intern(QString::fromLatin1(text));
}

NameId NameTable::intern(const QString& text)
{
    const auto found = m_index.constFind(text);
    if (found != m_index.constEnd())
        return found.value();
    const auto id = static_cast<NameId>(m_names.size());
    m_names.push_back(text);
    m_index.insert(text, id);
    return id;
}

const QString& NameTable::text(NameId id) const
{
    static const QString EMPTY;
    if (id < 0 || static_cast<size_t>(id) >= m_names.size())
        return EMPTY;
    return m_names[static_cast<size_t>(id)];
}

// ── BinXmlDecoder ────────────────────────────────────────────────────────────

BinXmlDecoder::BinXmlDecoder(NameTable& names) : m_names(names)
{
    m_valueStack.resize(MAX_DEPTH + 2);
}

void BinXmlDecoder::beginChunk(const Chunk& chunk)
{
    m_chunk = chunk.data();
    m_nameCache.clear();
    m_templates.clear();
}

Result BinXmlDecoder::decode(const RawRecord& record, Visitor& visitor)
{
    if (!m_chunk || record.binxml == nullptr)
        return Result::Error;
    return decodeStream(record.offsetBinxml, record.binxmlSize, visitor, 0);
}

NameId BinXmlDecoder::nameAt(uint32_t offset)
{
    const auto found = m_nameCache.find(offset);
    if (found != m_nameCache.end())
        return found->second;
    if (static_cast<size_t>(offset) + 8 > CHUNK_SIZE)
        return -1;
    const uint16_t length = read16(m_chunk + offset + 6);
    if (static_cast<size_t>(offset) + 8 + 2u * length > CHUNK_SIZE)
        return -1;
    const NameId id = m_names.intern(utf16ToText(m_chunk + offset + 8, length));
    m_nameCache.emplace(offset, id);
    return id;
}

NameId BinXmlDecoder::readName(Cursor& cursor)
{
    const uint32_t offset = cursor.u32();
    if (!cursor.ok())
        return -1;
    if (cursor.position() == offset) {
        if (!cursor.available(8))
            return -1;
        const uint16_t length = read16(cursor.pointer() + 6);
        cursor.skip(10 + 2u * length);
    }
    return nameAt(offset);
}

Value BinXmlDecoder::entityValue(NameId id) const
{
    const QString& text = m_names.text(id);
    const char16_t* character = nullptr;
    if (text == QLatin1String("amp"))
        character = ENTITY_AMP;
    else if (text == QLatin1String("lt"))
        character = ENTITY_LT;
    else if (text == QLatin1String("gt"))
        character = ENTITY_GT;
    else if (text == QLatin1String("quot"))
        character = ENTITY_QUOT;
    else if (text == QLatin1String("apos"))
        character = ENTITY_APOS;
    if (!character)
        return {};
    return {type::String, reinterpret_cast<const uint8_t*>(character), 2};
}

bool BinXmlDecoder::emitValue(NameId currentAttribute, const Value& value, Visitor& visitor)
{
    return currentAttribute >= 0 ? visitor.attribute(currentAttribute, value) : visitor.text(value);
}

Result BinXmlDecoder::decodeStream(size_t start, size_t size, Visitor& visitor, int depth)
{
    if (depth > MAX_DEPTH || start >= CHUNK_SIZE)
        return Result::Error;
    const size_t end = std::min(start + size, CHUNK_SIZE);
    Cursor cursor(m_chunk, end, start);
    NameId currentAttribute = -1;

    while (cursor.ok() && cursor.position() < end) {
        const uint8_t code = cursor.u8();
        switch (code & ~token::MoreFlag) {
        case token::EndOfStream:
            return Result::Ok;
        case token::FragmentHeader:
            cursor.skip(3);
            break;
        case token::InstanceTemplate: {
            const Result result = instantiate(cursor, visitor, depth);
            if (result != Result::Ok)
                return result;
            break;
        }
        case token::OpenElement: {
            cursor.u32();
            const NameId name = readName(cursor);
            if (code & token::MoreFlag)
                cursor.u32();
            if (!cursor.ok())
                return Result::Error;
            currentAttribute = -1;
            if (!visitor.openElement(name))
                return Result::Interrupted;
            break;
        }
        case token::Attribute:
            currentAttribute = readName(cursor);
            break;
        case token::EndOpen:
            currentAttribute = -1;
            break;
        case token::CloseEmpty:
        case token::CloseElement:
            currentAttribute = -1;
            if (!visitor.closeElement())
                return Result::Interrupted;
            break;
        case token::Value: {
            const uint8_t valueType = cursor.u8();
            Value value{valueType, nullptr, 0};
            if (valueType == type::String) {
                value.size = 2u * cursor.u16();
            } else {
                value.size = fixedSize(valueType);
                if (value.size == 0)
                    return Result::Error;
            }
            value.data = cursor.take(value.size);
            if (!cursor.ok())
                return Result::Error;
            if (!emitValue(currentAttribute, value, visitor))
                return Result::Interrupted;
            break;
        }
        case token::CData: {
            Value value{type::String, nullptr, 2u * cursor.u16()};
            value.data = cursor.take(value.size);
            if (!cursor.ok())
                return Result::Error;
            if (!visitor.text(value))
                return Result::Interrupted;
            break;
        }
        case token::CharRef: {
            const Value value{type::String, cursor.take(2), 2};
            if (!cursor.ok())
                return Result::Error;
            if (!emitValue(currentAttribute, value, visitor))
                return Result::Interrupted;
            break;
        }
        case token::EntityRef: {
            const Value value = entityValue(readName(cursor));
            if (value.data && !emitValue(currentAttribute, value, visitor))
                return Result::Interrupted;
            break;
        }
        case token::PiTarget:
            readName(cursor);
            break;
        case token::PiData:
            cursor.skip(2u * cursor.u16());
            break;
        default:
            return Result::Error;
        }
    }
    return cursor.ok() ? Result::Ok : Result::Error;
}

Result BinXmlDecoder::instantiate(Cursor& cursor, Visitor& visitor, int depth)
{
    cursor.u8();
    cursor.u32();
    const uint32_t offsetDefinition = cursor.u32();
    if (!cursor.ok())
        return Result::Error;

    if (cursor.position() == offsetDefinition) {
        cursor.skip(4 + 16);
        const uint32_t definitionSize = cursor.u32();
        cursor.skip(definitionSize);
    }

    const uint32_t count = cursor.u32();
    if (!cursor.ok() || count > MAX_VALUE_COUNT)
        return Result::Error;
    const uint8_t* descriptors = cursor.take(4u * count);
    if (!cursor.ok())
        return Result::Error;

    std::vector<Value>& values = m_valueStack[static_cast<size_t>(depth)];
    values.resize(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint16_t size = read16(descriptors + 4 * i);
        const uint8_t valueType = descriptors[4 * i + 2];
        const uint8_t* data = cursor.take(size);
        if (!cursor.ok())
            return Result::Error;
        values[i] = {valueType, data, size};
    }

    const Template* compiledTemplate = templateAt(offsetDefinition);
    if (!compiledTemplate)
        return Result::Error;
    return execute(*compiledTemplate, values, visitor, depth);
}

const BinXmlDecoder::Template* BinXmlDecoder::templateAt(uint32_t offsetDefinition)
{
    auto found = m_templates.find(offsetDefinition);
    if (found == m_templates.end()) {
        Template compiled;
        compile(offsetDefinition, compiled);
        found = m_templates.emplace(offsetDefinition, std::move(compiled)).first;
    }
    return found->second.isValid ? &found->second : nullptr;
}

bool BinXmlDecoder::compile(uint32_t offsetDefinition, Template& output)
{
    if (static_cast<size_t>(offsetDefinition) + 24 > CHUNK_SIZE)
        return false;
    const uint32_t size = read32(m_chunk + offsetDefinition + 20);
    const size_t start = static_cast<size_t>(offsetDefinition) + 24;
    const size_t end = std::min(start + size, CHUNK_SIZE);
    Cursor cursor(m_chunk, end, start);
    auto& operations = output.operations;
    operations.reserve(64);

    while (cursor.ok() && cursor.position() < end) {
        const uint8_t code = cursor.u8();
        switch (code & ~token::MoreFlag) {
        case token::EndOfStream:
            output.isValid = cursor.ok();
            return output.isValid;
        case token::FragmentHeader:
            cursor.skip(3);
            break;
        case token::OpenElement: {
            cursor.u16();
            cursor.u32();
            Operation operation{Code::Open};
            operation.name = readName(cursor);
            if (code & token::MoreFlag)
                cursor.u32();
            operations.push_back(operation);
            break;
        }
        case token::Attribute: {
            Operation operation{Code::Attribute};
            operation.name = readName(cursor);
            operations.push_back(operation);
            break;
        }
        case token::EndOpen:
            operations.push_back({Code::EndOpen});
            break;
        case token::CloseEmpty:
            operations.push_back({Code::CloseEmpty});
            break;
        case token::CloseElement:
            operations.push_back({Code::Close});
            break;
        case token::Value: {
            Operation operation{Code::Literal};
            operation.valueType = cursor.u8();
            if (operation.valueType == type::String)
                operation.size = 2u * cursor.u16();
            else if ((operation.size = fixedSize(operation.valueType)) == 0)
                return false;
            operation.offset = static_cast<uint32_t>(cursor.position());
            cursor.skip(operation.size);
            operations.push_back(operation);
            break;
        }
        case token::CData: {
            Operation operation{Code::Literal};
            operation.valueType = type::String;
            operation.size = 2u * cursor.u16();
            operation.offset = static_cast<uint32_t>(cursor.position());
            cursor.skip(operation.size);
            operations.push_back(operation);
            break;
        }
        case token::CharRef: {
            Operation operation{Code::Literal};
            operation.valueType = type::String;
            operation.size = 2;
            operation.offset = static_cast<uint32_t>(cursor.position());
            cursor.skip(2);
            operations.push_back(operation);
            break;
        }
        case token::EntityRef: {
            Operation operation{Code::Entity};
            operation.name = readName(cursor);
            operations.push_back(operation);
            break;
        }
        case token::PiTarget:
            readName(cursor);
            break;
        case token::PiData:
            cursor.skip(2u * cursor.u16());
            break;
        case token::Substitution:
        case token::OptionalSubstitution: {
            Operation operation{Code::Substitution};
            operation.isOptional = (code & ~token::MoreFlag) == token::OptionalSubstitution;
            operation.index = cursor.u16();
            operation.valueType = cursor.u8();
            operations.push_back(operation);
            break;
        }
        default:
            return false;
        }
    }
    output.isValid = cursor.ok();
    return output.isValid;
}

Result BinXmlDecoder::execute(const Template& compiledTemplate, const std::vector<Value>& values,
                              Visitor& visitor, int depth)
{
    NameId currentAttribute = -1;
    for (const Operation& operation : compiledTemplate.operations) {
        switch (operation.code) {
        case Code::Open:
            currentAttribute = -1;
            if (!visitor.openElement(operation.name))
                return Result::Interrupted;
            break;
        case Code::Attribute:
            currentAttribute = operation.name;
            break;
        case Code::EndOpen:
            currentAttribute = -1;
            break;
        case Code::CloseEmpty:
        case Code::Close:
            currentAttribute = -1;
            if (!visitor.closeElement())
                return Result::Interrupted;
            break;
        case Code::Literal: {
            const Value value{operation.valueType, m_chunk + operation.offset, operation.size};
            if (!emitValue(currentAttribute, value, visitor))
                return Result::Interrupted;
            break;
        }
        case Code::Entity: {
            const Value value = entityValue(operation.name);
            if (value.data && !emitValue(currentAttribute, value, visitor))
                return Result::Interrupted;
            break;
        }
        case Code::Substitution: {
            if (operation.index >= values.size())
                break;
            const Value& value = values[operation.index];
            if (value.type == type::Null || value.data == nullptr)
                break;
            if (operation.isOptional && value.size == 0)
                break;
            if (value.type == type::BinXml) {
                if (currentAttribute >= 0 || value.size == 0)
                    break;
                const auto offset = static_cast<size_t>(value.data - m_chunk);
                const Result result = decodeStream(offset, value.size, visitor, depth + 1);
                if (result != Result::Ok)
                    return result;
                break;
            }
            if (!emitValue(currentAttribute, value, visitor))
                return Result::Interrupted;
            break;
        }
        }
    }
    return Result::Ok;
}

} // namespace lh::evtx
