#include "FilterPanel.h"

#include "Components.h"
#include "core/Actions.h"
#include "core/Catalog.h"
#include "core/Queries.h"
#include "core/DateTime.h"
#include "ui/Workspace.h"
#include "ui/Icons.h"
#include "ui/Settings.h"
#include "ui/Theme.h"
#include "ui/dialogs/MessageDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateEdit>
#include <QGridLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QStringListModel>
#include <QToolButton>
#include <QVBoxLayout>

namespace lh::ui {

namespace {

QJsonObject readPresets()
{
    return QJsonDocument::fromJson(settings::read(settings::key::PRESETS).toByteArray()).object();
}

void writePresets(const QJsonObject& object)
{
    settings::write(settings::key::PRESETS, QJsonDocument(object).toJson(QJsonDocument::Compact));
}

QCompleter* completer(QStringListModel* model, QObject* parent)
{
    auto* result = new QCompleter(model, parent);
    result->setCaseSensitivity(Qt::CaseInsensitive);
    result->setFilterMode(Qt::MatchContains);
    result->setMaxVisibleItems(12);
    return result;
}

} // namespace

FilterPanel::FilterPanel(Workspace* workspace, QWidget* parent) : QFrame(parent), m_workspace(workspace)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(1, 0, 0, 0);
    outer->setSpacing(0);

    auto* header = new QHBoxLayout;
    header->setContentsMargins(18, 14, 12, 8);
    header->addWidget(new Label(qtTrId("common.filters"), Label::Role::Title, this));
    header->addStretch();
    auto* closeButton = new QToolButton(this);
    closeButton->setIcon(icon("close"));
    closeButton->setAutoRaise(true);
    closeButton->setToolTip(qtTrId("filters.hide_panel"));
    connect(closeButton, &QToolButton::clicked, this, &FilterPanel::closeRequested);
    header->addWidget(closeButton);
    outer->addLayout(header);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->viewport()->setAutoFillBackground(false);
    auto* content = new QWidget(m_scrollArea);
    content->setAutoFillBackground(false);
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(18, 4, 18, 18);
    layout->setSpacing(6);
    m_scrollArea->setWidget(content);
    outer->addWidget(m_scrollArea, 1);

    QVBoxLayout* target = layout;
    QWidget* container = content;
    auto section = [&](const QString& title) {
        target->addSpacing(10);
        target->addWidget(new Label(title.toUpper(), Label::Role::Section, container));
    };
    auto label = [&](const QString& text) { target->addWidget(new Label(text, Label::Role::Small, container)); };
    auto field = [&](const QString& placeholder) {
        auto* row = new QLineEdit(container);
        row->setPlaceholderText(placeholder);
        row->setClearButtonEnabled(true);
        target->addWidget(row);
        connect(row, &QLineEdit::textEdited, &m_timer, qOverload<>(&QTimer::start));
        connect(row, &QLineEdit::returnPressed, this, &FilterPanel::apply);
        return row;
    };

    // ── Who ──
    section(qtTrId("filters.section_who"));
    label(qtTrId("common.user"));
    m_user = field(qtTrId("filters.example_user"));
    m_users = new QStringListModel(this);
    m_user->setCompleter(completer(m_users, this));

    // ── What ──
    section(qtTrId("filters.section_what"));
    label(qtTrId("common.action_type"));
    m_category = new QComboBox(content);
    m_category->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_category->setMinimumContentsLength(12);
    m_category->addItem(qtTrId("filters.all_actions"), QString());
    for (const Category& category : categories())
        m_category->addItem(dot(QColor(QLatin1String(category.color))), categoryLabel(category),
                            QLatin1String(category.key));
    layout->addWidget(m_category);
    label(qtTrId("filters.event_ids"));
    m_events = field(qtTrId("filters.example_events"));
    m_events->setToolTip(qtTrId("filters.event_ids_help"));
    label(qtTrId("common.level"));
    m_level = new QComboBox(content);
    m_level->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_level->setMinimumContentsLength(12);
    for (const LevelFilter& level : filterLevels())
        m_level->addItem(qtTrId(level.label), level.value);
    m_level->setToolTip(qtTrId("filters.level_help"));
    layout->addWidget(m_level);
    label(qtTrId("filters.file_or_folder"));
    m_file = field(qtTrId("filters.example_file"));
    m_file->setToolTip(qtTrId("filters.file_help"));

    // ── When ──
    section(qtTrId("filters.section_when"));
    auto* quick = new QHBoxLayout;
    quick->setSpacing(4);
    const QList<QPair<QString, int>> periods{{qtTrId("filters.all"), 0},
                                             {qtTrId("filters.days_short").arg(1), 1},
                                             {qtTrId("filters.days_short").arg(7), 7},
                                             {qtTrId("filters.days_short").arg(30), 30}};
    for (const auto& [text, days] : periods) {
        auto* button = new QPushButton(text, content);
        button->setAutoDefault(false);
        button->setMinimumWidth(0);
        button->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        button->setToolTip(days > 0 ? qtTrId("filters.last_days_help")
                                          .arg(days)
                                    : qtTrId("filters.whole_period"));
        connect(button, &QPushButton::clicked, this, [this, days] { quickPeriod(days); });
        quick->addWidget(button);
    }
    layout->addLayout(quick);

    auto* grid = new QGridLayout;
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(6);
    m_withStart = new QCheckBox(qtTrId("filters.from"), content);
    m_start = new QDateEdit(content);
    m_withEnd = new QCheckBox(qtTrId("filters.until"), content);
    m_end = new QDateEdit(content);
    for (QDateEdit* date : {m_start, m_end}) {
        date->setCalendarPopup(true);
        date->setDisplayFormat(displayedDateFormat());
        date->setDate(QDate::currentDate());
    }
    grid->addWidget(m_withStart, 0, 0);
    grid->addWidget(m_start, 0, 1);
    grid->addWidget(m_withEnd, 1, 0);
    grid->addWidget(m_end, 1, 1);
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);

    layout->addSpacing(12);
    auto* resetButton = new QPushButton(icon("refresh"), qtTrId("common.clear_filters"), content);
    resetButton->setAutoDefault(false);
    connect(resetButton, &QPushButton::clicked, m_workspace, &Workspace::resetFilters);
    layout->addWidget(resetButton);

    // ── More criteria ──
    layout->addSpacing(10);
    m_advancedButton = new QToolButton(content);
    m_advancedButton->setText(qtTrId("filters.more_criteria"));
    m_advancedButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_advancedButton->setArrowType(Qt::RightArrow);
    m_advancedButton->setAutoRaise(true);
    m_advancedButton->setCheckable(true);
    layout->addWidget(m_advancedButton);
    m_advanced = new QWidget(content);
    m_advanced->setVisible(false);
    auto* advancedLayout = new QVBoxLayout(m_advanced);
    advancedLayout->setContentsMargins(0, 0, 0, 0);
    advancedLayout->setSpacing(6);
    layout->addWidget(m_advanced);
    connect(m_advancedButton, &QToolButton::toggled, this, [this](bool expanded) {
        m_advancedButton->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
        m_advanced->setVisible(expanded);
    });
    target = advancedLayout;
    container = m_advanced;

    label(qtTrId("filters.accounts"));
    m_accounts = new QComboBox(m_advanced);
    m_accounts->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_accounts->setMinimumContentsLength(12);
    m_accounts->addItem(qtTrId("filters.all_accounts"), static_cast<int>(AccountFilter::All));
    m_accounts->addItem(qtTrId("filters.chip_user_accounts"), static_cast<int>(AccountFilter::Users));
    m_accounts->addItem(qtTrId("filters.chip_system_accounts"), static_cast<int>(AccountFilter::System));
    m_accounts->setToolTip(
        qtTrId("filters.system_accounts_help"));
    target->addWidget(m_accounts);
    label(qtTrId("filters.folder_with_subfolders"));
    m_folder = field(qtTrId("filters.example_folder"));
    label(qtTrId("filters.program_used"));
    m_process = field(qtTrId("filters.example_program"));
    m_knownProcesses = new QStringListModel(this);
    m_process->setCompleter(completer(m_knownProcesses, this));
    label(qtTrId("common.computer"));
    m_machine = field(qtTrId("filters.computer_name"));
    m_machines = new QStringListModel(this);
    m_machine->setCompleter(completer(m_machines, this));
    label(qtTrId("common.windows_log"));
    m_channel = new QComboBox(m_advanced);
    m_channel->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_channel->setMinimumContentsLength(12);
    m_channel->setToolTip(qtTrId("filters.windows_log_help"));
    target->addWidget(m_channel);
    label(qtTrId("filters.ip_address"));
    m_ip = field(qtTrId("filters.example_ip"));
    label(qtTrId("common.log"));
    m_log = new QComboBox(m_advanced);
    m_log->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_log->setMinimumContentsLength(12);
    target->addWidget(m_log);

    m_hourRange = new QCheckBox(qtTrId("filters.between_certain_hours"), m_advanced);
    m_hourRange->setToolTip(qtTrId("filters.night_range_help"));
    target->addWidget(m_hourRange);
    auto* hours = new QHBoxLayout;
    hours->setSpacing(6);
    m_startHour = new QSpinBox(m_advanced);
    m_endHour = new QSpinBox(m_advanced);
    for (QSpinBox* hour : {m_startHour, m_endHour}) {
        hour->setRange(0, 23);
        hour->setSuffix(qtTrId("common.hours_suffix"));
    }
    m_startHour->setValue(20);
    m_endHour->setValue(7);
    hours->addWidget(new Label(qtTrId("common.from"), Label::Role::Secondary, m_advanced));
    hours->addWidget(m_startHour, 1);
    hours->addWidget(new Label(qtTrId("common.to"), Label::Role::Secondary, m_advanced));
    hours->addWidget(m_endHour, 1);
    target->addLayout(hours);

    m_exact = new QCheckBox(qtTrId("filters.exact_name"), m_advanced);
    m_exact->setToolTip(qtTrId("filters.exact_match_help"));
    target->addWidget(m_exact);
    m_handles = new QCheckBox(qtTrId("filters.include_simple_opens"), m_advanced);
    m_handles->setToolTip(qtTrId("filters.simple_opens_help"));
    target->addWidget(m_handles);

    section(qtTrId("filters.saved_filters"));
    auto* settingsRow = new QHBoxLayout;
    settingsRow->setSpacing(4);
    m_presets = new QComboBox(m_advanced);
    m_presets->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_presets->setMinimumContentsLength(10);
    m_presets->setToolTip(qtTrId("filters.saved_filters_help"));
    settingsRow->addWidget(m_presets, 1);
    auto* saveButton = new QToolButton(m_advanced);
    saveButton->setIcon(icon("bookmark"));
    saveButton->setToolTip(qtTrId("filters.save_help"));
    connect(saveButton, &QToolButton::clicked, this, &FilterPanel::savePreset);
    settingsRow->addWidget(saveButton);
    auto* deleteButton = new QToolButton(m_advanced);
    deleteButton->setIcon(icon("delete"));
    deleteButton->setToolTip(qtTrId("filters.delete_help"));
    connect(deleteButton, &QToolButton::clicked, this, &FilterPanel::deletePreset);
    settingsRow->addWidget(deleteButton);
    target->addLayout(settingsRow);
    layout->addStretch();

    m_timer.setSingleShot(true);
    m_timer.setInterval(350);
    connect(&m_timer, &QTimer::timeout, this, &FilterPanel::apply);
    for (QComboBox* list : {m_category, m_level, m_channel, m_log, m_accounts})
        connect(list, &QComboBox::currentIndexChanged, this, &FilterPanel::apply);
    for (QCheckBox* checkBox : {m_withStart, m_withEnd, m_hourRange, m_handles, m_exact})
        connect(checkBox, &QCheckBox::toggled, this, &FilterPanel::apply);
    for (QDateEdit* date : {m_start, m_end})
        connect(date, &QDateEdit::dateChanged, this, [this, date] {
            if (m_synchronizing)
                return;
            QCheckBox* bound = date == m_start ? m_withStart : m_withEnd;
            if (!bound->isChecked())
                bound->setChecked(true);
            else
                apply();
        });
    for (QSpinBox* hour : {m_startHour, m_endHour})
        connect(hour, &QSpinBox::valueChanged, this, [this] {
            if (m_hourRange->isChecked())
                apply();
        });
    connect(m_presets, &QComboBox::activated, this, &FilterPanel::applyPreset);

    connect(workspace, &Workspace::filtersChanged, this, &FilterPanel::synchronize);
    connect(workspace, &Workspace::dataModified, this, [this] {
        updateLists();
        loadCompletions();
    });
    loadPresets();
    updateLists();
    loadCompletions();
}

QSize FilterPanel::sizeHint() const
{
    return {std::max(310, minimumSizeHint().width()), 600};
}

QSize FilterPanel::minimumSizeHint() const
{
    const QWidget* content = m_scrollArea->widget();
    const QMargins margins = content->layout()->contentsMargins();
    const int width = std::max(content->minimumSizeHint().width(),
                               m_advanced->minimumSizeHint().width() + margins.left() + margins.right());
    return {width + m_scrollArea->verticalScrollBar()->sizeHint().width() + layout()->contentsMargins().left(),
            QFrame::minimumSizeHint().height()};
}

void FilterPanel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), theme::cardBackground(palette()));
    painter.fillRect(QRect(0, 0, 1, height()), theme::border(palette()));
}

void FilterPanel::updateLists()
{
    m_synchronizing = true;
    m_log->clear();
    m_log->addItem(qtTrId("filters.all_logs"), 0);
    for (const Source& source : m_workspace->sources())
        m_log->addItem(icon("logs"), source.label(), source.id);
    const QDate minimum = m_workspace->firstDate().isValid() ? m_workspace->firstDate() : QDate(1990, 1, 1);
    const QDate maximum = m_workspace->lastDate().isValid() ? m_workspace->lastDate() : QDate::currentDate();
    for (QDateEdit* date : {m_start, m_end})
        date->setDateRange(minimum.addYears(-1), maximum.addYears(1));
    if (!m_withStart->isChecked())
        m_start->setDate(minimum);
    if (!m_withEnd->isChecked())
        m_end->setDate(maximum);
    m_synchronizing = false;
    synchronize();
}

void FilterPanel::loadCompletions()
{
    struct Lists
    {
        QStringList users, process, machines, channels;
    };
    cancel(m_completionsToken);
    m_completionsToken = m_workspace->executor().runCached(
        this, QStringLiteral("completions"),
        [](Context& context) {
            sql::Connection& connection = context.connection;
            return Lists{queries::distinctValues(connection, QStringLiteral("user")),
                         queries::distinctValues(connection, QStringLiteral("process")),
                         queries::distinctValues(connection, QStringLiteral("machine")),
                         queries::distinctValues(connection, QStringLiteral("channel"), 200)};
        },
        [this](Lists lists) {
            m_completionsToken.reset();
            m_users->setStringList(lists.users);
            m_knownProcesses->setStringList(lists.process);
            m_machines->setStringList(lists.machines);
            m_channels = lists.channels;
            updateChannels();
        });
}

void FilterPanel::updateChannels()
{
    m_synchronizing = true;
    const QString current = m_workspace->filters().channel;
    m_channel->clear();
    m_channel->addItem(qtTrId("filters.all_windows_logs"), QString());
    QStringList channels = m_channels;
    if (!current.isEmpty() && !channels.contains(current))
        channels << current;
    for (const QString& channel : std::as_const(channels)) {
        m_channel->addItem(icon("logs"), shortChannelName(channel), channel);
        m_channel->setItemData(m_channel->count() - 1, channel, Qt::ToolTipRole);
    }
    m_channel->setCurrentIndex(std::max(0, m_channel->findData(current)));
    m_synchronizing = false;
}

void FilterPanel::synchronize()
{
    m_synchronizing = true;
    const Filters& f = m_workspace->filters();
    auto text = [](QLineEdit* field, const QString& value) {
        if (field->text() != value && !(field->hasFocus() && field->text().trimmed() == value.trimmed()))
            field->setText(value);
    };
    const QSignalBlocker b1(m_category), b2(m_log), b3(m_accounts), b4(m_withStart), b5(m_withEnd),
        b6(m_hourRange), b7(m_handles), b8(m_exact), b9(m_start), b10(m_end), b11(m_startHour), b12(m_endHour),
        b13(m_level), b14(m_channel);
    text(m_user, f.user);
    text(m_file, f.file);
    text(m_folder, f.folder);
    text(m_process, f.process);
    text(m_machine, f.machine);
    text(m_events, f.events);
    text(m_ip, f.ip);
    m_level->setCurrentIndex(std::max(0, m_level->findData(f.level)));
    if (!f.channel.isEmpty() && m_channel->findData(f.channel) < 0)
        m_channel->addItem(icon("logs"), shortChannelName(f.channel), f.channel);
    m_channel->setCurrentIndex(std::max(0, m_channel->findData(f.channel)));
    m_category->setCurrentIndex(std::max(0, m_category->findData(f.category)));
    m_log->setCurrentIndex(std::max(0, m_log->findData(f.sourceId)));
    m_accounts->setCurrentIndex(std::max(0, m_accounts->findData(static_cast<int>(f.accounts))));
    m_withStart->setChecked(f.since.isValid());
    if (f.since.isValid())
        m_start->setDate(f.since);
    m_withEnd->setChecked(f.until.isValid());
    if (f.until.isValid())
        m_end->setDate(f.until);
    const bool range = f.hourRange();
    m_hourRange->setChecked(range);
    if (range) {
        m_startHour->setValue(f.startHour);
        m_endHour->setValue(f.endHour);
    }
    m_handles->setChecked(f.includeHandle);
    m_exact->setChecked(f.exact);
    if (f.accounts != AccountFilter::All || !f.folder.isEmpty() || !f.process.trimmed().isEmpty()
        || !f.machine.trimmed().isEmpty() || f.sourceId > 0 || range || f.includeHandle || f.exact
        || !f.channel.isEmpty() || !f.ip.trimmed().isEmpty())
        m_advancedButton->setChecked(true);
    m_synchronizing = false;
}

void FilterPanel::apply()
{
    if (m_synchronizing)
        return;
    m_timer.stop();
    Filters f = m_workspace->filters();
    f.user = m_user->text().trimmed();
    f.category = m_category->currentData().toString();
    f.file = m_file->text().trimmed();
    f.folder = m_folder->text().trimmed();
    while (f.folder.size() > 3 && f.folder.endsWith(QLatin1Char('\\')))
        f.folder.chop(1);
    f.process = m_process->text().trimmed();
    f.machine = m_machine->text().trimmed();
    f.events = m_events->text().trimmed();
    f.level = m_level->currentData().toInt();
    f.channel = m_channel->currentData().toString();
    f.ip = m_ip->text().trimmed();
    f.sourceId = m_log->currentData().toLongLong();
    f.since = m_withStart->isChecked() ? m_start->date() : QDate();
    f.until = m_withEnd->isChecked() ? m_end->date() : QDate();
    if (f.since.isValid() && f.until.isValid() && f.since > f.until)
        std::swap(f.since, f.until);
    const bool range = m_hourRange->isChecked() && m_startHour->value() != m_endHour->value();
    f.startHour = range ? m_startHour->value() : -1;
    f.endHour = range ? m_endHour->value() : -1;
    f.accounts = static_cast<AccountFilter>(m_accounts->currentData().toInt());
    f.includeHandle = m_handles->isChecked();
    f.exact = m_exact->isChecked();
    m_workspace->setFilters(f);
}

void FilterPanel::quickPeriod(int days)
{
    Filters f = m_workspace->filters();
    if (days <= 0 || !m_workspace->lastDate().isValid()) {
        f.since = {};
        f.until = {};
    } else {
        f.until = m_workspace->lastDate();
        f.since = f.until.addDays(1 - days);
    }
    m_workspace->setFilters(f);
}

void FilterPanel::loadPresets()
{
    m_presets->clear();
    const QJsonObject presets = readPresets();
    m_presets->addItem(presets.isEmpty() ? qtTrId("filters.no_saved_filter")
                                         : qtTrId("filters.choose"));
    for (auto it = presets.begin(); it != presets.end(); ++it)
        m_presets->addItem(icon("bookmark"), it.key());
    m_presets->setCurrentIndex(0);
}

void FilterPanel::savePreset()
{
    const QString name = MessageDialog::prompt(this, qtTrId("filters.save_filters"),
                                               qtTrId("filters.save_explanation"),
                                               qtTrId("common.save"));
    if (name.isEmpty())
        return;
    QJsonObject presets = readPresets();
    if (presets.contains(name)
        && !MessageDialog::confirm(this, qtTrId("common.replace_question").arg(name),
                                   qtTrId("filters.name_already_used"), qtTrId("common.replace"),
                                   MessageDialog::Type::Warning))
        return;
    presets.insert(name, m_workspace->filters().toJson());
    writePresets(presets);
    loadPresets();
    m_presets->setCurrentIndex(m_presets->findText(name));
}

void FilterPanel::deletePreset()
{
    if (m_presets->currentIndex() <= 0)
        return;
    const QString name = m_presets->currentText();
    if (!MessageDialog::confirm(this, qtTrId("filters.delete_question").arg(name),
                                qtTrId("filters.delete_explanation"), qtTrId("filters.delete"),
                                MessageDialog::Type::Warning))
        return;
    QJsonObject presets = readPresets();
    presets.remove(name);
    writePresets(presets);
    loadPresets();
}

void FilterPanel::applyPreset(int index)
{
    if (index <= 0)
        return;
    const QJsonObject presets = readPresets();
    const QString name = m_presets->itemText(index);
    if (presets.contains(name))
        m_workspace->setFilters(Filters::fromJson(presets.value(name).toObject()));
}

} // namespace lh::ui
