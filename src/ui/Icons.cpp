#include "Icons.h"

#include <QGuiApplication>
#include <QHash>
#include <QIconEngine>
#include <QPainter>
#include <QPalette>
#include <QSvgRenderer>

#include <cstring>

namespace lh::ui {

namespace {

struct IconPath
{
    const char* name;
    const char* svg;
};

constexpr IconPath ICON_PATHS[] = {
    {"home", R"(<path d="M3 10.5 12 3l9 7.5"/><path d="M5 9v11a1 1 0 0 0 1 1h4v-6h4v6h4a1 1 0 0 0 1-1V9"/>)"},
    {"dashboard",
     R"(<rect x="3" y="3" width="7" height="9" rx="1.5"/><rect x="14" y="3" width="7" height="5" rx="1.5"/><rect x="14" y="12" width="7" height="9" rx="1.5"/><rect x="3" y="16" width="7" height="5" rx="1.5"/>)"},
    {"events",
     R"(<line x1="9" y1="6" x2="21" y2="6"/><line x1="9" y1="12" x2="21" y2="12"/><line x1="9" y1="18" x2="21" y2="18"/><circle cx="4.5" cy="6" r="1"/><circle cx="4.5" cy="12" r="1"/><circle cx="4.5" cy="18" r="1"/>)"},
    {"investigation",
     R"(<circle cx="11" cy="11" r="7.5"/><line x1="21" y1="21" x2="16.4" y2="16.4"/><circle cx="11" cy="9.3" r="2.3"/><path d="M7.3 15.2a4.3 4.3 0 0 1 7.4 0"/>)"},
    {"alerts",
     R"(<path d="M12 3 4.5 6v5.5c0 4.6 3.1 8.2 7.5 9.5 4.4-1.3 7.5-4.9 7.5-9.5V6L12 3z"/><line x1="12" y1="8" x2="12" y2="12.5"/><line x1="12" y1="16" x2="12.01" y2="16"/>)"},
    {"import",
     R"(<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><polyline points="14 3 14 8 19 8"/><line x1="12" y1="11" x2="12" y2="17"/><line x1="9" y1="14" x2="15" y2="14"/>)"},
    {"file",
     R"(<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><polyline points="14 3 14 8 19 8"/><line x1="9" y1="13" x2="15" y2="13"/><line x1="9" y1="17" x2="13" y2="17"/>)"},
    {"folder", R"(<path d="M3 7a2 2 0 0 1 2-2h4l2 2.5h8a2 2 0 0 1 2 2V18a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z"/>)"},
    {"open",
     R"(<path d="M3 18V7a2 2 0 0 1 2-2h4l2 2.5h6a2 2 0 0 1 2 2V11"/><path d="M3 18l2.6-6.2a2 2 0 0 1 1.8-1.3H21l-2.8 7.3a2 2 0 0 1-1.9 1.2H4a1 1 0 0 1-1-1z"/>)"},
    {"save",
     R"(<path d="M5 3h11l5 5v11a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2z"/><path d="M7 3v5h8V3"/><rect x="7" y="13" width="10" height="8" rx="1"/>)"},
    {"export",
     R"(<path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><polyline points="7 10 12 15 17 10"/><line x1="12" y1="15" x2="12" y2="3"/>)"},
    {"report",
     R"(<path d="M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8z"/><polyline points="14 3 14 8 19 8"/><line x1="9" y1="18" x2="9" y2="15"/><line x1="12" y1="18" x2="12" y2="12"/><line x1="15" y1="18" x2="15" y2="14"/>)"},
    {"filters", R"(<path d="M3.5 4h17l-6.8 8.3V19l-3.4 2v-8.7z"/>)"},
    {"search", R"(<circle cx="11" cy="11" r="7"/><line x1="21" y1="21" x2="16.2" y2="16.2"/>)"},
    {"close", R"(<line x1="6" y1="6" x2="18" y2="18"/><line x1="18" y1="6" x2="6" y2="18"/>)"},
    {"settings",
     R"(<line x1="4" y1="6" x2="7" y2="6"/><circle cx="9.5" cy="6" r="2.5"/><line x1="12" y1="6" x2="20" y2="6"/><line x1="4" y1="12" x2="12" y2="12"/><circle cx="14.5" cy="12" r="2.5"/><line x1="17" y1="12" x2="20" y2="12"/><line x1="4" y1="18" x2="5" y2="18"/><circle cx="7.5" cy="18" r="2.5"/><line x1="10" y1="18" x2="20" y2="18"/>)"},
    {"logs",
     R"(<ellipse cx="12" cy="5.5" rx="8" ry="2.5"/><path d="M4 5.5v13c0 1.4 3.6 2.5 8 2.5s8-1.1 8-2.5v-13"/><path d="M4 12c0 1.4 3.6 2.5 8 2.5s8-1.1 8-2.5"/>)"},
    {"help",
     R"(<circle cx="12" cy="12" r="9"/><path d="M9.5 9.5a2.5 2.5 0 0 1 4.9.7c0 1.7-2.4 2.3-2.4 3.8"/><line x1="12" y1="17" x2="12.01" y2="17"/>)"},
    {"info", R"(<circle cx="12" cy="12" r="9"/><line x1="12" y1="11" x2="12" y2="16.5"/><line x1="12" y1="7.5" x2="12.01" y2="7.5"/>)"},
    {"copy",
     R"(<rect x="9" y="9" width="12" height="12" rx="2"/><path d="M5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1"/>)"},
    {"user", R"(<circle cx="12" cy="8" r="4"/><path d="M4 21a8 8 0 0 1 16 0"/>)"},
    {"users",
     R"(<circle cx="9" cy="8" r="3.5"/><path d="M2.5 20a6.5 6.5 0 0 1 13 0"/><path d="M16 4.5a3.5 3.5 0 0 1 0 7"/><path d="M18 14a6.5 6.5 0 0 1 3.5 6"/>)"},
    {"refresh",
     R"(<path d="M20 11a8 8 0 0 0-14.6-4.5L3 9"/><polyline points="3 4 3 9 8 9"/><path d="M4 13a8 8 0 0 0 14.6 4.5L21 15"/><polyline points="21 20 21 15 16 15"/>)"},
    {"delete",
     R"(<polyline points="3 6 5 6 21 6"/><path d="M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6"/><line x1="10" y1="11" x2="10" y2="17"/><line x1="14" y1="11" x2="14" y2="17"/><path d="M9 6V4a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v2"/>)"},
    {"theme", R"(<path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5z"/>)"},
    {"chevron-right", R"(<polyline points="9 6 15 12 9 18"/>)"},
    {"back", R"(<line x1="20" y1="12" x2="4" y2="12"/><polyline points="10 6 4 12 10 18"/>)"},
    {"pane-hide",
     R"(<rect x="3" y="4" width="18" height="16" rx="2"/><line x1="9" y1="4" x2="9" y2="20"/><polyline points="16 9 13 12 16 15"/>)"},
    {"pane-show",
     R"(<rect x="3" y="4" width="18" height="16" rx="2"/><line x1="9" y1="4" x2="9" y2="20"/><polyline points="13 9 16 12 13 15"/>)"},
    {"chevron-down", R"(<polyline points="6 9 12 15 18 9"/>)"},
    {"link",
     R"(<path d="M14 4h6v6"/><line x1="20" y1="4" x2="11" y2="13"/><path d="M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5"/>)"},
    {"clock", R"(<circle cx="12" cy="12" r="9"/><polyline points="12 7 12 12 15.5 14"/>)"},
    {"calendar",
     R"(<rect x="3" y="5" width="18" height="16" rx="2"/><line x1="3" y1="10" x2="21" y2="10"/><line x1="8" y1="3" x2="8" y2="7"/><line x1="16" y1="3" x2="16" y2="7"/>)"},
    {"process",
     R"(<rect x="3" y="4" width="18" height="16" rx="2"/><polyline points="7 9 10 12 7 15"/><line x1="12" y1="15" x2="17" y2="15"/>)"},
    {"machine",
     R"(<rect x="3" y="4" width="18" height="7" rx="1.5"/><rect x="3" y="13" width="18" height="7" rx="1.5"/><line x1="7" y1="7.5" x2="7.01" y2="7.5"/><line x1="7" y1="16.5" x2="7.01" y2="16.5"/>)"},
    {"activity", R"(<polyline points="3 12 7 12 10 5 14 19 17 12 21 12"/>)"},
    {"check", R"(<polyline points="4 12.5 9.5 18 20 6"/>)"},
    {"plus", R"(<line x1="12" y1="5" x2="12" y2="19"/><line x1="5" y1="12" x2="19" y2="12"/>)"},
    {"bookmark", R"(<path d="M6 3h12a1 1 0 0 1 1 1v17l-7-4.5L5 21V4a1 1 0 0 1 1-1z"/>)"},
    {"warning",
     R"(<path d="M10.3 3.9 2.4 17.5a2 2 0 0 0 1.7 3h15.8a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0z"/><line x1="12" y1="9" x2="12" y2="13.5"/><line x1="12" y1="17" x2="12.01" y2="17"/>)"},
    {"drop",
     R"(<path d="M4 15v3a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-3"/><polyline points="8 8 12 4 16 8"/><line x1="12" y1="4" x2="12" y2="15"/>)"},
    {"panel",
     R"(<rect x="3" y="4" width="18" height="16" rx="2"/><line x1="15" y1="4" x2="15" y2="20"/>)"},
    {"target", R"(<circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="5"/><circle cx="12" cy="12" r="1"/>)"},
    {"columns",
     R"(<rect x="3" y="4" width="18" height="16" rx="2"/><line x1="9" y1="4" x2="9" y2="20"/><line x1="15" y1="4" x2="15" y2="20"/>)"},
};

QByteArray findIconPath(const char* name)
{
    for (const IconPath& iconPath : ICON_PATHS)
        if (std::strcmp(iconPath.name, name) == 0)
            return QByteArray(iconPath.svg);
    return QByteArrayLiteral(R"(<rect x="4" y="4" width="16" height="16" rx="3"/>)");
}

class IconEngine : public QIconEngine
{
public:
    IconEngine(QByteArray iconPath, QColor color) : m_iconPath(std::move(iconPath)), m_color(color) {}

    void paint(QPainter* painter, const QRect& rectangle, QIcon::Mode mode, QIcon::State) override
    {
        QSvgRenderer renderer(document(colorFor(mode)));
        renderer.render(painter, rectangle);
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State, qreal scale) override
    {
        const QColor color = colorFor(mode);
        const QSize physical = (QSizeF(size) * scale).toSize();
        const QString key = QStringLiteral("%1x%2-%3").arg(physical.width()).arg(physical.height()).arg(color.rgba());
        auto it = m_cache.constFind(key);
        if (it != m_cache.cend())
            return *it;
        QImage image(physical, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            QSvgRenderer renderer(document(color));
            renderer.render(&painter, QRectF(QPointF(0, 0), QSizeF(physical)));
        }
        QPixmap result = QPixmap::fromImage(std::move(image));
        result.setDevicePixelRatio(scale);
        if (m_cache.size() > 32)
            m_cache.clear();
        m_cache.insert(key, result);
        return result;
    }

    QIconEngine* clone() const override { return new IconEngine(m_iconPath, m_color); }
    QString key() const override { return QStringLiteral("loghunter-svg"); }

private:
    QColor colorFor(QIcon::Mode mode) const
    {
        const QPalette palette = QGuiApplication::palette();
        if (mode == QIcon::Disabled) {
            QColor color = m_color.isValid() ? m_color : palette.color(QPalette::WindowText);
            color.setAlphaF(0.38F);
            return color;
        }
        if (m_color.isValid())
            return m_color;
        if (mode == QIcon::Selected)
            return palette.color(QPalette::HighlightedText);
        return palette.color(QPalette::WindowText);
    }

    QByteArray document(const QColor& color) const
    {
        QByteArray svg = QByteArrayLiteral(R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="none" )"
                                           R"(stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round" stroke=")");
        svg += color.name(QColor::HexRgb).toLatin1();
        svg += "\" stroke-opacity=\"" + QByteArray::number(color.alphaF(), 'f', 2) + "\">";
        svg += m_iconPath;
        svg += "</svg>";
        return svg;
    }

    QByteArray m_iconPath;
    QColor m_color;
    QHash<QString, QPixmap> m_cache;
};

class DotEngine : public QIconEngine
{
public:
    explicit DotEngine(QColor color) : m_color(color) {}

    void paint(QPainter* painter, const QRect& rectangle, QIcon::Mode, QIcon::State) override
    {
        const double diameter = std::min(rectangle.width(), rectangle.height()) * 0.62;
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_color);
        painter->drawEllipse(QRectF(rectangle).center(), diameter / 2, diameter / 2);
        painter->restore();
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        return scaledPixmap(size, mode, state, 1.0);
    }

    QPixmap scaledPixmap(const QSize& size, QIcon::Mode mode, QIcon::State state, qreal scale) override
    {
        const QSize physical = (QSizeF(size) * scale).toSize();
        QPixmap result(physical);
        result.fill(Qt::transparent);
        {
            QPainter painter(&result);
            paint(&painter, QRect(QPoint(0, 0), physical), mode, state);
        }
        result.setDevicePixelRatio(scale);
        return result;
    }

    QIconEngine* clone() const override { return new DotEngine(m_color); }
    QString key() const override { return QStringLiteral("loghunter-pastille"); }

private:
    QColor m_color;
};

} // namespace

QIcon icon(const char* name)
{
    return QIcon(new IconEngine(findIconPath(name), QColor()));
}

QIcon icon(const char* name, const QColor& color)
{
    return QIcon(new IconEngine(findIconPath(name), color));
}

QIcon dot(const QColor& color)
{
    return QIcon(new DotEngine(color));
}

} // namespace lh::ui
