#include "XmlEvent.h"

#include <QVarLengthArray>

namespace lh::logs {

namespace {

constexpr QByteArrayView TAG_OPEN("<Event");
constexpr QByteArrayView TAG_CLOSE("</Event>");
constexpr qsizetype MAX_DEPTH = 64;
constexpr qsizetype MAX_ENTITY_SIZE = 10;

bool isBlank(char c) noexcept
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool isBlank(QByteArrayView text) noexcept
{
    for (const char c : text)
        if (!isBlank(c))
            return false;
    return true;
}

void appendChar(QString& output, uint32_t code)
{
    if (code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff))
        return;
    if (code < 0x20 && code != u'\t' && code != u'\n' && code != u'\r')
        return;
    if (code < 0x10000) {
        output += QChar(static_cast<char16_t>(code));
        return;
    }
    output += QChar(QChar::highSurrogate(code));
    output += QChar(QChar::lowSurrogate(code));
}

QString decodeText(QByteArrayView raw)
{
    qsizetype ampersand = raw.indexOf('&');
    if (ampersand < 0)
        return QString::fromUtf8(raw);
    QString output;
    qsizetype position = 0;
    while (ampersand >= 0) {
        output += QString::fromUtf8(raw.sliced(position, ampersand - position));
        const qsizetype end = raw.indexOf(';', ampersand);
        if (end < 0 || end - ampersand > MAX_ENTITY_SIZE) {
            output += QLatin1Char('&');
            position = ampersand + 1;
        } else {
            const QByteArrayView name = raw.sliced(ampersand + 1, end - ampersand - 1);
            if (name == "amp") {
                output += QLatin1Char('&');
            } else if (name == "lt") {
                output += QLatin1Char('<');
            } else if (name == "gt") {
                output += QLatin1Char('>');
            } else if (name == "quot") {
                output += QLatin1Char('"');
            } else if (name == "apos") {
                output += QLatin1Char('\'');
            } else if (name.startsWith('#')) {
                const bool hexadecimal = name.size() > 1 && (name[1] == 'x' || name[1] == 'X');
                bool ok = false;
                const uint32_t code = hexadecimal ? name.sliced(2).toUInt(&ok, 16) : name.sliced(1).toUInt(&ok, 10);
                if (ok)
                    appendChar(output, code);
            } else {
                output += QString::fromUtf8(raw.sliced(ampersand, end - ampersand + 1));
            }
            position = end + 1;
        }
        ampersand = raw.indexOf('&', position);
    }
    output += QString::fromUtf8(raw.sliced(position));
    return output;
}

class Decoder
{
public:
    explicit Decoder(evtx::DecodedEvent& event) : m_event(event) {}

    bool decode(QByteArrayView f);

private:
    enum class Zone : uint8_t { Other, System, EventData, UserData };

    struct Level
    {
        QByteArrayView name;
        bool children = false;
    };

    void open(QByteArrayView name);
    void attribute(QByteArrayView name, QByteArrayView value);
    void text(QByteArrayView raw, bool literal, bool beforeClosing);
    void close();

    evtx::DecodedEvent& m_event;
    QVarLengthArray<Level, 16> m_stack;
    Zone m_zone = Zone::Other;
    QString m_dataName;
    QString m_currentText;
    bool m_inData = false;
};

bool Decoder::decode(QByteArrayView f)
{
    const qsizetype n = f.size();
    qsizetype p = 0;
    while (p < n) {
        if (f[p] != '<') {
            qsizetype q = f.indexOf('<', p);
            if (q < 0)
                q = n;
            if (!m_stack.isEmpty())
                text(f.sliced(p, q - p), false, q + 1 < n && f[q + 1] == '/');
            p = q;
            continue;
        }
        const QByteArrayView rest = f.sliced(p);
        if (rest.startsWith("<!--")) {
            const qsizetype q = f.indexOf("-->", p + 4);
            if (q < 0)
                return false;
            p = q + 3;
            continue;
        }
        if (rest.startsWith("<![CDATA[")) {
            const qsizetype q = f.indexOf("]]>", p + 9);
            if (q < 0)
                return false;
            if (!m_stack.isEmpty())
                text(f.sliced(p + 9, q - p - 9), true, true);
            p = q + 3;
            continue;
        }
        if (rest.startsWith("<?") || rest.startsWith("<!")) {
            const qsizetype q = f.indexOf('>', p);
            if (q < 0)
                return false;
            p = q + 1;
            continue;
        }
        if (rest.startsWith("</")) {
            const qsizetype q = f.indexOf('>', p);
            if (q < 0 || m_stack.isEmpty())
                return false;
            close();
            if (m_stack.isEmpty())
                return true;
            p = q + 1;
            continue;
        }

        qsizetype q = p + 1;
        while (q < n && !isBlank(f[q]) && f[q] != '>' && f[q] != '/')
            ++q;
        if (q == p + 1 || q >= n || m_stack.size() >= MAX_DEPTH)
            return false;
        open(f.sliced(p + 1, q - p - 1));
        while (true) {
            while (q < n && isBlank(f[q]))
                ++q;
            if (q >= n)
                return false;
            if (f[q] == '>') {
                ++q;
                break;
            }
            if (f[q] == '/') {
                if (q + 1 >= n || f[q + 1] != '>')
                    return false;
                close();
                q += 2;
                break;
            }
            const qsizetype nameStart = q;
            while (q < n && f[q] != '=' && !isBlank(f[q]) && f[q] != '>' && f[q] != '/')
                ++q;
            const QByteArrayView attributeName = f.sliced(nameStart, q - nameStart);
            while (q < n && isBlank(f[q]))
                ++q;
            if (q >= n || f[q] != '=')
                return false;
            ++q;
            while (q < n && isBlank(f[q]))
                ++q;
            if (q >= n || (f[q] != '"' && f[q] != '\''))
                return false;
            const qsizetype valueEnd = f.indexOf(f[q], q + 1);
            if (valueEnd < 0)
                return false;
            attribute(attributeName, f.sliced(q + 1, valueEnd - q - 1));
            q = valueEnd + 1;
        }
        if (m_stack.isEmpty())
            return true;
        p = q;
    }
    return false;
}

void Decoder::open(QByteArrayView name)
{
    if (!m_stack.isEmpty())
        m_stack.back().children = true;
    m_stack.push_back({name, false});
    const qsizetype depth = m_stack.size();
    if (depth == 2) {
        if (name == "System")
            m_zone = Zone::System;
        else if (name == "EventData")
            m_zone = Zone::EventData;
        else if (name == "UserData")
            m_zone = Zone::UserData;
        else
            m_zone = Zone::Other;
    } else if (depth == 3 && m_zone == Zone::EventData) {
        m_inData = true;
        m_dataName.clear();
    }
    m_currentText.clear();
}

void Decoder::attribute(QByteArrayView name, QByteArrayView value)
{
    if (m_stack.size() != 3)
        return;
    const QByteArrayView element = m_stack.back().name;
    if (m_zone == Zone::System) {
        if (element == "TimeCreated" && name == "SystemTime")
            m_event.fileTime = evtx::isoToFileTime(decodeText(value));
        else if (element == "Provider" && name == "Name")
            m_event.provider = decodeText(value);
        else if (element == "Security" && name == "UserID")
            m_event.sid = decodeText(value);
    } else if (m_zone == Zone::EventData && m_inData && element == "Data" && name == "Name") {
        m_dataName = decodeText(value);
    }
}

void Decoder::text(QByteArrayView raw, bool literal, bool beforeClosing)
{
    const qsizetype depth = m_stack.size();
    const bool useful = (m_zone == Zone::System && depth == 3) || (m_zone == Zone::EventData && m_inData)
                        || (m_zone == Zone::UserData && depth >= 3);
    if (!useful)
        return;
    if (!literal && isBlank(raw) && (!beforeClosing || m_stack.back().children))
        return;
    const QString value = literal ? QString::fromUtf8(raw) : decodeText(raw);
    if (m_zone != Zone::System) {
        m_currentText += value;
        return;
    }
    const QByteArrayView element = m_stack.back().name;
    uint64_t integer = 0;
    if (element == "EventID") {
        if (evtx::parseIntegerText(value, integer)) {
            m_event.eventId = static_cast<uint32_t>(integer);
            m_event.eventIdKnown = true;
        }
    } else if (element == "EventRecordID") {
        if (evtx::parseIntegerText(value, integer))
            m_event.recordId = integer;
    } else if (element == "Level") {
        if (evtx::parseIntegerText(value, integer))
            m_event.level = static_cast<uint8_t>(integer);
    } else if (element == "Keywords") {
        if (evtx::parseIntegerText(value, integer))
            m_event.keywords = integer;
    } else if (element == "Computer") {
        m_event.machine += value;
    } else if (element == "Channel") {
        m_event.channel += value;
    }
}

void Decoder::close()
{
    const qsizetype depth = m_stack.size();
    if (m_zone == Zone::EventData && depth == 3 && m_inData) {
        QString name = m_dataName.isEmpty() ? QString::fromUtf8(m_stack.back().name) : m_dataName;
        m_event.data.push_back({std::move(name), std::move(m_currentText)});
        m_currentText.clear();
        m_dataName.clear();
        m_inData = false;
    } else if (m_zone == Zone::UserData && depth >= 3 && !m_currentText.isEmpty()) {
        m_event.data.push_back({QString::fromUtf8(m_stack.back().name), std::move(m_currentText)});
        m_currentText.clear();
    }
    m_stack.pop_back();
    if (depth == 2)
        m_zone = Zone::Other;
}

void writeElement(QString& output, QLatin1StringView name, QStringView value)
{
    output += QLatin1Char('<');
    output += name;
    output += QLatin1Char('>');
    evtx::appendEscapedXml(output, value, false);
    output += QLatin1String("</");
    output += name;
    output += QLatin1Char('>');
}

} // namespace

qsizetype eventStart(QByteArrayView text, qsizetype from)
{
    while (true) {
        const qsizetype position = text.indexOf(TAG_OPEN, from);
        if (position < 0 || position + TAG_OPEN.size() >= text.size())
            return -1;
        const char next = text[position + TAG_OPEN.size()];
        if (next == '>' || isBlank(next))
            return position;
        from = position + TAG_OPEN.size();
    }
}

qsizetype lastEvent(QByteArrayView text)
{
    qsizetype from = text.size();
    while (true) {
        const qsizetype position = text.lastIndexOf(TAG_OPEN, from);
        if (position < 0)
            return -1;
        if (eventStart(text, position) == position)
            return position;
        if (position == 0)
            return -1;
        from = position - 1;
    }
}

bool decodeEvent(QByteArrayView fragment, bool xml, evtx::DecodedEvent& output)
{
    output = evtx::DecodedEvent();
    Decoder decoder(output);
    if (!decoder.decode(fragment) || !output.eventIdKnown)
        return false;
    if (xml)
        output.xml = QString::fromUtf8(fragment);
    return true;
}

ScanResult scanEvents(QByteArrayView text, qsizetype start, qsizetype limit, bool xml, const Callback& callback)
{
    ScanResult scan;
    evtx::DecodedEvent event;
    qsizetype position = eventStart(text, start);
    while (position >= 0 && position < limit) {
        ++scan.readCount;
        const qsizetype closing = text.indexOf(TAG_CLOSE, position);
        if (closing < 0) {
            ++scan.errors;
            break;
        }
        const qsizetype next = eventStart(text, position + TAG_OPEN.size());
        if (next >= 0 && next < closing) {
            ++scan.errors;
            position = next;
            continue;
        }
        const qsizetype end = closing + TAG_CLOSE.size();
        if (scan.start < 0)
            scan.start = position;
        scan.end = end;
        if (decodeEvent(text.sliced(position, end - position), xml, event)) {
            ++scan.kept;
            if (!callback(event))
                break;
        } else {
            ++scan.errors;
        }
        position = next;
    }
    return scan;
}

void writeEvent(QString& output, const evtx::DecodedEvent& e)
{
    output += QLatin1String("<Event xmlns=\"http://schemas.microsoft.com/win/2004/08/events/event\"><System>"
                            "<Provider Name=\"");
    evtx::appendEscapedXml(output, e.provider, true);
    output += QLatin1String("\"/>");
    writeElement(output, QLatin1StringView("EventID"), QString::number(e.eventId));
    writeElement(output, QLatin1StringView("Level"), QString::number(e.level));
    writeElement(output, QLatin1StringView("Keywords"), QString(QLatin1String("0x") + QString::number(e.keywords, 16)));
    if (e.fileTime != 0)
        output += QLatin1String("<TimeCreated SystemTime=\"") + evtx::fileTimeToIso(e.fileTime) + QLatin1String("\"/>");
    if (!e.channel.isEmpty())
        writeElement(output, QLatin1StringView("Channel"), e.channel);
    output += QLatin1String("</System><EventData>");
    for (const evtx::DataField& field : e.data) {
        output += QLatin1String("<Data Name=\"");
        evtx::appendEscapedXml(output, field.name, true);
        output += QLatin1String("\">");
        evtx::appendEscapedXml(output, field.value, false);
        output += QLatin1String("</Data>");
    }
    output += QLatin1String("</EventData></Event>\n");
}

} // namespace lh::logs
