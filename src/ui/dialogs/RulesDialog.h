// Detection rules: activation, minimum level, addition and removal of the user's Sigma rules.
#pragma once

#include "core/Alerts.h"

#include <QDialog>
#include <QSet>

#include <vector>

class QComboBox;
class QPushButton;
class QTableWidget;

namespace lh::ui {

class Label;
class SearchField;

class RulesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RulesDialog(const AlertParameters& parameters, QWidget* parent = nullptr);

    [[nodiscard]] QStringList disabledRules() const;
    [[nodiscard]] int minimumLevel() const;
    [[nodiscard]] bool filesChanged() const { return m_filesChanged; }

private:
    void reload();
    void refreshRows();
    void refreshRow(int row);
    void applyFilter();
    void updateSummary();
    void updateDetail();
    void setShownEnabled(bool enabled);
    void addRules(const QStringList& paths);
    void removeSelection();

    [[nodiscard]] const sigma::Entry& entryAt(int row) const;
    [[nodiscard]] bool active(const sigma::Entry& entry) const;
    [[nodiscard]] bool shown(const sigma::Entry& entry) const;
    [[nodiscard]] QString stateLabel(const sigma::Entry& entry) const;
    [[nodiscard]] QString stateExplanation(const sigma::Entry& entry) const;
    [[nodiscard]] QStringList selectedFiles() const;

    QString m_folder;
    std::vector<sigma::Entry> m_entries;
    QSet<QString> m_disabled;
    bool m_filesChanged = false;
    bool m_updating = false;
    Label* m_summary;
    SearchField* m_search;
    QComboBox* m_filter;
    QTableWidget* m_table;
    Label* m_description;
    Label* m_note;
    QPushButton* m_remove;
    QComboBox* m_level;
};

} // namespace lh::ui
