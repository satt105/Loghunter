#include "DashboardPage.h"

#include "core/Catalog.h"
#include "core/Format.h"
#include "core/DateTime.h"
#include "ui/Workspace.h"
#include "ui/Icons.h"
#include "ui/Settings.h"
#include "ui/Theme.h"
#include "ui/widgets/Components.h"
#include "ui/widgets/ChartViews.h"

#include <QGridLayout>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace lh::ui {

DashboardPage::DashboardPage(Workspace* workspace, QWidget* parent) : Page(workspace, parent)
{
    m_stack = new QStackedWidget(this);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_stack);

    m_empty = new EmptyState("dashboard", qtTrId("common.no_data_to_analyze"),
                             qtTrId("summary.empty_explanation"),
                             m_stack);
    connect(m_empty->addButton(qtTrId("common.import_logs"), "import", true), &QPushButton::clicked, this,
            &DashboardPage::importLogs);
    m_noResult = new EmptyState("filters", qtTrId("summary.no_event_for_filters"),
                                qtTrId("summary.no_event_advice"), m_stack);
    connect(m_noResult->addButton(qtTrId("summary.reset_filters"), "refresh", true),
            &QPushButton::clicked, workspace, &Workspace::resetFilters);

    auto* scrollArea = new QScrollArea(m_stack);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    m_content = new QWidget(scrollArea);
    scrollArea->setWidget(m_content);
    auto* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(24, 4, 24, 24);
    layout->setSpacing(16);

    auto* indicators = new QHBoxLayout;
    indicators->setSpacing(12);
    m_events = new IndicatorTile("activity", qtTrId("common.events"), m_content);
    m_users = new IndicatorTile("users", qtTrId("common.users"), m_content);
    m_computers = new IndicatorTile("machine", qtTrId("summary.computers"), m_content);
    m_period = new IndicatorTile("calendar", qtTrId("common.period_covered"), m_content);
    for (IndicatorTile* tile : {m_events, m_users, m_computers, m_period})
        indicators->addWidget(tile);
    m_events->setClickable(true);
    m_events->setToolTip(qtTrId("summary.show_events"));
    m_users->setClickable(true);
    connect(m_events, &IndicatorTile::clicked, this,
            [this] { m_workspace->navigate({Navigation::Target::Events, {}}); });
    connect(m_users, &IndicatorTile::clicked, this,
            [this] { m_workspace->navigate({Navigation::Target::User, {}}); });
    layout->addLayout(indicators);

    auto* banner = new Card({}, m_content);
    auto* bannerRow = new QHBoxLayout;
    bannerRow->setSpacing(14);
    m_alertsIcon = new QLabel(banner);
    m_alertsIcon->setFixedSize(28, 28);
    bannerRow->addWidget(m_alertsIcon, 0, Qt::AlignVCenter);
    auto* alertTexts = new QVBoxLayout;
    alertTexts->setSpacing(2);
    m_alertsTitle = new Label({}, Label::Role::Title, banner);
    m_alertsDetail = new Label({}, Label::Role::Secondary, banner);
    m_alertsDetail->setWordWrap(true);
    alertTexts->addWidget(m_alertsTitle);
    alertTexts->addWidget(m_alertsDetail);
    bannerRow->addLayout(alertTexts, 1);
    m_viewAlerts = new QPushButton(icon("alerts"), qtTrId("summary.see_alerts"), banner);
    m_viewAlerts->setAutoDefault(false);
    m_viewAlerts->setMinimumHeight(32);
    bannerRow->addWidget(m_viewAlerts, 0, Qt::AlignVCenter);
    banner->content()->addLayout(bannerRow);
    layout->addWidget(banner);
    connect(m_viewAlerts, &QPushButton::clicked, this,
            [this] { m_workspace->navigate({Navigation::Target::Alerts, {}}); });

    m_timeline = new TimelineChart(m_content);
    m_actions = new DonutChart(m_content);
    m_hours = new HoursChart(m_content);
    m_week = new HeatmapChart(m_content);
    m_userRanking = new RankingChart(m_content);
    m_processRanking = new RankingChart(m_content);
    m_eventRanking = new RankingChart(m_content);
    m_ipRanking = new RankingChart(m_content);
    m_eventRanking->setMinimumHeight(260);
    m_ipRanking->setMinimumHeight(260);
    m_timeline->setMinimumHeight(220);
    m_actions->setMinimumHeight(220);
    m_hours->setMinimumHeight(200);
    m_week->setMinimumHeight(200);
    m_userRanking->setMinimumHeight(260);
    m_processRanking->setMinimumHeight(260);

    m_timelineCard = cardWith(qtTrId("common.activity_over_time"), m_timeline, m_content,
                              qtTrId("summary.click_to_select_period"));
    m_actionsCard = cardWith(qtTrId("common.action_types"), m_actions, m_content, qtTrId("summary.click_to_filter_action"));
    m_hoursCard =
        cardWith(qtTrId("summary.activity_by_hour"), m_hours, m_content, qtTrId("summary.click_to_select_hour"));
    m_weekCard =
        cardWith(qtTrId("summary.activity_by_day_and_hour"), m_week, m_content, qtTrId("summary.activity_by_day_and_hour_help"));
    m_usersCard = cardWith(qtTrId("common.most_active_users"), m_userRanking, m_content,
                           qtTrId("summary.click_to_see_user_activity"));
    m_processCard = cardWith(qtTrId("summary.most_used_programs"), m_processRanking, m_content,
                             qtTrId("summary.click_to_filter_program"));
    m_eventsCard = cardWith(qtTrId("summary.most_frequent_events"), m_eventRanking, m_content,
                            qtTrId("summary.click_to_filter_event"));
    m_ipCard = cardWith(qtTrId("summary.suspicious_ips"), m_ipRanking, m_content, qtTrId("summary.suspicious_ips_help"));
    m_grid = new QGridLayout;
    m_grid->setSpacing(16);
    layout->addLayout(m_grid);
    m_moreCharts = new LinkButton({}, m_content);
    connect(m_moreCharts, &LinkButton::clicked, this, [this] {
        showDetailedCharts(!m_detailedCharts);
        settings::write(settings::key::DETAILED_CHARTS, m_detailedCharts);
    });
    layout->addWidget(m_moreCharts, 0, Qt::AlignLeft);
    layout->addStretch();

    m_stack->addWidget(scrollArea);
    m_stack->addWidget(m_empty);
    m_stack->addWidget(m_noResult);
    m_loading = new LoadingIndicator(scrollArea);

    connect(m_timeline, &ChartView::elementClicked, this, [this](int index) {
        const auto& periods = m_timeline->timeline().periods;
        if (index < 0 || index >= periods.size())
            return;
        m_workspace->modifyFilters([&](Filters& f) {
            f.since = periods[index].start;
            f.until = periods[index].end;
        });
    });
    connect(m_actions, &ChartView::elementClicked, this, [this](int index) {
        if (index < 0 || index >= m_actions->elements().size())
            return;
        const QString key = m_actions->elements()[index].key;
        m_workspace->modifyFilters([&](Filters& f) {
            if (m_distributionByLog)
                f.channel = key;
            else
                f.category = key;
        });
    });
    connect(m_eventRanking, &ChartView::elementClicked, this, [this](int index) {
        if (index < 0 || index >= m_eventRanking->elements().size())
            return;
        const QStringList key = m_eventRanking->elements()[index].key.split(QLatin1Char('\n'));
        m_workspace->modifyFilters([&](Filters& f) {
            f.events = key.value(0);
            f.channel = key.value(1);
        });
    });
    connect(m_ipRanking, &ChartView::elementClicked, this, [this](int index) {
        if (index < 0 || index >= m_ipRanking->elements().size())
            return;
        m_workspace->modifyFilters([&](Filters& f) {
            f.ip = m_ipRanking->elements()[index].key;
            f.exact = true;
        });
    });
    auto hourRange = [this](int hour) {
        m_workspace->modifyFilters([&](Filters& f) {
            f.startHour = hour;
            f.endHour = (hour + 1) % 24;
        });
    };
    connect(m_hours, &ChartView::elementClicked, this, hourRange);
    connect(m_week, &ChartView::elementClicked, this, [hourRange](int index) { hourRange(index % 24); });
    connect(m_userRanking, &ChartView::elementClicked, this, [this](int index) {
        if (index >= 0 && index < m_userRanking->elements().size())
            m_workspace->navigate({Navigation::Target::User, m_userRanking->elements()[index].key});
    });
    connect(m_processRanking, &ChartView::elementClicked, this, [this](int index) {
        if (index >= 0 && index < m_processRanking->elements().size())
            m_workspace->modifyFilters([&](Filters& f) { f.process = m_processRanking->elements()[index].key; });
    });

    arrange(true);
    showDetailedCharts(settings::read(settings::key::DETAILED_CHARTS, false).toBool());
}

void DashboardPage::showDetailedCharts(bool shown)
{
    m_detailedCharts = shown;
    m_weekCard->setVisible(shown);
    m_processCard->setVisible(shown);
    m_moreCharts->changeText(shown ? qtTrId("summary.hide_detailed_charts")
                                   : qtTrId("summary.show_more_charts"));
}

void DashboardPage::resizeEvent(QResizeEvent* event)
{
    Page::resizeEvent(event);
    arrange(event->size().width() >= 1000);
}

void DashboardPage::arrange(bool wide)
{
    if (m_layout == static_cast<int>(wide))
        return;
    m_layout = static_cast<int>(wide);

    const QList<QWidget*> cards{m_timelineCard, m_actionsCard, m_eventsCard, m_usersCard,
                                m_ipCard,       m_hoursCard,   m_weekCard,   m_processCard};
    for (QWidget* card : cards)
        m_grid->removeWidget(card);
    for (int c = 0; c < 3; ++c)
        m_grid->setColumnStretch(c, 0);
    if (wide) {
        m_grid->addWidget(m_timelineCard, 0, 0, 1, 2);
        m_grid->addWidget(m_actionsCard, 0, 2);
        m_grid->addWidget(m_eventsCard, 1, 0);
        m_grid->addWidget(m_usersCard, 1, 1);
        m_grid->addWidget(m_ipCard, 1, 2);
        m_grid->addWidget(m_hoursCard, 2, 0);
        m_grid->addWidget(m_weekCard, 2, 1, 1, 2);
        m_grid->addWidget(m_processCard, 3, 0, 1, 2);
        for (int c = 0; c < 3; ++c)
            m_grid->setColumnStretch(c, 1);
    } else {
        int row = 0;
        for (QWidget* card : cards)
            m_grid->addWidget(card, row++, 0);
        m_grid->setColumnStretch(0, 1);
    }
}

void DashboardPage::refresh()
{
    cancel(m_token);
    if (m_workspace->isEmpty()) {
        m_loading->stop();
        m_stack->setCurrentWidget(m_empty);
        return;
    }
    m_loading->start();
    m_alertsIcon->clear();
    m_alertsTitle->setText(qtTrId("common.searching_suspicious_activity"));
    m_alertsTitle->setColor({});
    m_alertsDetail->clear();
    const Filters filters = m_workspace->filters();
    m_token = m_workspace->executor().runCached(
        this, QStringLiteral("summary|") + filters.key(),
        [filters](Context& context) { return queries::summary(context, filters); },
        [this](queries::Summary summary) {
            m_token.reset();
            m_loading->stop();
            showSummary(summary);
        },
        [this](const QString& message) {
            m_token.reset();
            m_loading->stop();
            m_noResult->changeText(qtTrId("summary.calculation_failed"), message);
            m_stack->setCurrentWidget(m_noResult);
        });
    m_workspace->detections(this, [this](const QList<Detection>& detections) { showDetections(detections); });
}

void DashboardPage::clear()
{
    cancel(m_token);
    m_loading->stop();
    m_content->hide();
    m_stack->setCurrentIndex(0);
}

void DashboardPage::showSummary(const queries::Summary& s)
{
    if (s.total == 0) {
        m_noResult->changeText(qtTrId("summary.no_event_for_filters"),
                               qtTrId("summary.no_event_advice"));
        m_stack->setCurrentWidget(m_noResult);
        return;
    }
    m_stack->setCurrentIndex(0);
    m_content->show();

    const qint64 total = m_workspace->eventCount();
    QString detail = qtTrId("summary.whole_project");
    if (s.total < total)
        detail = m_workspace->filters().isEmpty() ? qtTrId("summary.excluding_simple_file_opens")
                                                  : qtTrId("summary.out_of_total").arg(formatNumber(total));
    m_events->setValue(formatNumber(s.total), detail);
    m_users->setValue(formatNumber(s.users), qtTrId("summary.click_to_see"));
    m_computers->setValue(formatNumber(s.machines),
                          s.files > 0 ? qtTrId("summary.tracked_file_count", pluralCount(s.files))
                                      : QString());
    const QDate first = QDate::fromString(s.first.left(10), Qt::ISODate);
    const QDate last = QDate::fromString(s.last.left(10), Qt::ISODate);
    if (first.isValid() && last.isValid()) {
        m_period->setValue(qtTrId("summary.day_count", pluralCount(first.daysTo(last) + 1)),
                           first == last
                               ? qtTrId("summary.on_date").arg(formatDate(first))
                               : qtTrId("common.period_from_to").arg(formatDate(first), formatDate(last)));
    } else {
        m_period->setValue(qtTrId("summary.unknown"));
    }

    m_timeline->set(s.byDay);
    QList<render::Element> actions;
    m_distributionByLog = s.fileAccesses == 0;
    if (m_distributionByLog) {
        static const QColor COLORS[] = {QColor(0x3b, 0x82, 0xf6), QColor(0x14, 0xb8, 0xa6), QColor(0xf5, 0x9e, 0x0b),
                                        QColor(0x8b, 0x5c, 0xf6), QColor(0xef, 0x44, 0x44), QColor(0x22, 0xc5, 0x5e),
                                        QColor(0xec, 0x48, 0x99), QColor(0x94, 0xa3, 0xb8)};
        int rank = 0;
        for (const auto& [channel, count] : s.byChannel)
            actions.append({shortChannelName(channel), count, COLORS[rank++ % std::size(COLORS)], channel});
        m_actionsCard->setTitle(qtTrId("summary.windows_logs"));
        m_actionsCard->setSubtitle(qtTrId("summary.click_to_filter_log"));
    } else {
        actions = render::elementsByCategory(s.byCategory);
        m_actionsCard->setTitle(qtTrId("common.action_types"));
        m_actionsCard->setSubtitle(qtTrId("summary.click_to_filter_action"));
    }
    m_actions->set(actions);
    QList<render::Element> events;
    for (const queries::EventRanking& e : s.topEvents)
        events.append({eventLabel(e.identifier, e.channel, e.provider), e.total, {},
                       QString::number(e.identifier) + QLatin1Char('\n') + e.channel});
    m_eventRanking->set(events);
    const bool suspicious = !s.topSuspiciousIps.isEmpty();
    m_ipCard->setTitle(suspicious ? qtTrId("summary.suspicious_ips") : qtTrId("summary.most_active_ips"));
    m_ipCard->setSubtitle(suspicious ? qtTrId("summary.suspicious_ips_help") : qtTrId("summary.most_active_ips_help"));
    QList<render::Element> addresses;
    for (const auto& [ip, count] : suspicious ? s.topSuspiciousIps : s.topIp)
        addresses.append({ip, count, suspicious ? theme::severityColor(Severity::High) : QColor(), ip});
    m_ipRanking->set(addresses);
    m_hours->set(s.byHour);
    m_week->set(s.byDayHour);
    m_userRanking->set(render::elementsFromSeries(s.topUsers));
    m_processRanking->set(render::elementsFromSeries(s.topProcesses), true);
}

void DashboardPage::showDetections(const QList<Detection>& detections)
{
    QList<Detection> findings;
    for (const Detection& detection : detections)
        if (detection.count > 0)
            findings.append(detection);
    std::stable_sort(findings.begin(), findings.end(), [](const Detection& a, const Detection& b) {
        return static_cast<int>(a.severity) > static_cast<int>(b.severity);
    });

    const bool alerts = !findings.isEmpty();
    const QColor color = alerts ? theme::severityColor(findings.first().severity) : theme::successColor();
    m_alertsIcon->setPixmap(
        icon(alerts ? "alerts" : "check", color).pixmap(m_alertsIcon->size(), devicePixelRatioF()));
    m_alertsTitle->setColor(color);
    if (!alerts) {
        m_alertsTitle->setText(qtTrId("common.no_suspicious_activity_detected"));
        m_alertsDetail->setText(qtTrId("summary.no_alert_explanation"));
        m_viewAlerts->setText(qtTrId("summary.see_details"));
        return;
    }
    QStringList summarize;
    for (const Detection& detection : findings)
        summarize << QStringLiteral("%1 (%2)").arg(detection.title, formatNumber(detection.count));
    m_alertsTitle->setText(qtTrId("summary.alert_count", static_cast<int>(findings.size())));
    m_alertsDetail->setText(summarize.join(QStringLiteral(" · ")));
    m_viewAlerts->setText(qtTrId("summary.see_alerts"));
}

} // namespace lh::ui
