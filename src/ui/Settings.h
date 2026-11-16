// Persistent interface settings: keys, remembered folders and recent projects.
#pragma once

#include <QStandardPaths>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace lh::ui::settings {

namespace key {
inline constexpr char THEME[] = "display/theme";
inline constexpr char LANGUAGE[] = "display/language";
inline constexpr char DATE_FORMAT[] = "display/date_format";
inline constexpr char TIME_ZONE[] = "display/time_zone";
inline constexpr char LAST_CRASH[] = "diagnostic/last_crash";
inline constexpr char DETECTION_PARAMETERS[] = "detection/parameters";
inline constexpr char GEOMETRY[] = "window/geometry";
inline constexpr char FILTER_PANEL[] = "window/filter_panel";
inline constexpr char LOGS_FOLDER[] = "folders/logs";
inline constexpr char PROJECTS_FOLDER[] = "folders/projects";
inline constexpr char EXPORTS_FOLDER[] = "folders/exports";
inline constexpr char RULES_FOLDER[] = "folders/rules";
inline constexpr char REPORTS_FOLDER[] = "report/folder";
inline constexpr char REPORT_AUTHOR[] = "report/author";
inline constexpr char REPORT_FORMAT[] = "report/format";
inline constexpr char PRESETS[] = "filters/presets";
inline constexpr char RECENT_PROJECTS[] = "projects/recents";
inline constexpr char DETAIL_VISIBLE[] = "events/detail_visible";
inline constexpr char GROUP[] = "events/group";
inline constexpr char TIMELINE[] = "events/timeline";
inline constexpr char HIDDEN_COLUMNS[] = "events/hidden_columns";
inline constexpr char HIDE_USELESS_COLUMNS[] = "events/hide_useless_columns";
inline constexpr char DETAILED_CHARTS[] = "dashboard/detailed_charts";
} // namespace key

QVariant read(const char* key, const QVariant& fallback = {});
void write(const char* key, const QVariant& value);
void remove(const char* key);

QString rememberedFolder(const char* key);
void rememberFolder(const char* key, const QString& path);

inline constexpr int MAX_RECENTS = 10;
QStringList recentProjects();
void addRecent(const QString& path);
void removeRecent(const QString& path);
void clearRecents();

} // namespace lh::ui::settings
