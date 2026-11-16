// Mapping of Sigma tags to MITRE ATT&CK tactics and techniques.
#pragma once

#include <QString>
#include <QStringList>

namespace lh::mitre {

struct Mapping
{
    QStringList tactics;
    QStringList techniques;

    [[nodiscard]] bool isEmpty() const { return tactics.isEmpty() && techniques.isEmpty(); }
};

Mapping parse(const QStringList& tags);
QString tacticLabel(QStringView code);
QString techniqueLink(QStringView technique);
QString summarize(const QStringList& tags);

} // namespace lh::mitre
