#include "Rule.h"

#include <algorithm>
#include <array>

namespace lh::sigma {

namespace {

struct Rejection
{
    QString message;
    bool notApplicable = false;
};

// ── IP addresses ─────────────────────────────────────────────────────────────

bool parseIpv4(QStringView text, QByteArray& bytes)
{
    const QList<QStringView> parts = text.split(u'.');
    if (parts.size() != 4)
        return false;
    bytes.resize(4);
    for (int i = 0; i < 4; ++i) {
        bool ok = false;
        const uint value = parts[i].toUInt(&ok);
        if (!ok || value > 255 || parts[i].isEmpty())
            return false;
        bytes[i] = static_cast<char>(value);
    }
    return true;
}

bool ipv6Groups(QStringView part, std::vector<quint16>& groups)
{
    if (part.isEmpty())
        return true;
    for (const QStringView group : part.split(u':')) {
        if (group.contains(u'.')) {
            QByteArray v4;
            if (!parseIpv4(group, v4))
                return false;
            groups.push_back(static_cast<quint16>((static_cast<uchar>(v4[0]) << 8) | static_cast<uchar>(v4[1])));
            groups.push_back(static_cast<quint16>((static_cast<uchar>(v4[2]) << 8) | static_cast<uchar>(v4[3])));
            continue;
        }
        bool ok = false;
        const uint value = group.toUInt(&ok, 16);
        if (!ok || group.isEmpty() || group.size() > 4)
            return false;
        groups.push_back(static_cast<quint16>(value));
    }
    return true;
}

bool parseIp(QStringView text, QByteArray& bytes)
{
    text = text.trimmed();
    const qsizetype zone = text.indexOf(u'%');
    if (zone >= 0)
        text = text.left(zone);
    if (text.startsWith(u'[') && text.endsWith(u']'))
        text = text.mid(1, text.size() - 2);
    if (!text.contains(u':'))
        return parseIpv4(text, bytes);
    std::vector<quint16> left;
    std::vector<quint16> right;
    const qsizetype double_ = text.indexOf(QLatin1String("::"));
    if (double_ >= 0) {
        if (!ipv6Groups(text.left(double_), left) || !ipv6Groups(text.mid(double_ + 2), right))
            return false;
        if (left.size() + right.size() > 7)
            return false;
    } else if (!ipv6Groups(text, left) || left.size() != 8) {
        return false;
    }
    std::vector<quint16> groups = left;
    groups.resize(8 - right.size(), 0);
    groups.insert(groups.end(), right.begin(), right.end());
    bytes.resize(16);
    for (int i = 0; i < 8; ++i) {
        bytes[2 * i] = static_cast<char>(groups[static_cast<size_t>(i)] >> 8);
        bytes[2 * i + 1] = static_cast<char>(groups[static_cast<size_t>(i)] & 0xff);
    }
    const bool mapped = std::all_of(bytes.cbegin(), bytes.cbegin() + 10, [](char c) { return c == 0; })
                        && static_cast<uchar>(bytes[10]) == 0xff && static_cast<uchar>(bytes[11]) == 0xff;
    if (mapped)
        bytes = bytes.mid(12);
    return true;
}

bool inNetwork(const QByteArray& address, const QByteArray& network, int prefix)
{
    if (address.size() != network.size())
        return false;
    int remaining = prefix;
    for (qsizetype i = 0; i < network.size() && remaining > 0; ++i, remaining -= 8) {
        const int bits = std::min(8, remaining);
        const auto mask = static_cast<uchar>(0xff << (8 - bits));
        if ((static_cast<uchar>(address[i]) & mask) != (static_cast<uchar>(network[i]) & mask))
            return false;
    }
    return true;
}

double numberFrom(const QString& text, bool& ok)
{
    const QString t = text.trimmed();
    if (t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive))
        return static_cast<double>(t.mid(2).toULongLong(&ok, 16));
    return t.toDouble(&ok);
}

// ── Log sources ──────────────────────────────────────────────────────────────

struct ServiceDefinition
{
    const char* name;
    std::array<const char*, 4> channels;
};

constexpr ServiceDefinition SERVICES[] = {
    {"security", {"Security"}},
    {"system", {"System"}},
    {"application", {"Application"}},
    {"sysmon", {"Microsoft-Windows-Sysmon/Operational"}},
    {"powershell", {"Microsoft-Windows-PowerShell/Operational", "PowerShellCore/Operational"}},
    {"powershell-classic", {"Windows PowerShell"}},
    {"taskscheduler", {"Microsoft-Windows-TaskScheduler/Operational"}},
    {"wmi", {"Microsoft-Windows-WMI-Activity/Operational"}},
    {"windefend", {"Microsoft-Windows-Windows Defender/Operational"}},
    {"bits-client", {"Microsoft-Windows-Bits-Client/Operational"}},
    {"terminalservices-localsessionmanager", {"Microsoft-Windows-TerminalServices-LocalSessionManager/Operational"}},
    {"firewall-as", {"Microsoft-Windows-Windows Firewall With Advanced Security/Firewall"}},
    {"driver-framework", {"Microsoft-Windows-DriverFrameworks-UserMode/Operational"}},
    {"codeintegrity-operational", {"Microsoft-Windows-CodeIntegrity/Operational"}},
    {"dns-server", {"DNS Server"}},
    {"dns-server-analytic", {"Microsoft-Windows-DNS-Server/Analytical"}},
    {"ntlm", {"Microsoft-Windows-NTLM/Operational"}},
    {"applocker",
     {"Microsoft-Windows-AppLocker/EXE and DLL", "Microsoft-Windows-AppLocker/MSI and Script",
      "Microsoft-Windows-AppLocker/Packaged app-Execution", "Microsoft-Windows-AppLocker/Packaged app-Deployment"}},
    {"security-mitigations",
     {"Microsoft-Windows-Security-Mitigations/Kernel Mode", "Microsoft-Windows-Security-Mitigations/User Mode"}},
    {"printservice-admin", {"Microsoft-Windows-PrintService/Admin"}},
    {"printservice-operational", {"Microsoft-Windows-PrintService/Operational"}},
    {"smbclient-security", {"Microsoft-Windows-SmbClient/Security"}},
    {"smbclient-connectivity", {"Microsoft-Windows-SmbClient/Connectivity"}},
    {"openssh", {"OpenSSH/Operational"}},
    {"msexchange-management", {"MSExchange Management"}},
    {"ldap", {"Microsoft-Windows-LDAP-Client/Debug"}},
    {"dhcp", {"Microsoft-Windows-DHCP-Server/Operational"}},
    {"vhdmp", {"Microsoft-Windows-VHDMP/Operational"}},
    {"appxdeployment-server", {"Microsoft-Windows-AppXDeploymentServer/Operational"}},
    {"appxpackaging-om", {"Microsoft-Windows-AppxPackaging/Operational"}},
    {"lsa-server", {"Microsoft-Windows-LSA/Operational"}},
    {"diagnosis-scripted", {"Microsoft-Windows-Diagnosis-Scripted/Operational"}},
    {"shell-core", {"Microsoft-Windows-Shell-Core/Operational"}},
    {"capi2", {"Microsoft-Windows-CAPI2/Operational"}},
    {"certificateservicesclient-lifecycle-system",
     {"Microsoft-Windows-CertificateServicesClient-Lifecycle-System/Operational"}},
    {"kernel-shimengine", {"Microsoft-Windows-Kernel-ShimEngine/Operational"}},
};

struct CategoryDefinition
{
    QString name;
    Category type;
    std::vector<Target> targets;
};

const std::vector<CategoryDefinition>& sigmaCategories()
{
    static const std::vector<CategoryDefinition> list = [] {
        const QString sysmon = QStringLiteral("microsoft-windows-sysmon/operational");
        const QString security = QStringLiteral("security");
        const QString powershell = QStringLiteral("microsoft-windows-powershell/operational");
        const QString powershellCore = QStringLiteral("powershellcore/operational");
        const QString classic = QStringLiteral("windows powershell");
        auto s = [&](std::vector<quint32> ids) { return Target{sysmon, std::move(ids)}; };
        return std::vector<CategoryDefinition>{
            {QStringLiteral("process_creation"), Category::ProcessCreation, {s({1}), {security, {4688}}}},
            {QStringLiteral("file_change"), Category::Other, {s({2})}},
            {QStringLiteral("network_connection"), Category::NetworkConnection, {s({3}), {security, {5156}}}},
            {QStringLiteral("sysmon_status"), Category::Other, {s({4, 16})}},
            {QStringLiteral("process_termination"), Category::ProcessTermination, {s({5}), {security, {4689}}}},
            {QStringLiteral("driver_load"), Category::Other, {s({6})}},
            {QStringLiteral("image_load"), Category::Other, {s({7})}},
            {QStringLiteral("create_remote_thread"), Category::Other, {s({8})}},
            {QStringLiteral("raw_access_thread"), Category::Other, {s({9})}},
            {QStringLiteral("process_access"), Category::Other, {s({10})}},
            {QStringLiteral("file_event"), Category::Other, {s({11})}},
            {QStringLiteral("registry_add"), Category::Other, {s({12})}},
            {QStringLiteral("registry_delete"), Category::Other, {s({12})}},
            {QStringLiteral("registry_set"), Category::Other, {s({13})}},
            {QStringLiteral("registry_rename"), Category::Other, {s({14})}},
            {QStringLiteral("registry_event"), Category::Other, {s({12, 13, 14})}},
            {QStringLiteral("create_stream_hash"), Category::Other, {s({15})}},
            {QStringLiteral("pipe_created"), Category::Other, {s({17, 18})}},
            {QStringLiteral("wmi_event"), Category::Other, {s({19, 20, 21})}},
            {QStringLiteral("dns_query"), Category::Other, {s({22})}},
            {QStringLiteral("file_delete"), Category::Other, {s({23, 26})}},
            {QStringLiteral("clipboard_capture"), Category::Other, {s({24})}},
            {QStringLiteral("process_tampering"), Category::Other, {s({25})}},
            {QStringLiteral("file_block_executable"), Category::Other, {s({27})}},
            {QStringLiteral("file_block_shredding"), Category::Other, {s({28})}},
            {QStringLiteral("file_executable_detected"), Category::Other, {s({29})}},
            {QStringLiteral("sysmon_error"), Category::Other, {s({255})}},
            {QStringLiteral("ps_module"), Category::Other, {{powershell, {4103}}, {powershellCore, {4103}}}},
            {QStringLiteral("ps_script"), Category::Other, {{powershell, {4104}}, {powershellCore, {4104}}}},
            {QStringLiteral("ps_classic_start"), Category::Other, {{classic, {400}}}},
            {QStringLiteral("ps_classic_provider_start"), Category::Other, {{classic, {600}}}},
            {QStringLiteral("ps_classic_script"), Category::Other, {{classic, {800}}}},
        };
    }();
    return list;
}

void focusOn(const yaml::Node* source, Rule& rule)
{
    if (!source)
        return;
    const QString product = source->text(u"product").toLower();
    const QString service = source->text(u"service").toLower();
    const QString category = source->text(u"category").toLower();
    if (!product.isEmpty() && product != QLatin1String("windows"))
        throw Rejection{qtTrId("rules.error_product").arg(product), true};

    std::vector<Target> serviceChannels;
    if (!service.isEmpty()) {
        const auto found = std::find_if(std::begin(SERVICES), std::end(SERVICES), [&](const ServiceDefinition& d) {
            return service == QLatin1String(d.name);
        });
        if (found == std::end(SERVICES))
            throw Rejection{qtTrId("rules.error_service").arg(service), true};
        for (const char* channel : found->channels)
            if (channel)
                serviceChannels.push_back({QString::fromLatin1(channel).toLower(), {}});
    }
    if (category.isEmpty()) {
        rule.targets = std::move(serviceChannels);
        return;
    }
    const auto& categories = sigmaCategories();
    const auto found = std::find_if(categories.begin(), categories.end(),
                                    [&](const CategoryDefinition& d) { return d.name == category; });
    if (found == categories.end())
        throw Rejection{qtTrId("rules.error_category").arg(category), true};
    rule.category = found->type;
    rule.targets = found->targets;
    if (!serviceChannels.empty()) {
        std::vector<Target> commonTargets;
        for (const Target& target : found->targets)
            for (const Target& channel : serviceChannels)
                if (target.channel == channel.channel)
                    commonTargets.push_back(target);
        if (!commonTargets.empty())
            rule.targets = std::move(commonTargets);
    }
}

const char* securityEquivalent(Category category, QStringView field)
{
    struct Pair
    {
        const char* sysmon;
        const char* security;
    };
    static constexpr Pair CREATION[] = {{"Image", "NewProcessName"},     {"ParentImage", "ParentProcessName"},
                                        {"ProcessId", "NewProcessId"},   {"ParentProcessId", "ProcessId"},
                                        {"User", "SubjectUserName"},     {"IntegrityLevel", "MandatoryLabel"},
                                        {"LogonId", "SubjectLogonId"}};
    static constexpr Pair END[] = {{"Image", "ProcessName"}, {"User", "SubjectUserName"}};
    static constexpr Pair NETWORK[] = {{"Image", "Application"},        {"SourceIp", "SourceAddress"},
                                       {"DestinationIp", "DestAddress"}, {"DestinationPort", "DestPort"},
                                       {"SourcePort", "SourcePort"}};
    auto find = [&](const auto& table) -> const char* {
        for (const Pair& pair : table)
            if (field.compare(QLatin1String(pair.sysmon), Qt::CaseInsensitive) == 0)
                return pair.security;
        return nullptr;
    };
    switch (category) {
    case Category::ProcessCreation:
        return find(CREATION);
    case Category::ProcessTermination:
        return find(END);
    case Category::NetworkConnection:
        return find(NETWORK);
    case Category::Other:
        break;
    }
    return nullptr;
}

// ── Values and modifiers ─────────────────────────────────────────────────────

struct Modifiers
{
    enum class TextEncoding { None, Utf16Le, Utf16Be, Utf16 };

    bool contains = false;
    bool startsWith = false;
    bool endsWith = false;
    bool all = false;
    bool caseSensitive = false;
    bool windash = false;
    bool base64 = false;
    bool base64Offset = false;
    bool regex = false;
    bool cidr = false;
    bool exists = false;
    bool reference = false;
    int comparison = -1;
    TextEncoding encoding = TextEncoding::None;
    QRegularExpression::PatternOptions regexOptions = QRegularExpression::NoPatternOption;
};

Modifiers readModifiers(const QStringList& names)
{
    Modifiers m;
    for (const QString& raw : names) {
        const QString name = raw.toLower();
        if (name == QLatin1String("contains"))
            m.contains = true;
        else if (name == QLatin1String("startswith"))
            m.startsWith = true;
        else if (name == QLatin1String("endswith"))
            m.endsWith = true;
        else if (name == QLatin1String("all"))
            m.all = true;
        else if (name == QLatin1String("cased"))
            m.caseSensitive = true;
        else if (name == QLatin1String("windash"))
            m.windash = true;
        else if (name == QLatin1String("base64"))
            m.base64 = true;
        else if (name == QLatin1String("base64offset"))
            m.base64Offset = true;
        else if (name == QLatin1String("re"))
            m.regex = true;
        else if (name == QLatin1String("i") && m.regex)
            m.regexOptions |= QRegularExpression::CaseInsensitiveOption;
        else if (name == QLatin1String("m") && m.regex)
            m.regexOptions |= QRegularExpression::MultilineOption;
        else if (name == QLatin1String("s") && m.regex)
            m.regexOptions |= QRegularExpression::DotMatchesEverythingOption;
        else if (name == QLatin1String("cidr"))
            m.cidr = true;
        else if (name == QLatin1String("exists"))
            m.exists = true;
        else if (name == QLatin1String("fieldref"))
            m.reference = true;
        else if (name == QLatin1String("gt"))
            m.comparison = 0;
        else if (name == QLatin1String("gte"))
            m.comparison = 1;
        else if (name == QLatin1String("lt"))
            m.comparison = 2;
        else if (name == QLatin1String("lte"))
            m.comparison = 3;
        else if (name == QLatin1String("utf16le") || name == QLatin1String("wide"))
            m.encoding = Modifiers::TextEncoding::Utf16Le;
        else if (name == QLatin1String("utf16be"))
            m.encoding = Modifiers::TextEncoding::Utf16Be;
        else if (name == QLatin1String("utf16"))
            m.encoding = Modifiers::TextEncoding::Utf16;
        else
            throw Rejection{qtTrId("rules.error_modifier").arg(raw), false};
    }
    if (m.encoding != Modifiers::TextEncoding::None && !m.base64 && !m.base64Offset)
        throw Rejection{qtTrId("rules.error_encoding"), false};
    return m;
}

QByteArray encoder(const QString& value, Modifiers::TextEncoding encoding)
{
    if (encoding == Modifiers::TextEncoding::None)
        return value.toUtf8();
    QByteArray bytes;
    if (encoding == Modifiers::TextEncoding::Utf16)
        bytes.append("\xff\xfe", 2);
    for (const QChar c : value) {
        const char16_t u = c.unicode();
        if (encoding == Modifiers::TextEncoding::Utf16Be) {
            bytes.append(static_cast<char>(u >> 8));
            bytes.append(static_cast<char>(u & 0xff));
        } else {
            bytes.append(static_cast<char>(u & 0xff));
            bytes.append(static_cast<char>(u >> 8));
        }
    }
    return bytes;
}

QStringList dashVariants(const QString& value)
{
    QList<qsizetype> positions;
    for (qsizetype i = 0; i < value.size(); ++i)
        if (value[i] == u'-' && (i == 0 || value[i - 1].isSpace()))
            positions << i;
    if (positions.isEmpty())
        return {value};
    QStringList variants;
    for (const char16_t dash : {u'-', u'/', u'–', u'—', u'―'}) {
        QString variant = value;
        for (const qsizetype position : positions)
            variant[position] = QChar(dash);
        variants << variant;
    }
    return variants;
}

struct Segment
{
    QChar wildcard;
    QString text;
};

std::vector<Segment> splitSegments(const QString& value, bool literal)
{
    std::vector<Segment> segments;
    auto appendText = [&](QChar c) {
        if (segments.empty() || !segments.back().wildcard.isNull())
            segments.push_back({QChar(), QString()});
        segments.back().text += c;
    };
    if (literal) {
        if (!value.isEmpty())
            segments.push_back({QChar(), value});
        return segments;
    }
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value[i];
        if (c == u'\\' && i + 1 < value.size()
            && (value[i + 1] == u'*' || value[i + 1] == u'?' || value[i + 1] == u'\\')) {
            appendText(value[++i]);
        } else if (c == u'*') {
            if (segments.empty() || segments.back().wildcard != u'*')
                segments.push_back({QChar(u'*'), QString()});
        } else if (c == u'?') {
            segments.push_back({QChar(u'?'), QString()});
        } else {
            appendText(c);
        }
    }
    return segments;
}

Pattern textPattern(std::vector<Segment> segments, const Modifiers& m)
{
    const Segment star{QChar(u'*'), QString()};
    if (m.contains || m.endsWith)
        if (segments.empty() || segments.front().wildcard != u'*')
            segments.insert(segments.begin(), star);
    if (m.contains || m.startsWith)
        if (segments.empty() || segments.back().wildcard != u'*')
            segments.push_back(star);

    Pattern pattern;
    pattern.caseSensitive = m.caseSensitive;
    auto text = [&](const Segment& s) { return m.caseSensitive ? s.text : s.text.toLower(); };
    const bool point = std::any_of(segments.begin(), segments.end(), [](const Segment& s) { return s.wildcard == u'?'; });
    const auto stars =
        std::count_if(segments.begin(), segments.end(), [](const Segment& s) { return s.wildcard == u'*'; });
    if (!point) {
        if (segments.empty()) {
            pattern.type = Pattern::Type::Equals;
            return pattern;
        }
        if (stars == 0) {
            pattern.type = Pattern::Type::Equals;
            pattern.text = text(segments.front());
            return pattern;
        }
        if (segments.size() == 1) {
            pattern.type = Pattern::Type::Always;
            return pattern;
        }
        if (segments.size() == 2) {
            const bool start = segments.front().wildcard == u'*';
            pattern.type = start ? Pattern::Type::EndsWith : Pattern::Type::StartsWith;
            pattern.text = text(start ? segments.back() : segments.front());
            return pattern;
        }
        if (segments.size() == 3 && stars == 2 && segments[1].wildcard.isNull()) {
            pattern.type = Pattern::Type::Contains;
            pattern.text = text(segments[1]);
            return pattern;
        }
    }
    QString expression = QStringLiteral("\\A");
    for (const Segment& segment : segments) {
        if (segment.wildcard == u'*')
            expression += QLatin1String(".*");
        else if (segment.wildcard == u'?')
            expression += QLatin1Char('.');
        else
            expression += QRegularExpression::escape(segment.text);
    }
    expression += QLatin1String("\\z");
    QRegularExpression::PatternOptions options = QRegularExpression::DotMatchesEverythingOption;
    if (!m.caseSensitive)
        options |= QRegularExpression::CaseInsensitiveOption;
    pattern.type = Pattern::Type::Wildcard;
    pattern.expression = QRegularExpression(expression, options);
    pattern.expression.optimize();
    return pattern;
}

std::vector<Pattern> compileValues(const yaml::Node& node, const Modifiers& m, bool keyword)
{
    std::vector<const yaml::Node*> values;
    if (node.isList()) {
        for (const yaml::Node& element : node.elements) {
            if (element.isTable() || element.isList())
                throw Rejection{qtTrId("rules.error_nested_value"), false};
            values.push_back(&element);
        }
    } else if (node.isTable()) {
        throw Rejection{qtTrId("rules.error_table_value"), false};
    } else {
        values.push_back(&node);
    }

    std::vector<Pattern> patterns;
    if (m.exists) {
        for (const yaml::Node* value : values) {
            Pattern pattern;
            pattern.type = Pattern::Type::Exists;
            pattern.expected = value->value.compare(QLatin1String("false"), Qt::CaseInsensitive) != 0;
            patterns.push_back(pattern);
        }
        return patterns;
    }
    for (const yaml::Node* value : values) {
        if (value->isNull()) {
            Pattern pattern;
            pattern.type = Pattern::Type::Null;
            patterns.push_back(pattern);
            continue;
        }
        const QString text = value->value;
        if (m.regex) {
            Pattern pattern;
            pattern.type = Pattern::Type::Regex;
            pattern.expression = QRegularExpression(text, m.regexOptions);
            if (!pattern.expression.isValid())
                throw Rejection{qtTrId("rules.error_regex").arg(pattern.expression.errorString()), false};
            pattern.expression.optimize();
            patterns.push_back(std::move(pattern));
            continue;
        }
        if (m.cidr) {
            const qsizetype bar = text.indexOf(QLatin1Char('/'));
            Pattern pattern;
            pattern.type = Pattern::Type::Cidr;
            bool ok = parseIp(bar >= 0 ? QStringView(text).left(bar) : QStringView(text), pattern.network);
            pattern.prefix = bar >= 0 ? text.mid(bar + 1).toInt(&ok) : static_cast<int>(pattern.network.size()) * 8;
            if (!ok)
                throw Rejection{qtTrId("rules.error_network").arg(text), false};
            patterns.push_back(std::move(pattern));
            continue;
        }
        if (m.comparison >= 0) {
            Pattern pattern;
            pattern.type = Pattern::Type::Comparison;
            pattern.op = m.comparison;
            bool ok = false;
            pattern.number = numberFrom(text, ok);
            if (!ok)
                throw Rejection{qtTrId("rules.error_number").arg(text), false};
            patterns.push_back(pattern);
            continue;
        }
        if (m.reference) {
            Pattern pattern;
            pattern.type = Pattern::Type::Reference;
            pattern.text = text;
            patterns.push_back(std::move(pattern));
            continue;
        }
        const QStringList variants = m.windash ? dashVariants(text) : QStringList{text};
        for (const QString& variant : variants) {
            if (m.base64 || m.base64Offset) {
                const QByteArray bytes = encoder(variant, m.encoding);
                if (m.base64) {
                    patterns.push_back(textPattern(splitSegments(QString::fromLatin1(bytes.toBase64()), true), m));
                } else {
                    static constexpr int STARTS[] = {0, 2, 3};
                    static constexpr int ENDS[] = {0, 3, 2};
                    for (int i = 0; i < 3; ++i) {
                        QByteArray shifted(i, ' ');
                        shifted.append(bytes);
                        const QByteArray code = shifted.toBase64();
                        const int end = ENDS[(bytes.size() + i) % 3];
                        const QByteArray piece = code.mid(STARTS[i], code.size() - STARTS[i] - end);
                        patterns.push_back(textPattern(splitSegments(QString::fromLatin1(piece), true), m));
                    }
                }
                continue;
            }
            Modifiers effectives = m;
            if (keyword)
                effectives.contains = true;
            patterns.push_back(textPattern(splitSegments(variant, false), effectives));
        }
    }
    return patterns;
}

Group compileGroup(const yaml::Node& table)
{
    Group group;
    for (size_t i = 0; i < table.keys.size(); ++i) {
        QStringList parts = table.keys[i].split(QLatin1Char('|'));
        Condition condition;
        condition.field = parts.takeFirst().trimmed();
        const Modifiers modifiers = readModifiers(parts);
        condition.all = modifiers.all;
        condition.patterns = compileValues(table.elements[i], modifiers, condition.field.isEmpty());
        group.conditions.push_back(std::move(condition));
    }
    return group;
}

Selection compileSelection(const QString& name, const yaml::Node& node)
{
    Selection selection;
    selection.name = name;
    if (node.isTable()) {
        selection.groups.push_back(compileGroup(node));
        return selection;
    }
    if (node.isList()) {
        const bool tables = std::all_of(node.elements.begin(), node.elements.end(),
                                        [](const yaml::Node& e) { return e.isTable(); });
        if (tables) {
            for (const yaml::Node& element : node.elements)
                selection.groups.push_back(compileGroup(element));
            return selection;
        }
    }
    if (node.isNull())
        throw Rejection{qtTrId("rules.error_empty_selection").arg(name), false};
    Group group;
    Condition condition;
    condition.patterns = compileValues(node, Modifiers(), true);
    group.conditions.push_back(std::move(condition));
    selection.groups.push_back(std::move(group));
    return selection;
}

// ── Condition ────────────────────────────────────────────────────────────────

class ConditionParser
{
public:
    ConditionParser(const QString& text, const std::vector<Selection>& selections) : m_selections(selections)
    {
        QString token;
        auto cut = [&] {
            if (!token.isEmpty())
                m_tokens << token;
            token.clear();
        };
        for (const QChar c : text) {
            if (c == u'|')
                throw Rejection{qtTrId("rules.error_aggregation"), true};
            if (c == u'(' || c == u')') {
                cut();
                m_tokens << QString(c);
            } else if (c.isSpace()) {
                cut();
            } else {
                token += c;
            }
        }
        cut();
    }

    Expression analyze()
    {
        Expression result = parseOr();
        if (m_position < m_tokens.size())
            throw Rejection{qtTrId("rules.error_condition_unexpected").arg(m_tokens[m_position]), false};
        return result;
    }

private:
    [[nodiscard]] bool isWord(const char* word) const
    {
        return m_position < m_tokens.size()
               && m_tokens[m_position].compare(QLatin1String(word), Qt::CaseInsensitive) == 0;
    }

    Expression parseOr()
    {
        Expression left = parseAnd();
        if (!isWord("or"))
            return left;
        Expression node;
        node.type = Expression::Type::Or;
        node.children.push_back(std::move(left));
        while (isWord("or")) {
            ++m_position;
            node.children.push_back(parseAnd());
        }
        return node;
    }

    Expression parseAnd()
    {
        Expression left = parseNot();
        if (!isWord("and"))
            return left;
        Expression node;
        node.type = Expression::Type::And;
        node.children.push_back(std::move(left));
        while (isWord("and")) {
            ++m_position;
            node.children.push_back(parseNot());
        }
        return node;
    }

    Expression parseNot()
    {
        if (isWord("not")) {
            ++m_position;
            Expression node;
            node.type = Expression::Type::Not;
            node.children.push_back(parseNot());
            return node;
        }
        return parsePrimary();
    }

    Expression parsePrimary()
    {
        if (m_position >= m_tokens.size())
            throw Rejection{qtTrId("rules.error_condition_incomplete"), false};
        const QString token = m_tokens[m_position++];
        if (token == QLatin1String("(")) {
            Expression inner = parseOr();
            if (!isWord(")"))
                throw Rejection{qtTrId("rules.error_parenthesis"), false};
            ++m_position;
            return inner;
        }
        if (isWord("of")) {
            ++m_position;
            if (m_position >= m_tokens.size())
                throw Rejection{qtTrId("rules.error_of_target"), false};
            const QString target = m_tokens[m_position++];
            return quantifier(token, target);
        }
        for (size_t i = 0; i < m_selections.size(); ++i) {
            if (m_selections[i].name == token) {
                Expression node;
                node.selection = static_cast<int>(i);
                return node;
            }
        }
        throw Rejection{qtTrId("rules.error_unknown_selection").arg(token), false};
    }

    Expression quantifier(const QString& quantity, const QString& target)
    {
        Expression node;
        const bool all = quantity.compare(QLatin1String("all"), Qt::CaseInsensitive) == 0;
        int minimum = 1;
        if (!all && quantity.compare(QLatin1String("any"), Qt::CaseInsensitive) != 0) {
            bool ok = false;
            minimum = quantity.toInt(&ok);
            if (!ok || minimum < 1)
                throw Rejection{qtTrId("rules.error_quantifier").arg(quantity), false};
        }
        const bool them = target.compare(QLatin1String("them"), Qt::CaseInsensitive) == 0;
        const QRegularExpression matcher(QRegularExpression::wildcardToRegularExpression(target));
        for (size_t i = 0; i < m_selections.size(); ++i) {
            const QString& name = m_selections[i].name;
            if (them ? name.startsWith(QLatin1Char('_')) : !matcher.match(name).hasMatch())
                continue;
            Expression child;
            child.selection = static_cast<int>(i);
            node.children.push_back(child);
        }
        if (node.children.empty())
            throw Rejection{qtTrId("rules.error_no_selection").arg(target), false};
        if (all) {
            node.type = Expression::Type::And;
        } else if (minimum == 1) {
            node.type = Expression::Type::Or;
        } else {
            node.type = Expression::Type::AtLeast;
            node.minimum = minimum;
        }
        return node;
    }

    const std::vector<Selection>& m_selections;
    QStringList m_tokens;
    qsizetype m_position = 0;
};

// ── Required event identifiers ──────────────────────────────────────────────

std::optional<std::vector<quint32>> selectionIdentifiers(const Selection& selection)
{
    if (selection.groups.empty())
        return std::nullopt;
    std::vector<quint32> result;
    for (const Group& group : selection.groups) {
        std::optional<std::vector<quint32>> identifiers;
        for (const Condition& condition : group.conditions) {
            if (condition.all || condition.field.compare(QLatin1String("EventID"), Qt::CaseInsensitive) != 0)
                continue;
            std::vector<quint32> values;
            bool ok = true;
            for (const Pattern& pattern : condition.patterns) {
                bool isNumber = false;
                const quint32 value = pattern.text.toUInt(&isNumber);
                if (pattern.type != Pattern::Type::Equals || !isNumber) {
                    ok = false;
                    break;
                }
                values.push_back(value);
            }
            if (ok && !values.empty()) {
                identifiers = std::move(values);
                break;
            }
        }
        if (!identifiers)
            return std::nullopt;
        result.insert(result.end(), identifiers->begin(), identifiers->end());
    }
    return result;
}

std::optional<std::vector<quint32>> expressionIdentifiers(const Expression& expression, const Rule& rule)
{
    switch (expression.type) {
    case Expression::Type::Selection:
        return selectionIdentifiers(rule.selections[static_cast<size_t>(expression.selection)]);
    case Expression::Type::And: {
        std::optional<std::vector<quint32>> result;
        for (const Expression& child : expression.children) {
            auto identifiers = expressionIdentifiers(child, rule);
            if (!identifiers)
                continue;
            if (!result) {
                result = std::move(identifiers);
                continue;
            }
            std::vector<quint32> commonIds;
            for (const quint32 id : *result)
                if (std::find(identifiers->begin(), identifiers->end(), id) != identifiers->end())
                    commonIds.push_back(id);
            result = std::move(commonIds);
        }
        return result;
    }
    case Expression::Type::Or:
    case Expression::Type::AtLeast: {
        std::vector<quint32> result;
        for (const Expression& child : expression.children) {
            auto identifiers = expressionIdentifiers(child, rule);
            if (!identifiers)
                return std::nullopt;
            result.insert(result.end(), identifiers->begin(), identifiers->end());
        }
        return result;
    }
    case Expression::Type::Not:
        break;
    }
    return std::nullopt;
}

QString localized(const yaml::Node& document, const char* key, const QString& language)
{
    if (!language.isEmpty()) {
        const QString text = document.text(QStringLiteral("%1_%2").arg(QLatin1String(key), language));
        if (!text.isEmpty())
            return text.trimmed();
    }
    return document.text(QString::fromLatin1(key)).trimmed();
}

} // namespace

// ── Level ────────────────────────────────────────────────────────────────────

Level levelFromText(QStringView text)
{
    if (text.compare(QLatin1String("informational"), Qt::CaseInsensitive) == 0)
        return Level::Information;
    if (text.compare(QLatin1String("low"), Qt::CaseInsensitive) == 0)
        return Level::Low;
    if (text.compare(QLatin1String("high"), Qt::CaseInsensitive) == 0)
        return Level::High;
    if (text.compare(QLatin1String("critical"), Qt::CaseInsensitive) == 0)
        return Level::Critical;
    return Level::Medium;
}

// ── Event ────────────────────────────────────────────────────────────────────

void Event::add(const QString& name, const QString& value)
{
    if (m_count < m_names.size()) {
        m_names[m_count] = name;
        m_values[m_count] = value;
        m_computed[m_count] = 0;
    } else {
        m_names.push_back(name);
        m_values.push_back(value);
        m_lowercase.emplace_back();
        m_computed.push_back(0);
    }
    ++m_count;
}

void Event::set(quint32 eventId, const QString& channel, const QString& provider, const QString& machine,
                const std::vector<evtx::DataField>& fields)
{
    static const QString EVENT_ID = QStringLiteral("EventID");
    static const QString CHANNEL = QStringLiteral("Channel");
    static const QString PROVIDER = QStringLiteral("Provider_Name");
    static const QString COMPUTER = QStringLiteral("Computer");
    m_eventId = eventId;
    m_channel = channel.toLower();
    m_security = m_channel == QLatin1String("security");
    m_count = 0;
    add(EVENT_ID, QString::number(eventId));
    add(CHANNEL, channel);
    add(PROVIDER, provider);
    add(COMPUTER, machine);
    for (const evtx::DataField& field : fields)
        add(field.name, field.value);
    if (m_channel != QLatin1String("windows powershell"))
        return;
    for (const evtx::DataField& field : fields) {
        if (!field.value.contains(QLatin1Char('\n')))
            continue;
        for (const QStringView row : QStringView(field.value).split(u'\n')) {
            const QStringView bare = row.trimmed();
            const qsizetype equal = bare.indexOf(u'=');
            if (equal > 0)
                add(bare.left(equal).toString(), bare.mid(equal + 1).trimmed().toString());
        }
    }
}

int Event::index(QStringView name) const
{
    for (size_t i = 0; i < m_count; ++i)
        if (m_names[i].compare(name, Qt::CaseInsensitive) == 0)
            return static_cast<int>(i);
    return -1;
}

const QString& Event::lowerValue(int index) const
{
    const auto i = static_cast<size_t>(index);
    if (!m_computed[i]) {
        m_lowercase[i] = m_values[i].toLower();
        m_computed[i] = 1;
    }
    return m_lowercase[i];
}

// ── Pattern ──────────────────────────────────────────────────────────────────

bool Pattern::matches(const QString& value, const QString& lowercase) const
{
    switch (type) {
    case Type::Equals:
        return caseSensitive ? value == text : lowercase == text;
    case Type::Contains:
        return (caseSensitive ? value : lowercase).contains(text);
    case Type::StartsWith:
        return (caseSensitive ? value : lowercase).startsWith(text);
    case Type::EndsWith:
        return (caseSensitive ? value : lowercase).endsWith(text);
    case Type::Wildcard:
    case Type::Regex:
        return expression.match(value).hasMatch();
    case Type::Always:
        return true;
    case Type::Cidr: {
        QByteArray address;
        return parseIp(value, address) && inNetwork(address, network, prefix);
    }
    case Type::Comparison: {
        bool ok = false;
        const double v = numberFrom(value, ok);
        if (!ok)
            return false;
        switch (op) {
        case 0:
            return v > number;
        case 1:
            return v >= number;
        case 2:
            return v < number;
        default:
            return v <= number;
        }
    }
    case Type::Null:
    case Type::Exists:
    case Type::Reference:
        break;
    }
    return false;
}

// ── Rule ─────────────────────────────────────────────────────────────────────

bool Rule::appliesTo(quint32 identifier, const QString& lowerChannel) const
{
    if (requiredIdentifiers
        && std::find(requiredIdentifiers->begin(), requiredIdentifiers->end(), identifier) == requiredIdentifiers->end())
        return false;
    if (targets.empty())
        return true;
    for (const Target& target : targets) {
        if (!target.channel.isEmpty() && target.channel != lowerChannel)
            continue;
        if (target.identifiers.empty()
            || std::find(target.identifiers.begin(), target.identifiers.end(), identifier) != target.identifiers.end())
            return true;
    }
    return false;
}

int Rule::fieldIndex(const Event& event, const QString& field) const
{
    if (category != Category::Other && event.isSecurity()) {
        if (const char* equivalent = securityEquivalent(category, field)) {
            const int index = event.index(QString::fromLatin1(equivalent));
            if (index >= 0)
                return index;
        }
    }
    return event.index(field);
}

bool Rule::evaluate(const Condition& criterion, const Event& event) const
{
    if (criterion.field.isEmpty()) {
        for (const Pattern& pattern : criterion.patterns) {
            bool found = false;
            for (int i = 0; i < event.count() && !found; ++i)
                found = pattern.matches(event.value(i), event.lowerValue(i));
            if (criterion.all && !found)
                return false;
            if (!criterion.all && found)
                return true;
        }
        return criterion.all;
    }
    const int index = fieldIndex(event, criterion.field);
    for (const Pattern& pattern : criterion.patterns) {
        bool ok = false;
        switch (pattern.type) {
        case Pattern::Type::Null:
            ok = index < 0 || event.value(index).isEmpty();
            break;
        case Pattern::Type::Exists:
            ok = (index >= 0) == pattern.expected;
            break;
        case Pattern::Type::Reference: {
            const int other = fieldIndex(event, pattern.text);
            ok = index >= 0 && other >= 0 && event.lowerValue(index) == event.lowerValue(other);
            break;
        }
        case Pattern::Type::Equals:
            ok = index >= 0 ? pattern.matches(event.value(index), event.lowerValue(index))
                            : pattern.text.isEmpty();
            break;
        default:
            ok = index >= 0 && pattern.matches(event.value(index), event.lowerValue(index));
        }
        if (criterion.all && !ok)
            return false;
        if (!criterion.all && ok)
            return true;
    }
    return criterion.all;
}

bool Rule::evaluate(const Selection& selection, const Event& event) const
{
    for (const Group& group : selection.groups) {
        const bool matched = std::all_of(group.conditions.begin(), group.conditions.end(),
                                         [&](const Condition& c) { return evaluate(c, event); });
        if (matched)
            return true;
    }
    return false;
}

bool Rule::evaluate(const Expression& expression, const Event& event) const
{
    switch (expression.type) {
    case Expression::Type::Selection:
        return evaluate(selections[static_cast<size_t>(expression.selection)], event);
    case Expression::Type::And:
        return std::all_of(expression.children.begin(), expression.children.end(),
                           [&](const Expression& e) { return evaluate(e, event); });
    case Expression::Type::Or:
        return std::any_of(expression.children.begin(), expression.children.end(),
                           [&](const Expression& e) { return evaluate(e, event); });
    case Expression::Type::Not:
        return !evaluate(expression.children.front(), event);
    case Expression::Type::AtLeast: {
        int matchedCount = 0;
        for (const Expression& child : expression.children)
            if (evaluate(child, event) && ++matchedCount >= expression.minimum)
                return true;
        return false;
    }
    }
    return false;
}

bool Rule::evaluate(const Event& event) const
{
    return evaluate(condition, event);
}

// ── Compilation ──────────────────────────────────────────────────────────────

Compilation compile(const yaml::Node& document, const QString& origin, const QString& language)
{
    Compilation result;
    Rule& rule = result.rule;
    rule.origin = origin;
    try {
        if (!document.isTable())
            throw Rejection{qtTrId("rules.error_not_a_rule"), false};
        rule.title = localized(document, "title", language);
        rule.id = document.text(u"id").trimmed();
        if (rule.id.isEmpty())
            rule.id = rule.title;
        rule.description = localized(document, "description", language);
        if (const yaml::Node* author = document.field(u"author"))
            rule.author = author->texts().join(QStringLiteral(", "));
        rule.level = levelFromText(document.text(u"level"));
        if (const yaml::Node* tags = document.field(u"tags"))
            for (const QString& tag : tags->texts())
                rule.tags << tag.trimmed().toLower();
        if (const yaml::Node* links = document.field(u"references"))
            rule.links = links->texts();
        const yaml::Node* detection = document.field(u"detection");
        if (!detection || !detection->isTable())
            throw Rejection{qtTrId("rules.error_no_detection"), true};
        const QString status = document.text(u"status").toLower();
        if (status == QLatin1String("deprecated") || status == QLatin1String("unsupported")) {
            result.status = Compilation::Status::Deprecated;
            return result;
        }
        if (rule.title.isEmpty())
            throw Rejection{qtTrId("rules.error_no_title"), false};
        focusOn(document.field(u"logsource"), rule);

        const yaml::Node* condition = nullptr;
        for (size_t i = 0; i < detection->keys.size(); ++i) {
            const QString& name = detection->keys[i];
            if (name == QLatin1String("condition")) {
                condition = &detection->elements[i];
                continue;
            }
            if (name == QLatin1String("timeframe"))
                continue;
            rule.selections.push_back(compileSelection(name, detection->elements[i]));
        }
        if (!condition || condition->isNull())
            throw Rejection{qtTrId("rules.error_no_condition"), false};
        const QStringList conditions = condition->texts();
        if (conditions.isEmpty())
            throw Rejection{qtTrId("rules.error_unreadable_condition"), false};
        if (conditions.size() == 1) {
            rule.condition = ConditionParser(conditions.front(), rule.selections).analyze();
        } else {
            rule.condition.type = Expression::Type::Or;
            for (const QString& text : conditions)
                rule.condition.children.push_back(ConditionParser(text, rule.selections).analyze());
        }
        rule.requiredIdentifiers = expressionIdentifiers(rule.condition, rule);
        result.status = Compilation::Status::Usable;
    } catch (const Rejection& rejection) {
        result.error = rejection.message;
        result.status = rejection.notApplicable ? Compilation::Status::NotApplicable : Compilation::Status::Invalid;
    }
    return result;
}

} // namespace lh::sigma
