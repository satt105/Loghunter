#include "Catalog.h"

#include "Normalization.h"

namespace lh {

namespace {

struct Entry
{
    const char* channel;
    quint32 identifier;
    const char* label;
};

constexpr char SECURITY[] = "Security";
constexpr char SYSTEM[] = "System";
constexpr char SYSMON[] = "Microsoft-Windows-Sysmon/Operational";
constexpr char POWERSHELL[] = "Microsoft-Windows-PowerShell/Operational";
constexpr char POWERSHELL_CLASSIC[] = "Windows PowerShell";
constexpr char DEFENDER[] = "Microsoft-Windows-Windows Defender/Operational";
constexpr char TASKS[] = "Microsoft-Windows-TaskScheduler/Operational";
constexpr char RDP_CONNECTION[] = "Microsoft-Windows-TerminalServices-RemoteConnectionManager/Operational";
constexpr char RDP_SESSION[] = "Microsoft-Windows-TerminalServices-LocalSessionManager/Operational";
constexpr char APPLICATION[] = "Application";

constexpr Entry ENTRIES[] = {
    {SECURITY, 1100, QT_TRID_NOOP("event.security_1100")},
    {SECURITY, 1102, QT_TRID_NOOP("event.security_1102")},
    {SECURITY, 4608, QT_TRID_NOOP("event.security_4608")},
    {SECURITY, 4616, QT_TRID_NOOP("event.security_4616")},
    {SECURITY, 4624, QT_TRID_NOOP("event.security_4624")},
    {SECURITY, 4625, QT_TRID_NOOP("event.security_4625")},
    {SECURITY, 4634, QT_TRID_NOOP("event.security_4634")},
    {SECURITY, 4647, QT_TRID_NOOP("event.security_4647")},
    {SECURITY, 4648, QT_TRID_NOOP("event.security_4648")},
    {SECURITY, 4656, QT_TRID_NOOP("event.security_4656")},
    {SECURITY, 4657, QT_TRID_NOOP("event.security_4657")},
    {SECURITY, 4660, QT_TRID_NOOP("event.security_4660")},
    {SECURITY, 4662, QT_TRID_NOOP("event.security_4662")},
    {SECURITY, 4663, QT_TRID_NOOP("event.security_4663")},
    {SECURITY, 4670, QT_TRID_NOOP("event.security_4670")},
    {SECURITY, 4672, QT_TRID_NOOP("event.security_4672")},
    {SECURITY, 4673, QT_TRID_NOOP("event.security_4673")},
    {SECURITY, 4688, QT_TRID_NOOP("event.security_4688")},
    {SECURITY, 4689, QT_TRID_NOOP("event.security_4689")},
    {SECURITY, 4697, QT_TRID_NOOP("event.security_4697")},
    {SECURITY, 4698, QT_TRID_NOOP("event.security_4698")},
    {SECURITY, 4699, QT_TRID_NOOP("event.security_4699")},
    {SECURITY, 4700, QT_TRID_NOOP("event.security_4700")},
    {SECURITY, 4701, QT_TRID_NOOP("event.security_4701")},
    {SECURITY, 4702, QT_TRID_NOOP("event.security_4702")},
    {SECURITY, 4719, QT_TRID_NOOP("event.security_4719")},
    {SECURITY, 4720, QT_TRID_NOOP("event.security_4720")},
    {SECURITY, 4722, QT_TRID_NOOP("event.security_4722")},
    {SECURITY, 4723, QT_TRID_NOOP("event.security_4723")},
    {SECURITY, 4724, QT_TRID_NOOP("event.security_4724")},
    {SECURITY, 4725, QT_TRID_NOOP("event.security_4725")},
    {SECURITY, 4726, QT_TRID_NOOP("event.security_4726")},
    {SECURITY, 4728, QT_TRID_NOOP("event.security_4728")},
    {SECURITY, 4729, QT_TRID_NOOP("event.security_4729")},
    {SECURITY, 4732, QT_TRID_NOOP("event.security_4732")},
    {SECURITY, 4733, QT_TRID_NOOP("event.security_4733")},
    {SECURITY, 4738, QT_TRID_NOOP("event.security_4738")},
    {SECURITY, 4740, QT_TRID_NOOP("event.security_4740")},
    {SECURITY, 4756, QT_TRID_NOOP("event.security_4756")},
    {SECURITY, 4757, QT_TRID_NOOP("event.security_4757")},
    {SECURITY, 4767, QT_TRID_NOOP("event.security_4767")},
    {SECURITY, 4768, QT_TRID_NOOP("event.security_4768")},
    {SECURITY, 4769, QT_TRID_NOOP("event.security_4769")},
    {SECURITY, 4771, QT_TRID_NOOP("event.security_4771")},
    {SECURITY, 4776, QT_TRID_NOOP("event.security_4776")},
    {SECURITY, 4778, QT_TRID_NOOP("event.security_4778")},
    {SECURITY, 4779, QT_TRID_NOOP("event.security_4779")},
    {SECURITY, 4781, QT_TRID_NOOP("event.security_4781")},
    {SECURITY, 4798, QT_TRID_NOOP("event.security_4798")},
    {SECURITY, 4799, QT_TRID_NOOP("event.security_4799")},
    {SECURITY, 4800, QT_TRID_NOOP("event.security_4800")},
    {SECURITY, 4801, QT_TRID_NOOP("event.security_4801")},
    {SECURITY, 4946, QT_TRID_NOOP("event.security_4946")},
    {SECURITY, 4947, QT_TRID_NOOP("event.security_4947")},
    {SECURITY, 4948, QT_TRID_NOOP("event.security_4948")},
    {SECURITY, 5140, QT_TRID_NOOP("event.security_5140")},
    {SECURITY, 5145, QT_TRID_NOOP("event.security_5145")},
    {SECURITY, 5156, QT_TRID_NOOP("event.security_5156")},
    {SECURITY, 5157, QT_TRID_NOOP("event.security_5157")},
    {SYSTEM, 104, QT_TRID_NOOP("event.system_104")},
    {SYSTEM, 1074, QT_TRID_NOOP("event.system_1074")},
    {SYSTEM, 6005, QT_TRID_NOOP("event.system_6005")},
    {SYSTEM, 6006, QT_TRID_NOOP("event.system_6006")},
    {SYSTEM, 6008, QT_TRID_NOOP("event.system_6008")},
    {SYSTEM, 7034, QT_TRID_NOOP("event.system_7034")},
    {SYSTEM, 7036, QT_TRID_NOOP("event.system_7036")},
    {SYSTEM, 7040, QT_TRID_NOOP("event.system_7040")},
    {SYSTEM, 7045, QT_TRID_NOOP("event.system_7045")},
    {SYSMON, 1, QT_TRID_NOOP("event.sysmon_1")},
    {SYSMON, 2, QT_TRID_NOOP("event.sysmon_2")},
    {SYSMON, 3, QT_TRID_NOOP("event.sysmon_3")},
    {SYSMON, 5, QT_TRID_NOOP("event.sysmon_5")},
    {SYSMON, 6, QT_TRID_NOOP("event.sysmon_6")},
    {SYSMON, 7, QT_TRID_NOOP("event.sysmon_7")},
    {SYSMON, 8, QT_TRID_NOOP("event.sysmon_8")},
    {SYSMON, 10, QT_TRID_NOOP("event.sysmon_10")},
    {SYSMON, 11, QT_TRID_NOOP("event.sysmon_11")},
    {SYSMON, 12, QT_TRID_NOOP("event.sysmon_12")},
    {SYSMON, 13, QT_TRID_NOOP("event.sysmon_13")},
    {SYSMON, 15, QT_TRID_NOOP("event.sysmon_15")},
    {SYSMON, 17, QT_TRID_NOOP("event.sysmon_17")},
    {SYSMON, 18, QT_TRID_NOOP("event.sysmon_18")},
    {SYSMON, 22, QT_TRID_NOOP("event.sysmon_22")},
    {SYSMON, 23, QT_TRID_NOOP("event.sysmon_23")},
    {SYSMON, 25, QT_TRID_NOOP("event.sysmon_25")},
    {POWERSHELL, 4103, QT_TRID_NOOP("event.powershell_4103")},
    {POWERSHELL, 4104, QT_TRID_NOOP("event.powershell_4104")},
    {POWERSHELL_CLASSIC, 400, QT_TRID_NOOP("event.powershell_classic_400")},
    {POWERSHELL_CLASSIC, 800, QT_TRID_NOOP("event.powershell_classic_800")},
    {DEFENDER, 1116, QT_TRID_NOOP("event.defender_1116")},
    {DEFENDER, 1117, QT_TRID_NOOP("event.defender_1117")},
    {DEFENDER, 5001, QT_TRID_NOOP("event.defender_5001")},
    {DEFENDER, 5007, QT_TRID_NOOP("event.defender_5007")},
    {TASKS, 106, QT_TRID_NOOP("event.tasks_106")},
    {TASKS, 200, QT_TRID_NOOP("event.tasks_200")},
    {RDP_CONNECTION, 1149, QT_TRID_NOOP("event.rdp_1149")},
    {RDP_SESSION, 21, QT_TRID_NOOP("event.rdp_21")},
    {RDP_SESSION, 24, QT_TRID_NOOP("event.rdp_24")},
    {RDP_SESSION, 25, QT_TRID_NOOP("event.rdp_25")},
    {SECURITY, 4964, QT_TRID_NOOP("event.security_4964")},
    {SECURITY, 4765, QT_TRID_NOOP("event.security_4765")},
    {SECURITY, 4794, QT_TRID_NOOP("event.security_4794")},
    {SECURITY, 4627, QT_TRID_NOOP("event.security_4627")},
    {SECURITY, 4658, QT_TRID_NOOP("event.security_4658")},
    {SECURITY, 4659, QT_TRID_NOOP("event.security_4659")},
    {SECURITY, 4661, QT_TRID_NOOP("event.security_4661")},
    {SECURITY, 4674, QT_TRID_NOOP("event.security_4674")},
    {SECURITY, 4690, QT_TRID_NOOP("event.security_4690")},
    {SECURITY, 4703, QT_TRID_NOOP("event.security_4703")},
    {SECURITY, 4904, QT_TRID_NOOP("event.security_4904")},
    {SECURITY, 4905, QT_TRID_NOOP("event.security_4905")},
    {SECURITY, 4907, QT_TRID_NOOP("event.security_4907")},
    {SECURITY, 4985, QT_TRID_NOOP("event.security_4985")},
    {SECURITY, 5058, QT_TRID_NOOP("event.security_5058")},
    {SECURITY, 5059, QT_TRID_NOOP("event.security_5059")},
    {SECURITY, 5061, QT_TRID_NOOP("event.security_5061")},
    {SECURITY, 5152, QT_TRID_NOOP("event.security_5152")},
    {SECURITY, 5154, QT_TRID_NOOP("event.security_5154")},
    {SECURITY, 5158, QT_TRID_NOOP("event.security_5158")},
    {SECURITY, 5379, QT_TRID_NOOP("event.security_5379")},
    {SECURITY, 5382, QT_TRID_NOOP("event.security_5382")},
    {SECURITY, 6416, QT_TRID_NOOP("event.security_6416")},
    {SECURITY, 8222, QT_TRID_NOOP("event.security_8222")},
    {SYSTEM, 12, QT_TRID_NOOP("event.system_12")},
    {SYSTEM, 13, QT_TRID_NOOP("event.system_13")},
    {SYSTEM, 41, QT_TRID_NOOP("event.system_41")},
    {SYSTEM, 10016, QT_TRID_NOOP("event.system_10016")},
    {APPLICATION, 1000, QT_TRID_NOOP("event.application_1000")},
    {APPLICATION, 1001, QT_TRID_NOOP("event.application_1001")},
    {APPLICATION, 1002, QT_TRID_NOOP("event.application_1002")},
    {APPLICATION, 1026, QT_TRID_NOOP("event.application_1026")},
    {APPLICATION, 11707, QT_TRID_NOOP("event.application_11707")},
    {APPLICATION, 11708, QT_TRID_NOOP("event.application_11708")},
    {APPLICATION, 11724, QT_TRID_NOOP("event.application_11724")},
    {POWERSHELL, 4100, QT_TRID_NOOP("event.powershell_4100")},
    {POWERSHELL, 4105, QT_TRID_NOOP("event.powershell_4105")},
    {POWERSHELL, 4106, QT_TRID_NOOP("event.powershell_4106")},
    {POWERSHELL, 40961, QT_TRID_NOOP("event.powershell_40961")},
    {POWERSHELL, 40962, QT_TRID_NOOP("event.powershell_40962")},
    {POWERSHELL, 53504, QT_TRID_NOOP("event.powershell_53504")},
    {POWERSHELL_CLASSIC, 403, QT_TRID_NOOP("event.powershell_classic_403")},
    {POWERSHELL_CLASSIC, 600, QT_TRID_NOOP("event.powershell_classic_600")},
    {DEFENDER, 1150, QT_TRID_NOOP("event.defender_1150")},
    {DEFENDER, 1151, QT_TRID_NOOP("event.defender_1151")},
    {DEFENDER, 2000, QT_TRID_NOOP("event.defender_2000")},
    {DEFENDER, 2010, QT_TRID_NOOP("event.defender_2010")},
};

const QList<LevelFilter> LEVELS{
    {0, QT_TRID_NOOP("level.all")},
    {8, QT_TRID_NOOP("level.important")},
    {level::Critical, QT_TRID_NOOP("level.critical")},
    {level::Error, QT_TRID_NOOP("level.error")},
    {level::Warning, QT_TRID_NOOP("level.warning")},
    {level::Information, QT_TRID_NOOP("level.information")},
    {level::Verbose, QT_TRID_NOOP("level.verbose")},
    {6, QT_TRID_NOOP("level.audit_failures")},
    {7, QT_TRID_NOOP("level.audit_successes")},
};

} // namespace

QString eventDescription(quint32 identifier, QStringView channel)
{
    for (const Entry& entry : ENTRIES)
        if (entry.identifier == identifier && channel.compare(QLatin1String(entry.channel), Qt::CaseInsensitive) == 0)
            return qtTrId(entry.label);
    if (channel.isEmpty())
        for (const Entry& entry : ENTRIES)
            if (entry.identifier == identifier && QLatin1String(entry.channel) == QLatin1String(SECURITY))
                return qtTrId(entry.label);
    return {};
}

QString eventLabel(quint32 identifier, QStringView channel, QStringView provider)
{
    QString description = eventDescription(identifier, channel);
    if (description.isEmpty()) {
        description = provider.toString();
        if (description.startsWith(QLatin1String("Microsoft-Windows-")))
            description = description.mid(18).replace(QLatin1Char('-'), QLatin1Char(' '));
    }
    return description.isEmpty() ? qtTrId("catalog.event_number").arg(identifier)
                                 : qtTrId("catalog.event_number_description").arg(identifier).arg(description);
}

const QList<LevelFilter>& filterLevels()
{
    return LEVELS;
}

QString levelFilterLabel(int value)
{
    for (const LevelFilter& level : LEVELS)
        if (level.value == value)
            return qtTrId(level.label);
    return {};
}

QString levelLabel(int level, int audit)
{
    if (audit == audit::Failure)
        return qtTrId("level.audit_failure");
    if (audit == audit::Success)
        return qtTrId("level.audit_success");
    switch (level) {
    case level::Critical:
        return qtTrId("level.critical");
    case level::Error:
        return qtTrId("level.error");
    case level::Warning:
        return qtTrId("level.warning");
    case level::Verbose:
        return qtTrId("level.verbose");
    default:
        return qtTrId("level.information");
    }
}

int levelRank(int level, int audit)
{
    if (level == level::Critical)
        return 3;
    if (level == level::Error || audit == audit::Failure)
        return 2;
    if (level == level::Warning)
        return 1;
    return 0;
}

QString shortChannelName(QStringView channel)
{
    if (channel.compare(QLatin1String(SECURITY), Qt::CaseInsensitive) == 0)
        return qtTrId("channel.security");
    if (channel.compare(QLatin1String(SYSTEM), Qt::CaseInsensitive) == 0)
        return qtTrId("channel.system");
    if (channel.compare(QLatin1String("Application"), Qt::CaseInsensitive) == 0)
        return qtTrId("channel.application");
    QStringView shortened = channel;
    if (shortened.startsWith(QLatin1String("Microsoft-Windows-"), Qt::CaseInsensitive))
        shortened = shortened.mid(18);
    const qsizetype bar = shortened.indexOf(QLatin1Char('/'));
    if (bar > 0)
        shortened = shortened.left(bar);
    return shortened.toString();
}

} // namespace lh
