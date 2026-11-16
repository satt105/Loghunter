#include "Timeline.h"

#include "core/Format.h"
#include "core/DateTime.h"
#include "ui/Workspace.h"
#include "ui/Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QToolTip>

#include <cmath>

namespace lh::ui {

namespace {

constexpr int MAX_BARS = 160;
constexpr qint64 SECOND = 1'000'000;
constexpr qint64 DAY = 86'400 * SECOND;

} // namespace

Timeline::Timeline(Workspace* workspace, QWidget* parent) : QWidget(parent), m_workspace(workspace)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(sizeHint().height());
}

void Timeline::setFilters(const Filters& filters)
{
    m_filters = filters;
    load();
}

void Timeline::load()
{
    cancel(m_token);
    m_loading = true;
    update();
    const Filters filters = m_filters;
    const QByteArray timeZone = m_workspace->timeZone();
    m_token = m_workspace->executor().runCached(
        this, QStringLiteral("histogram|%1|%2").arg(filters.key(), QString::fromLatin1(timeZone)),
        [filters, timeZone](Context& context) { return queries::histogram(context, filters, MAX_BARS, timeZone); },
        [this](queries::Histogram histogram) {
            m_token.reset();
            m_loading = false;
            m_histogram = std::move(histogram);
            prepareLabels();
            update();
        },
        [this](const QString&) {
            m_token.reset();
            m_loading = false;
            m_histogram = {};
            prepareLabels();
            update();
        });
}

void Timeline::prepareLabels()
{
    m_values.clear();
    m_alerts.clear();
    m_labels.clear();
    m_tooltips.clear();
    m_hover = -1;
    const queries::Histogram& h = m_histogram;
    if (h.values.empty() || h.step <= 0)
        return;
    TimeZoneConverter converter(m_workspace->timeZone());
    const qint64 extent = h.step * static_cast<qint64>(h.values.size());
    const bool multipleYears = extent > 330 * DAY;
    for (size_t i = 0; i < h.values.size(); ++i) {
        const qint64 start = h.start + static_cast<qint64>(i) * h.step;
        const QString iso = converter.convert(start).timestamp;
        const QString isoEnd = converter.convert(start + h.step - 1).timestamp;
        QString label;
        if (h.step >= DAY)
            label = iso.mid(8, 2) + QLatin1Char('/') + iso.mid(5, 2)
                    + (multipleYears ? QLatin1Char('/') + iso.mid(2, 2) : QString());
        else if (extent > DAY)
            label = iso.mid(8, 2) + QLatin1Char('/') + iso.mid(5, 2) + QLatin1Char(' ') + iso.mid(11, 5);
        else if (h.step < 60 * SECOND)
            label = iso.mid(11, 8);
        else
            label = iso.mid(11, 5);
        m_labels << label;
        const qint64 value = h.values[i];
        const qint64 alerts = i < h.alerts.size() ? h.alerts[i] : 0;
        QString tooltip = qtTrId("timeline.tooltip")
                              .arg(formatTimestamp(iso), formatTimestamp(isoEnd), formatEvents(value));
        if (alerts > 0)
            tooltip += QLatin1Char('\n') + qtTrId("timeline.tooltip_alerts", pluralCount(alerts));
        m_tooltips << tooltip;
        m_values << value;
        m_alerts << alerts;
    }
}

void Timeline::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const render::ChartStyle style = render::ChartStyle::fromPalette(palette(), font());
    m_zones.clear();
    const QRectF zone = QRectF(rect()).adjusted(4, 6, -4, -2);
    if (m_values.isEmpty()) {
        m_plot = {};
        render::drawMessage(painter, zone,
                            m_loading             ? qtTrId("events.loading")
                            : m_filters.isEmpty() ? qtTrId("timeline.no_date")
                                                  : qtTrId("timeline.no_date_for_filters"),
                            style);
        return;
    }
    m_plot = render::drawHistogram(painter, zone, m_values, m_alerts, m_labels, m_tooltips, style,
                                   m_hover, &m_zones);
    if (m_selectionStart < 0 || m_selectionEnd < 0 || m_plot.isEmpty())
        return;
    const int start = std::min(m_selectionStart, m_selectionEnd);
    const int end = std::max(m_selectionStart, m_selectionEnd);
    const double location = m_plot.width() / static_cast<double>(m_values.size());
    const QRectF selection(m_plot.left() + location * start, m_plot.top(), location * (end - start + 1),
                           m_plot.height());
    QColor background = theme::accent(palette());
    background.setAlphaF(0.18F);
    painter.fillRect(selection, background);
    painter.setPen(QPen(theme::accent(palette()), 1));
    painter.drawRect(selection);
}

int Timeline::binAt(double x) const
{
    if (m_plot.isEmpty() || m_values.isEmpty())
        return -1;
    const double location = m_plot.width() / static_cast<double>(m_values.size());
    const int index = static_cast<int>(std::floor((x - m_plot.left()) / location));
    return std::clamp(index, 0, static_cast<int>(m_values.size()) - 1);
}

void Timeline::mousePressEvent(QMouseEvent* event)
{
    const QPointF point = event->position();
    if (event->button() == Qt::LeftButton && !m_plot.isEmpty() && point.x() >= m_plot.left()
        && point.x() <= m_plot.right()) {
        m_selectionStart = m_selectionEnd = binAt(point.x());
        QToolTip::hideText();
        update();
    }
    QWidget::mousePressEvent(event);
}

void Timeline::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF point = event->position();
    if (m_selectionStart >= 0) {
        const int bin = binAt(point.x());
        if (bin != m_selectionEnd) {
            m_selectionEnd = bin;
            update();
        }
        QWidget::mouseMoveEvent(event);
        return;
    }
    int hover = -1;
    QString tooltip;
    for (const render::Zone& zone : std::as_const(m_zones)) {
        if (zone.contains(point)) {
            hover = zone.index;
            tooltip = zone.tooltip;
            break;
        }
    }
    if (hover != m_hover) {
        m_hover = hover;
        setCursor(hover >= 0 ? Qt::PointingHandCursor : Qt::ArrowCursor);
        update();
    }
    if (tooltip.isEmpty())
        QToolTip::hideText();
    else
        QToolTip::showText(event->globalPosition().toPoint(), tooltip, this);
    QWidget::mouseMoveEvent(event);
}

void Timeline::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_selectionStart >= 0 && m_selectionEnd >= 0) {
        const int start = std::min(m_selectionStart, m_selectionEnd);
        const int end = std::max(m_selectionStart, m_selectionEnd);
        m_selectionStart = m_selectionEnd = -1;
        update();
        const qint64 startUtc = m_histogram.start + static_cast<qint64>(start) * m_histogram.step;
        const qint64 endUtc = m_histogram.start + static_cast<qint64>(end + 1) * m_histogram.step;
        emit periodChosen(startUtc, endUtc);
    }
    QWidget::mouseReleaseEvent(event);
}

void Timeline::leaveEvent(QEvent* event)
{
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
    QToolTip::hideText();
    QWidget::leaveEvent(event);
}

} // namespace lh::ui
