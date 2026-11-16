// Settings: appearance, language, date format, display time zone and detection thresholds.
#pragma once

#include "core/Alerts.h"
#include "ui/Theme.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QSpinBox;
class QTabWidget;

namespace lh::ui {

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Tab { General, Detection };

    SettingsDialog(const AlertParameters& parameters, const QByteArray& projectTimeZone, Tab tab,
                   QWidget* parent = nullptr);

    [[nodiscard]] ThemeMode theme() const;
    [[nodiscard]] QString language() const;
    [[nodiscard]] QString dateFormat() const;
    [[nodiscard]] QByteArray timeZone() const;
    [[nodiscard]] AlertParameters parameters() const;
    [[nodiscard]] bool rulesRequested() const { return m_rulesRequested; }

private:
    void populateDetection(const AlertParameters& parameters);

    AlertParameters m_parameters;
    bool m_rulesRequested = false;
    QTabWidget* m_tabs;
    QComboBox* m_theme;
    QComboBox* m_language;
    QComboBox* m_dateFormat;
    QComboBox* m_timeZone;
    QSpinBox* m_deletionThreshold;
    QSpinBox* m_opening;
    QSpinBox* m_closing;
    QCheckBox* m_weekend;
    QSpinBox* m_burstActions;
    QSpinBox* m_burstSeconds;
    QCheckBox* m_ignoreSystem;
    QLineEdit* m_executables;
    QLineEdit* m_ransomware;
    QCheckBox* m_ruleAnalysis;
    QCheckBox* m_fileAnalysis;
    QCheckBox* m_mitre;
};

} // namespace lh::ui
