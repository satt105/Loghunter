#include "Extraction.h"

namespace lh::evtx {

void appendEscapedXml(QString& output, QStringView text, bool attribute)
{
    for (const QChar c : text) {
        switch (c.unicode()) {
        case u'&':
            output += QLatin1String("&amp;");
            break;
        case u'<':
            output += QLatin1String("&lt;");
            break;
        case u'>':
            output += QLatin1String("&gt;");
            break;
        case u'"':
            if (attribute)
                output += QLatin1String("&quot;");
            else
                output += c;
            break;
        default:
            if (c.unicode() >= 0x20 || c == u'\t' || c == u'\n' || c == u'\r')
                output += c;
        }
    }
}

const QString& DecodedEvent::dataValue(QStringView name) const
{
    static const QString EMPTY;
    for (const DataField& field : data)
        if (field.name == name)
            return field.value;
    return EMPTY;
}

Extractor::Extractor(NameTable& names) : m_names(names)
{
    m_stack.reserve(16);
}

bool Extractor::extract(BinXmlDecoder& decoder, const RawRecord& record)
{
    m_event = DecodedEvent();
    m_stack.clear();
    m_zone = Zone::Other;
    m_dataName.clear();
    m_currentText.clear();
    m_inData = false;
    m_error = false;
    m_tagOpen = false;
    m_xmlAttribute = -1;
    if (m_xmlEnabled)
        m_event.xml.reserve(2048);

    const Result result = decoder.decode(record, *this);
    if (result == Result::Interrupted)
        return false;
    if (result == Result::Error) {
        m_error = true;
        return false;
    }
    if (!m_event.eventIdKnown)
        return false;
    if (m_event.fileTime == 0)
        m_event.fileTime = record.writtenAt;
    return true;
}

void Extractor::finishTag()
{
    if (!m_tagOpen)
        return;
    if (m_xmlAttribute >= 0)
        m_event.xml += QLatin1Char('"');
    m_event.xml += QLatin1Char('>');
    m_tagOpen = false;
    m_xmlAttribute = -1;
}

bool Extractor::openElement(NameId name)
{
    if (m_xmlEnabled) {
        finishTag();
        m_event.xml += QLatin1Char('<');
        m_event.xml += m_names.text(name);
        m_tagOpen = true;
    }
    m_stack.push_back(name);
    const size_t depth = m_stack.size();
    if (depth == 2) {
        switch (name) {
        case name::System:
            m_zone = Zone::System;
            break;
        case name::EventData:
            m_zone = Zone::EventData;
            break;
        case name::UserData:
            m_zone = Zone::UserData;
            break;
        default:
            m_zone = Zone::Other;
        }
    } else if (depth == 3 && m_zone == Zone::EventData) {
        m_inData = true;
        m_dataName.clear();
    }
    m_currentText.clear();
    return true;
}

bool Extractor::attribute(NameId name, const Value& value)
{
    const bool forXml = m_xmlEnabled && m_tagOpen;
    const size_t depth = m_stack.size();
    const NameId element = depth > 0 ? m_stack.back() : -1;
    if (depth == 3 && m_zone == Zone::System && element == name::TimeCreated && name == name::SystemTime)
        m_event.fileTime = valueToFileTime(value);
    const bool useful = depth == 3
                        && ((m_zone == Zone::System && ((element == name::Provider && name == name::Name)
                                                        || (element == name::Security && name == name::UserID)))
                            || (m_zone == Zone::EventData && m_inData && element == name::Data && name == name::Name));
    if (!forXml && !useful)
        return true;
    const QString text = valueToText(value);
    if (forXml) {
        if (name != m_xmlAttribute) {
            if (m_xmlAttribute >= 0)
                m_event.xml += QLatin1Char('"');
            m_event.xml += QLatin1Char(' ');
            m_event.xml += m_names.text(name);
            m_event.xml += QLatin1String("=\"");
            m_xmlAttribute = name;
        }
        appendEscapedXml(m_event.xml, text, true);
    }
    if (!useful)
        return true;
    if (m_zone == Zone::System) {
        if (element == name::Provider)
            m_event.provider += text;
        else
            m_event.sid += text;
    } else {
        m_dataName += text;
    }
    return true;
}

bool Extractor::text(const Value& value)
{
    const size_t depth = m_stack.size();
    QString text;
    const bool converted = m_xmlEnabled || m_zone == Zone::EventData || m_zone == Zone::UserData
                           || (m_zone == Zone::System && depth == 3
                               && (m_stack.back() == name::Computer || m_stack.back() == name::Channel));
    if (converted)
        text = valueToText(value);
    if (m_xmlEnabled) {
        finishTag();
        appendEscapedXml(m_event.xml, text, false);
    }
    switch (m_zone) {
    case Zone::System: {
        if (depth != 3)
            return true;
        uint64_t integer = 0;
        switch (m_stack.back()) {
        case name::EventID:
            if (valueToInteger(value, integer)) {
                m_event.eventId = static_cast<uint32_t>(integer);
                m_event.eventIdKnown = true;
            }
            break;
        case name::EventRecordID:
            if (valueToInteger(value, integer))
                m_event.recordId = integer;
            break;
        case name::Level:
            if (valueToInteger(value, integer))
                m_event.level = static_cast<uint8_t>(integer);
            break;
        case name::Keywords:
            if (valueToInteger(value, integer))
                m_event.keywords = integer;
            break;
        case name::Computer:
            m_event.machine += text;
            break;
        case name::Channel:
            m_event.channel += text;
            break;
        default:
            break;
        }
        return true;
    }
    case Zone::EventData:
        if (m_inData)
            m_currentText += text;
        return true;
    case Zone::UserData:
        if (depth >= 3)
            m_currentText += text;
        return true;
    case Zone::Other:
        return true;
    }
    return true;
}

bool Extractor::closeElement()
{
    const size_t depth = m_stack.size();
    if (depth == 0)
        return true;
    if (m_xmlEnabled) {
        if (m_tagOpen) {
            if (m_xmlAttribute >= 0)
                m_event.xml += QLatin1Char('"');
            m_event.xml += QLatin1String("/>");
            m_tagOpen = false;
            m_xmlAttribute = -1;
        } else {
            m_event.xml += QLatin1String("</");
            m_event.xml += m_names.text(m_stack.back());
            m_event.xml += QLatin1Char('>');
        }
    }
    if (m_zone == Zone::EventData && depth == 3 && m_inData) {
        QString fieldName = m_dataName.isEmpty() ? m_names.text(m_stack.back()) : m_dataName;
        m_event.data.push_back({std::move(fieldName), std::move(m_currentText)});
        m_currentText.clear();
        m_dataName.clear();
        m_inData = false;
    } else if (m_zone == Zone::UserData && depth >= 3 && !m_currentText.isEmpty()) {
        m_event.data.push_back({m_names.text(m_stack.back()), std::move(m_currentText)});
        m_currentText.clear();
    }
    m_stack.pop_back();
    if (depth == 2)
        m_zone = Zone::Other;
    return true;
}

} // namespace lh::evtx
