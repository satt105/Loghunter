// Logs imported into the project: information, filtering, removal.
#pragma once

#include <QDialog>

class QPushButton;
class QTableWidget;

namespace lh::ui {

class Workspace;
class Label;

class SourcesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SourcesDialog(Workspace* workspace, QWidget* parent = nullptr);

signals:
    void importRequested();
    void removalRequested(const QList<qint64>& identifiers);

private:
    void populate();
    [[nodiscard]] QList<qint64> selection() const;

    Workspace* m_workspace;
    QTableWidget* m_table;
    Label* m_summary;
    QPushButton* m_filter;
    QPushButton* m_remove;
};

} // namespace lh::ui
