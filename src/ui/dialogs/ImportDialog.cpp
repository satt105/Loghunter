#include "ImportDialog.h"

#include "core/Format.h"
#include "core/DateTime.h"
#include "ui/Workspace.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/dialogs/MessageDialog.h"
#include "ui/widgets/Components.h"
#include "ui/widgets/TableModel.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLoggingCategory>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

namespace lh::ui {

namespace {

Q_LOGGING_CATEGORY(log, "loghunter.import")

QString statusLabel(FileReport::Status status)
{
    switch (status) {
    case FileReport::Status::Imported:
        return qtTrId("import.status_imported");
    case FileReport::Status::AlreadyImported:
        return qtTrId("import.status_already_imported");
    case FileReport::Status::Error:
        return qtTrId("import.status_error");
    case FileReport::Status::Cancelled:
        return qtTrId("import.status_cancelled");
    }
    return {};
}

} // namespace

ImportDialog::ImportDialog(Workspace* workspace, const QStringList& paths, QWidget* parent)
    : QDialog(parent), m_workspace(workspace), m_paths(paths)
{
    setWindowTitle(qtTrId("import.importing_logs"));
    setWindowModality(Qt::WindowModal);
    setMinimumWidth(560);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* body = new QVBoxLayout;
    body->setContentsMargins(24, 24, 28, 24);
    body->setSpacing(16);
    layout->addLayout(body, 1);

    auto* header = new QHBoxLayout;
    header->setSpacing(16);
    m_badge = new IconBadge("import", theme::brand(QApplication::palette()), 44, this);
    header->addWidget(m_badge, 0, Qt::AlignTop);
    auto* texts = new QVBoxLayout;
    texts->setSpacing(4);
    m_title = new Label({}, Label::Role::Title, this);
    m_file = new Label({}, Label::Role::Normal, this);
    m_file->setMinimumWidth(440);
    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 1000);
    m_bar->setTextVisible(false);
    m_bar->setMaximumHeight(8);
    m_details = new Label({}, Label::Role::Small, this);
    texts->addWidget(m_title);
    texts->addWidget(m_file);
    texts->addSpacing(8);
    texts->addWidget(m_bar);
    texts->addWidget(m_details);
    header->addLayout(texts, 1);
    body->addLayout(header);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels({qtTrId("common.log"), qtTrId("import.column_result"),
                                        qtTrId("import.column_events_added"), qtTrId("common.detail")});
    configureTable(m_table);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setMinimumHeight(160);
    m_table->hide();
    body->addWidget(m_table, 1);

    m_footer = new DialogFooter(this);
    m_cancel = m_footer->addButton(qtTrId("common.cancel"));
    connect(m_cancel, &QPushButton::clicked, this, &ImportDialog::reject);
    layout->addWidget(m_footer);

    m_title->setText(m_paths.size() > 1 ? qtTrId("import.importing_log_count", pluralCount(m_paths.size()))
                                        : qtTrId("import.importing_log"));
    m_file->setText(qtTrId("import.preparing"));
    m_timer.setInterval(100);
    connect(&m_timer, &QTimer::timeout, this, &ImportDialog::updateProgress);
}

ImportDialog::~ImportDialog()
{
    cancelAndWait();
}

void ImportDialog::start()
{
    if (m_thread)
        return;
    m_workspace->setBusy(true);
    Session& session = m_workspace->session();
    Importer importer(session.workPath(), session.timeZone(), m_workspace->parameters().ruleConfiguration());
    m_workspace->suspendAccess();
    m_thread = QThread::create([this, importer, paths = m_paths]() mutable {
        try {
            ImportReport report = importer.importFiles(
                paths,
                [this](const ImportProgress& progress) {
                    QMutexLocker lock(&m_lock);
                    m_progress = progress;
                },
                m_cancellation);
            QMutexLocker lock(&m_lock);
            m_report = std::move(report);
        } catch (const std::exception& e) {
            QMutexLocker lock(&m_lock);
            m_error = QString::fromUtf8(e.what());
        }
    });
    m_thread->setObjectName(QStringLiteral("Import"));
    connect(m_thread, &QThread::finished, this, &ImportDialog::finish);
    m_thread->start();
    m_timer.start();
}

void ImportDialog::cancelAndWait()
{
    if (!m_thread)
        return;
    m_cancellation = true;
    m_thread->wait();
    finish();
}

void ImportDialog::reject()
{
    if (!m_thread) {
        QDialog::reject();
        return;
    }
    if (m_cancellation)
        return;
    MessageDialog box(MessageDialog::Type::Warning, qtTrId("import.stop_question"),
                      qtTrId("import.stop_explanation"),
                      this);
    QPushButton* stopButton = box.addButton(qtTrId("import.stop"), true);
    box.addButton(qtTrId("import.continue"));
    box.exec();
    if (box.clickedButton() != stopButton || !m_thread)
        return;
    m_cancellation = true;
    m_file->setText(qtTrId("common.cancelling"));
    m_cancel->setEnabled(false);
}

void ImportDialog::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), theme::cardBackground(palette()));
}

void ImportDialog::updateProgress()
{
    ImportProgress p;
    {
        QMutexLocker lock(&m_lock);
        p = m_progress;
    }
    if (m_cancellation)
        return;
    if (p.phase != m_phase) {
        m_phase = p.phase;
        m_phaseStart.start();
    }
    switch (p.phase) {
    case ImportProgress::Phase::Preparation:
        m_file->setText(qtTrId("import.preparing"));
        m_bar->setRange(0, 0);
        return;
    case ImportProgress::Phase::Saving:
    case ImportProgress::Phase::Finalization:
        m_file->setText(p.phase == ImportProgress::Phase::Saving ? qtTrId("import.saving")
                                                                  : qtTrId("import.finishing"));
        m_bar->setRange(0, 0);
        m_details->setText(QStringLiteral("%1 · %2").arg(
            qtTrId("import.events_found_count", pluralCount(p.events)),
            qtTrId("import.step_running_for").arg(formatDuration(static_cast<double>(m_phaseStart.elapsed()) / 1000))));
        return;
    case ImportProgress::Phase::Detection:
        m_file->setText(qtTrId("import.searching_threats"));
        if (p.toAnalyze > 0) {
            m_bar->setRange(0, 1000);
            m_bar->setValue(static_cast<int>(std::clamp(
                static_cast<double>(p.analyzed) / static_cast<double>(p.toAnalyze), 0.0, 1.0) * 1000));
        } else {
            m_bar->setRange(0, 0);
        }
        m_details->setText(qtTrId("import.events_found_count", pluralCount(p.events)));
        return;
    case ImportProgress::Phase::Analysis:
    case ImportProgress::Phase::Finished:
        break;
    }
    m_bar->setRange(0, 1000);
    const double fraction = p.totalRecords > 0
                                ? static_cast<double>(p.recordsRead) / static_cast<double>(p.totalRecords)
                                : 0.0;
    m_bar->setValue(static_cast<int>(std::clamp(fraction, 0.0, 1.0) * 1000));
    m_file->setText(
        p.fileCount > 1
            ? qtTrId("import.file_position").arg(p.fileName).arg(p.file + 1).arg(p.fileCount)
            : p.fileName);
    QStringList details;
    details << qtTrId("import.events_found_count", pluralCount(p.events));
    if (p.secondsRemaining >= 0)
        details << qtTrId("import.time_remaining").arg(formatDuration(p.secondsRemaining));
    m_details->setText(details.join(QStringLiteral(" · ")));
}

void ImportDialog::finish()
{
    if (!m_thread)
        return;
    m_timer.stop();
    m_thread->wait();
    m_thread->deleteLater();
    m_thread = nullptr;

    ImportReport report;
    QString error;
    {
        QMutexLocker lock(&m_lock);
        report = m_report;
        error = m_error;
    }
    m_workspace->resumeAccess(report.count(FileReport::Status::Imported) > 0);
    m_workspace->setBusy(false);
    QStringList durations;
    for (const auto& [step, seconds] : report.durations)
        durations << QStringLiteral("%1 %2 s").arg(step).arg(seconds, 0, 'f', 2);
    qCInfo(log).noquote() << QStringLiteral("Import: %1 event(s) in %2 s (%3, %4 match(es))%5")
                                 .arg(report.addedEvents())
                                 .arg(report.seconds, 0, 'f', 2)
                                 .arg(durations.join(QStringLiteral(", ")))
                                 .arg(report.detection.matches)
                                 .arg(report.cancelled ? QStringLiteral(" (cancelled)") : QString());
    for (const FileReport& file : report.files)
        if (file.status == FileReport::Status::Error)
            qCWarning(log).noquote() << file.path << ":" << file.message;
    if (!error.isEmpty())
        qCWarning(log).noquote() << "Import interrupted:" << error;

    m_report = report;
    m_error = error;
    showOutcome();
    emit completed(report);
}

void ImportDialog::showOutcome()
{
    const ImportReport& r = m_report;
    const int imported = r.count(FileReport::Status::Imported);
    const int errors = r.count(FileReport::Status::Error);
    const int already = r.count(FileReport::Status::AlreadyImported);
    QString title = errors > 0 ? qtTrId("import.outcome_errors") : qtTrId("import.outcome_finished");
    QString text;
    const char* iconName = errors > 0 ? "warning" : "check";
    QColor color = errors > 0 ? theme::severityColor(Severity::Medium) : theme::successColor();
    if (!m_error.isEmpty()) {
        title = qtTrId("import.outcome_failed");
        text = m_error;
        iconName = "warning";
        color = theme::severityColor(Severity::Critical);
    } else if (r.cancelled) {
        title = qtTrId("import.outcome_cancelled");
        text = qtTrId("import.events_kept_count", pluralCount(r.addedEvents()));
        iconName = "warning";
        color = theme::severityColor(Severity::Medium);
    } else if (r.seconds < 1) {
        text = qtTrId("import.events_imported_count_fast", pluralCount(r.addedEvents()));
    } else {
        text = qtTrId("import.events_imported_count_duration", pluralCount(r.addedEvents()))
                   .arg(formatDuration(r.seconds));
    }
    if (m_error.isEmpty() && !r.cancelled && r.detection.rules > 0)
        text += QLatin1Char(' ') + (r.detection.matches > 0
                                           ? qtTrId("import.alert_count", pluralCount(r.detection.matches))
                                           : qtTrId("import.no_alert"));
    QStringList extras;
    if (imported > 0 && imported != r.files.size())
        extras << qtTrId("common.imported_log_count", imported);
    if (already > 0)
        extras << qtTrId("import.logs_already_present_count", already);
    if (errors > 0)
        extras << qtTrId("import.unreadable_logs_count", errors);
    if (!extras.isEmpty())
        text += QStringLiteral(" (") + extras.join(QStringLiteral(", ")) + QLatin1Char(')');
    m_badge->set(iconName, color);
    m_title->setText(title);
    m_file->setText(text);
    m_file->setWordWrap(true);
    m_bar->hide();
    m_details->hide();

    m_table->setRowCount(static_cast<int>(r.files.size()));
    for (int row = 0; row < r.files.size(); ++row) {
        const FileReport& f = r.files[row];
        auto* name = new QTableWidgetItem(QFileInfo(f.path).fileName());
        name->setToolTip(QDir::toNativeSeparators(f.path));
        name->setIcon(icon(f.status == FileReport::Status::Error ? "warning" : "file"));
        auto* status = new QTableWidgetItem(statusLabel(f.status));
        auto* events = new QTableWidgetItem(formatNumber(f.events));
        events->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QString detail = f.message;
        if (detail.isEmpty() && f.status == FileReport::Status::Imported) {
            QStringList parts;
            if (!f.machine.isEmpty())
                parts << qtTrId("common.computer_name").arg(f.machine);
            if (f.duplicates > 0)
                parts << qtTrId("import.duplicate_events_count", pluralCount(f.duplicates));
            detail = parts.join(QStringLiteral(" · "));
        }
        auto* message = new QTableWidgetItem(detail);
        message->setToolTip(detail);
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, status);
        m_table->setItem(row, 2, events);
        m_table->setItem(row, 3, message);
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setStretchLastSection(true);
    const bool remark = std::any_of(r.files.cbegin(), r.files.cend(), [](const FileReport& f) {
        return f.status == FileReport::Status::Imported && !f.message.isEmpty();
    });
    const bool array = r.files.size() > 1 || errors > 0 || remark;
    m_table->setVisible(array);

    m_footer->clear();
    QPushButton* closeButton = m_footer->addButton(qtTrId("common.close"), true);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    closeButton->setFocus();
    if (array)
        resize(std::max(width(), 720), std::max(height(), 380));
    else
        adjustSize();
}

} // namespace lh::ui
