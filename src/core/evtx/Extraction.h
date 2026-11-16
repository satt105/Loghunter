// Extraction of the useful fields of an EVTX record and reconstruction of its XML.
#pragma once

#include "BinXml.h"

#include <QString>

#include <vector>

namespace lh::evtx {

struct DataField
{
    QString name;
    QString value;
};

struct DecodedEvent
{
    uint32_t eventId = 0;
    bool eventIdKnown = false;
    uint64_t recordId = 0;
    uint64_t fileTime = 0;
    uint8_t level = 0;
    uint64_t keywords = 0;
    QString machine;
    QString channel;
    QString provider;
    QString sid;
    std::vector<DataField> data;
    QString xml;

    [[nodiscard]] const QString& dataValue(QStringView name) const;
};

class Extractor final : public Visitor
{
public:
    explicit Extractor(NameTable& names);

    void setXmlEnabled(bool active) { m_xmlEnabled = active; }

    bool extract(BinXmlDecoder& decoder, const RawRecord& record);

    [[nodiscard]] const DecodedEvent& event() const noexcept { return m_event; }
    [[nodiscard]] bool lastFailed() const noexcept { return m_error; }

    bool openElement(NameId name) override;
    bool attribute(NameId name, const Value& value) override;
    bool text(const Value& value) override;
    bool closeElement() override;

private:
    enum class Zone : uint8_t { Other, System, EventData, UserData };

    void finishTag();

    NameTable& m_names;
    DecodedEvent m_event;
    std::vector<NameId> m_stack;
    Zone m_zone = Zone::Other;
    QString m_dataName;
    QString m_currentText;
    bool m_inData = false;
    bool m_error = false;
    bool m_xmlEnabled = false;
    bool m_tagOpen = false;
    NameId m_xmlAttribute = -1;
};

void appendEscapedXml(QString& output, QStringView text, bool attribute);

} // namespace lh::evtx
