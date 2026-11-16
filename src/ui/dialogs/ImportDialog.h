// Background log import: progress, cancellation and outcome.
#pragma once

#include "core/Import.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QMutex>
#include <QTimer>

#include <atomic>

class QProgressBar;
class QPushButton;
class QTableWidget;
class QThread;

namespace lh::ui {

class Workspace;
class Label;
class IconBadge;
class DialogFooter;

class ImportDialog : public QDialog
{
    Q_OBJECT

public:
    ImportDialog(Workspace* workspace, const QStringList& paths, QWidget* parent = nullptr);
    ~ImportDialog() override;

    void start();
    [[nodiscard]] bool isRunning() const { return m_thread != nullptr; }
    void cancelAndWait();

signals:
    void completed(const lh::ImportReport& report);

protected:
    void reject() override;
    void paintEvent(QPaintEvent* event) override;

private:
    void updateProgress();
    void finish();
    void showOutcome();

    Workspace* m_workspace;
    QStringList m_paths;
    QThread* m_thread = nullptr;
    std::atomic_bool m_cancellation{false};
    QMutex m_lock;
    ImportProgress m_progress;
    ImportReport m_report;
    QString m_error;
    QTimer m_timer;
    ImportProgress::Phase m_phase = ImportProgress::Phase::Preparation;
    QElapsedTimer m_phaseStart;

    IconBadge* m_badge;
    Label* m_title;
    Label* m_file;
    QProgressBar* m_bar;
    Label* m_details;
    QTableWidget* m_table;
    DialogFooter* m_footer;
    QPushButton* m_cancel;
};

} // namespace lh::ui
