// Summary report options (format, content, destination).
#pragma once

#include "render/Report.h"

#include <QDialog>

class QCheckBox;
class QLineEdit;
class QRadioButton;
class QSpinBox;

namespace lh::ui {

class ReportDialog : public QDialog
{
    Q_OBJECT

public:
    ReportDialog(const QString& project, const QString& scope, QWidget* parent = nullptr);

    [[nodiscard]] report::Options options() const;
    [[nodiscard]] QString path() const;
    [[nodiscard]] bool openAfter() const;

protected:
    void accept() override;

private:
    void choosePath();
    void adjustExtension();

    QString m_project;
    QLineEdit* m_title;
    QLineEdit* m_author;
    QRadioButton* m_pdf;
    QRadioButton* m_html;
    QCheckBox* m_charts;
    QCheckBox* m_rankings;
    QCheckBox* m_alerts;
    QCheckBox* m_logs;
    QSpinBox* m_rows;
    QLineEdit* m_path;
    QCheckBox* m_open;
};

} // namespace lh::ui
