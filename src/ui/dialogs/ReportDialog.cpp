#include "ReportDialog.h"

#include "ui/Icons.h"
#include "ui/Settings.h"
#include "ui/dialogs/MessageDialog.h"
#include "ui/widgets/Components.h"

#include <QCheckBox>
#include <QDate>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace lh::ui {

ReportDialog::ReportDialog(const QString& project, const QString& scope, QWidget* parent)
    : QDialog(parent), m_project(project)
{
    setWindowTitle(qtTrId("report.create_report"));
    setMinimumWidth(560);
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(12);

    auto* intro = new Label(qtTrId("report.scope_filters").arg(scope),
                            Label::Role::Secondary, this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto* document = new QGroupBox(qtTrId("report.document"), this);
    auto* shape = new QFormLayout(document);
    m_title = new QLineEdit(report::Options().title, document);
    m_author = new QLineEdit(settings::read(settings::key::REPORT_AUTHOR).toString(), document);
    m_author->setPlaceholderText(qtTrId("report.optional"));
    shape->addRow(qtTrId("report.title"), m_title);
    shape->addRow(qtTrId("report.analyst"), m_author);
    auto* formats = new QHBoxLayout;
    m_pdf = new QRadioButton(qtTrId("report.format_pdf"), document);
    m_html = new QRadioButton(qtTrId("report.format_html"), document);
    formats->addWidget(m_pdf);
    formats->addWidget(m_html);
    formats->addStretch();
    shape->addRow(qtTrId("report.format"), formats);
    (settings::read(settings::key::REPORT_FORMAT).toString() == QLatin1String("html") ? m_html : m_pdf)
        ->setChecked(true);
    layout->addWidget(document);

    auto* content = new QGroupBox(qtTrId("report.contents"), this);
    auto* contentLayout = new QVBoxLayout(content);
    m_charts = new QCheckBox(qtTrId("report.activity_charts"), content);
    m_rankings = new QCheckBox(qtTrId("report.most_active_users_files_folders"), content);
    m_alerts = new QCheckBox(qtTrId("report.detailed_alerts"), content);
    m_logs = new QCheckBox(qtTrId("report.logs_analyzed_list"), content);
    for (QCheckBox* option : {m_charts, m_rankings, m_alerts, m_logs}) {
        option->setChecked(true);
        contentLayout->addWidget(option);
    }
    auto* rows = new QHBoxLayout;
    rows->setContentsMargins(24, 0, 0, 0);
    rows->addWidget(new QLabel(qtTrId("report.findings_by_alert"), content));
    m_rows = new QSpinBox(content);
    m_rows->setRange(5, 1000);
    m_rows->setValue(25);
    rows->addWidget(m_rows);
    rows->addStretch();
    contentLayout->addLayout(rows);
    connect(m_alerts, &QCheckBox::toggled, m_rows, &QWidget::setEnabled);
    layout->addWidget(content);

    auto* destination = new QGroupBox(qtTrId("report.destination"), this);
    auto* destinationLayout = new QVBoxLayout(destination);
    auto* pathRow = new QHBoxLayout;
    const QString folder = settings::rememberedFolder(settings::key::REPORTS_FOLDER);
    const QString base = qtTrId("report.file_name")
                             .arg(project.isEmpty() ? QStringLiteral("LogHunter") : project,
                                  QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd")));
    m_path = new QLineEdit(QDir::toNativeSeparators(QDir(folder).filePath(base + QStringLiteral(".pdf"))), destination);
    auto* browseButton = new QPushButton(icon("open"), qtTrId("report.browse"), destination);
    browseButton->setAutoDefault(false);
    pathRow->addWidget(m_path, 1);
    pathRow->addWidget(browseButton);
    destinationLayout->addLayout(pathRow);
    m_open = new QCheckBox(qtTrId("report.open_after"), destination);
    m_open->setChecked(true);
    destinationLayout->addWidget(m_open);
    layout->addWidget(destination);
    connect(browseButton, &QPushButton::clicked, this, &ReportDialog::choosePath);
    connect(m_pdf, &QRadioButton::toggled, this, &ReportDialog::adjustExtension);
    adjustExtension();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(qtTrId("report.generate"));
    buttons->button(QDialogButtonBox::Ok)->setIcon(icon("report"));
    buttons->button(QDialogButtonBox::Cancel)->setText(qtTrId("common.cancel"));
    connect(buttons, &QDialogButtonBox::accepted, this, &ReportDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

void ReportDialog::adjustExtension()
{
    QString path = m_path->text().trimmed();
    const QFileInfo info(path);
    const QString extension = m_pdf->isChecked() ? QStringLiteral(".pdf") : QStringLiteral(".html");
    path = QDir::toNativeSeparators(QDir(info.path()).filePath(info.completeBaseName() + extension));
    m_path->setText(path);
}

void ReportDialog::choosePath()
{
    const bool pdf = m_pdf->isChecked();
    const QString path = QFileDialog::getSaveFileName(
        this, qtTrId("report.save_report"), m_path->text(),
        pdf ? qtTrId("report.pdf_file_filter") : qtTrId("report.html_file_filter"));
    if (!path.isEmpty())
        m_path->setText(QDir::toNativeSeparators(path));
}

void ReportDialog::accept()
{
    const QFileInfo info(m_path->text().trimmed());
    if (info.fileName().isEmpty() || !QFileInfo(info.path()).isDir()) {
        MessageDialog::warn(this, qtTrId("report.invalid_destination"),
                            qtTrId("report.invalid_destination_explanation"));
        return;
    }
    if (info.exists()
        && !MessageDialog::confirm(this, qtTrId("common.replace_question").arg(info.fileName()),
                                   qtTrId("report.file_exists"), qtTrId("common.replace"),
                                   MessageDialog::Type::Warning))
        return;
    settings::write(settings::key::REPORTS_FOLDER, info.path());
    settings::write(settings::key::REPORT_AUTHOR, m_author->text().trimmed());
    settings::write(settings::key::REPORT_FORMAT, m_pdf->isChecked() ? QStringLiteral("pdf") : QStringLiteral("html"));
    QDialog::accept();
}

report::Options ReportDialog::options() const
{
    report::Options options;
    options.format = m_pdf->isChecked() ? report::Options::Format::Pdf : report::Options::Format::Html;
    options.title = m_title->text().trimmed().isEmpty() ? options.title : m_title->text().trimmed();
    options.author = m_author->text().trimmed();
    options.project = m_project;
    options.charts = m_charts->isChecked();
    options.rankings = m_rankings->isChecked();
    options.alerts = m_alerts->isChecked();
    options.logs = m_logs->isChecked();
    options.rowsPerAlert = m_rows->value();
    return options;
}

QString ReportDialog::path() const
{
    return QDir::fromNativeSeparators(m_path->text().trimmed());
}

bool ReportDialog::openAfter() const
{
    return m_open->isChecked();
}

} // namespace lh::ui
