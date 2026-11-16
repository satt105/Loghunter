// Action categories, Windows access rights and event classification.
#pragma once

#include <QByteArrayView>
#include <QString>
#include <QStringList>

#include <array>
#include <cstdint>

namespace lh {

inline constexpr uint32_t EVT_HANDLE = 4656;
inline constexpr uint32_t EVT_DELETE = 4660;
inline constexpr uint32_t EVT_ACCESS = 4663;
inline constexpr uint32_t EVT_PERMISSIONS = 4670;
inline constexpr uint32_t EVT_CLOSE = 4658;
inline constexpr uint32_t EVT_DUPLICATION = 4690;
inline constexpr uint32_t EVT_SHARE = 5145;

namespace category {
inline constexpr char Modification[] = "modification";
inline constexpr char Deletion[] = "deletion";
inline constexpr char Permissions[] = "permissions";
inline constexpr char Read[] = "read";
inline constexpr char Handle[] = "handle";
inline constexpr char Other[] = "other";
} // namespace category

struct Category
{
    const char* key;
    const char* label;
    const char* color;
    const char* description;
};

const std::array<Category, 6>& categories();
const Category* findCategory(QStringView key);
int categoryIndex(QByteArrayView key);
QString categoryLabel(QStringView key);
QString categoryLabel(const Category& category);
QString categoryColor(QStringView key);
QString actionLabel(const QString& action);

namespace action {
inline const QString Write = QStringLiteral("write");
inline const QString Attributes = QStringLiteral("attributes");
inline const QString Deletion = QStringLiteral("deletion");
inline const QString Permissions = QStringLiteral("permissions");
inline const QString Read = QStringLiteral("read");
inline const QString Execution = QStringLiteral("execution");
inline const QString ReadPermissions = QStringLiteral("read_permissions");
inline const QString Handle = QStringLiteral("handle");
inline const QString Access = QStringLiteral("access");
} // namespace action

QStringList decodeRights(const QString& accessList, const QString& mask);

struct Classification
{
    QString action;
    const char* category;
};

Classification classifyAccess(const QStringList& rights);

[[nodiscard]] bool rightsContainDeletion(const QStringList& rights);
[[nodiscard]] bool rightsContainPermissions(const QStringList& rights);

bool isSystemAccount(const QString& user, const QString& sid = {});

QStringList executableExtensions();
QStringList ransomwareExtensions();

} // namespace lh
