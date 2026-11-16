#include "Settings.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>

namespace lh::ui::settings {

namespace {

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity SENSITIVITY = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity SENSITIVITY = Qt::CaseSensitive;
#endif

QString normalize(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

void removeFrom(QStringList& list, const QString& normalized)
{
    list.removeIf([&](const QString& element) { return element.compare(normalized, SENSITIVITY) == 0; });
}

} // namespace

QVariant read(const char* key, const QVariant& fallback)
{
    return QSettings().value(QLatin1String(key), fallback);
}

void write(const char* key, const QVariant& value)
{
    QSettings().setValue(QLatin1String(key), value);
}

void remove(const char* key)
{
    QSettings().remove(QLatin1String(key));
}

QString rememberedFolder(const char* key)
{
    const QString folder = read(key).toString();
    return !folder.isEmpty() && QFileInfo(folder).isDir()
               ? folder
               : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

void rememberFolder(const char* key, const QString& path)
{
    const QFileInfo info(path);
    write(key, info.isDir() ? info.absoluteFilePath() : info.absolutePath());
}

QStringList recentProjects()
{
    return read(key::RECENT_PROJECTS).toStringList();
}

void addRecent(const QString& path)
{
    const QString normalized = normalize(path);
    QStringList list = recentProjects();
    removeFrom(list, normalized);
    list.prepend(normalized);
    while (list.size() > MAX_RECENTS)
        list.removeLast();
    write(key::RECENT_PROJECTS, list);
}

void removeRecent(const QString& path)
{
    QStringList list = recentProjects();
    removeFrom(list, normalize(path));
    write(key::RECENT_PROJECTS, list);
}

void clearRecents()
{
    remove(key::RECENT_PROJECTS);
}

} // namespace lh::ui::settings
