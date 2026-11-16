// Catalog of common Windows events and severity levels.
#pragma once

#include <QList>
#include <QString>

namespace lh {

QString eventDescription(quint32 identifier, QStringView channel);
QString eventLabel(quint32 identifier, QStringView channel, QStringView provider = {});

struct LevelFilter
{
    int value;
    const char* label;
};
const QList<LevelFilter>& filterLevels();
QString levelFilterLabel(int value);

QString levelLabel(int level, int audit);
int levelRank(int level, int audit);

QString shortChannelName(QStringView channel);

} // namespace lh
