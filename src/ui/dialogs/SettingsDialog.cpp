#include "SettingsDialog.h"

#include "core/DateTime.h"
#include "ui/Application.h"
#include "ui/Workspace.h"
#include "ui/widgets/Components.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTimeZone>
#include <QVBoxLayout>

namespace lh::ui {

namespace {

QStringList parseExtensions(const QString& text)
{
    QStringList list;
    for (QString extension : text.split(QRegularExpression(QStringLiteral("[\\s,;]+")), Qt::SkipEmptyParts)) {
        extension = extension.trimmed().toLower();
        if (!extension.startsWith(QLatin1Char('.')))
            extension.prepend(QLatin1Char('.'));
        if (extension.size() > 1 && !list.contains(extension))
            list << extension;
    }
    return list;
}

QLabel* help(const QString& text, QWidget* parent)
{
    auto* labelWidget = new Label(text, Label::Role::Small, parent);
    labelWidget->setWordWrap(true);
    return labelWidget;
}

QString timeZoneLabel(const QByteArray& identifier, const QDateTime& instant)
{
    const int seconds = QTimeZone(identifier).offsetFromUtc(instant);
    const int minutes = std::abs(seconds) / 60;
    return QStringLiteral("%1 (UTC%2%3:%4)")
        .arg(QString::fromLatin1(identifier))
        .arg(QChar(seconds < 0 ? u'-' : u'+'))
        .arg(minutes / 60, 2, 10, QLatin1Char('0'))
        .arg(minutes % 60, 2, 10, QLatin1Char('0'));
}

} // namespace

SettingsDialog::SettingsDialog(const AlertParameters& parameters, const QByteArray& projectTimeZone,
                               Tab tab, QWidget* parent)
    : QDialog(parent), m_parameters(parameters)
{
    setWindowTitle(qtTrId("settings.title"));
    setMinimumWidth(620);
    resize(680, 720);
    auto* layout = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_tabs->tabBar()->setDrawBase(false);
    layout->addWidget(m_tabs, 1);

    // ── General ──
    auto* general = new QWidget(m_tabs);
    auto* generalLayout = new QVBoxLayout(general);
    generalLayout->setSpacing(12);

    auto* appearance = new QGroupBox(qtTrId("settings.section_appearance"), general);
    auto* appearanceForm = new QFormLayout(appearance);
    m_theme = new QComboBox(appearance);
    m_theme->addItem(qtTrId("common.same_as_system"), static_cast<int>(ThemeMode::System));
    m_theme->addItem(qtTrId("common.theme_light"), static_cast<int>(ThemeMode::Light));
    m_theme->addItem(qtTrId("common.theme_dark"), static_cast<int>(ThemeMode::Dark));
    m_theme->setCurrentIndex(m_theme->findData(static_cast<int>(theme::mode())));
    appearanceForm->addRow(qtTrId("settings.theme"), m_theme);
    const QStringList languages = availableLanguages();
    m_language = new QComboBox(appearance);
    m_language->addItem(qtTrId("common.same_as_system"), QString());
    for (const QString& code : languages)
        m_language->addItem(languageName(code), code);
    m_language->setCurrentIndex(std::max(0, m_language->findData(chosenLanguage())));
    appearanceForm->addRow(qtTrId("settings.language"), m_language);
    const QDate sample(QDate::currentDate().year(), 12, 31);
    m_dateFormat = new QComboBox(appearance);
    m_dateFormat->addItem(qtTrId("settings.date_format_language").arg(sample.toString(qtTrId("format.date"))),
                          QString());
    for (const QString& format : dateFormats())
        m_dateFormat->addItem(sample.toString(format), format);
    m_dateFormat->setCurrentIndex(std::max(0, m_dateFormat->findData(chosenDateFormat())));
    appearanceForm->addRow(qtTrId("settings.date_format"), m_dateFormat);
    appearanceForm->addRow(help(qtTrId("settings.language_restart"), appearance));
    generalLayout->addWidget(appearance);

    auto* hour = new QGroupBox(qtTrId("common.time_zone"), general);
    auto* hourForm = new QFormLayout(hour);
    m_timeZone = new QComboBox(hour);
    m_timeZone->setEditable(true);
    m_timeZone->setInsertPolicy(QComboBox::NoInsert);
    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QByteArray utc = QByteArrayLiteral("UTC");
    m_timeZone->addItem(qtTrId("settings.computer_time").arg(timeZoneLabel(systemTimeZone(), now)),
                        QByteArray());
    m_timeZone->addItem(qtTrId("settings.time_zone_utc"), utc);
    for (const QByteArray& identifier : QTimeZone::availableTimeZoneIds())
        if (identifier != utc)
            m_timeZone->addItem(timeZoneLabel(identifier, now), identifier);
    m_timeZone->completer()->setFilterMode(Qt::MatchContains);
    m_timeZone->completer()->setCompletionMode(QCompleter::PopupCompletion);
    m_timeZone->lineEdit()->setPlaceholderText(qtTrId("settings.time_zone_placeholder"));
    const QByteArray current = Workspace::defaultTimeZone().isEmpty() && projectTimeZone == systemTimeZone() ? QByteArray()
                                                                                                                : projectTimeZone;
    const int index = current.isEmpty() ? 0 : m_timeZone->findData(current);
    m_timeZone->setCurrentIndex(std::max(0, index));
    hourForm->addRow(qtTrId("settings.show_times"), m_timeZone);
    hourForm->addRow(help(qtTrId("settings.time_zone_help"),
                          hour));
    generalLayout->addWidget(hour);
    generalLayout->addStretch();
    m_tabs->addTab(general, qtTrId("settings.section_general"));

    // ── Analysis ──
    auto* detection = new QWidget(m_tabs);
    auto* detectionLayout = new QVBoxLayout(detection);
    detectionLayout->setSpacing(12);

    auto* types = new QGroupBox(qtTrId("settings.section_analysis_types"), detection);
    auto* typesLayout = new QVBoxLayout(types);
    m_ruleAnalysis = new QCheckBox(qtTrId("window.analysis_rules"), types);
    m_ruleAnalysis->setToolTip(qtTrId("window.analysis_rules_help"));
    m_fileAnalysis = new QCheckBox(qtTrId("window.analysis_files"), types);
    m_fileAnalysis->setToolTip(qtTrId("window.analysis_files_help"));
    m_mitre = new QCheckBox(qtTrId("window.analysis_mitre"), types);
    m_mitre->setToolTip(qtTrId("window.analysis_mitre_help"));
    for (QCheckBox* typeCheckBox : {m_ruleAnalysis, m_fileAnalysis, m_mitre})
        typesLayout->addWidget(typeCheckBox);
    typesLayout->addWidget(help(qtTrId("settings.analysis_types_help"), types));
    detectionLayout->addWidget(types);

    auto* rules = new QGroupBox(qtTrId("settings.section_rules"), detection);
    auto* rulesRow = new QHBoxLayout(rules);
    rulesRow->addWidget(help(qtTrId("settings.rules_help"), rules), 1);
    auto* rulesButton = new QPushButton(qtTrId("settings.manage_rules"), rules);
    rulesButton->setAutoDefault(false);
    connect(rulesButton, &QPushButton::clicked, this, [this] {
        m_rulesRequested = true;
        accept();
    });
    rulesRow->addWidget(rulesButton);
    detectionLayout->addWidget(rules);

    auto* thresholds = new QGroupBox(qtTrId("settings.section_thresholds"), detection);
    auto* thresholdsForm = new QFormLayout(thresholds);
    m_deletionThreshold = new QSpinBox(thresholds);
    m_deletionThreshold->setRange(1, 1'000'000);
    m_deletionThreshold->setSuffix(qtTrId("settings.deletions_per_day_suffix"));
    thresholdsForm->addRow(qtTrId("settings.mass_deletions"), m_deletionThreshold);
    auto* bursts = new QHBoxLayout;
    m_burstActions = new QSpinBox(thresholds);
    m_burstActions->setRange(2, 1'000'000);
    m_burstActions->setSuffix(qtTrId("settings.actions_suffix"));
    m_burstSeconds = new QSpinBox(thresholds);
    m_burstSeconds->setRange(1, 86'400);
    m_burstSeconds->setSuffix(qtTrId("settings.seconds_suffix"));
    bursts->addWidget(m_burstActions, 1);
    bursts->addWidget(new QLabel(qtTrId("settings.bursts_in"), thresholds));
    bursts->addWidget(m_burstSeconds, 1);
    thresholdsForm->addRow(qtTrId("settings.bursts"), bursts);
    detectionLayout->addWidget(thresholds);

    auto* schedule = new QGroupBox(qtTrId("settings.section_working_hours"), detection);
    auto* hoursForm = new QFormLayout(schedule);
    auto* range = new QHBoxLayout;
    m_opening = new QSpinBox(schedule);
    m_opening->setRange(0, 23);
    m_opening->setSuffix(qtTrId("common.hours_suffix"));
    m_closing = new QSpinBox(schedule);
    m_closing->setRange(1, 24);
    m_closing->setSuffix(qtTrId("common.hours_suffix"));
    range->addWidget(new QLabel(qtTrId("common.from"), schedule));
    range->addWidget(m_opening, 1);
    range->addWidget(new QLabel(qtTrId("common.to"), schedule));
    range->addWidget(m_closing, 1);
    hoursForm->addRow(qtTrId("settings.normal_activity"), range);
    m_weekend = new QCheckBox(qtTrId("settings.weekend_out_of_hours"), schedule);
    hoursForm->addRow(m_weekend);
    detectionLayout->addWidget(schedule);

    auto* scope = new QGroupBox(qtTrId("settings.section_watched"), detection);
    auto* scopeForm = new QFormLayout(scope);
    m_ignoreSystem = new QCheckBox(qtTrId("settings.ignore_system_accounts"), scope);
    scopeForm->addRow(m_ignoreSystem);
    m_executables = new QLineEdit(scope);
    scopeForm->addRow(qtTrId("settings.executables_scripts"), m_executables);
    m_ransomware = new QLineEdit(scope);
    scopeForm->addRow(qtTrId("settings.ransomware"), m_ransomware);
    scopeForm->addRow(help(qtTrId("settings.extensions_help"), scope));
    detectionLayout->addWidget(scope);
    for (QWidget* group : std::initializer_list<QWidget*>{thresholds, schedule, scope})
        connect(m_fileAnalysis, &QCheckBox::toggled, group, &QWidget::setEnabled);

    auto* defaultsButton = new QPushButton(qtTrId("settings.restore_defaults"), detection);
    defaultsButton->setAutoDefault(false);
    connect(defaultsButton, &QPushButton::clicked, this, [this] { populateDetection(AlertParameters()); });
    auto* defaultRow = new QHBoxLayout;
    defaultRow->addStretch();
    defaultRow->addWidget(defaultsButton);
    detectionLayout->addLayout(defaultRow);
    detectionLayout->addStretch();
    auto* scrollArea = new QScrollArea(m_tabs);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    scrollArea->setWidget(detection);
    m_tabs->addTab(scrollArea, qtTrId("settings.section_analysis"));
    populateDetection(parameters);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(qtTrId("settings.apply"));
    buttons->button(QDialogButtonBox::Cancel)->setText(qtTrId("common.cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    m_tabs->setCurrentIndex(tab == Tab::Detection ? 1 : 0);
}

void SettingsDialog::populateDetection(const AlertParameters& p)
{
    m_deletionThreshold->setValue(p.deletionThreshold);
    m_opening->setValue(p.openingHour);
    m_closing->setValue(p.closingHour);
    m_weekend->setChecked(p.weekendOffHours);
    m_burstActions->setValue(p.burstActions);
    m_burstSeconds->setValue(p.burstSeconds);
    m_ignoreSystem->setChecked(p.ignoreSystemAccounts);
    m_executables->setText(p.executableExtensions.join(QLatin1Char(' ')));
    m_ransomware->setText(p.ransomwareExtensions.join(QLatin1Char(' ')));
    m_ruleAnalysis->setChecked(p.ruleAnalysis);
    m_fileAnalysis->setChecked(p.fileAnalysis);
    m_mitre->setChecked(p.mitre);
}

ThemeMode SettingsDialog::theme() const
{
    return static_cast<ThemeMode>(m_theme->currentData().toInt());
}

QString SettingsDialog::language() const
{
    return m_language->currentData().toString();
}

QString SettingsDialog::dateFormat() const
{
    return m_dateFormat->currentData().toString();
}

QByteArray SettingsDialog::timeZone() const
{
    const QString text = m_timeZone->currentText().trimmed();
    int index = m_timeZone->findText(text);
    if (index < 0 && !text.isEmpty())
        index = m_timeZone->findText(text, Qt::MatchContains);
    if (index >= 0)
        return m_timeZone->itemData(index).toByteArray();
    const QByteArray entered = text.toLatin1();
    return isValidTimeZone(entered) ? entered : QByteArray();
}

AlertParameters SettingsDialog::parameters() const
{
    AlertParameters p = m_parameters;
    p.deletionThreshold = m_deletionThreshold->value();
    p.openingHour = m_opening->value();
    p.closingHour = std::max(m_closing->value(), m_opening->value() + 1);
    p.weekendOffHours = m_weekend->isChecked();
    p.burstActions = m_burstActions->value();
    p.burstSeconds = m_burstSeconds->value();
    p.ignoreSystemAccounts = m_ignoreSystem->isChecked();
    p.executableExtensions = parseExtensions(m_executables->text());
    p.ransomwareExtensions = parseExtensions(m_ransomware->text());
    p.ruleAnalysis = m_ruleAnalysis->isChecked();
    p.fileAnalysis = m_fileAnalysis->isChecked();
    p.mitre = m_mitre->isChecked();
    return p;
}

} // namespace lh::ui
