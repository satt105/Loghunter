// Application theme: light or dark scheme and colors derived from the palette.
#pragma once

#include "core/Alerts.h"
#include "render/Charts.h"

#include <QColor>
#include <QFont>
#include <QPalette>

namespace lh::ui {

enum class ThemeMode { System, Light, Dark };

namespace theme {

using render::blend;

void initialize();
void apply(ThemeMode mode);
ThemeMode mode();
bool isDark();

QColor pageBackground(const QPalette& palette);
QColor cardBackground(const QPalette& palette);
QColor hoverBackground(const QPalette& palette);
QColor selectionBackground(const QPalette& palette);
QColor border(const QPalette& palette);
QColor text(const QPalette& palette);
QColor secondaryText(const QPalette& palette);
QColor accent(const QPalette& palette);
QColor brand(const QPalette& palette);

QColor severityColor(Severity severity);
QColor levelColor(int rank);
QColor successColor();
QColor categoryColor(const QString& key);

QFont font(const QFont& base, double factor, QFont::Weight weight = QFont::Normal);
QString styleSheet(const QPalette& palette);

} // namespace theme

} // namespace lh::ui
