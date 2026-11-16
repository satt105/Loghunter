#include "ChartViews.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

namespace lh::ui {

ChartView::ChartView(QWidget* parent) : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void ChartView::dataModified()
{
    m_hover = -1;
    update();
}

void ChartView::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    m_zones.clear();
    const render::ChartStyle style = render::ChartStyle::fromPalette(palette(), font());
    draw(painter, QRectF(rect()).adjusted(2, 4, -2, -2), style, m_hover, &m_zones);
}

int ChartView::zoneAt(const QPointF& point) const
{
    for (const render::Zone& zone : m_zones)
        if (zone.contains(point))
            return zone.index;
    return -1;
}

void ChartView::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF point = event->position();
    const int index = zoneAt(point);
    if (index != m_hover) {
        m_hover = index;
        update();
        setCursor(index >= 0 && m_clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }
    QString tooltip;
    for (const render::Zone& zone : m_zones) {
        if (zone.index == index && zone.contains(point)) {
            tooltip = zone.tooltip;
            break;
        }
    }
    if (tooltip.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(event->globalPosition().toPoint(), tooltip, this);
    QWidget::mouseMoveEvent(event);
}

void ChartView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_clickable && event->button() == Qt::LeftButton) {
        const int index = zoneAt(event->position());
        if (index >= 0)
            emit elementClicked(index);
    }
    QWidget::mouseReleaseEvent(event);
}

void ChartView::leaveEvent(QEvent* event)
{
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}

// ── Concrete charts ──────────────────────────────────────────────────────────

void TimelineChart::set(const render::DateSeries& series)
{
    m_timeline = render::aggregate(series);
    dataModified();
}

void TimelineChart::draw(QPainter& painter, const QRectF& zone, const render::ChartStyle& style,
                         int hover, QList<render::Zone>* zones)
{
    render::drawTimeline(painter, zone, m_timeline, style, hover, zones);
}

void HoursChart::set(const std::array<qint64, 24>& values)
{
    m_values = QList<qint64>(values.begin(), values.end());
    dataModified();
}

void HoursChart::draw(QPainter& painter, const QRectF& zone, const render::ChartStyle& style, int hover,
                      QList<render::Zone>* zones)
{
    QStringList labels;
    QStringList tooltips;
    for (int hour = 0; hour < m_values.size(); ++hour) {
        labels << QString::number(hour);
        tooltips << qtTrId("charts.tooltip_hour").arg(hour).arg(hour + 1).arg(formatEvents(m_values[hour]));
    }
    render::drawBars(painter, zone, labels, m_values, style, hover, zones, tooltips);
}

void DonutChart::set(const QList<render::Element>& elements)
{
    m_elements = elements;
    dataModified();
}

void DonutChart::draw(QPainter& painter, const QRectF& zone, const render::ChartStyle& style, int hover,
                      QList<render::Zone>* zones)
{
    render::drawDonut(painter, zone, m_elements, style, hover, zones);
}

void RankingChart::set(const QList<render::Element>& elements, bool elideLeft)
{
    m_elements = elements;
    m_elideLeft = elideLeft;
    dataModified();
}

void RankingChart::draw(QPainter& painter, const QRectF& zone, const render::ChartStyle& style,
                        int hover, QList<render::Zone>* zones)
{
    render::drawRanking(painter, zone, m_elements, style, hover, zones, m_elideLeft);
}

void HeatmapChart::set(const render::WeekMatrix& matrix)
{
    m_matrix = matrix;
    dataModified();
}

void HeatmapChart::draw(QPainter& painter, const QRectF& zone, const render::ChartStyle& style,
                        int hover, QList<render::Zone>* zones)
{
    render::drawHeatmap(painter, zone, m_matrix, style, hover, zones);
}

} // namespace lh::ui
