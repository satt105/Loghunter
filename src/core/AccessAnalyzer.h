// Analysis of file access events (4656, 4660, 4663, 4670, 5145) and handle correlation.
#pragma once

#include "Actions.h"
#include "Normalization.h"
#include "evtx/Extraction.h"

#include <QHash>
#include <QSet>
#include <QString>

#include <optional>

namespace lh {

struct AnalysisStatistics
{
    qint64 kept = 0;
    qint64 withoutPath = 0;
    qint64 discardedDuplicates = 0;
};

class AccessAnalyzer
{
public:
    enum class Preparation { File, Generic };
    using Volumes = QHash<QString, QString>;

    static Preparation prepare(const evtx::DecodedEvent& event, TimeZoneConverter& timeZone,
                               EventRow& row);
    static QString volumeKey(const QString& machine, const QString& volume);
    static std::optional<QString> convertVolume(const Volumes& volumes, const QString& machine,
                                                const QString& path);

    bool correlate(EventRow& row);
    void setVolumes(const Volumes& volumes) { m_volumes = volumes; }

    [[nodiscard]] const AnalysisStatistics& statistics() const noexcept { return m_statistics; }
    [[nodiscard]] const Volumes& volumes() const noexcept { return m_volumes; }

private:
    void resolveVolume(EventRow& row);

    struct HandleKey
    {
        size_t machine = 0;
        quint64 process = 0;
        quint64 handle = 0;
        friend bool operator==(const HandleKey&, const HandleKey&) = default;
        friend size_t qHash(const HandleKey& key, size_t seed = 0) noexcept
        {
            return qHashMulti(seed, key.machine, key.process, key.handle);
        }
    };

    void rememberHandle(const HandleKey& key, const QString& path);

    QHash<HandleKey, QString> m_namesByHandle;
    QSet<HandleKey> m_countedDeletions;
    QSet<HandleKey> m_countedPermissions;
    AnalysisStatistics m_statistics;
    Volumes m_volumes;
    QHash<QString, QString> m_lettersByPath;
    QHash<QString, QString> m_pendingVolumes;
};

QString normalizeUser(const QString& subject, const QString& target, const QString& subjectDomain,
                      const QString& targetDomain);
QString parentFolder(const QString& path);
QString fileName(const QString& path);
QString fileExtension(const QString& path);

} // namespace lh
