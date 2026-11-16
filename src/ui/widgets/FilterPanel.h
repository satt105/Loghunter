// Side panel of the filters.
#pragma once

#include "ui/Executor.h"

#include <QFrame>
#include <QTimer>

class QCheckBox;
class QComboBox;
class QCompleter;
class QDateEdit;
class QLineEdit;
class QScrollArea;
class QSpinBox;
class QStringListModel;
class QToolButton;

namespace lh::ui {

class Workspace;

class FilterPanel : public QFrame
{
    Q_OBJECT

public:
    explicit FilterPanel(Workspace* workspace, QWidget* parent = nullptr);
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

signals:
    void closeRequested();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void synchronize();
    void apply();
    void updateLists();
    void updateChannels();
    void loadCompletions();
    void quickPeriod(int days);
    void loadPresets();
    void savePreset();
    void deletePreset();
    void applyPreset(int index);

    Workspace* m_workspace;
    bool m_synchronizing = false;
    QTimer m_timer;
    TokenPtr m_completionsToken;

    QLineEdit* m_user;
    QComboBox* m_category;
    QLineEdit* m_events;
    QComboBox* m_level;
    QComboBox* m_channel;
    QLineEdit* m_ip;
    QLineEdit* m_file;
    QLineEdit* m_folder;
    QLineEdit* m_process;
    QLineEdit* m_machine;
    QComboBox* m_log;
    QCheckBox* m_withStart;
    QDateEdit* m_start;
    QCheckBox* m_withEnd;
    QDateEdit* m_end;
    QCheckBox* m_hourRange;
    QSpinBox* m_startHour;
    QSpinBox* m_endHour;
    QComboBox* m_accounts;
    QCheckBox* m_handles;
    QCheckBox* m_exact;
    QComboBox* m_presets;
    QToolButton* m_advancedButton;
    QWidget* m_advanced;
    QScrollArea* m_scrollArea;
    QStringListModel* m_users;
    QStringListModel* m_knownProcesses;
    QStringListModel* m_machines;
    QStringList m_channels;
};

} // namespace lh::ui
