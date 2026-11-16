#include "Normalization.h"

#include "Actions.h"
#include "AccessAnalyzer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>

#include <initializer_list>

namespace lh {

namespace {

constexpr qsizetype MAX_DETAIL_SIZE = 1000;
constexpr uint64_t KEYWORD_FAILURE = 0x0010000000000000ULL;
constexpr uint64_t KEYWORD_SUCCESS = 0x0020000000000000ULL;
constexpr char16_t VALUE_SEPARATOR = 0x1f;

QString first(const evtx::DecodedEvent& e, std::initializer_list<const char16_t*> names)
{
    for (const char16_t* name : names) {
        QString value = cleanValue(e.dataValue(name));
        if (!value.isEmpty())
            return value;
    }
    return {};
}

bool targetFirst(uint32_t id)
{
    return id == 4624 || id == 4625 || id == 4634 || id == 4647 || (id >= 4800 && id <= 4803);
}

bool isGroup(uint32_t id)
{
    return id == 4728 || id == 4729 || id == 4732 || id == 4733 || id == 4756 || id == 4757;
}

QString sidName(const QString& sid)
{
    if (sid == QLatin1String("S-1-5-18"))
        return QStringLiteral("NT AUTHORITY\\SYSTEM");
    if (sid == QLatin1String("S-1-5-19"))
        return QStringLiteral("NT AUTHORITY\\LOCAL SERVICE");
    if (sid == QLatin1String("S-1-5-20"))
        return QStringLiteral("NT AUTHORITY\\NETWORK SERVICE");
    return sid;
}

QString classicValue(const evtx::DecodedEvent& e, QStringView key)
{
    for (const evtx::DataField& field : e.data) {
        for (const QStringView row : QStringView(field.value).split(u'\n')) {
            const QStringView bare = row.trimmed();
            if (bare.size() > key.size() && bare.startsWith(key) && bare[key.size()] == u'=')
                return bare.mid(key.size() + 1).trimmed().toString();
        }
    }
    return {};
}

bool isClassicPowerShell(QStringView channel)
{
    return channel.compare(QLatin1String("Windows PowerShell"), Qt::CaseInsensitive) == 0;
}

QString canonicalizeAccount(const QString& user)
{
    const qsizetype separator = user.lastIndexOf(QLatin1Char('\\'));
    const QString name = user.mid(separator + 1).toLower();
    const QString domain = separator > 0 ? user.left(separator).toLower() : QString();
    const bool authority = domain.isEmpty() || domain == QLatin1String("nt authority")
                           || domain == QLatin1String("autorite nt") || domain == QStringLiteral("autorité nt");
    if (!authority)
        return user;
    if (name == QLatin1String("system") || name == QStringLiteral("système") || name == QLatin1String("localsystem"))
        return sidName(QStringLiteral("S-1-5-18"));
    if (name == QLatin1String("local service") || name == QLatin1String("localservice")
        || name == QLatin1String("service local"))
        return sidName(QStringLiteral("S-1-5-19"));
    if (name == QLatin1String("network service") || name == QLatin1String("networkservice")
        || name == QStringLiteral("service réseau"))
        return sidName(QStringLiteral("S-1-5-20"));
    return user;
}

QString eventUser(const evtx::DecodedEvent& e, bool security, QString& sid)
{
    const bool target = security && targetFirst(e.eventId);
    QString user = target ? normalizeUser(e.dataValue(u"TargetUserName"), e.dataValue(u"SubjectUserName"),
                                          e.dataValue(u"TargetDomainName"), e.dataValue(u"SubjectDomainName"))
                          : normalizeUser(e.dataValue(u"SubjectUserName"), e.dataValue(u"TargetUserName"),
                                          e.dataValue(u"SubjectDomainName"), e.dataValue(u"TargetDomainName"));
    sid = cleanValue(e.dataValue(target ? u"TargetUserSid" : u"SubjectUserSid"));
    if (!user.isEmpty())
        return user;
    if (e.eventId == 1149 && e.channel.contains(QLatin1String("RemoteConnectionManager"), Qt::CaseInsensitive))
        user = normalizeUser(e.dataValue(u"Param1"), {}, e.dataValue(u"Param2"), {});
    if (user.isEmpty())
        user = first(e, {u"User", u"UserName", u"Detection User", u"SamAccountName", u"TargetUser"});
    if (user.isEmpty() && security)
        user = first(e, {u"AccountName"});
    if (user.isEmpty() && isClassicPowerShell(e.channel))
        user = classicValue(e, u"UserId");
    if (!user.isEmpty())
        return user;
    sid = e.sid;
    return sidName(e.sid);
}

QString eventSourceIp(const evtx::DecodedEvent& e)
{
    if (e.eventId == 1149 && e.channel.contains(QLatin1String("RemoteConnectionManager"), Qt::CaseInsensitive)) {
        const QString ip = cleanIp(e.dataValue(u"Param3"));
        if (!ip.isEmpty())
            return ip;
    }
    for (const char16_t* name : {u"IpAddress", u"SourceAddress", u"SourceIp", u"ClientAddress", u"SourceNetworkAddress",
                                 u"Address", u"ClientIP", u"Source Network Address"}) {
         const QString ip = cleanIp(e.dataValue(name));
         if (!ip.isEmpty())
             return ip;
     }
    return {};
}

QString eventDestinationIp(const evtx::DecodedEvent& e)
{
    for (const char16_t* name : {u"DestinationIp", u"DestAddress", u"DestinationAddress"}) {
        const QString ip = cleanIp(e.dataValue(name));
        if (!ip.isEmpty())
            return ip;
    }
    return {};
}

QString shorten(QString text)
{
    for (QChar& c : text)
        if (c == u'\r' || c == u'\n' || c == u'\t')
            c = u' ';
    text = text.simplified();
    if (text.size() > MAX_DETAIL_SIZE)
        text = text.left(MAX_DETAIL_SIZE - 1) + QChar(0x2026);
    return text;
}

QString eventDetail(const evtx::DecodedEvent& e, bool security)
{
    const uint32_t id = e.eventId;
    if (isClassicPowerShell(e.channel)) {
        const QString command = classicValue(e, u"CommandLine");
        return shorten(command.isEmpty() ? classicValue(e, u"HostApplication") : command);
    }
    if (security && (id == 4624 || id == 4625 || id == 4634 || id == 4647 || id == 4648)) {
        QStringList parts;
        auto key = [&](const char16_t* name) {
            const QString value = cleanValue(e.dataValue(name));
            if (!value.isEmpty())
                parts << QStringView(name).toString() + QLatin1Char('=') + value;
        };
        key(u"LogonType");
        if (id == 4648) {
            key(u"TargetUserName");
            key(u"TargetServerName");
        }
        key(u"WorkstationName");
        key(u"AuthenticationPackageName");
        if (id == 4625) {
            key(u"Status");
            key(u"SubStatus");
        }
        return shorten(parts.join(QStringLiteral(" · ")));
    }
    if (security && isGroup(id)) {
        QString member = first(e, {u"MemberName", u"MemberSid"});
        const QString group = cleanValue(e.dataValue(u"TargetUserName"));
        if (!member.isEmpty() && !group.isEmpty())
            return shorten(member + QStringLiteral(" → ") + group);
        return shorten(member.isEmpty() ? group : member);
    }
    if (security && id == 5145) {
        const QString share = cleanValue(e.dataValue(u"ShareName"));
        QString target = cleanValue(e.dataValue(u"RelativeTargetName"));
        while (target.startsWith(QLatin1Char('\\')))
            target.remove(0, 1);
        if (!share.isEmpty())
            return shorten(target.isEmpty() ? share : share + QLatin1Char('\\') + target);
    }
    if (id == 7045) {
        const QString service = cleanValue(e.dataValue(u"ServiceName"));
        const QString image = cleanValue(e.dataValue(u"ImagePath"));
        if (!service.isEmpty() && !image.isEmpty())
            return shorten(service + QStringLiteral(" · ") + image);
    }
    return shorten(first(e, {u"CommandLine", u"ScriptBlockText", u"Payload", u"ImagePath", u"ServiceFileName",
                             u"TaskName", u"TargetFilename", u"TargetObject", u"QueryName", u"ShareName",
                             u"ObjectName", u"TargetUserName", u"SamAccountName", u"Threat Name", u"Details",
                             u"Message", u"ServiceName", u"param1"}));
}

void hashInto(uint64_t& fingerprint, QStringView text, bool insensitive)
{
    for (const QChar c : text) {
        const char16_t u = insensitive ? c.toCaseFolded().unicode() : c.unicode();
        fingerprint ^= u;
        fingerprint *= 0x100000001b3ULL;
    }
    fingerprint ^= VALUE_SEPARATOR;
    fingerprint *= 0x100000001b3ULL;
}

} // namespace

QString cleanValue(const QString& value)
{
    const QString text = value.trimmed();
    return text == QLatin1String("-") ? QString() : text;
}

bool isSecurityChannel(QStringView channel)
{
    return channel.compare(QLatin1String("Security"), Qt::CaseInsensitive) == 0;
}

QString cleanIp(const QString& value)
{
    QString ip = value.trimmed();
    if (ip.startsWith(QLatin1String("::ffff:"), Qt::CaseInsensitive) && ip.contains(QLatin1Char('.')))
        ip = ip.mid(7);
    if (ip.isEmpty() || ip == QLatin1String("-"))
        return {};
    bool separator = false;
    for (const QChar c : ip) {
        if (c == u'.' || c == u':') {
            separator = true;
            continue;
        }
        const bool hexadecimal = (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
        if (!hexadecimal && c != u'%' && c != u'[' && c != u']')
            return {};
    }
    return separator ? ip : QString();
}

void normalize(const evtx::DecodedEvent& e, TimeZoneConverter& timeZone, EventRow& row)
{
    row = EventRow();
    row.eventId = e.eventId;
    row.recordId = e.recordId;
    row.machine = e.machine;
    row.channel = e.channel;
    row.provider = e.provider;
    row.level = e.level >= level::Critical && e.level <= level::Verbose ? e.level : level::Information;
    row.audit = (e.keywords & KEYWORD_FAILURE)   ? audit::Failure
                : (e.keywords & KEYWORD_SUCCESS) ? audit::Success
                                                 : audit::None;
    const bool security = isSecurityChannel(e.channel);
    QString sid;
    row.user = canonicalizeAccount(eventUser(e, security, sid));
    row.isSystem = !row.user.isEmpty() && isSystemAccount(row.user, sid);
    if (!sid.startsWith(QLatin1String("S-1-5-21-")) || row.user == sid)
        sid.clear();
    row.userSid = sid;
    row.process = first(e, {u"Image", u"NewProcessName", u"ProcessName", u"Application", u"CallerProcessName",
                            u"SourceImage", u"Process Name", u"ProcessPath"});
    row.sourceIp = eventSourceIp(e);
    row.destinationIp = eventDestinationIp(e);
    row.detail = eventDetail(e, security);
    if (row.detail.isEmpty())
        row.detail = shorten(e.dataValue(u"Message"));
    row.tsUtc = e.fileTime != 0 ? evtx::fileTimeToUnixMicroseconds(e.fileTime) : UNKNOWN_TIMESTAMP;
    row.local = timeZone.convert(row.tsUtc);
}

qint64 eventSignature(const EventRow& l)
{
    uint64_t fingerprint = 0xcbf29ce484222325ULL;
    const uint64_t integer = static_cast<uint64_t>(l.eventId) | (static_cast<uint64_t>(l.level & 0xff) << 32)
                             | (static_cast<uint64_t>(l.audit & 0xff) << 40);
    for (int shift = 0; shift < 48; shift += 8) {
        fingerprint ^= (integer >> shift) & 0xff;
        fingerprint *= 0x100000001b3ULL;
    }
    hashInto(fingerprint, l.channel, true);
    hashInto(fingerprint, l.provider, true);
    hashInto(fingerprint, l.machine, true);
    hashInto(fingerprint, l.user, true);
    hashInto(fingerprint, l.process, true);
    hashInto(fingerprint, l.file, false);
    hashInto(fingerprint, l.detail, false);
    hashInto(fingerprint, l.sourceIp, false);
    hashInto(fingerprint, l.destinationIp, false);
    hashInto(fingerprint, l.action, false);
    hashInto(fingerprint, l.access, false);
    return static_cast<qint64>(fingerprint & 0x7fffffffffffffffULL);
}

QString indentXml(const QString& xml)
{
    if (xml.isEmpty())
        return {};
    QString output;
    QXmlStreamReader reader(xml);
    QXmlStreamWriter writer(&output);
    writer.setAutoFormatting(true);
    writer.setAutoFormattingIndent(2);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.hasError())
            return xml;
        const QXmlStreamReader::TokenType type = reader.tokenType();
        if (type != QXmlStreamReader::StartDocument && type != QXmlStreamReader::EndDocument)
            writer.writeCurrentToken(reader);
    }
    return output.trimmed();
}

namespace {

QJsonValue jsonElement(QXmlStreamReader& reader, bool unnamed)
{
    QJsonObject attributes;
    for (const QXmlStreamAttribute& attribute : reader.attributes()) {
        if (unnamed && attribute.name() == QLatin1String("Name"))
            continue;
        if (attribute.name() == QLatin1String("xmlns"))
            continue;
        attributes.insert(attribute.name().toString(), attribute.value().toString());
    }
    QJsonObject object;
    QString text;
    bool children = false;
    while (!reader.atEnd()) {
        const QXmlStreamReader::TokenType type = reader.readNext();
        if (type == QXmlStreamReader::EndElement)
            break;
        if (type == QXmlStreamReader::Characters) {
            text += reader.text();
            continue;
        }
        if (type != QXmlStreamReader::StartElement)
            continue;
        children = true;
        const QString elementName = reader.name().toString();
        const QString dataName = reader.attributes().value(QLatin1String("Name")).toString();
        const bool dataValue = elementName == QLatin1String("Data") && !dataName.isEmpty();
        const QString key = dataValue ? dataName : elementName;
        const QJsonValue value = jsonElement(reader, dataValue);
        if (!object.contains(key)) {
            object.insert(key, value);
            continue;
        }
        QJsonValue existing = object.value(key);
        QJsonArray array = existing.isArray() ? existing.toArray() : QJsonArray{existing};
        array.append(value);
        object.insert(key, array);
    }
    if (!children && attributes.isEmpty())
        return text;
    if (!attributes.isEmpty())
        object.insert(QStringLiteral("#attributes"), attributes);
    if (!text.trimmed().isEmpty())
        object.insert(QStringLiteral("#text"), text);
    return object;
}

} // namespace

QString xmlToJson(const QString& xml)
{
    if (xml.isEmpty())
        return {};
    QXmlStreamReader reader(xml);
    QJsonObject root;
    while (!reader.atEnd()) {
        if (reader.readNext() == QXmlStreamReader::StartElement) {
            const QString name = reader.name().toString();
            root.insert(name, jsonElement(reader, false));
            break;
        }
    }
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Indented)).trimmed();
}

} // namespace lh
