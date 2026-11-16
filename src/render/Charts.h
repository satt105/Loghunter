// Chart drawing (QPainter), shared by the screen and the reports.
#pragma once

#include "core/Format.h"

#include <QColor>
#include <QDate>
#include <QFont>
#include <QList>
#include <QMap>
#include <QPainterPath>
#include <QPair>
#include <QPalette>
#include <QRectF>
#include <QString>
#include <QStringList>

#include <array>

class QPainter;

namespace lh::render {

QColor blend(const QColor& a, const QColor& b, double t);

struct ChartStyle
{
    QColor text;
    QColor secondaryText;
    QColor grid;
    QColor background;
    QColor accent;
    QColor strongAccent;
    QFont font;
    bool transparency = true;

    static ChartStyle fromPalette(const QPalette& palette, const QFont& font);
    static ChartStyle forPrint();
};

struct Zone
{
    QRectF rectangle;
    QPainterPath shape;
    int index = -1;
    QString tooltip;

    [[nodiscard]] bool contains(const QPointF& point) const
    {
        return shape.isEmpty() ? rectangle.contains(point) : shape.contains(point);
    }
};

struct Element
{
    QString label;
    qint64 value = 0;
    QColor color;
    QString key;
};

QList<Element> elementsFromSeries(const QList<QPair<QString, qint64>>& series);
QList<Element> elementsByCategory(const QMap<QString, qint64>& byCategory);

using DateSeries = QList<QPair<QDate, qint64>>;
using WeekMatrix = std::array<std::array<qint64, 24>, 7>;

enum class Granularity { Day, Week, Month };
struct Period
{
    QDate start;
    QDate end;
    qint64 value = 0;
};
struct Timeline
{
    Granularity granularity = Granularity::Day;
    QList<Period> periods;
};
Timeline aggregate(const DateSeries& series);
QString periodLabel(const Period& period, Granularity granularity);

void drawTimeline(QPainter& p, const QRectF& zone, const Timeline& timeline,
                  const ChartStyle& style, int hover = -1, QList<Zone>* zones = nullptr);

void drawBars(QPainter& p, const QRectF& zone, const QStringList& labels, const QList<qint64>& values,
              const ChartStyle& style, int hover = -1, QList<Zone>* zones = nullptr,
              const QStringList& tooltips = {});

QRectF drawHistogram(QPainter& p, const QRectF& zone, const QList<qint64>& values, const QList<qint64>& alerts,
                     const QStringList& labels, const QStringList& tooltips, const ChartStyle& style,
                     int hover = -1, QList<Zone>* zones = nullptr);

void drawDonut(QPainter& p, const QRectF& zone, const QList<Element>& elements, const ChartStyle& style,
               int hover = -1, QList<Zone>* zones = nullptr);

double rankingRowHeight(const ChartStyle& style);

void drawRanking(QPainter& p, const QRectF& zone, const QList<Element>& elements, const ChartStyle& style,
                 int hover = -1, QList<Zone>* zones = nullptr, bool elideLeft = false);

void drawHeatmap(QPainter& p, const QRectF& zone, const WeekMatrix& matrix, const ChartStyle& style,
                 int hover = -1, QList<Zone>* zones = nullptr);

void drawMessage(QPainter& p, const QRectF& zone, const QString& message, const ChartStyle& style);

} // namespace lh::render
