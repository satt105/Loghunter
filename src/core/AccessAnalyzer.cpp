#include "AccessAnalyzer.h"

namespace lh {

namespace {

constexpr qsizetype MAX_CACHE_SIZE = 500'000;
constexpr qsizetype MAX_VOLUMES_SIZE = 200'000;
constexpr QLatin1StringView VOLUME_PREFIX("\\Device\\HarddiskVolume");

bool isDrivePath(const QString& path)
{
    return path.size() >= 3 && path[0].isLetter() && path[1] == u':' && path[2] == u'\\';
}

std::pair<QString, QString> splitVolume(const QString& path)
{
    const qsizetype end = path.indexOf(QLatin1Char('\\'), VOLUME_PREFIX.size());
    return end < 0 ? std::pair{path, QString()} : std::pair{path.left(end), path.mid(end)};
}

quint64 hexInteger(const QString& text)
{
    bool ok = false;
    const QString t = text.trimmed();
    const quint64 value = t.startsWith(QLatin1String("0x"), Qt::CaseInsensitive) ? t.mid(2).toULongLong(&ok, 16)
                                                                                 : t.toULongLong(&ok, 10);
    return ok ? value : 0;
}

QString sharePath(const QString& root, const QString& relative)
{
    QString base = cleanValue(root);
    if (base.startsWith(QLatin1String("\\??\\")) || base.startsWith(QLatin1String("\\\\?\\")))
        base.remove(0, 4);
    if (base.isEmpty())
        return {};
    while (base.size() > 3 && base.endsWith(QLatin1Char('\\')))
        base.chop(1);
    QString remainder = cleanValue(relative);
    while (remainder.startsWith(QLatin1Char('\\')))
        remainder.remove(0, 1);
    if (remainder.isEmpty())
        return base;
    if (!base.endsWith(QLatin1Char('\\')))
        base += QLatin1Char('\\');
    return base + remainder;
}

QString netbiosDomain(const QString& domain)
{
    if (!domain.contains(QLatin1Char('.')))
        return domain;
    const QString shortened = domain.section(QLatin1Char('.'), 0, 0).left(15).toUpper();
    return shortened.isEmpty() ? domain : shortened;
}

} // namespace

QString normalizeUser(const QString& subject, const QString& target, const QString& subjectDomain,
                      const QString& targetDomain)
{
    QString user = cleanValue(subject);
    if (user.isEmpty())
        user = cleanValue(target);
    QString domain = cleanValue(subjectDomain);
    if (domain.isEmpty())
        domain = cleanValue(targetDomain);
    if (user.isEmpty())
        return {};
    const qsizetype separator = user.lastIndexOf(QLatin1Char('\\'));
    const qsizetype atSign = user.lastIndexOf(QLatin1Char('@'));
    if (separator >= 0) {
        domain = user.left(separator);
        user.remove(0, separator + 1);
    } else if (atSign > 0 && QStringView(user).mid(atSign + 1).compare(domain, Qt::CaseInsensitive) == 0) {
        user.truncate(atSign);
    }
    return domain.isEmpty() ? user : netbiosDomain(domain) + QLatin1Char('\\') + user;
}

QString parentFolder(const QString& path)
{
    const qsizetype separator = std::max(path.lastIndexOf(QLatin1Char('\\')), path.lastIndexOf(QLatin1Char('/')));
    return separator > 0 ? path.left(separator) : QString();
}

QString fileName(const QString& path)
{
    const qsizetype separator = std::max(path.lastIndexOf(QLatin1Char('\\')), path.lastIndexOf(QLatin1Char('/')));
    return separator >= 0 && separator + 1 < path.size() ? path.mid(separator + 1) : path;
}

QString fileExtension(const QString& path)
{
    const qsizetype separator = std::max(path.lastIndexOf(QLatin1Char('\\')), path.lastIndexOf(QLatin1Char('/')));
    const qsizetype point = path.lastIndexOf(QLatin1Char('.'));
    if (point <= separator + 1 || path.size() - point > 16)
        return {};
    const QString extension = path.mid(point).toLower();
    return extension.contains(QLatin1Char(' ')) ? QString() : extension;
}

AccessAnalyzer::Preparation AccessAnalyzer::prepare(const evtx::DecodedEvent& event,
                                                    TimeZoneConverter& timeZone, EventRow& row)
{
    normalize(event, timeZone, row);
    const uint32_t id = event.eventId;
    if (id != EVT_HANDLE && id != EVT_DELETE && id != EVT_ACCESS && id != EVT_PERMISSIONS && id != EVT_SHARE)
        return Preparation::Generic;
    if (!event.channel.isEmpty() && !isSecurityChannel(event.channel))
        return Preparation::Generic;
    const QString& objectType = event.dataValue(u"ObjectType");
    if (id != EVT_DELETE && !objectType.isEmpty() && objectType != QLatin1String("File"))
        return Preparation::Generic;

    if (id == EVT_SHARE) {
        row.file = sharePath(event.dataValue(u"ShareLocalPath"), event.dataValue(u"RelativeTargetName"));
        if (row.file.isEmpty())
            return Preparation::Generic;
    } else {
        row.processId = hexInteger(event.dataValue(u"ProcessId"));
        row.handle = hexInteger(event.dataValue(u"HandleId"));
        row.file = cleanValue(event.dataValue(u"ObjectName"));
    }
    if (!row.file.isEmpty()) {
        row.folder = parentFolder(row.file);
        row.extension = fileExtension(row.file);
    }

    const QStringList rights = decodeRights(event.dataValue(u"AccessList"), event.dataValue(u"AccessMask"));
    row.access = rights.join(QStringLiteral(", "));
    Classification classification;
    switch (id) {
    case EVT_DELETE:
        classification = {action::Deletion, category::Deletion};
        break;
    case EVT_PERMISSIONS:
        classification = {action::Permissions, category::Permissions};
        break;
    case EVT_HANDLE:
        classification = {action::Handle, category::Handle};
        break;
    default:
        classification = classifyAccess(rights);
        break;
    }
    row.action = classification.action;
    row.category = classification.category;
    row.process = cleanValue(event.dataValue(u"ProcessName"));
    return Preparation::File;
}

void AccessAnalyzer::rememberHandle(const HandleKey& key, const QString& path)
{
    if (m_namesByHandle.size() >= MAX_CACHE_SIZE) {
        m_namesByHandle.clear();
        m_countedDeletions.clear();
        m_countedPermissions.clear();
    }
    auto found = m_namesByHandle.find(key);
    if (found == m_namesByHandle.end()) {
        m_namesByHandle.insert(key, path);
    } else if (found.value() != path) {
        found.value() = path;
        m_countedDeletions.remove(key);
        m_countedPermissions.remove(key);
    }
}

QString AccessAnalyzer::volumeKey(const QString& machine, const QString& volume)
{
    return machine.toLower() + QLatin1Char('|') + volume.toLower();
}

std::optional<QString> AccessAnalyzer::convertVolume(const Volumes& volumes, const QString& machine,
                                                     const QString& path)
{
    if (volumes.isEmpty() || !path.startsWith(VOLUME_PREFIX, Qt::CaseInsensitive))
        return std::nullopt;
    const auto [volume, remainder] = splitVolume(path);
    const auto found = volumes.constFind(volumeKey(machine, volume));
    if (found == volumes.cend())
        return std::nullopt;
    return *found + (remainder.isEmpty() ? QStringLiteral("\\") : remainder);
}

void AccessAnalyzer::resolveVolume(EventRow& row)
{
    if (row.file.startsWith(VOLUME_PREFIX, Qt::CaseInsensitive)) {
        if (const auto converted = convertVolume(m_volumes, row.machine, row.file)) {
            row.file = *converted;
            row.folder = parentFolder(row.file);
            return;
        }
        const auto [volume, remainder] = splitVolume(row.file);
        if (remainder.size() < 2)
            return;
        const QString tailKey = volumeKey(row.machine, remainder);
        const QString letter = m_lettersByPath.value(tailKey);
        if (letter.isEmpty()) {
            if (m_pendingVolumes.size() < MAX_VOLUMES_SIZE)
                m_pendingVolumes.insert(tailKey, volume);
            return;
        }
        m_volumes.insert(volumeKey(row.machine, volume), letter);
        row.file = letter + remainder;
        row.folder = parentFolder(row.file);
        return;
    }
    if (!isDrivePath(row.file) || row.file.size() < 5)
        return;
    const QString tailKey = volumeKey(row.machine, row.file.mid(2));
    const QString letter = row.file.left(2).toUpper();
    bool ambiguous = false;
    const auto known = m_lettersByPath.find(tailKey);
    if (known == m_lettersByPath.end()) {
        if (m_lettersByPath.size() < MAX_VOLUMES_SIZE)
            m_lettersByPath.insert(tailKey, letter);
    } else if (known.value() != letter) {
        known.value().clear();
        ambiguous = true;
    }
    const auto wait = m_pendingVolumes.find(tailKey);
    if (wait != m_pendingVolumes.end()) {
        const QString key = volumeKey(row.machine, wait.value());
        if (!ambiguous && !m_volumes.contains(key))
            m_volumes.insert(key, letter);
        m_pendingVolumes.erase(wait);
    }
}

bool AccessAnalyzer::correlate(EventRow& row)
{
    if (!row.file.isEmpty())
        resolveVolume(row);
    const HandleKey key{qHash(row.machine), row.processId, row.handle};
    const bool knownHandle = key.handle != 0;
    if (knownHandle && !row.file.isEmpty())
        rememberHandle(key, row.file);

    switch (row.eventId) {
    case EVT_DELETE:
        if (knownHandle && m_countedDeletions.remove(key)) {
            ++m_statistics.discardedDuplicates;
            return false;
        }
        if (row.file.isEmpty() && knownHandle) {
            row.file = m_namesByHandle.value(key);
            row.folder = parentFolder(row.file);
            row.extension = fileExtension(row.file);
        }
        break;
    case EVT_PERMISSIONS:
        if (knownHandle && m_countedPermissions.remove(key)) {
            ++m_statistics.discardedDuplicates;
            return false;
        }
        break;
    case EVT_ACCESS:
        if (knownHandle) {
            if (row.category == category::Deletion)
                m_countedDeletions.insert(key);
            else if (row.category == category::Permissions)
                m_countedPermissions.insert(key);
        }
        break;
    default:
        break;
    }

    if (row.file.isEmpty()) {
        ++m_statistics.withoutPath;
        return false;
    }
    ++m_statistics.kept;
    return true;
}

} // namespace lh
