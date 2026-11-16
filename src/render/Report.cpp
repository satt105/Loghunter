#include "Report.h"

#include "Charts.h"
#include "core/Actions.h"
#include "core/Catalog.h"
#include "core/Mitre.h"
#include "core/Format.h"
#include "core/DateTime.h"
#include "loghunter_version.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QLocale>
#include <QPainter>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSvgGenerator>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextObjectInterface>

#include <functional>
#include <stdexcept>
#include <vector>

namespace lh::report {

namespace internal {

constexpr int WIDTH = 680;

struct Chart
{
    int width = WIDTH;
    int height = 220;
    std::function<void(QPainter&, const QRectF&)> draw;
};

constexpr int CHART_TYPE = QTextFormat::UserObject + 1;
constexpr int INDEX_PROPERTY = QTextFormat::UserProperty + 1;

class ChartObject : public QObject, public QTextObjectInterface
{
    Q_OBJECT
    Q_INTERFACES(QTextObjectInterface)

public:
    ChartObject(const std::vector<Chart>& charts, QObject* parent)
        : QObject(parent), m_charts(charts)
    {
    }

    QSizeF intrinsicSize(QTextDocument* document, int, const QTextFormat& format) override
    {
        const Chart* chart = find(format);
        if (!chart)
            return {};
        const double scale = documentScale(document);
        return {chart->width * scale, chart->height * scale};
    }

    void drawObject(QPainter* painter, const QRectF& rectangle, QTextDocument*, int, const QTextFormat& format) override
    {
        const Chart* chart = find(format);
        if (!chart)
            return;
        const double scale = rectangle.width() / chart->width;
        painter->save();
        painter->translate(rectangle.topLeft());
        painter->scale(scale, scale);
        chart->draw(*painter, QRectF(0, 0, chart->width, chart->height));
        painter->restore();
    }

private:
    const Chart* find(const QTextFormat& format) const
    {
        const int index = format.property(INDEX_PROPERTY).toInt();
        return index >= 0 && static_cast<size_t>(index) < m_charts.size()
                   ? &m_charts[static_cast<size_t>(index)]
                   : nullptr;
    }

    static double documentScale(QTextDocument* document)
    {
        const QPaintDevice* device = document->documentLayout()->paintDevice();
        return device ? device->logicalDpiX() / 96.0 : 1.0;
    }

    const std::vector<Chart>& m_charts;
};

} // namespace internal

namespace {

using internal::Chart;
using internal::WIDTH;
using internal::ChartObject;
using internal::INDEX_PROPERTY;
using internal::CHART_TYPE;

QString escape(const QString& text)
{
    return text.toHtmlEscaped();
}

const char* severityClass(Severity severity)
{
    switch (severity) {
    case Severity::Critical:
        return "critical";
    case Severity::High:
        return "high";
    case Severity::Medium:
        return "medium";
    case Severity::Low:
        break;
    }
    return "low";
}

const QString STYLE_WEB = QStringLiteral(R"(
:root { --text: #1f2937; --secondary: #6b7280; --border: #e5e7eb; --accent: #2563eb; }
* { box-sizing: border-box; }
body { margin: 0; background: #eef2f6; color: var(--text);
       font: 14px/1.5 "Segoe UI", system-ui, -apple-system, "Helvetica Neue", Arial, sans-serif; }
main { max-width: 960px; margin: 32px auto; background: #fff; padding: 40px 48px 32px; border-radius: 12px;
       box-shadow: 0 1px 3px rgba(15, 23, 42, .08), 0 8px 24px rgba(15, 23, 42, .06); }
h1 { font-size: 26px; margin: 0 0 4px; letter-spacing: -.01em; }
h2 { font-size: 19px; margin: 40px 0 12px; padding-bottom: 6px; border-bottom: 2px solid var(--border); }
h3 { font-size: 15px; margin: 24px 0 4px; }
.header { border-bottom: 3px solid var(--accent); padding-bottom: 16px; margin-bottom: 20px; }
.meta, .note, .label { color: var(--secondary); }
.meta { font-size: 13px; margin: 0; }
.note { font-size: 12.5px; }
table { width: 100%; border-collapse: collapse; font-size: 12.5px; margin: 8px 0 6px; }
th { text-align: left; background: #f1f5f9; color: #334155; font-weight: 600; padding: 7px 9px;
     border-bottom: 1px solid var(--border); }
td { padding: 6px 9px; border-bottom: 1px solid #eef2f6; vertical-align: top; }
.n { text-align: right; white-space: nowrap; font-variant-numeric: tabular-nums; }
table.infos td { border: none; padding: 3px 12px 3px 0; }
table.infos td.label { width: 170px; }
table.indicators td { border: 1px solid var(--border); padding: 12px 14px; width: 25%; }
.value { font-size: 22px; font-weight: 600; line-height: 1.2; }
.severity { font-weight: 600; white-space: nowrap; }
.critical { color: #b91c1c; } .high { color: #c2410c; } .medium { color: #a16207; } .low { color: #1d4ed8; }
.card { border: 1px solid var(--border); border-radius: 10px; padding: 14px 16px 10px; margin: 12px 0; }
.card h4 { margin: 0 0 8px; font-size: 13px; color: #334155; }
svg.chart { display: block; width: 100%; height: auto; }
ul { padding-left: 20px; } li { margin: 3px 0; }
footer { margin-top: 40px; padding-top: 12px; border-top: 1px solid var(--border); color: var(--secondary);
         font-size: 12px; }
@media print {
  body { background: #fff; } main { box-shadow: none; margin: 0; padding: 0; max-width: none; border-radius: 0; }
  h2, h3 { break-after: avoid; } tr, .card { break-inside: avoid; }
}
)");

const QString STYLE_PDF = QStringLiteral(R"(
body { font-size: 9pt; color: #1f2937; }
h1 { font-size: 18pt; font-weight: 600; color: #111827; margin-bottom: 2px; }
h2 { font-size: 13pt; font-weight: 600; color: #1e3a8a; margin-top: 16px; margin-bottom: 6px; }
h3 { font-size: 10.5pt; font-weight: 600; margin-top: 12px; margin-bottom: 2px; }
h4 { font-size: 9.5pt; font-weight: 600; color: #334155; margin-top: 8px; margin-bottom: 2px; }
p { margin-top: 2px; margin-bottom: 4px; }
.meta, .note, .label { color: #6b7280; }
table { border-collapse: collapse; }
th { background-color: #eef2f7; color: #334155; font-weight: 600; text-align: left; border: 1px solid #e2e8f0; }
td { border: 1px solid #e5e7eb; }
table.infos td { border: none; }
.n { text-align: right; }
.value { font-size: 14pt; font-weight: 600; }
.severity { font-weight: 600; }
.critical { color: #b91c1c; } .high { color: #c2410c; } .medium { color: #a16207; } .low { color: #1d4ed8; }
)");

// ── Document construction ────────────────────────────────────────────────────

class Builder
{
public:
    Builder(const Data& data, const Options& options, bool pdf)
        : m_data(data), m_options(options), m_pdf(pdf), m_style(render::ChartStyle::forPrint())
    {
        m_style.transparency = !pdf;
    }

    QString build()
    {
        m_html.reserve(256 * 1024);
        m_html += QStringLiteral("<!DOCTYPE html>\n<html lang=\"%1\"><head><meta charset=\"utf-8\">"
                                 "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
                                 "<meta name=\"generator\" content=\"" LOGHUNTER_NAME_VERSION "\">")
                      .arg(QLocale().bcp47Name());
        m_html += QStringLiteral("<title>%1</title>").arg(escape(m_options.title));
        if (!m_pdf)
            m_html += QStringLiteral("<style>") + STYLE_WEB + QStringLiteral("</style>");
        m_html += QStringLiteral("</head><body><main>");
        header();
        if (m_data.summary.total == 0) {
            m_html += QStringLiteral("<p>%1</p>").arg(qtTrId("report.no_event_matches_scope"));
        } else {
            summary();
            if (m_options.charts)
                activity();
            if (m_options.rankings)
                rankings();
            if (m_options.alerts)
                alerts();
        }
        if (m_options.logs)
            logs();
        methodology();
        if (!m_pdf)
            m_html += QStringLiteral("<footer>%1 · %2</footer>")
                          .arg(qtTrId("report.generated_by").arg(QStringLiteral(LOGHUNTER_NAME_VERSION)),
                               QStringLiteral(LOGHUNTER_PUBLISHER));
        m_html += QStringLiteral("</main></body></html>\n");
        return m_html;
    }

    [[nodiscard]] const std::vector<Chart>& charts() const { return m_charts; }

private:
    struct Column
    {
        QString title;
        bool count = false;
    };

    [[nodiscard]] QString path(const QString& text) const
    {
        QString result = escape(text);
        const QString breakMark = m_pdf ? QString(QLatin1Char('\\')) + QChar(0x200B) : QStringLiteral("\\<wbr>");
        result.replace(QLatin1Char('\\'), breakMark);
        return result;
    }

    void table(const QList<Column>& columns, const QList<QStringList>& rows)
    {
        const QString rightAligned = QStringLiteral(" class=\"n\" align=\"right\"");
        m_html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\"><thead><tr>");
        for (const Column& column : columns)
            m_html += QStringLiteral("<th%1>%2</th>").arg(column.count ? rightAligned : QString(), escape(column.title));
        m_html += QStringLiteral("</tr></thead><tbody>");
        for (const QStringList& row : rows) {
            m_html += QStringLiteral("<tr>");
            for (qsizetype i = 0; i < row.size(); ++i) {
                const bool count = i < columns.size() && columns[i].count;
                m_html += QStringLiteral("<td%1>%2</td>").arg(count ? rightAligned : QString(), row[i]);
            }
            m_html += QStringLiteral("</tr>");
        }
        m_html += QStringLiteral("</tbody></table>");
    }

    void chart(const QString& title, Chart chart)
    {
        if (m_pdf) {
            constexpr int HEADER = 26;
            QFont font = m_style.font;
            font.setPixelSize(13);
            font.setBold(true);
            chart.draw = [drawing = std::move(chart.draw), title, font,
                          color = m_style.text](QPainter& p, const QRectF& zone) {
        p.save();
        p.setFont(font);
        p.setPen(color);
        p.drawText(QRectF(zone.left(), zone.top(), zone.width(), HEADER - 6), Qt::AlignLeft | Qt::AlignVCenter,
                   title);
        p.restore();
        drawing(p, zone.adjusted(0, HEADER, 0, 0));
    };
            chart.height += HEADER;
            m_html += QStringLiteral("<p><img src=\"chart:%1\" width=\"%2\" height=\"%3\"></p>")
                          .arg(m_charts.size())
                          .arg(chart.width)
                          .arg(chart.height);
        } else {
            m_html += QStringLiteral("<div class=\"card\"><h4>%1</h4>%2</div>").arg(escape(title), svg(title, chart));
        }
        m_charts.push_back(std::move(chart));
    }

    static QString svg(const QString& title, const Chart& chart)
    {
        QBuffer buffer;
        buffer.open(QIODevice::WriteOnly);
        QSvgGenerator generator;
        generator.setOutputDevice(&buffer);
        generator.setSize(QSize(chart.width, chart.height));
        generator.setViewBox(QRect(0, 0, chart.width, chart.height));
        generator.setResolution(96);
        generator.setTitle(title);
        {
            QPainter painter(&generator);
            chart.draw(painter, QRectF(0, 0, chart.width, chart.height));
        }
        QString text = QString::fromUtf8(buffer.data());
        const qsizetype start = text.indexOf(QLatin1String("<svg"));
        if (start < 0)
            return {};
        text.remove(0, start);
        const qsizetype tagEnd = text.indexOf(QLatin1Char('>'));
        QString tag = text.left(tagEnd);
        static const QRegularExpression dimensions(QStringLiteral(R"(\s(width|height)="[^"]*")"));
        tag.remove(dimensions);
        tag.insert(4, QStringLiteral(" class=\"chart\" role=\"img\""));
        return tag + text.mid(tagEnd);
    }

    void header()
    {
        const QDateTime& generation = m_data.generation;
        m_html += QStringLiteral("<div class=\"header\"><h1>%1</h1><p class=\"meta\">").arg(escape(m_options.title));
        QStringList meta;
        if (!m_options.project.isEmpty())
            meta << qtTrId("common.project_name").arg(escape(m_options.project));
        meta << qtTrId("report.footer_generated")
                    .arg(formatDate(generation.date()), generation.toString(QStringLiteral("HH:mm")),
                         QStringLiteral(LOGHUNTER_NAME_VERSION));
        if (!m_options.author.isEmpty())
            meta << qtTrId("report.analyst_name").arg(escape(m_options.author));
        m_html += meta.join(QStringLiteral(" · ")) + QStringLiteral("</p></div>");

        const auto& s = m_data.summary;
        const QString period = s.first.isEmpty()
                                   ? qtTrId("report.none")
                                   : qtTrId("common.period_from_to").arg(formatTimestamp(s.first, false),
                                                                  formatTimestamp(s.last, false));
        m_html += QStringLiteral("<table class=\"infos\" cellspacing=\"0\" cellpadding=\"2\">");
        auto info = [&](const QString& label, const QString& value) {
            m_html += QStringLiteral("<tr><td class=\"label\" width=\"150\">%1</td><td>%2</td></tr>")
                          .arg(escape(label), value);
        };
        const QString scope = m_data.filters.description(m_data.timeZone);
        info(qtTrId("report.scope"), escape(scope.left(1).toUpper() + scope.mid(1)));
        info(qtTrId("common.period_covered"), escape(period.left(1).toUpper() + period.mid(1)));
        info(qtTrId("common.time_zone"), escape(QString::fromLatin1(m_data.timeZone)));
        info(qtTrId("report.logs_analyzed"), formatNumber(m_data.sources.size()));
        m_html += QStringLiteral("</table>");
    }

    void summary()
    {
        const auto& s = m_data.summary;
        m_html += QStringLiteral("<h2>1. %1</h2>").arg(qtTrId("report.summary"));
        m_html += QStringLiteral(
            "<table class=\"indicators\" width=\"100%\" cellspacing=\"0\" cellpadding=\"8\"><tr>");
        auto indicator = [&](qint64 value, const QString& label) {
            m_html += QStringLiteral("<td width=\"25%\"><span class=\"value\">%1</span><br>"
                                     "<span class=\"label\">%2</span></td>")
                          .arg(formatNumber(value), escape(label));
        };
        const int fired = findingCount(m_data.detections);
        indicator(s.total, qtTrId("report.unit_events", pluralCount(s.total)));
        indicator(s.users, qtTrId("report.unit_users", pluralCount(s.users)));
        indicator(s.machines, qtTrId("report.unit_machines", pluralCount(s.machines)));
        indicator(fired, qtTrId("report.unit_alerts", fired));
        m_html += QStringLiteral("</tr></table>");
        if (fired > 0) {
            const Severity maximum = maxSeverity(m_data.detections);
            m_html += QStringLiteral("<p>%1 <span class=\"severity %2\">%3</span></p>")
                          .arg(escape(qtTrId("report.risk_score").arg(riskScore(m_data.detections))),
                               QLatin1String(severityClass(maximum)), escape(severityLabel(maximum)));
        }

        if (s.fileAccesses > 0) {
            QList<QStringList> rows;
            for (const Category& category : categories()) {
                const qint64 count = s.byCategory.value(QLatin1String(category.key), 0);
                if (count == 0)
                    continue;
                const double part = static_cast<double>(count) / static_cast<double>(s.fileAccesses);
                rows.append(
                    {escape(categoryLabel(category)), formatNumber(count), formatPercentage(part)});
            }
            m_html += QStringLiteral("<h3>%1</h3>").arg(qtTrId("report.action_breakdown"));
            table({{qtTrId("common.action")}, {qtTrId("common.events"), true}, {qtTrId("report.share"), true}},
                  rows);
        }

        if (!m_data.detections.isEmpty()) {
            m_html += QStringLiteral("<h3>%1</h3>").arg(qtTrId("common.alerts"));
            QList<QStringList> alerts;
            for (const Detection& detection : m_data.detections) {
                if (detection.sigma && detection.count == 0)
                    continue;
                const QString severity =
                    detection.count > 0
                        ? QStringLiteral("<span class=\"severity %1\">%2</span>")
                              .arg(QLatin1String(severityClass(detection.severity)), severityLabel(detection.severity))
                        : QString();
                const QString findings =
                    detection.count > 0
                        ? escape(detection.quantity())
                        : QStringLiteral("<span class=\"note\">%1</span>").arg(qtTrId("report.no_finding"));
                if (m_data.parameters.mitre)
                    alerts.append({escape(detection.title), severity, escape(mitre::summarize(detection.tags)),
                                   findings});
                else
                    alerts.append({escape(detection.title), severity, findings});
            }
            if (m_data.parameters.mitre)
                table({{qtTrId("report.detection")},
                       {qtTrId("report.severity")},
                       {qtTrId("report.mitre")},
                       {qtTrId("report.findings"), true}},
                      alerts);
            else
                table({{qtTrId("report.detection")}, {qtTrId("report.severity")}, {qtTrId("report.findings"), true}},
                      alerts);
        }
    }

    void activity()
    {
        const auto& s = m_data.summary;
        const render::ChartStyle style = m_style;
        m_html += QStringLiteral("<h2>2. %1</h2>").arg(qtTrId("report.activity"));

        const render::Timeline timeline = render::aggregate(s.byDay);
        const QString timelineTitle = timeline.granularity == render::Granularity::Day
                                          ? qtTrId("report.activity_by_day")
                                      : timeline.granularity == render::Granularity::Week
                                          ? qtTrId("report.activity_by_week")
                                          : qtTrId("report.activity_by_month");
        chart(timelineTitle, {WIDTH, 210, [timeline, style](QPainter& p, const QRectF& zone) {
                                         render::drawTimeline(p, zone, timeline, style);
                                     }});

        QStringList hours;
        QList<qint64> values;
        for (int h = 0; h < 24; ++h) {
            hours << QString::number(h);
            values << s.byHour[static_cast<size_t>(h)];
        }
        chart(qtTrId("report.activity_by_hour_of_day"),
              {WIDTH, 190, [hours, values, style](QPainter& p, const QRectF& zone) {
                   render::drawBars(p, zone, hours, values, style);
               }});

        if (s.fileAccesses > 0) {
            const QList<render::Element> distribution = render::elementsByCategory(s.byCategory);
            chart(qtTrId("common.action_types"),
                  {WIDTH, 190, [distribution, style](QPainter& p, const QRectF& zone) {
                       render::drawDonut(p, zone, distribution, style);
                   }});
        }

        const render::WeekMatrix matrix = s.byDayHour;
        chart(qtTrId("report.activity_by_weekday_and_hour"),
              {WIDTH, 200, [matrix, style](QPainter& p, const QRectF& zone) {
                   render::drawHeatmap(p, zone, matrix, style);
               }});

        auto ranking = [&](const QString& title, const queries::Series& series, bool elideLeft) {
            if (series.isEmpty())
                return;
            const QList<render::Element> elements = render::elementsFromSeries(series);
            const int height =
                static_cast<int>(static_cast<double>(elements.size()) * render::rankingRowHeight(style)) + 4;
            chart(title, {WIDTH, height, [elements, style, elideLeft](QPainter& p, const QRectF& zone) {
                                  render::drawRanking(p, zone, elements, style, -1, nullptr, elideLeft);
                              }});
        };
        queries::Series events;
        for (const queries::EventRanking& e : s.topEvents)
            events.append({eventLabel(e.identifier, e.channel, e.provider), e.total});
        ranking(qtTrId("summary.most_frequent_events"), events, false);
        ranking(qtTrId("common.most_active_users"), s.topUsers, false);
        ranking(s.topSuspiciousIps.isEmpty() ? qtTrId("summary.most_active_ips") : qtTrId("summary.suspicious_ips"),
                s.topSuspiciousIps.isEmpty() ? s.topIp : s.topSuspiciousIps, false);
        ranking(qtTrId("report.most_active_processes"), s.topProcesses, true);
    }

    void rankings()
    {
        m_html += QStringLiteral("<h2>3. %1</h2>").arg(qtTrId("report.users_files_folders"));
        if (!m_data.users.isEmpty()) {
            m_html += QStringLiteral("<h3>%1</h3>").arg(qtTrId("common.users"));
            QList<QStringList> rows;
            for (const auto& u : m_data.users)
                rows.append({escape(u.name.isEmpty() ? qtTrId("common.unknown") : u.name)
                                   + (u.isSystem ? QStringLiteral(" <span class=\"note\">(%1)</span>")
                                                   .arg(qtTrId("report.system_account"))
                                                 : QString()),
                             formatNumber(u.total), escape(formatTimestamp(u.last, false))});
            table({{qtTrId("common.user")},
                   {qtTrId("common.events"), true},
                   {qtTrId("common.last_activity")}},
                  rows);
        }
        if (!m_data.files.isEmpty()) {
            m_html += QStringLiteral("<h3>%1</h3>").arg(qtTrId("common.most_accessed_files"));
            QList<QStringList> rows;
            for (const auto& f : m_data.files)
                rows.append({path(f.path), formatNumber(f.total), formatNumber(f.users),
                             escape(categoryLabel(f.dominantCategory)),
                             escape(formatTimestamp(f.last, false))});
            table({{qtTrId("common.file")},
                   {qtTrId("common.events"), true},
                   {qtTrId("common.users"), true},
                   {qtTrId("report.main_action")},
                   {qtTrId("common.last_access")}},
                  rows);
        }
        if (!m_data.folders.isEmpty()) {
            m_html += QStringLiteral("<h3>%1</h3>").arg(qtTrId("report.most_active_folders"));
            QList<QStringList> rows;
            for (const auto& d : m_data.folders)
                rows.append({path(d.path.isEmpty() ? qtTrId("report.root") : d.path),
                             formatNumber(d.total), formatNumber(d.writes), formatNumber(d.deletions),
                             formatNumber(d.permissions), formatNumber(d.users)});
            table({{qtTrId("common.folder")},
                   {qtTrId("common.events"), true},
                   {qtTrId("common.writes"), true},
                   {qtTrId("common.deletions"), true},
                   {qtTrId("report.permissions"), true},
                   {qtTrId("common.users"), true}},
                  rows);
        }
    }

    void alerts()
    {
        const AlertParameters& p = m_data.parameters;
        m_html += QStringLiteral("<h2 style=\"page-break-before: always\">4. %1</h2>")
                      .arg(qtTrId("report.detailed_alerts"));
        const QStringList settings{
            qtTrId("report.setting_deletions").arg(p.deletionThreshold),
            (p.weekendOffHours ? qtTrId("report.setting_hours_weekend") : qtTrId("report.setting_hours"))
                .arg(p.openingHour)
                .arg(p.closingHour),
            qtTrId("report.setting_bursts").arg(p.burstActions).arg(p.burstSeconds),
            p.ignoreSystemAccounts ? qtTrId("report.setting_system_accounts_ignored")
                                   : qtTrId("report.setting_system_accounts_included")};
        m_html += QStringLiteral("<p class=\"note\">%1</p>")
                      .arg(escape(qtTrId("report.settings_list").arg(settings.join(QStringLiteral(" · ")))));
        bool none = true;
        for (const Detection& detection : m_data.detections) {
            if (detection.count == 0)
                continue;
            none = false;
            m_html += QStringLiteral("<h3>%1 · <span class=\"severity %2\">%3</span></h3><p>%4 <b>%5</b>.</p>")
                          .arg(escape(detection.title), QLatin1String(severityClass(detection.severity)),
                               severityLabel(detection.severity), escape(detection.description),
                               escape(detection.quantity()));
            QStringList notes;
            const QString techniques = m_data.parameters.mitre ? mitre::summarize(detection.tags) : QString();
            if (!techniques.isEmpty())
                notes << qtTrId("report.mitre_value").arg(techniques);
            notes << (detection.sigma ? qtTrId("alerts.origin_sigma") : qtTrId("alerts.origin_builtin"));
            m_html += QStringLiteral("<p class=\"note\">%1</p>").arg(escape(notes.join(QStringLiteral(" · "))));
            QList<Column> columns;
            for (const ResultColumn& column : detection.columns)
                columns.append({column.title, column.type == ResultColumn::Type::Number});
            QList<QStringList> rows;
            const qsizetype limit =
                std::min<qsizetype>(detection.rows.size(), std::max(1, m_options.rowsPerAlert));
            for (qsizetype i = 0; i < limit; ++i) {
                QStringList cells;
                const QVariantList& row = detection.rows[i];
                for (qsizetype c = 0; c < row.size() && c < detection.columns.size(); ++c) {
                    const auto type = detection.columns[c].type;
                    const QString text = formatCell(row[c], type);
                    cells << (type == ResultColumn::Type::Path ? path(text) : escape(text));
                }
                rows.append(cells);
            }
            table(columns, rows);
            if (detection.count > limit)
                m_html +=
                    QStringLiteral("<p class=\"note\">%1</p>")
                        .arg(escape(qtTrId("report.other_findings", pluralCount(detection.count - limit))));
        }
        if (none)
            m_html += QStringLiteral("<p>%1</p>").arg(qtTrId("report.no_suspicious_activity_in_scope"));
    }

    void logs()
    {
        m_html += QStringLiteral("<h2>%1%2</h2>")
                      .arg(m_data.summary.total == 0 ? QString() : QStringLiteral("5. "),
                           qtTrId("report.logs_analyzed"));
        if (m_data.sources.isEmpty()) {
            m_html += QStringLiteral("<p>%1</p>").arg(qtTrId("report.no_log_imported"));
            return;
        }
        QList<QStringList> rows;
        for (const Source& source : m_data.sources)
            rows.append({escape(source.name),
                         escape(source.machine),
                         escape(formatDateTime(source.importedAt.toLocalTime())),
                         formatNumber(source.records), formatNumber(source.events),
                         formatNumber(source.duplicates)});
        table({{qtTrId("common.log")},
               {qtTrId("common.machine")},
               {qtTrId("common.imported_on")},
               {qtTrId("common.records"), true},
               {qtTrId("common.events_kept"), true},
               {qtTrId("common.duplicates"), true}},
              rows);
    }

    void methodology()
    {
        m_html += QStringLiteral("<h2>%1</h2><ul>").arg(qtTrId("report.methodology"));
        const QStringList points{
            qtTrId("report.methodology_sources"),
            qtTrId("report.methodology_classification"),
            qtTrId("report.methodology_sigma"),
            qtTrId("report.methodology_deletions"),
            qtTrId("report.methodology_time_zone")
                .arg(QString::fromLatin1(m_data.timeZone)),
            qtTrId("report.methodology_limits")};
        for (const QString& point : points)
            m_html += QStringLiteral("<li>%1</li>").arg(escape(point));
        m_html += QStringLiteral("</ul>");
    }

    const Data& m_data;
    const Options& m_options;
    bool m_pdf;
    render::ChartStyle m_style;
    QString m_html;
    std::vector<Chart> m_charts;
};

// ── PDF ──────────────────────────────────────────────────────────────────────

void insertCharts(QTextDocument& document)
{
    QList<QPair<int, int>> locations;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            const QTextCharFormat format = fragment.charFormat();
            if (!format.isImageFormat())
                continue;
            const QString name = format.toImageFormat().name();
            if (name.startsWith(QLatin1String("chart:")))
                locations.append({fragment.position(), name.mid(10).toInt()});
        }
    }
    for (auto it = locations.crbegin(); it != locations.crend(); ++it) {
        QTextCursor cursor(&document);
        cursor.setPosition(it->first);
        cursor.setPosition(it->first + 1, QTextCursor::KeepAnchor);
        QTextCharFormat object;
        object.setObjectType(CHART_TYPE);
        object.setProperty(INDEX_PROPERTY, it->second);
        cursor.insertText(QString(QChar::ObjectReplacementCharacter), object);
    }
}

void writePdf(const QString& html, const std::vector<Chart>& charts, const Options& options,
              const Data& data, QIODevice& output)
{
    QPdfWriter pdf(&output);
    pdf.setResolution(300);
    pdf.setPageSize(QPageSize(QPageSize::A4));
    pdf.setPageMargins(QMarginsF(0, 0, 0, 0));
    pdf.setTitle(options.title);
    pdf.setCreator(QStringLiteral(LOGHUNTER_NAME_VERSION));

    const double mm = pdf.resolution() / 25.4;
    const QRectF page(QPointF(0, 0), QSizeF(pdf.width(), pdf.height()));
    const QMarginsF margins(15 * mm, 15 * mm, 15 * mm, 18 * mm);
    const QRectF body = page.marginsRemoved(margins);

    QTextDocument document;
    document.documentLayout()->setPaintDevice(&pdf);
    auto* objects = new ChartObject(charts, &document);
    document.documentLayout()->registerHandler(CHART_TYPE, objects);
    QFont font(QStringLiteral("Segoe UI"));
    font.setStyleHint(QFont::SansSerif);
    font.setPointSizeF(9);
    document.setDefaultFont(font);
    document.setDefaultStyleSheet(STYLE_PDF);
    document.setDocumentMargin(0);
    document.setHtml(html);
    insertCharts(document);
    document.setPageSize(body.size());

    const int pages = document.pageCount();
    QPainter painter;
    if (!painter.begin(&pdf))
        throw std::runtime_error(qtTrId("report.error_pdf").toStdString());
    QFont footerFont = font;
    footerFont.setPointSizeF(7.5);
    const QString footer = QStringLiteral("%1 · %2").arg(options.title, formatDateTime(data.generation));
    for (int i = 0; i < pages; ++i) {
        if (i > 0)
            pdf.newPage();
        painter.save();
        painter.translate(body.left(), body.top() - i * body.height());
        document.drawContents(&painter, QRectF(0, i * body.height(), body.width(), body.height()));
        painter.restore();

        const double y = page.height() - margins.bottom() + 5 * mm;
        painter.setPen(QPen(QColor(0xe5, 0xe7, 0xeb), 0.3 * mm));
        painter.drawLine(QPointF(body.left(), y), QPointF(body.right(), y));
        painter.setFont(footerFont);
        painter.setPen(QColor(0x6b, 0x72, 0x80));
        const QRectF banner(body.left(), y + 1.5 * mm, body.width(), 6 * mm);
        painter.drawText(banner, Qt::AlignLeft | Qt::AlignTop, footer);
        painter.drawText(banner, Qt::AlignRight | Qt::AlignTop, qtTrId("report.page_number").arg(i + 1).arg(pages));
    }
    painter.end();
}

} // namespace

Data collect(Context& context, const Filters& filters, const AlertParameters& parameters,
             const QByteArray& timeZone)
{
    Data data;
    data.filters = filters;
    data.parameters = parameters;
    data.timeZone = timeZone;
    data.generation = QDateTime::currentDateTime();
    data.sources = db::sources(context.connection);
    data.summary = queries::summary(context, filters);
    if (data.summary.total == 0)
        return data;
    data.detections = detect(context, filters, parameters);
    data.users = queries::users(context, filters, 15);
    data.files = queries::files(context, filters, 15);
    data.folders = queries::folders(context, filters, 15);
    context.throwIfCancelled();
    return data;
}

QString html(const Data& data, const Options& options)
{
    return Builder(data, options, false).build();
}

void write(const Data& data, const Options& options, const QString& path)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(
            qtTrId("report.error_write").arg(path, file.errorString()).toStdString());
    if (options.format == Options::Format::Html) {
        file.write(html(data, options).toUtf8());
    } else {
        Builder builder(data, options, true);
        const QString content = builder.build();
        writePdf(content, builder.charts(), options, data, file);
    }
    if (!file.commit())
        throw std::runtime_error(
            qtTrId("report.error_save").arg(path, file.errorString()).toStdString());
}

} // namespace lh::report

#include "Report.moc"
