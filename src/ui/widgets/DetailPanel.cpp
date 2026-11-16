#include "DetailPanel.h"

#include "Components.h"
#include "core/Actions.h"
#include "core/Alerts.h"
#include "core/Catalog.h"
#include "core/Format.h"
#include "core/Mitre.h"
#include "core/Normalization.h"
#include "core/DateTime.h"
#include "ui/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QClipboard>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTabBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace lh::ui {

namespace {

enum View { SummaryView, JsonView, XmlView };

QString clean(const QString& value)
{
    QStringList rows;
    for (const QStringView row : QStringView(value).split(QLatin1Char('\n'))) {
        QString cleaned = row.trimmed().toString();
        cleaned.replace(QLatin1Char('\t'), QLatin1Char(' '));
        if (!cleaned.isEmpty())
            rows.append(cleaned);
    }
    return rows.isEmpty() ? value : rows.join(QLatin1Char('\n'));
}

QString breakable(const QString& text, bool breakLetters = false)
{
    constexpr int maxLength = 16;
    int remainder = 0;
    bool tooLong = false;
    for (const QChar c : text) {
        remainder = c.isSpace() ? 0 : remainder + 1;
        tooLong = tooLong || remainder > maxLength;
    }
    if (!tooLong && !breakLetters)
        return text;
    static const QString separators = QStringLiteral("\\/;,-_:)]}|=&+");
    const QChar breakMark(0x200B);
    QString result;
    result.reserve(text.size() + text.size() / 4);
    remainder = 0;
    for (qsizetype i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isSpace()) {
            remainder = 0;
        } else if (remainder > 0 && !c.isLowSurrogate()
                   && (remainder >= maxLength || (breakLetters && c.isUpper() && text.at(i - 1).isLower()))) {
            result += breakMark;
            remainder = 0;
        }
        result += c;
        if (!c.isSpace())
            ++remainder;
        if (separators.contains(c)) {
            result += breakMark;
            remainder = 0;
        }
    }
    return result;
}

QPlainTextEdit* textView(QWidget* parent)
{
    auto* view = new QPlainTextEdit(parent);
    view->setReadOnly(true);
    view->setFrameShape(QFrame::NoFrame);
    view->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    view->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    view->viewport()->setAutoFillBackground(false);
    return view;
}

} // namespace

DetailPanel::DetailPanel(Workspace* workspace, QWidget* parent) : QFrame(parent), m_workspace(workspace)
{
    setMinimumWidth(300);
    m_stack = new QStackedWidget(this);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(1, 0, 0, 0);
    outer->addWidget(m_stack);

    auto* empty = new EmptyState("info", qtTrId("detail.no_event_selected"), qtTrId("detail.empty_explanation"),
                                 m_stack);
    m_stack->addWidget(empty);

    auto* frame = new QWidget(m_stack);
    auto* frameLayout = new QVBoxLayout(frame);
    frameLayout->setContentsMargins(0, 6, 0, 0);
    frameLayout->setSpacing(0);
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(12, 0, 8, 0);
    m_tabs = new QTabBar(frame);
    m_tabs->setDrawBase(false);
    m_tabs->setExpanding(false);
    m_tabs->addTab(qtTrId("detail.view_summary"));
    m_tabs->addTab(qtTrId("detail.view_json"));
    m_tabs->addTab(qtTrId("detail.view_xml"));
    m_tabs->setTabToolTip(JsonView, qtTrId("detail.view_json_help"));
    m_tabs->setTabToolTip(XmlView, qtTrId("detail.view_xml_help"));
    bar->addWidget(m_tabs);
    bar->addStretch();
    auto* copyButton = new QToolButton(frame);
    copyButton->setIcon(icon("copy"));
    copyButton->setAutoRaise(true);
    copyButton->setToolTip(qtTrId("detail.copy_view"));
    bar->addWidget(copyButton);
    frameLayout->addLayout(bar);
    frameLayout->addWidget(separator(Qt::Horizontal, frame));

    m_views = new QStackedWidget(frame);
    auto* scrollArea = new QScrollArea(m_views);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scrollArea->viewport()->setAutoFillBackground(false);
    m_content = new QWidget(scrollArea);
    m_content->setAutoFillBackground(false);
    scrollArea->setWidget(m_content);
    m_views->addWidget(scrollArea);
    m_json = textView(m_views);
    m_xml = textView(m_views);
    m_views->addWidget(m_json);
    m_views->addWidget(m_xml);
    frameLayout->addWidget(m_views, 1);
    m_stack->addWidget(frame);

    auto* layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(18, 14, 14, 16);
    layout->setSpacing(6);
    m_action = new Label({}, Label::Role::Title, m_content);
    m_action->setWordWrap(true);
    m_timestamp = new Label({}, Label::Role::Secondary, m_content);
    m_timestamp->setWordWrap(true);
    layout->addWidget(m_action);
    layout->addWidget(m_timestamp);
    layout->addSpacing(8);
    layout->addWidget(separator(Qt::Horizontal, m_content));
    layout->addSpacing(6);

    m_alerts = new QVBoxLayout;
    m_alerts->setSpacing(6);
    layout->addLayout(m_alerts);
    m_fields = new QVBoxLayout;
    m_fields->setSpacing(9);
    layout->addLayout(m_fields);
    m_data = new QVBoxLayout;
    m_data->setSpacing(9);
    layout->addLayout(m_data);
    layout->addSpacing(14);

    m_investigateFile = new QPushButton(icon("investigation"), qtTrId("common.file_history"), m_content);
    m_investigateUser = new QPushButton(icon("user"), qtTrId("common.user_activity"), m_content);
    for (QPushButton* button : {m_investigateFile, m_investigateUser}) {
        button->setAutoDefault(false);
        layout->addWidget(button);
    }
    layout->addStretch();

    connect(m_tabs, &QTabBar::currentChanged, this, &DetailPanel::showView);
    connect(copyButton, &QToolButton::clicked, this, &DetailPanel::copy);
    connect(m_investigateFile, &QPushButton::clicked, this, [this] {
        if (m_detail)
            m_workspace->navigate({Navigation::Target::File, m_detail->row.file});
    });
    connect(m_investigateUser, &QPushButton::clicked, this, [this] {
        if (m_detail)
            m_workspace->navigate({Navigation::Target::User, m_detail->row.user});
    });
}

void DetailPanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), theme::cardBackground(palette()));
    painter.fillRect(QRect(0, 0, 1, height()), theme::border(palette()));
}

void DetailPanel::addSection(QVBoxLayout* target, const QString& title)
{
    auto* labelWidget = new Label(title.toUpper(), Label::Role::Section, m_content);
    labelWidget->setContentsMargins(0, 10, 0, 0);
    target->addWidget(labelWidget);
}

void DetailPanel::addField(QVBoxLayout* target, const QString& label, const QString& value, bool remember)
{
    if (value.isEmpty())
        return;
    if (remember)
        m_values.append({label, value});
    auto* row = new QWidget(m_content);
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    auto* labelWidget = new Label(breakable(label, true), Label::Role::Small, row);
    labelWidget->setFixedWidth(fontMetrics().height() * 7);
    labelWidget->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    labelWidget->setWordWrap(true);
    const QString cleaned = clean(value);
    const QString displayed = breakable(cleaned);
    auto* text = new Label(displayed, Label::Role::Normal, row);
    text->setWordWrap(true);
    text->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    text->setTextFormat(Qt::PlainText);
    QSizePolicy policy = text->sizePolicy();
    policy.setHorizontalPolicy(QSizePolicy::Ignored);
    text->setSizePolicy(policy);
    if (displayed == cleaned)
        text->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* button = new QToolButton(row);
    button->setIcon(icon("copy"));
    button->setAutoRaise(true);
    button->setIconSize(QSize(14, 14));
    button->setToolTip(qtTrId("common.copy_value").arg(label));
    connect(button, &QToolButton::clicked, this, [value] { QGuiApplication::clipboard()->setText(value); });
    layout->addWidget(labelWidget, 0, Qt::AlignTop);
    layout->addWidget(text, 1);
    layout->addWidget(button, 0, Qt::AlignTop);
    target->addWidget(row);
}

void DetailPanel::clearFields()
{
    clearLayout(m_alerts);
    clearLayout(m_fields);
    clearLayout(m_data);
    m_values.clear();
}

void DetailPanel::display(qint64 identifier)
{
    m_detail.reset();
    m_rawXml.clear();
    m_xmlLoaded = false;
    if (identifier > 0) {
        try {
            m_detail = queries::detail(m_workspace->session().mainConnection(), identifier);
        } catch (const std::exception&) {
            m_detail.reset();
        }
    }
    if (!m_detail) {
        m_stack->setCurrentIndex(0);
        return;
    }
    const queries::EventDetail& d = *m_detail;
    const queries::TableRow& l = d.row;
    clearFields();

    const QString description = eventDescription(l.eventId, l.channel);
    if (!l.action.isEmpty()) {
        m_action->setText(actionLabel(l.action));
        m_action->setColor(theme::categoryColor(l.category));
    } else {
        m_action->setText(description.isEmpty() ? eventLabel(l.eventId, l.channel, l.provider) : description);
        const int rank = levelRank(l.level, l.audit);
        m_action->setColor(l.alert >= 0 ? theme::severityColor(severityFromLevel(l.alert))
                           : rank > 0   ? theme::levelColor(rank)
                                        : QColor());
    }
    m_timestamp->setText(l.timestamp.isEmpty()
                             ? qtTrId("detail.unknown_timestamp")
                             : qtTrId("detail.local_time")
                                   .arg(formatTimestamp(l.timestamp), QString::fromLatin1(m_workspace->timeZone())));

    if (!d.alerts.isEmpty()) {
        addSection(m_alerts, qtTrId("detail.section_alerts"));
        for (const queries::EventAlert& alert : d.alerts) {
            const Severity severity = severityFromLevel(alert.level);
            auto* row = new QWidget(m_content);
            auto* layout = new QHBoxLayout(row);
            layout->setContentsMargins(0, 0, 0, 0);
            layout->setSpacing(8);
            auto* alertDot = new QLabel(row);
            alertDot->setPixmap(icon("alerts", theme::severityColor(severity)).pixmap(16, 16));
            layout->addWidget(alertDot, 0, Qt::AlignTop);
            auto* texts = new QVBoxLayout;
            texts->setSpacing(0);
            auto* link = new LinkButton(alert.title, row);
            link->setWordWrap(true);
            const QString rule = alert.rule;
            connect(link, &LinkButton::clicked, this, [this, rule] {
                m_workspace->navigate({Navigation::Target::Alerts, QStringLiteral("sigma:") + rule});
            });
            texts->addWidget(link);
            const QString techniques = m_workspace->parameters().mitre ? mitre::summarize(alert.tags) : QString();
            auto* precision = new Label(
                techniques.isEmpty() ? severityLabel(severity)
                                     : qtTrId("detail.severity_mitre").arg(severityLabel(severity), techniques),
                Label::Role::Small, row);
            precision->setWordWrap(true);
            texts->addWidget(precision);
            layout->addLayout(texts, 1);
            m_alerts->addWidget(row);
            m_values.append({qtTrId("common.alert"), alert.title});
        }
    }

    addSection(m_fields, qtTrId("detail.section_event"));
    addField(m_fields, qtTrId("detail.event"),
             description.isEmpty() ? QString::number(l.eventId)
                                   : qtTrId("detail.value_detail").arg(l.eventId).arg(description));
    addField(m_fields, qtTrId("common.level"), levelLabel(l.level, l.audit));
    addField(m_fields, qtTrId("common.windows_log"), l.channel);
    addField(m_fields, qtTrId("detail.provider"), l.provider);
    addField(m_fields, qtTrId("common.user"),
             l.user.isEmpty() ? qtTrId("common.unknown")
             : l.isSystem     ? qtTrId("detail.user_system_account").arg(l.user)
                              : l.user);
    addField(m_fields, qtTrId("common.computer"), l.machine);
    addField(m_fields, qtTrId("common.program"), l.process);
    addField(m_fields, qtTrId("common.source_ip"), l.sourceIp);
    addField(m_fields, qtTrId("common.destination_ip"), d.destinationIp);
    if (l.file.isEmpty())
        addField(m_fields, qtTrId("common.detail"), l.detail);
    addField(m_fields, qtTrId("common.file"), l.file);
    addField(m_fields, qtTrId("common.folder"), d.folder);
    addField(m_fields, qtTrId("detail.extension"), d.extension);
    if (!l.category.isEmpty())
        addField(m_fields, qtTrId("common.action_type"), categoryLabel(l.category));
    addField(m_fields, qtTrId("common.requested_rights"), d.access);
    if (d.tsUtcKnown)
        addField(m_fields, qtTrId("detail.utc_time"), formatUtc(d.tsUtc));
    if (d.recordId > 0)
        addField(m_fields, qtTrId("detail.record_number"), QString::number(d.recordId));
    addField(m_fields, qtTrId("common.log"), d.source);

    if (!d.data.isEmpty()) {
        addSection(m_data, qtTrId("detail.section_data"));
        for (const auto& [name, value] : d.data)
            addField(m_data, name, value.isEmpty() ? QStringLiteral("·") : value, false);
    }

    m_investigateUser->setEnabled(!l.user.isEmpty());
    m_investigateFile->setVisible(!l.file.isEmpty());
    m_stack->setCurrentIndex(1);
    showView(m_tabs->currentIndex());
}

void DetailPanel::showView(int index)
{
    m_views->setCurrentIndex(index);
    if (!m_detail || index == SummaryView)
        return;
    if (!m_xmlLoaded) {
        try {
            m_rawXml = queries::xml(m_workspace->session().mainConnection(), m_detail->row.id);
        } catch (const std::exception&) {
            m_rawXml.clear();
        }
        m_xmlLoaded = true;
        if (m_rawXml.isEmpty()) {
            m_xml->setPlainText(qtTrId("detail.xml_unavailable"));
            m_json->setPlainText(qtTrId("detail.xml_unavailable"));
        } else {
            m_xml->setPlainText(indentXml(m_rawXml));
            m_json->setPlainText(xmlToJson(m_rawXml));
        }
    }
}

void DetailPanel::copy()
{
    if (!m_detail)
        return;
    switch (m_tabs->currentIndex()) {
    case JsonView:
        QGuiApplication::clipboard()->setText(m_json->toPlainText());
        return;
    case XmlView:
        QGuiApplication::clipboard()->setText(m_rawXml.isEmpty() ? m_xml->toPlainText() : indentXml(m_rawXml));
        return;
    default:
        break;
    }
    QString text = m_action->text() + QLatin1Char('\n') + formatTimestamp(m_detail->row.timestamp)
                   + QLatin1Char('\n');
    for (const auto& [label, value] : std::as_const(m_values))
        text += qtTrId("common.label_value").arg(label, value) + QLatin1Char('\n');
    for (const auto& [name, value] : std::as_const(m_detail->data))
        text += qtTrId("common.label_value").arg(name, value) + QLatin1Char('\n');
    QGuiApplication::clipboard()->setText(text);
}

} // namespace lh::ui
