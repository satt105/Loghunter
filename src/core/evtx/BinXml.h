// BinXML decoder (binary XML of EVTX logs).
#pragma once

#include "EvtxFile.h"
#include "Bytes.h"

#include <QHash>
#include <QString>

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace lh::evtx {

namespace type {
inline constexpr uint8_t Null = 0x00;
inline constexpr uint8_t String = 0x01;
inline constexpr uint8_t AnsiString = 0x02;
inline constexpr uint8_t Int8 = 0x03;
inline constexpr uint8_t UInt8 = 0x04;
inline constexpr uint8_t Int16 = 0x05;
inline constexpr uint8_t UInt16 = 0x06;
inline constexpr uint8_t Int32 = 0x07;
inline constexpr uint8_t UInt32 = 0x08;
inline constexpr uint8_t Int64 = 0x09;
inline constexpr uint8_t UInt64 = 0x0a;
inline constexpr uint8_t Real32 = 0x0b;
inline constexpr uint8_t Real64 = 0x0c;
inline constexpr uint8_t Boolean = 0x0d;
inline constexpr uint8_t Binary = 0x0e;
inline constexpr uint8_t Guid = 0x0f;
inline constexpr uint8_t SizeT = 0x10;
inline constexpr uint8_t FileTime = 0x11;
inline constexpr uint8_t SysTime = 0x12;
inline constexpr uint8_t Sid = 0x13;
inline constexpr uint8_t HexInt32 = 0x14;
inline constexpr uint8_t HexInt64 = 0x15;
inline constexpr uint8_t EvtHandle = 0x20;
inline constexpr uint8_t BinXml = 0x21;
inline constexpr uint8_t EvtXml = 0x23;
inline constexpr uint8_t Array = 0x80;
} // namespace type

struct Value
{
    uint8_t type = type::Null;
    const uint8_t* data = nullptr;
    uint32_t size = 0;

    [[nodiscard]] bool empty() const noexcept { return type == type::Null || size == 0; }
};

using NameId = int32_t;
namespace name {
enum : NameId {
    Event,
    System,
    Provider,
    EventID,
    TimeCreated,
    SystemTime,
    EventRecordID,
    Channel,
    Computer,
    EventData,
    UserData,
    Data,
    Name,
    Level,
    Keywords,
    Security,
    UserID,
    KnownCount
};
} // namespace name

class NameTable
{
public:
    NameTable();
    NameId intern(const QString& text);
    [[nodiscard]] const QString& text(NameId id) const;

private:
    QHash<QString, NameId> m_index;
    std::vector<QString> m_names;
};

class Visitor
{
public:
    virtual ~Visitor() = default;
    virtual bool openElement(NameId name) = 0;
    virtual bool attribute(NameId name, const Value& value) = 0;
    virtual bool text(const Value& value) = 0;
    virtual bool closeElement() = 0;
};

enum class Result { Ok, Interrupted, Error };

class BinXmlDecoder
{
public:
    explicit BinXmlDecoder(NameTable& names);

    void beginChunk(const Chunk& chunk);

    Result decode(const RawRecord& record, Visitor& visitor);

private:
    enum class Code : uint8_t { Open, Attribute, EndOpen, CloseEmpty, Close, Literal, Entity, Substitution };

    struct Operation
    {
        Code code;
        uint8_t valueType = 0;
        bool isOptional = false;
        uint16_t index = 0;
        NameId name = -1;
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    struct Template
    {
        std::vector<Operation> operations;
        bool isValid = false;
    };

    Result decodeStream(size_t start, size_t size, Visitor& visitor, int depth);
    Result instantiate(Cursor& cursor, Visitor& visitor, int depth);
    Result execute(const Template& tmpl, const std::vector<Value>& values, Visitor& visitor,
                   int depth);
    const Template* templateAt(uint32_t offsetDefinition);
    bool compile(uint32_t offsetDefinition, Template& output);
    bool emitValue(NameId currentAttribute, const Value& value, Visitor& visitor);

    NameId readName(Cursor& cursor);
    NameId nameAt(uint32_t offset);
    Value entityValue(NameId name) const;

    NameTable& m_names;
    const uint8_t* m_chunk = nullptr;
    std::unordered_map<uint32_t, NameId> m_nameCache;
    std::unordered_map<uint32_t, Template> m_templates;
    std::vector<std::vector<Value>> m_valueStack;
};

// ── Value conversions ────────────────────────────────────────────────────────

QString valueToText(const Value& value);

bool valueToInteger(const Value& value, uint64_t& output);

uint64_t valueToFileTime(const Value& value);

uint64_t isoToFileTime(const QString& text);
bool parseIntegerText(const QString& text, uint64_t& output);

int64_t fileTimeToUnixMicroseconds(uint64_t fileTime) noexcept;
uint64_t unixMicrosecondsToFileTime(int64_t microseconds) noexcept;
QString fileTimeToIso(uint64_t fileTime);

QString utf16ToText(const uint8_t* data, size_t characterCount);

} // namespace lh::evtx
