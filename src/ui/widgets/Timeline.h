// Interactive timeline: event histogram, period selection with the mouse.
#pragma once

#include "core/Filters.h"
#include "core/Queries.h"
#include "render/Charts.h"
#include "ui/Executor.h"

#include <QWidget>

namespace lh::ui {

class Workspace;

class Timeline : public QWidget
{
    Q_OBJECT

public:
    explicit Timeline(Workspace* workspace, QWidget* parent = nullptr);

    void setFilters(const Filters& filters);
    [[nodiscard]] QSize sizeHint() const override { return {640, 118}; }
    [[nodiscard]] QSize minimumSizeHint() const override { return {240, 90}; }

signals:
    void periodChosen(qint64 startUtc, qint64 endUtc);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void load();
    void prepareLabels();
    [[nodiscard]] int binAt(double x) const;

    Workspace* m_workspace;
    Filters m_filters;
    queries::Histogram m_histogram;
    QList<qint64> m_values;
    QList<qint64> m_alerts;
    QStringList m_labels;
    QStringList m_tooltips;
    QList<render::Zone> m_zones;
    QRectF m_plot;
    TokenPtr m_token;
    bool m_loading = false;
    int m_hover = -1;
    int m_selectionStart = -1;
    int m_selectionEnd = -1;
};

} // namespace lh::ui
