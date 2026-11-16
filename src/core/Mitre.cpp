#include "Mitre.h"

#include <QRegularExpression>

namespace lh::mitre {

namespace {

struct Tactic
{
    const char* code;
    const char* label;
};

constexpr Tactic TACTICS[] = {
    {"reconnaissance", QT_TRID_NOOP("mitre.reconnaissance")},
    {"resource-development", QT_TRID_NOOP("mitre.resource_development")},
    {"initial-access", QT_TRID_NOOP("mitre.initial_access")},
    {"execution", QT_TRID_NOOP("mitre.execution")},
    {"persistence", QT_TRID_NOOP("mitre.persistence")},
    {"privilege-escalation", QT_TRID_NOOP("mitre.privilege_escalation")},
    {"defense-evasion", QT_TRID_NOOP("mitre.defense_evasion")},
    {"credential-access", QT_TRID_NOOP("mitre.credential_access")},
    {"discovery", QT_TRID_NOOP("mitre.discovery")},
    {"lateral-movement", QT_TRID_NOOP("mitre.lateral_movement")},
    {"collection", QT_TRID_NOOP("mitre.collection")},
    {"command-and-control", QT_TRID_NOOP("mitre.command_and_control")},
    {"exfiltration", QT_TRID_NOOP("mitre.exfiltration")},
    {"impact", QT_TRID_NOOP("mitre.impact")},
};

} // namespace

Mapping parse(const QStringList& tags)
{
    static const QRegularExpression TECHNIQUE(QStringLiteral("^attack\\.(t\\d{4}(?:\\.\\d{3})?)$"),
                                              QRegularExpression::CaseInsensitiveOption);
    Mapping result;
    for (const QString& tag : tags) {
        const QRegularExpressionMatch technique = TECHNIQUE.match(tag.trimmed());
        if (technique.hasMatch()) {
            const QString code = technique.captured(1).toUpper();
            if (!result.techniques.contains(code))
                result.techniques << code;
            continue;
        }
        if (!tag.startsWith(QLatin1String("attack."), Qt::CaseInsensitive))
            continue;
        const QString code = tag.mid(7).toLower().replace(QLatin1Char('_'), QLatin1Char('-'));
        for (const Tactic& tactic : TACTICS)
            if (code == QLatin1String(tactic.code) && !result.tactics.contains(code))
                result.tactics << code;
    }
    return result;
}

QString tacticLabel(QStringView code)
{
    for (const Tactic& tactic : TACTICS)
        if (code == QLatin1String(tactic.code))
            return qtTrId(tactic.label);
    return code.toString();
}

QString techniqueLink(QStringView technique)
{
    QString path = technique.toString().toUpper();
    path.replace(QLatin1Char('.'), QLatin1Char('/'));
    return QStringLiteral("https://attack.mitre.org/techniques/%1/").arg(path);
}

QString summarize(const QStringList& tags)
{
    const Mapping mapping = parse(tags);
    QStringList parts;
    for (const QString& tactic : mapping.tactics)
        parts << tacticLabel(tactic);
    parts << mapping.techniques;
    return parts.join(QStringLiteral(" · "));
}

} // namespace lh::mitre
