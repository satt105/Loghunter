#include "Charts.h"

#include "core/Actions.h"
#include "core/DateTime.h"

#include <QFontMetricsF>
#include <QLinearGradient>
#include <QLocale>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace lh::render {

namespace {

constexpr const char* SHORT_DAYS[] = {
    QT_TRID_NOOP("charts.monday_short"), QT_TRID_NOOP("charts.tuesday_short"),
    QT_TRID_NOOP("charts.wednesday_short"), QT_TRID_NOOP("charts.thursday_short"),
    QT_TRID_NOOP("charts.friday_short"), QT_TRID_NOOP("charts.saturday_short"),
    QT_TRID_NOOP("charts.sunday_short")};
constexpr const char* LONG_DAYS[] = {
    QT_TRID_NOOP("charts.monday"),    QT_TRID_NOOP("charts.tuesday"),
    QT_TRID_NOOP("charts.wednesday"), QT_TRID_NOOP("charts.thursday"),
    QT_TRID_NOOP("charts.friday"), QT_TRID_NOOP("charts.saturday"),
    QT_TRID_NOOP("charts.sunday")};

QString noData()
{
    return qtTrId("charts.no_data");
}

QFont smallFont(const ChartStyle& style, double factor = 0.9)
{
    QFont font = style.font;
    font.setPixelSize(std::max(8, static_cast<int>(std::lround(style.font.pixelSize() * factor))));
    return font;
}

struct Axis
{
    qint64 top = 1;
    qint64 step = 1;
};

Axis computeAxis(qint64 maximum, int ticks = 4)
{
    if (maximum <= 0)
        return {ticks, 1};
    const double raw = static_cast<double>(maximum) / ticks;
    const double power = std::pow(10.0, std::floor(std::log10(raw)));
    const double normalized = raw / power;
    const double factor = normalized <= 1 ? 1 : normalized <= 2 ? 2 : normalized <= 2.5 ? 2.5 : normalized <= 5 ? 5 : 10;
    const qint64 step = std::max<qint64>(1, static_cast<qint64>(std::ceil(factor * power)));
    return {step * ((maximum + step - 1) / step), step};
}

QRectF drawAxis(QPainter& p, const QRectF& zone, const Axis& axis, const ChartStyle& style,
                double xAxisHeight)
{
    const QFont smaller = smallFont(style);
    const QFontMetricsF metrics(smaller);
    double labelsWidth = 0;
    for (qint64 value = 0; value <= axis.top; value += axis.step)
        labelsWidth = std::max(labelsWidth, metrics.horizontalAdvance(formatShortNumber(value)));
    labelsWidth += 10;
    const QRectF plot(zone.left() + labelsWidth, zone.top() + metrics.height() / 2,
                      std::max(10.0, zone.width() - labelsWidth - 6),
                      std::max(10.0, zone.height() - xAxisHeight - metrics.height() / 2));
    p.setFont(smaller);
    for (qint64 value = 0; value <= axis.top; value += axis.step) {
        const double y = plot.bottom() - plot.height() * static_cast<double>(value) / static_cast<double>(axis.top);
        p.setPen(QPen(style.grid, 1));
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
        p.setPen(style.secondaryText);
        p.drawText(QRectF(zone.left(), y - metrics.height() / 2, labelsWidth - 6, metrics.height()),
                   Qt::AlignRight | Qt::AlignVCenter, formatShortNumber(value));
    }
    return plot;
}

} // namespace

QColor blend(const QColor& a, const QColor& b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    return QColor::fromRgbF(static_cast<float>(a.redF() + (b.redF() - a.redF()) * t),
                            static_cast<float>(a.greenF() + (b.greenF() - a.greenF()) * t),
                            static_cast<float>(a.blueF() + (b.blueF() - a.blueF()) * t),
                            static_cast<float>(a.alphaF() + (b.alphaF() - a.alphaF()) * t));
}

QList<Element> elementsFromSeries(const QList<QPair<QString, qint64>>& series)
{
    QList<Element> list;
    list.reserve(series.size());
    for (const auto& [label, value] : series)
        list.append({label.isEmpty() ? qtTrId("common.unknown") : label, value, {}, label});
    return list;
}

QList<Element> elementsByCategory(const QMap<QString, qint64>& byCategory)
{
    QList<Element> list;
    for (const Category& category : categories()) {
        const qint64 count = byCategory.value(QLatin1String(category.key), 0);
        if (count > 0)
            list.append({categoryLabel(category), count, QColor(QLatin1String(category.color)),
                         QLatin1String(category.key)});
    }
    return list;
}

// ── Style ────────────────────────────────────────────────────────────────────

ChartStyle ChartStyle::fromPalette(const QPalette& palette, const QFont& font)
{
    ChartStyle style;
    const QColor base = palette.color(QPalette::Base);
    style.text = palette.color(QPalette::Text);
    style.secondaryText = palette.color(QPalette::PlaceholderText);
    style.grid = blend(base, style.text, 0.10);
    style.background = blend(base, style.text, 0.06);
    const bool dark = base.lightness() < 128;
    style.accent = dark ? QColor(0x60, 0xa5, 0xfa) : QColor(0x25, 0x63, 0xeb);
    style.strongAccent = dark ? QColor(0xbf, 0xdb, 0xfe) : QColor(0x1e, 0x3a, 0x8a);
    style.font = font;
    if (font.pixelSize() <= 0)
        style.font.setPixelSize(std::max(9, static_cast<int>(std::lround(font.pointSizeF() * 96.0 / 72.0))));
    return style;
}

ChartStyle ChartStyle::forPrint()
{
    ChartStyle style;
    style.text = QColor(0x1f, 0x29, 0x37);
    style.secondaryText = QColor(0x6b, 0x72, 0x80);
    style.grid = QColor(0xe5, 0xe7, 0xeb);
    style.background = QColor(0xf1, 0xf5, 0xf9);
    style.accent = QColor(0x25, 0x63, 0xeb);
    style.strongAccent = QColor(0x1e, 0x40, 0xaf);
    style.font = QFont(QStringLiteral("Segoe UI"));
    style.font.setStyleHint(QFont::SansSerif);
    style.font.setPixelSize(12);
    return style;
}

// ── Timeline ─────────────────────────────────────────────────────────────────

Timeline aggregate(const DateSeries& series)
{
    Timeline timeline;
    if (series.isEmpty())
        return timeline;
    const qint64 days = series.first().first.daysTo(series.last().first) + 1;
    timeline.granularity = days > 730 ? Granularity::Month : days > 120 ? Granularity::Week : Granularity::Day;
    for (const auto& [date, value] : series) {
        QDate start = date;
        QDate end = date;
        if (timeline.granularity == Granularity::Week) {
            start = date.addDays(1 - date.dayOfWeek());
            end = start.addDays(6);
        } else if (timeline.granularity == Granularity::Month) {
            start = QDate(date.year(), date.month(), 1);
            end = start.addMonths(1).addDays(-1);
        }
        if (!timeline.periods.isEmpty() && timeline.periods.last().start == start)
            timeline.periods.last().value += value;
        else
            timeline.periods.append({start, end, value});
    }
    return timeline;
}

QString periodLabel(const Period& period, Granularity granularity)
{
    switch (granularity) {
    case Granularity::Day:
        return QLocale().toString(period.start, qtTrId("charts.long_date_format"));
    case Granularity::Week:
        return qtTrId("charts.week").arg(formatDate(period.start), formatDate(period.end));
    case Granularity::Month:
        return QLocale().toString(period.start, qtTrId("charts.month_format"));
    }
    return {};
}

void drawTimeline(QPainter& p, const QRectF& zone, const Timeline& timeline, const ChartStyle& style,
                  int hover, QList<Zone>* zones)
{
    const auto& periods = timeline.periods;
    if (periods.isEmpty()) {
        drawMessage(p, zone, noData(), style);
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    qint64 maximum = 0;
    for (const Period& period : periods)
        maximum = std::max(maximum, period.value);
    const QFont smaller = smallFont(style);
    const QFontMetricsF metrics(smaller);
    const Axis axis = computeAxis(maximum);
    const QRectF plot = drawAxis(p, zone, axis, style, metrics.height() + 8);

    const qsizetype n = periods.size();
    const double step = n > 1 ? plot.width() / static_cast<double>(n - 1) : 0;
    auto xOf = [&](qsizetype i) { return n > 1 ? plot.left() + static_cast<double>(i) * step : plot.center().x(); };
    auto yOf = [&](qint64 value) {
        return plot.bottom() - plot.height() * static_cast<double>(value) / static_cast<double>(axis.top);
    };

    if (n == 1) {
        const double width = std::min(40.0, plot.width() / 3);
        const QRectF bar(plot.center().x() - width / 2, yOf(periods[0].value), width,
                         plot.bottom() - yOf(periods[0].value));
        p.setPen(Qt::NoPen);
        p.setBrush(hover == 0 ? style.strongAccent : style.accent);
        p.drawRoundedRect(bar, 3, 3);
    } else {
        QPainterPath row;
        row.moveTo(xOf(0), yOf(periods[0].value));
        for (qsizetype i = 1; i < n; ++i)
            row.lineTo(xOf(i), yOf(periods[i].value));
        QPainterPath area = row;
        area.lineTo(xOf(n - 1), plot.bottom());
        area.lineTo(xOf(0), plot.bottom());
        area.closeSubpath();
        if (style.transparency) {
            QLinearGradient gradient(plot.topLeft(), plot.bottomLeft());
            QColor top = style.accent;
            top.setAlphaF(0.35F);
            QColor bottom = style.accent;
            bottom.setAlphaF(0.04F);
            gradient.setColorAt(0, top);
            gradient.setColorAt(1, bottom);
            p.fillPath(area, gradient);
        } else {
            p.fillPath(area, blend(Qt::white, style.accent, 0.16));
        }
        p.setPen(QPen(style.accent, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        p.drawPath(row);

        if (hover >= 0 && hover < n) {
            const QPointF point(xOf(hover), yOf(periods[hover].value));
            p.setPen(QPen(style.secondaryText, 1, Qt::DashLine));
            p.drawLine(QPointF(point.x(), plot.top()), QPointF(point.x(), plot.bottom()));
            p.setPen(QPen(style.strongAccent, 2));
            p.setBrush(style.background);
            p.drawEllipse(point, 4.5, 4.5);
        }
    }

    p.setFont(smaller);
    p.setPen(style.secondaryText);
    const bool multipleYears = periods.first().start.year() != periods.last().start.year();
    QString format = displayedDayFormat(multipleYears);
    if (timeline.granularity == Granularity::Month)
        format = qtTrId("charts.short_month_format");
    auto label = [&](const Period& period) { return QLocale().toString(period.start, format); };
    const double labelWidth = metrics.horizontalAdvance(label(periods.first())) + 18;
    const qsizetype maxLabels = std::max<qsizetype>(1, static_cast<qsizetype>(plot.width() / labelWidth));
    const qsizetype stride = std::max<qsizetype>(1, (n + maxLabels - 1) / maxLabels);
    for (qsizetype i = 0; i < n; i += stride) {
        const QString text = label(periods[i]);
        const double width = metrics.horizontalAdvance(text);
        double x = xOf(i) - width / 2;
        x = std::clamp(x, zone.left(), zone.right() - width);
        p.drawText(QRectF(x, plot.bottom() + 6, width + 1, metrics.height()), Qt::AlignLeft | Qt::AlignTop, text);
    }

    if (zones) {
        for (qsizetype i = 0; i < n; ++i) {
            const double left = n > 1 ? xOf(i) - step / 2 : plot.left();
            const double width = n > 1 ? step : plot.width();
            Zone elementZone;
            elementZone.rectangle = QRectF(left, plot.top(), width, plot.height());
            elementZone.index = static_cast<int>(i);
            elementZone.tooltip =
                qtTrId("common.label_value")
                    .arg(periodLabel(periods[i], timeline.granularity), formatEvents(periods[i].value));
            zones->append(elementZone);
        }
    }
    p.restore();
}

// ── Bars ─────────────────────────────────────────────────────────────────────

void drawBars(QPainter& p, const QRectF& zone, const QStringList& labels, const QList<qint64>& values,
              const ChartStyle& style, int hover, QList<Zone>* zones, const QStringList& tooltips)
{
    const qsizetype n = values.size();
    qint64 maximum = 0;
    qsizetype maxIndex = -1;
    for (qsizetype i = 0; i < n; ++i) {
        if (values[i] > maximum) {
            maximum = values[i];
            maxIndex = i;
        }
    }
    if (n == 0 || maximum == 0) {
        drawMessage(p, zone, noData(), style);
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QFont smaller = smallFont(style);
    const QFontMetricsF metrics(smaller);
    const Axis axis = computeAxis(maximum);
    const QRectF plot = drawAxis(p, zone, axis, style, metrics.height() + 8);

    const double location = plot.width() / static_cast<double>(n);
    const double width = std::max(1.0, location * 0.68);
    p.setPen(Qt::NoPen);
    for (qsizetype i = 0; i < n; ++i) {
        const double height = plot.height() * static_cast<double>(values[i]) / static_cast<double>(axis.top);
        const QRectF bar(plot.left() + location * static_cast<double>(i) + (location - width) / 2,
                         plot.bottom() - height, width, height);
        QColor color = style.accent;
        if (i == maxIndex)
            color = style.strongAccent;
        if (i == hover)
            color = blend(color, style.text, 0.25);
        p.setBrush(color);
        p.drawRoundedRect(bar, std::min(3.0, width / 3), std::min(3.0, width / 3));
    }

    p.setFont(smaller);
    p.setPen(style.secondaryText);
    double maxWidth = 0;
    for (const QString& label : labels)
        maxWidth = std::max(maxWidth, metrics.horizontalAdvance(label));
    const qsizetype stride = std::max<qsizetype>(1, static_cast<qsizetype>(std::ceil((maxWidth + 6) / location)));
    for (qsizetype i = 0; i < std::min(n, labels.size()); i += stride) {
        p.drawText(QRectF(plot.left() + location * static_cast<double>(i) - 20, plot.bottom() + 6,
                          location + 40, metrics.height()),
                   Qt::AlignHCenter | Qt::AlignTop, labels[i]);
    }

    if (zones) {
        for (qsizetype i = 0; i < n; ++i) {
            Zone elementZone;
            elementZone.rectangle =
                QRectF(plot.left() + location * static_cast<double>(i), plot.top(), location, plot.height());
            elementZone.index = static_cast<int>(i);
            elementZone.tooltip = i < tooltips.size() ? tooltips[i]
                                  : i < labels.size()
                                      ? qtTrId("common.label_value").arg(labels[i], formatEvents(values[i]))
                                      : formatEvents(values[i]);
            zones->append(elementZone);
        }
    }
    p.restore();
}

// ── Histogram ────────────────────────────────────────────────────────────────

QRectF drawHistogram(QPainter& p, const QRectF& zone, const QList<qint64>& values, const QList<qint64>& alerts,
                     const QStringList& labels, const QStringList& tooltips, const ChartStyle& style,
                     int hover, QList<Zone>* zones)
{
    const qsizetype n = values.size();
    const qint64 maximum = n > 0 ? *std::max_element(values.cbegin(), values.cend()) : 0;
    if (n == 0 || maximum == 0) {
        drawMessage(p, zone, noData(), style);
        return {};
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QFont smaller = smallFont(style);
    const QFontMetricsF metrics(smaller);
    const Axis axis = computeAxis(maximum, 3);
    const QRectF plot = drawAxis(p, zone, axis, style, metrics.height() + 6);
    const double location = plot.width() / static_cast<double>(n);
    const double width = location > 4 ? location * 0.82 : location;
    const QColor red(0xef, 0x44, 0x44);
    p.setPen(Qt::NoPen);
    for (qsizetype i = 0; i < n; ++i) {
        if (values[i] <= 0)
            continue;
        const double left = plot.left() + location * static_cast<double>(i) + (location - width) / 2;
        const double height =
            std::max(1.0, plot.height() * static_cast<double>(values[i]) / static_cast<double>(axis.top));
        QColor color = i == hover ? style.strongAccent : style.accent;
        p.setBrush(color);
        p.drawRect(QRectF(left, plot.bottom() - height, width, height));
        const qint64 alert = i < alerts.size() ? alerts[i] : 0;
        if (alert > 0) {
            const double alertHeight = std::clamp(
                plot.height() * static_cast<double>(alert) / static_cast<double>(axis.top), 2.0, height);
            p.setBrush(red);
            p.drawRect(QRectF(left, plot.bottom() - alertHeight, width, alertHeight));
        }
    }

    p.setFont(smaller);
    p.setPen(style.secondaryText);
    double maxWidth = 0;
    for (const QString& label : labels)
        maxWidth = std::max(maxWidth, metrics.horizontalAdvance(label));
    const qsizetype stride = std::max<qsizetype>(1, static_cast<qsizetype>(std::ceil((maxWidth + 12) / location)));
    double previousEnd = zone.left() - maxWidth - 12;
    for (qsizetype i = 0; i < std::min(n, labels.size()); i += stride) {
        const double textWidth = metrics.horizontalAdvance(labels[i]);
        const double x = std::clamp(plot.left() + location * static_cast<double>(i), zone.left(),
                                    zone.right() - textWidth);
        if (x < previousEnd + 12)
            continue;
        previousEnd = x + textWidth;
        p.drawText(QRectF(x, plot.bottom() + 4, textWidth + 1, metrics.height()), Qt::AlignLeft | Qt::AlignTop,
                   labels[i]);
    }

    if (zones) {
        for (qsizetype i = 0; i < n; ++i) {
            Zone elementZone;
            elementZone.rectangle =
                QRectF(plot.left() + location * static_cast<double>(i), plot.top(), location, plot.height());
            elementZone.index = static_cast<int>(i);
            elementZone.tooltip = i < tooltips.size() ? tooltips[i] : formatEvents(values[i]);
            zones->append(elementZone);
        }
    }
    p.restore();
    return plot;
}

// ── Donut ────────────────────────────────────────────────────────────────────

void drawDonut(QPainter& p, const QRectF& zone, const QList<Element>& elements, const ChartStyle& style,
               int hover, QList<Zone>* zones)
{
    qint64 total = 0;
    for (const Element& element : elements)
        total += std::max<qint64>(0, element.value);
    if (total == 0) {
        drawMessage(p, zone, noData(), style);
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);

    const QFontMetricsF metrics(style.font);
    const double rowHeight = std::max(20.0, metrics.height() * 1.55);
    const double diameter = std::max(40.0, std::min(zone.height() - 12, zone.width() * 0.42));
    const QRectF outer(zone.left() + 6, zone.center().y() - diameter / 2, diameter, diameter);
    const double thickness = diameter * 0.2;
    const QRectF inner = outer.adjusted(thickness, thickness, -thickness, -thickness);

    double startAngle = 90.0;
    int shown = 0;
    for (const Element& element : elements)
        shown += element.value > 0 ? 1 : 0;
    const double gap = shown > 1 ? 1.2 : 0.0;
    p.setPen(Qt::NoPen);
    for (qsizetype i = 0; i < elements.size(); ++i) {
        const Element& element = elements[i];
        if (element.value <= 0)
            continue;
        const double extent = 360.0 * static_cast<double>(element.value) / static_cast<double>(total);
        const double visible = std::max(0.3, extent - gap);
        const bool highlighted = i == hover;
        const QRectF edge = highlighted ? outer.adjusted(-4, -4, 4, 4) : outer;
        QPainterPath segment;
        segment.arcMoveTo(edge, startAngle - gap / 2);
        segment.arcTo(edge, startAngle - gap / 2, -visible);
        segment.arcTo(inner, startAngle - gap / 2 - visible, visible);
        segment.closeSubpath();
        p.setBrush(element.color.isValid() ? element.color : style.accent);
        p.drawPath(segment);
        if (zones) {
            Zone elementZone;
            elementZone.shape = segment;
            elementZone.rectangle = segment.boundingRect();
            elementZone.index = static_cast<int>(i);
            elementZone.tooltip = qtTrId("charts.tooltip_share").arg(
                element.label, formatNumber(element.value),
                formatPercentage(static_cast<double>(element.value) / static_cast<double>(total)));
            zones->append(elementZone);
        }
        startAngle -= extent;
    }

    QFont large = style.font;
    large.setPixelSize(static_cast<int>(std::lround(style.font.pixelSize() * 1.45)));
    large.setBold(true);
    const QFontMetricsF largeMetrics(large);
    const QFont smaller = smallFont(style, 0.85);
    const QFontMetricsF smallMetrics(smaller);
    const double centerHeight = largeMetrics.height() + smallMetrics.height();
    p.setFont(large);
    p.setPen(style.text);
    p.drawText(QRectF(inner.left(), inner.center().y() - centerHeight / 2, inner.width(),
                      largeMetrics.height()),
               Qt::AlignCenter, formatShortNumber(total));
    p.setFont(smaller);
    p.setPen(style.secondaryText);
    p.drawText(QRectF(inner.left(), inner.center().y() - centerHeight / 2 + largeMetrics.height(),
                      inner.width(), smallMetrics.height()),
               Qt::AlignCenter,
               qtTrId("charts.unit_events", pluralCount(total)));

    const double legendLeft = outer.right() + 22;
    const double legendWidth = zone.right() - legendLeft;
    if (legendWidth > 60) {
        const qsizetype rows = std::min<qsizetype>(elements.size(),
                                                   static_cast<qsizetype>(zone.height() / rowHeight));
        double y = zone.center().y() - rowHeight * static_cast<double>(rows) / 2;
        p.setFont(style.font);
        for (qsizetype i = 0; i < rows; ++i) {
            const Element& element = elements[i];
            const QRectF row(legendLeft, y, legendWidth, rowHeight);
            if (i == hover) {
                p.setPen(Qt::NoPen);
                p.setBrush(style.background);
                p.drawRoundedRect(row.adjusted(-6, 0, 0, 0), 4, 4);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(element.color.isValid() ? element.color : style.accent);
            p.drawRoundedRect(QRectF(row.left(), row.center().y() - 5, 10, 10), 3, 3);

            const QString percentage =
                formatPercentage(static_cast<double>(element.value) / static_cast<double>(total));
            const QString value = formatNumber(element.value);
            const double valueWidth = metrics.horizontalAdvance(value) + 12;
            double percentageWidth = metrics.horizontalAdvance(formatPercentage(1.0)) + 4;
            if (row.width() - 18 - valueWidth - percentageWidth < 90)
                percentageWidth = 0;
            const QRectF labelZone(row.left() + 18, row.top(),
                                   row.width() - 18 - valueWidth - percentageWidth, row.height());
            p.setPen(style.text);
            p.drawText(labelZone, Qt::AlignLeft | Qt::AlignVCenter,
                       metrics.elidedText(element.label, Qt::ElideRight, labelZone.width()));
            p.drawText(QRectF(labelZone.right(), row.top(), valueWidth, row.height()),
                       Qt::AlignRight | Qt::AlignVCenter, value);
            if (percentageWidth > 0) {
                p.setPen(style.secondaryText);
                p.drawText(QRectF(row.right() - percentageWidth, row.top(), percentageWidth, row.height()),
                           Qt::AlignRight | Qt::AlignVCenter, percentage);
            }
            if (zones) {
                Zone elementZone;
                elementZone.rectangle = row;
                elementZone.index = static_cast<int>(i);
                elementZone.tooltip = qtTrId("charts.tooltip_share").arg(element.label, value, percentage);
                zones->append(elementZone);
            }
            y += rowHeight;
        }
    }
    p.restore();
}

// ── Ranking ──────────────────────────────────────────────────────────────────

double rankingRowHeight(const ChartStyle& style)
{
    return std::max(22.0, QFontMetricsF(style.font).height() * 1.85);
}

void drawRanking(QPainter& p, const QRectF& zone, const QList<Element>& elements, const ChartStyle& style,
                 int hover, QList<Zone>* zones, bool elideLeft)
{
    if (elements.isEmpty()) {
        drawMessage(p, zone, noData(), style);
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setFont(style.font);
    const QFontMetricsF metrics(style.font);
    const double rowHeight = rankingRowHeight(style);
    const qsizetype rows =
        std::min<qsizetype>(elements.size(), std::max<qsizetype>(1, static_cast<qsizetype>(zone.height() / rowHeight)));

    qint64 maximum = 1;
    double valuesWidth = 0;
    for (qsizetype i = 0; i < rows; ++i) {
        maximum = std::max(maximum, elements[i].value);
        valuesWidth = std::max(valuesWidth, metrics.horizontalAdvance(formatNumber(elements[i].value)));
    }
    valuesWidth += 12;
    double needed = 0;
    for (qsizetype i = 0; i < rows; ++i)
        needed = std::max(needed, metrics.horizontalAdvance(elements[i].label) + 8);
    const double available = zone.width() - valuesWidth - std::max(60.0, zone.width() * 0.22) - 10;
    const double labelsWidth = std::clamp(needed, 80.0, std::max(80.0, available));
    const double barsLeft = zone.left() + labelsWidth + 10;
    const double barsWidth = std::max(20.0, zone.right() - valuesWidth - barsLeft);
    const double thickness = std::max(6.0, std::min(10.0, rowHeight * 0.36));

    for (qsizetype i = 0; i < rows; ++i) {
        const Element& element = elements[i];
        const QRectF row(zone.left(), zone.top() + rowHeight * static_cast<double>(i), zone.width(), rowHeight);
        if (i == hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(style.background);
            p.drawRoundedRect(row, 4, 4);
        }
        const QRectF labelZone(row.left() + 4, row.top(), labelsWidth - 4, row.height());
        p.setPen(style.text);
        p.drawText(labelZone, Qt::AlignLeft | Qt::AlignVCenter,
                   metrics.elidedText(element.label, elideLeft ? Qt::ElideLeft : Qt::ElideRight,
                                      labelZone.width()));
        const QRectF track(barsLeft, row.center().y() - thickness / 2, barsWidth, thickness);
        p.setPen(Qt::NoPen);
        p.setBrush(style.background);
        p.drawRoundedRect(track, thickness / 2, thickness / 2);
        const double proportion = static_cast<double>(element.value) / static_cast<double>(maximum);
        QRectF bar = track;
        bar.setWidth(std::max(thickness, track.width() * proportion));
        QColor color = element.color.isValid() ? element.color : style.accent;
        if (i == hover)
            color = blend(color, style.text, 0.2);
        p.setBrush(color);
        p.drawRoundedRect(bar, thickness / 2, thickness / 2);
        p.setPen(style.secondaryText);
        p.drawText(QRectF(zone.right() - valuesWidth, row.top(), valuesWidth - 4, row.height()),
                   Qt::AlignRight | Qt::AlignVCenter, formatNumber(element.value));
        if (zones) {
            Zone elementZone;
            elementZone.rectangle = row;
            elementZone.index = static_cast<int>(i);
            elementZone.tooltip = qtTrId("common.label_value").arg(element.label, formatEvents(element.value));
            zones->append(elementZone);
        }
    }
    p.restore();
}

// ── Heatmap ──────────────────────────────────────────────────────────────────

void drawHeatmap(QPainter& p, const QRectF& zone, const WeekMatrix& matrix, const ChartStyle& style,
                 int hover, QList<Zone>* zones)
{
    qint64 maximum = 0;
    for (const auto& day : matrix)
        for (const qint64 value : day)
            maximum = std::max(maximum, value);
    if (maximum == 0) {
        drawMessage(p, zone, noData(), style);
        return;
    }
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    const QFont smaller = smallFont(style);
    const QFontMetricsF metrics(smaller);
    p.setFont(smaller);

    QStringList shortLabels;
    double labelsWidth = 0;
    for (const char* day : SHORT_DAYS) {
        shortLabels << qtTrId(day);
        labelsWidth = std::max(labelsWidth, metrics.horizontalAdvance(shortLabels.last()) + 12);
    }
    const double headerHeight = metrics.height() + 6;
    const double cellWidth = (zone.width() - labelsWidth) / 24.0;
    const double cellHeight = std::min((zone.height() - headerHeight) / 7.0, std::max(cellWidth, 14.0) * 1.6);
    const double gridHeight = cellHeight * 7;
    const double top = zone.top() + headerHeight + std::max(0.0, (zone.height() - headerHeight - gridHeight) / 2);
    const double margin = cellWidth > 10 ? 1.5 : 0.5;

    p.setPen(style.secondaryText);
    const int hourStep =
        cellWidth * 3 >= metrics.horizontalAdvance(qtTrId("charts.hour").arg(21)) + 4 ? 3 : 6;
    for (int hour = 0; hour < 24; hour += hourStep) {
        p.drawText(QRectF(zone.left() + labelsWidth + cellWidth * hour, top - headerHeight,
                          cellWidth * hourStep, headerHeight - 4),
                   Qt::AlignLeft | Qt::AlignBottom, qtTrId("charts.hour").arg(hour));
    }
    for (int day = 0; day < 7; ++day) {
        const double y = top + cellHeight * day;
        p.setPen(style.secondaryText);
        p.drawText(QRectF(zone.left(), y, labelsWidth - 6, cellHeight), Qt::AlignLeft | Qt::AlignVCenter,
                   shortLabels[day]);
        for (int hour = 0; hour < 24; ++hour) {
            const qint64 value = matrix[static_cast<size_t>(day)][static_cast<size_t>(hour)];
            const QRectF cell(zone.left() + labelsWidth + cellWidth * hour + margin, y + margin,
                              cellWidth - 2 * margin, cellHeight - 2 * margin);
            const QColor color =
                value == 0 ? style.background
                           : blend(style.background, style.accent,
                                   0.18 + 0.82 * std::sqrt(static_cast<double>(value) / static_cast<double>(maximum)));
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawRoundedRect(cell, 2, 2);
            const int index = day * 24 + hour;
            if (index == hover) {
                p.setPen(QPen(style.text, 1.5));
                p.setBrush(Qt::NoBrush);
                p.drawRoundedRect(cell.adjusted(-0.5, -0.5, 0.5, 0.5), 2, 2);
            }
            if (zones) {
                Zone elementZone;
                elementZone.rectangle = cell.adjusted(-margin, -margin, margin, margin);
                elementZone.index = index;
                elementZone.tooltip = qtTrId("charts.tooltip_day_hour")
                                          .arg(qtTrId(LONG_DAYS[day]))
                                          .arg(hour)
                                          .arg(hour + 1)
                                          .arg(formatEvents(value));
                zones->append(elementZone);
            }
        }
    }
    p.restore();
}

// ── Miscellaneous ────────────────────────────────────────────────────────────

void drawMessage(QPainter& p, const QRectF& zone, const QString& message, const ChartStyle& style)
{
    p.save();
    p.setFont(style.font);
    p.setPen(style.secondaryText);
    p.drawText(zone, Qt::AlignCenter | Qt::TextWordWrap, message);
    p.restore();
}

} // namespace lh::render
