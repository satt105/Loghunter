#include "AlertsPage.h"

#include "core/Format.h"
#include "core/Mitre.h"
#include "ui/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/widgets/Components.h"
#include "ui/widgets/TableModel.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QTableView>
#include <QVBoxLayout>

namespace lh::ui {

namespace {

constexpr int ROLE_COUNT = Qt::UserRole + 1;
constexpr int ROLE_UNIT = Qt::UserRole + 2;
constexpr int ROLE_SEVERITY = Qt::UserRole + 3;
constexpr int ROLE_IDENTIFIER = Qt::UserRole + 4;

class DetectionDelegate : public QStyledItemDelegate
{
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        return {option.rect.width(), option.fontMetrics.height() * 2 + 22};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QPalette& pal = option.palette;
        const bool selected = option.state & QStyle::State_Selected;
        const QRectF frame = QRectF(option.rect).adjusted(2, 2, -2, -2);
        if (selected || (option.state & QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(selected ? theme::selectionBackground(pal) : theme::hoverBackground(pal));
            painter->drawRoundedRect(frame, 6, 6);
        }
        const qint64 count = index.data(ROLE_COUNT).toLongLong();
        const auto severity = static_cast<Severity>(index.data(ROLE_SEVERITY).toInt());
        const QColor color = count > 0 ? theme::severityColor(severity) : theme::secondaryText(pal);
        const QRect zone = option.rect.adjusted(12, 0, -12, 0);

        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        painter->drawEllipse(QPointF(zone.left() + 5, zone.center().y()), 5, 5);

        const QString value = count > 0 ? formatNumber(count) : QString();
        QFont large = option.font;
        large.setWeight(QFont::DemiBold);
        large.setPointSizeF(option.font.pointSizeF() * 1.15);
        const int valueWidth = QFontMetrics(large).horizontalAdvance(value) + 4;
        painter->setFont(large);
        painter->setPen(theme::text(pal));
        painter->drawText(QRect(zone.right() - valueWidth, zone.top(), valueWidth, zone.height()),
                          Qt::AlignRight | Qt::AlignVCenter, value);

        const int left = zone.left() + 20;
        const int width = zone.right() - valueWidth - left - 8;
        const QFontMetrics metrics(option.font);
        const int y = zone.center().y() - metrics.height();
        QFont bold = option.font;
        bold.setWeight(QFont::DemiBold);
        painter->setFont(bold);
        painter->drawText(QRect(left, y, width, metrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetrics(bold).elidedText(index.data().toString(), Qt::ElideRight, width));
        painter->setFont(option.font);
        painter->setPen(theme::secondaryText(pal));
        const QString detail = count > 0 ? qtTrId("alerts.name_and_severity")
                                               .arg(index.data(ROLE_UNIT).toString(), severityLabel(severity).toLower())
                                         : qtTrId("alerts.no_finding");
        painter->drawText(QRect(left, y + metrics.height() + 2, width, metrics.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, metrics.elidedText(detail, Qt::ElideRight, width));
        painter->restore();
    }
};

} // namespace

AlertsPage::AlertsPage(Workspace* workspace, QWidget* parent) : Page(workspace, parent)
{
    m_stack = new QStackedWidget(this);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_stack);

    m_empty = new EmptyState("alerts", qtTrId("common.no_data_to_analyze"), qtTrId("alerts.empty_explanation"),
                             m_stack);

    m_content = new QWidget(m_stack);
    auto* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(24, 2, 24, 16);
    layout->setSpacing(10);

    auto* bar = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    titles->setSpacing(2);
    m_summary = new Label({}, Label::Role::Title, m_content);
    m_risk = new Label({}, Label::Role::Secondary, m_content);
    m_risk->setToolTip(qtTrId("alerts.risk_score_help"));
    titles->addWidget(m_summary);
    titles->addWidget(m_risk);
    bar->addLayout(titles, 1);
    auto* rerunButton = new QPushButton(icon("refresh"), qtTrId("alerts.rerun_analysis"), m_content);
    rerunButton->setToolTip(qtTrId("alerts.rerun_analysis_help"));
    auto* exportButton = new QPushButton(icon("export"), qtTrId("alerts.export"), m_content);
    exportButton->setToolTip(qtTrId("alerts.export_help"));
    auto* settingsButton = new QPushButton(icon("settings"), qtTrId("alerts.adjust_alerts"), m_content);
    settingsButton->setToolTip(qtTrId("alerts.adjust_alerts_help"));
    for (QPushButton* button : {rerunButton, exportButton, settingsButton}) {
        button->setAutoDefault(false);
        bar->addWidget(button, 0, Qt::AlignVCenter);
    }
    connect(rerunButton, &QPushButton::clicked, this, &AlertsPage::reanalysisRequested);
    connect(exportButton, &QPushButton::clicked, this, &AlertsPage::exportRequested);
    connect(settingsButton, &QPushButton::clicked, this, &AlertsPage::settingsRequested);
    layout->addLayout(bar);

    auto* splitter = new QSplitter(Qt::Horizontal, m_content);
    splitter->setChildrenCollapsible(false);
    layout->addWidget(splitter, 1);

    auto* listCard = new Card(qtTrId("alerts.automatic_checks"), splitter);
    listCard->setMargins(8, 12, 8, 8);
    listCard->setMinimumWidth(280);
    m_list = new QListWidget(listCard);
    m_list->setItemDelegate(new DetectionDelegate(m_list));
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setUniformItemSizes(true);
    m_list->setMouseTracking(true);
    m_list->viewport()->setAutoFillBackground(false);
    listCard->content()->addWidget(m_list, 1);

    m_detail = new Card({}, splitter);
    m_detail->setMargins(18, 16, 18, 16);
    m_severity = new Label({}, Label::Role::Title, m_detail);
    m_description = new Label({}, Label::Role::Secondary, m_detail);
    m_description->setWordWrap(true);
    m_mitre = new Label({}, Label::Role::Normal, m_detail);
    m_mitre->setTextFormat(Qt::RichText);
    m_mitre->setOpenExternalLinks(true);
    m_mitre->setWordWrap(true);
    m_origin = new Label({}, Label::Role::Small, m_detail);
    m_origin->setWordWrap(true);
    m_detail->content()->addWidget(m_severity);
    m_detail->content()->addWidget(m_description);
    m_detail->content()->addWidget(m_mitre);
    m_detail->content()->addWidget(m_origin);

    m_detailStack = new QStackedWidget(m_detail);
    m_noFinding =
        new EmptyState("check", qtTrId("alerts.no_finding"), qtTrId("alerts.nothing_to_report"), m_detailStack);
    auto* results = new QWidget(m_detailStack);
    auto* resultsLayout = new QVBoxLayout(results);
    resultsLayout->setContentsMargins(0, 6, 0, 0);
    resultsLayout->setSpacing(8);
    m_model = new TableModel(this);
    QSortFilterProxyModel* proxy = createSortProxy(m_model, this);
    m_results = new QTableView(results);
    configureTable(m_results);
    m_results->setModel(proxy);
    m_results->setSortingEnabled(true);
    m_results->setContextMenuPolicy(Qt::CustomContextMenu);
    m_results->horizontalHeader()->setStretchLastSection(true);
    resultsLayout->addWidget(m_results, 1);
    auto* resultsFooter = new QHBoxLayout;
    m_note = new Label({}, Label::Role::Small, results);
    m_note->setWordWrap(true);
    resultsFooter->addWidget(m_note, 1);
    m_all = new QPushButton(icon("filters"), qtTrId("alerts.see_all_occurrences"), results);
    m_all->setAutoDefault(false);
    m_all->setToolTip(qtTrId("alerts.see_all_occurrences_help"));
    resultsFooter->addWidget(m_all);
    m_exploreButton = new QPushButton(icon("events"), qtTrId("alerts.see_related_events"), results);
    m_exploreButton->setAutoDefault(false);
    m_exploreButton->setEnabled(false);
    resultsFooter->addWidget(m_exploreButton);
    resultsLayout->addLayout(resultsFooter);
    m_detailStack->addWidget(results);
    m_detailStack->addWidget(m_noFinding);
    m_detail->content()->addWidget(m_detailStack, 1);

    splitter->addWidget(listCard);
    splitter->addWidget(m_detail);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({340, 900});

    m_stack->addWidget(m_content);
    m_stack->addWidget(m_empty);
    m_loading = new LoadingIndicator(m_content);

    auto currentLine = [this, proxy] {
        const QModelIndex index = m_results->currentIndex();
        return index.isValid() ? proxy->mapToSource(index).row() : -1;
    };
    connect(m_list, &QListWidget::currentRowChanged, this, &AlertsPage::showDetection);
    connect(m_results->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex& current) { m_exploreButton->setEnabled(current.isValid()); });
    connect(m_exploreButton, &QPushButton::clicked, this, [this, currentLine] { exploreRow(currentLine()); });
    connect(m_all, &QPushButton::clicked, this, [this] {
        const int index = m_list->currentRow();
        if (index >= 0 && index < m_detections.size())
            m_workspace->explore(m_detections[index].allOccurrences(m_workspace->filters()));
    });
    connect(m_results, &QTableView::doubleClicked, this,
            [this, proxy](const QModelIndex& index) { exploreRow(proxy->mapToSource(index).row()); });
    connect(m_results, &QWidget::customContextMenuRequested, this, &AlertsPage::resultsMenu);
}

void AlertsPage::select(const QString& identifier)
{
    m_identifier = identifier;
    for (int row = 0; row < m_list->count(); ++row)
        if (m_list->item(row)->data(ROLE_IDENTIFIER).toString() == identifier)
            m_list->setCurrentRow(row);
}

void AlertsPage::refresh()
{
    if (m_workspace->isEmpty()) {
        m_loading->stop();
        m_stack->setCurrentWidget(m_empty);
        return;
    }
    m_stack->setCurrentWidget(m_content);
    m_summary->setText(qtTrId("common.searching_suspicious_activity"));
    m_loading->start();
    m_workspace->detections(this, [this](const QList<Detection>& detections) {
        m_loading->stop();
        showDetections(detections);
    });
}

void AlertsPage::showDetections(const QList<Detection>& detections)
{
    m_detections = detections;
    std::stable_sort(m_detections.begin(), m_detections.end(), [](const Detection& a, const Detection& b) {
        if ((a.count > 0) != (b.count > 0))
            return a.count > 0;
        return static_cast<int>(a.severity) > static_cast<int>(b.severity);
    });
    const QSignalBlocker blocker(m_list);
    m_list->clear();
    for (const Detection& detection : m_detections) {
        auto* element = new QListWidgetItem(detection.title, m_list);
        element->setData(ROLE_COUNT, detection.count);
        element->setData(ROLE_UNIT, detection.quantity());
        element->setData(ROLE_SEVERITY, static_cast<int>(detection.severity));
        element->setData(ROLE_IDENTIFIER, detection.identifier);
        element->setToolTip(detection.description);
    }
    const int withFindings = findingCount(m_detections);
    m_summary->setText(withFindings == 0 ? qtTrId("common.no_suspicious_activity_detected")
                                         : qtTrId("alerts.count_to_review", withFindings));
    QStringList distribution;
    for (const Severity severity : {Severity::Critical, Severity::High, Severity::Medium, Severity::Low}) {
        const auto count = std::count_if(m_detections.cbegin(), m_detections.cend(), [severity](const Detection& d) {
            return d.count > 0 && d.severity == severity;
        });
        if (count > 0)
            distribution << qtTrId("common.label_value").arg(severityLabel(severity)).arg(count);
    }
    if (withFindings == 0) {
        m_risk->setText(qtTrId("alerts.no_risk"));
        m_risk->setColor(theme::successColor());
    } else {
        m_risk->setText(qtTrId("alerts.risk_score")
                            .arg(riskScore(m_detections))
                            .arg(distribution.join(QStringLiteral(" · "))));
        m_risk->setColor(theme::severityColor(maxSeverity(m_detections)));
    }
    int index = 0;
    for (int i = 0; i < m_detections.size(); ++i)
        if (m_detections[i].identifier == m_identifier)
            index = i;
    m_list->setCurrentRow(m_detections.isEmpty() ? -1 : index);
    showDetection(m_list->currentRow());
}

void AlertsPage::showDetection(int index)
{
    if (index < 0 || index >= m_detections.size()) {
        m_detail->setTitle({});
        m_severity->clear();
        m_description->clear();
        m_mitre->clear();
        m_mitre->hide();
        m_origin->clear();
        m_model->setColumns({});
        m_noFinding->changeText(qtTrId("alerts.no_alert"), qtTrId("alerts.no_alert_explanation"));
        m_detailStack->setCurrentIndex(1);
        return;
    }
    const Detection& d = m_detections[index];
    m_identifier = d.identifier;
    m_detail->setTitle(d.title);
    m_description->setText(d.description);
    const mitre::Mapping match =
        m_workspace->parameters().mitre ? mitre::parse(d.tags) : mitre::Mapping();
    QStringList parts;
    for (const QString& tactic : match.tactics)
        parts << mitre::tacticLabel(tactic).toHtmlEscaped();
    for (const QString& technique : match.techniques)
        parts << QStringLiteral("<a href=\"%1\">%2</a>").arg(mitre::techniqueLink(technique), technique);
    m_mitre->setText(parts.isEmpty()
                         ? QString()
                         : qtTrId("alerts.mitre").toHtmlEscaped().arg(parts.join(QStringLiteral(" · "))));
    m_mitre->setVisible(!parts.isEmpty());
    m_origin->setText(d.sigma ? (d.author.isEmpty() ? qtTrId("alerts.origin_sigma")
                                                    : qtTrId("alerts.origin_sigma_author").arg(d.author))
                              : qtTrId("alerts.origin_builtin"));
    m_all->setVisible(d.sigma);
    if (d.count > 0) {
        m_severity->setText(qtTrId("alerts.severity_and_count").arg(severityLabel(d.severity).toLower(), d.quantity()));
        m_severity->setColor(theme::severityColor(d.severity));
    } else {
        m_severity->setText(qtTrId("alerts.no_finding"));
        m_severity->setColor({});
    }

    QList<TableModel::Column> columns;
    for (const ResultColumn& column : d.columns)
        columns.append({column.title, column.type == ResultColumn::Type::Number
                                            || column.type == ResultColumn::Type::Duration});
    m_model->setColumns(columns);
    QList<TableModel::Row> rows;
    rows.reserve(d.rows.size());
    for (qsizetype i = 0; i < d.rows.size(); ++i) {
        TableModel::Row row;
        row.key = static_cast<int>(i);
        const QVariantList& values = d.rows[i];
        for (qsizetype c = 0; c < values.size() && c < d.columns.size(); ++c) {
            TableModel::Cell cell{formatCell(values[c], d.columns[c].type), values[c]};
            if (d.columns[c].type == ResultColumn::Type::Category)
                cell.icon = dot(theme::categoryColor(values[c].toString()));
            row.cells.append(cell);
        }
        rows.append(row);
    }
    m_model->setRows(rows);
    limitColumnWidths(m_results);
    m_exploreButton->setEnabled(false);
    m_note->setText(d.count > d.rows.size()
                        ? qtTrId("alerts.findings_truncated")
                              .arg(formatNumber(d.rows.size()), formatNumber(d.count))
                        : qtTrId("alerts.double_click_help"));
    if (d.count <= 0)
        m_noFinding->changeText(qtTrId("alerts.no_finding"), qtTrId("alerts.nothing_to_report"));
    m_detailStack->setCurrentIndex(d.count > 0 ? 0 : 1);
}

void AlertsPage::exploreRow(int row)
{
    const int index = m_list->currentRow();
    if (index < 0 || index >= m_detections.size())
        return;
    const Detection& d = m_detections[index];
    if (row >= 0 && row < d.explorations.size())
        m_workspace->explore(d.explorations[row]);
}

void AlertsPage::resultsMenu(const QPoint& position)
{
    const QModelIndex index = m_results->indexAt(position);
    if (!index.isValid())
        return;
    const int row = qobject_cast<QSortFilterProxyModel*>(m_results->model())->mapToSource(index).row();
    QMenu menu(this);
    menu.addAction(icon("events"), qtTrId("alerts.see_related_events"), this,
                   [this, row] { exploreRow(row); });
    menu.addAction(icon("copy"), qtTrId("common.copy"), QKeySequence::Copy, this,
                   [this] { copyRows(m_results); });
    menu.exec(m_results->viewport()->mapToGlobal(position));
}

} // namespace lh::ui
