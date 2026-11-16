// Vector icons of the interface (24 × 24 SVG line paths).
#pragma once

#include <QColor>
#include <QIcon>
#include <QPixmap>

namespace lh::ui {

QIcon icon(const char* name);
QIcon icon(const char* name, const QColor& color);

QIcon dot(const QColor& color);

} // namespace lh::ui
