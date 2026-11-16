#include "Actions.h"

#include <QHash>
#include <QSet>

namespace lh {

namespace {

struct Right
{
    uint32_t bit;
    const char* code;
    const char* name;
};

constexpr std::array<Right, 15> RIGHTS_TABLE{{
    {0x1, "%%4416", "ReadData"},
    {0x2, "%%4417", "WriteData"},
    {0x4, "%%4418", "AppendData"},
    {0x8, "%%4419", "ReadEA"},
    {0x10, "%%4420", "WriteEA"},
    {0x20, "%%4421", "Execute"},
    {0x40, "%%4422", "DeleteChild"},
    {0x80, "%%4423", "ReadAttributes"},
    {0x100, "%%4424", "WriteAttributes"},
    {0x10000, "%%1537", "DELETE"},
    {0x20000, "%%1538", "READ_CONTROL"},
    {0x40000, "%%1539", "WRITE_DAC"},
    {0x80000, "%%1540", "WRITE_OWNER"},
    {0x100000, "%%1541", "SYNCHRONIZE"},
    {0x1000000, "%%1542", "ACCESS_SYS_SEC"},
}};

bool containsAny(const QStringList& rights, std::initializer_list<const char*> names)
{
    for (const QString& right : rights)
        for (const char* name : names)
            if (right == QLatin1String(name))
                return true;
    return false;
}

} // namespace

const std::array<Category, 6>& categories()
{
    static const std::array<Category, 6> list{{
        {category::Modification, QT_TRID_NOOP("category.write"), "#3b82f6",
         QT_TRID_NOOP("category.write_description")},
        {category::Deletion, QT_TRID_NOOP("category.delete"), "#ef4444",
         QT_TRID_NOOP("category.delete_description")},
        {category::Permissions, QT_TRID_NOOP("category.permissions"), "#f59e0b",
         QT_TRID_NOOP("category.permissions_description")},
        {category::Read, QT_TRID_NOOP("category.read"), "#14b8a6",
         QT_TRID_NOOP("category.read_description")},
        {category::Handle, QT_TRID_NOOP("category.open"), "#8b5cf6",
         QT_TRID_NOOP("category.open_description")},
        {category::Other, QT_TRID_NOOP("category.other"), "#94a3b8",
         QT_TRID_NOOP("category.other_description")},
    }};
    return list;
}

const Category* findCategory(QStringView key)
{
    for (const Category& c : categories())
        if (key == QLatin1String(c.key))
            return &c;
    return nullptr;
}

int categoryIndex(QByteArrayView key)
{
    const auto& list = categories();
    for (size_t i = 0; i < list.size(); ++i)
        if (key == QByteArrayView(list[i].key))
            return static_cast<int>(i);
    return -1;
}

QString categoryLabel(QStringView key)
{
    const Category* c = findCategory(key);
    return c ? categoryLabel(*c) : key.toString();
}

QString categoryLabel(const Category& category)
{
    return qtTrId(category.label);
}

QString categoryColor(QStringView key)
{
    const Category* c = findCategory(key);
    return QLatin1String(c ? c->color : "#94a3b8");
}

QString actionLabel(const QString& action)
{
    static const QHash<QString, const char*> labels{
        {action::Write, QT_TRID_NOOP("action.write")},
        {action::Attributes, QT_TRID_NOOP("action.attributes")},
        {action::Deletion, QT_TRID_NOOP("action.delete")},
        {action::Permissions, QT_TRID_NOOP("action.permissions")},
        {action::Read, QT_TRID_NOOP("action.read")},
        {action::Execution, QT_TRID_NOOP("action.execute")},
        {action::ReadPermissions, QT_TRID_NOOP("action.read_permissions")},
        {action::Handle, QT_TRID_NOOP("action.handle")},
        {action::Access, QT_TRID_NOOP("action.access")},
    };
    const char* label = labels.value(action);
    return label ? qtTrId(label) : action;
}

QStringList decodeRights(const QString& accessList, const QString& mask)
{
    QStringList names;
    if (!mask.isEmpty()) {
        bool ok = false;
        const QString text = mask.trimmed();
        const uint64_t bits = text.startsWith(QLatin1String("0x"), Qt::CaseInsensitive)
                                  ? text.mid(2).toULongLong(&ok, 16)
                                  : text.toULongLong(&ok, 16);
        if (ok) {
            for (const Right& right : RIGHTS_TABLE)
                if (bits & right.bit)
                    names << QLatin1String(right.name);
            if (!names.isEmpty() || accessList.trimmed().isEmpty())
                return names;
        }
    }
    for (const QString& token : accessList.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        QString name = token;
        for (const Right& right : RIGHTS_TABLE) {
            if (token == QLatin1String(right.code)) {
                name = QLatin1String(right.name);
                break;
            }
        }
        if (!names.contains(name))
            names << name;
    }
    return names;
}

bool rightsContainDeletion(const QStringList& rights)
{
    return containsAny(rights, {"DELETE", "DeleteChild"});
}

bool rightsContainPermissions(const QStringList& rights)
{
    return containsAny(rights, {"WRITE_DAC", "WRITE_OWNER"});
}

Classification classifyAccess(const QStringList& rights)
{
    if (containsAny(rights, {"WriteData", "AppendData"}))
        return {action::Write, category::Modification};
    if (rightsContainDeletion(rights))
        return {action::Deletion, category::Deletion};
    if (rightsContainPermissions(rights))
        return {action::Permissions, category::Permissions};
    if (containsAny(rights, {"WriteEA", "WriteAttributes"}))
        return {action::Attributes, category::Modification};
    if (containsAny(rights, {"ReadData", "ReadEA", "ReadAttributes"}))
        return {action::Read, category::Read};
    if (containsAny(rights, {"Execute"}))
        return {action::Execution, category::Read};
    if (containsAny(rights, {"READ_CONTROL"}))
        return {action::ReadPermissions, category::Other};
    const QString text = rights.join(QLatin1Char(' ')).toLower();
    if (text.contains(QLatin1String("write")) || text.contains(QStringLiteral("écri")))
        return {action::Write, category::Modification};
    if (text.contains(QLatin1String("delete")) || text.contains(QLatin1String("suppr")))
        return {action::Deletion, category::Deletion};
    if (text.contains(QLatin1String("read")) || text.contains(QLatin1String("lect")))
        return {action::Read, category::Read};
    return {action::Access, category::Other};
}

bool isSystemAccount(const QString& user, const QString& sid)
{
    if (!sid.isEmpty()) {
        if (sid == QLatin1String("S-1-5-18") || sid == QLatin1String("S-1-5-19") || sid == QLatin1String("S-1-5-20"))
            return true;
        for (const char* prefix : {"S-1-5-80-", "S-1-5-82-", "S-1-5-90-", "S-1-5-96-"})
            if (sid.startsWith(QLatin1String(prefix)))
                return true;
    }
    const qsizetype separator = user.lastIndexOf(QLatin1Char('\\'));
    const QString name = user.mid(separator + 1).trimmed().toLower();
    if (name.endsWith(QLatin1Char('$')))
        return true;
    static const QSet<QString> SYSTEM_NAMES{
        QStringLiteral("system"),          QStringLiteral("système"),
        QStringLiteral("local service"),   QStringLiteral("network service"),
        QStringLiteral("service local"),   QStringLiteral("service réseau"),
        QStringLiteral("anonymous logon"), QStringLiteral("ouverture de session anonyme"),
    };
    return SYSTEM_NAMES.contains(name);
}

QStringList executableExtensions()
{
    return {QStringLiteral(".exe"), QStringLiteral(".dll"), QStringLiteral(".sys"), QStringLiteral(".scr"),
            QStringLiteral(".com"), QStringLiteral(".pif"), QStringLiteral(".msi"), QStringLiteral(".msp"),
            QStringLiteral(".bat"), QStringLiteral(".cmd"), QStringLiteral(".ps1"), QStringLiteral(".psm1"),
            QStringLiteral(".vbs"), QStringLiteral(".vbe"), QStringLiteral(".js"),  QStringLiteral(".jse"),
            QStringLiteral(".wsf"), QStringLiteral(".hta"), QStringLiteral(".jar"), QStringLiteral(".lnk")};
}

QStringList ransomwareExtensions()
{
    return {QStringLiteral(".encrypted"), QStringLiteral(".enc"),      QStringLiteral(".crypt"),
            QStringLiteral(".crypted"),   QStringLiteral(".crypto"),   QStringLiteral(".locked"),
            QStringLiteral(".locky"),     QStringLiteral(".cerber"),   QStringLiteral(".wncry"),
            QStringLiteral(".wnry"),      QStringLiteral(".ryk"),      QStringLiteral(".conti"),
            QStringLiteral(".lockbit"),   QStringLiteral(".akira"),    QStringLiteral(".blackcat"),
            QStringLiteral(".royal"),     QStringLiteral(".hive"),     QStringLiteral(".clop"),
            QStringLiteral(".phobos")};
}

} // namespace lh
