// Interactive charts of the interface (hover, click); drawing delegated to render::Charts.
#pragma once

#include "render/Charts.h"

#include <QWidget>

#include <array>

namespace lh::ui {

class ChartView : public QWidget
{
    Q_OBJECT

public:
    explicit ChartView(QWidget* parent = nullptr);
    void setClickable(bool clickable) { m_clickable = clickable; }
    [[nodiscard]] QSize sizeHint() const override { return {360, 220}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {200, 140}; }

signals:
    void elementClicked(int index);

protected:
    virtual void draw(QPainter& painter, const QRectF& zone, const render::ChartStyle& style, int hover,
                      QList<render::Zone>* zones) = 0;
    void dataModified();

    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int zoneAt(const QPointF& point) const;

    QList<render::Zone> m_zones;
    int m_hover = -1;
    bool m_clickable = true;
};

class TimelineChart : public ChartView
{
    Q_OBJECT

public:
    using ChartView::ChartView;
    void set(const render::DateSeries& series);
    [[nodiscard]] const render::Timeline& timeline() const { return m_timeline; }

protected:
    void draw(QPainter& painter, const QRectF& zone, const render::ChartStyle& style, int hover,
              QList<render::Zone>* zones) override;

private:
    render::Timeline m_timeline;
};

class HoursChart : public ChartView
{
    Q_OBJECT

public:
    using ChartView::ChartView;
    void set(const std::array<qint64, 24>& values);

protected:
    void draw(QPainter& painter, const QRectF& zone, const render::ChartStyle& style, int hover,
              QList<render::Zone>* zones) override;

private:
    QList<qint64> m_values;
};

class DonutChart : public ChartView
{
    Q_OBJECT

public:
    using ChartView::ChartView;
    void set(const QList<render::Element>& elements);
    [[nodiscard]] const QList<render::Element>& elements() const { return m_elements; }

protected:
    void draw(QPainter& painter, const QRectF& zone, const render::ChartStyle& style, int hover,
              QList<render::Zone>* zones) override;

private:
    QList<render::Element> m_elements;
};

class RankingChart : public ChartView
{
    Q_OBJECT

public:
    using ChartView::ChartView;
    void set(const QList<render::Element>& elements, bool elideLeft = false);
    [[nodiscard]] const QList<render::Element>& elements() const { return m_elements; }

protected:
    void draw(QPainter& painter, const QRectF& zone, const render::ChartStyle& style, int hover,
              QList<render::Zone>* zones) override;

private:
    QList<render::Element> m_elements;
    bool m_elideLeft = false;
};

class HeatmapChart : public ChartView
{
    Q_OBJECT

public:
    using ChartView::ChartView;
    void set(const render::WeekMatrix& matrix);

protected:
    void draw(QPainter& painter, const QRectF& zone, const render::ChartStyle& style, int hover,
              QList<render::Zone>* zones) override;

private:
    render::WeekMatrix m_matrix{};
};

} // namespace lh::ui
