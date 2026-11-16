#include "SourcesDialog.h"

#include "core/Format.h"
#include "core/DateTime.h"
#include "ui/Workspace.h"
#include "ui/Icons.h"
#include "ui/dialogs/MessageDialog.h"
#include "ui/widgets/Components.h"
#include "ui/widgets/TableModel.h"

#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>

namespace lh::ui {

SourcesDialog::SourcesDialog(Workspace* workspace, QWidget* parent) : QDialog(parent), m_workspace(workspace)
{
    setWindowTitle(qtTrId("sources.imported_logs"));
    resize(900, 420);
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    m_summary = new Label({}, Label::Role::Title, this);
    layout->addWidget(m_summary);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(8);
    m_table->setHorizontalHeaderLabels({qtTrId("common.log"), qtTrId("common.machine"),
                                        qtTrId("common.imported_on"), qtTrId("sources.size"),
                                        qtTrId("common.records"), qtTrId("common.events_kept"),
                                        qtTrId("common.duplicates"), qtTrId("sources.original_location")});
    configureTable(m_table);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table, 1);

    auto* actions = new QHBoxLayout;
    auto* importButton = new QPushButton(icon("import"), qtTrId("sources.import_more_logs"), this);
    m_filter = new QPushButton(icon("filters"), qtTrId("sources.filter_log"), this);
    m_remove = new QPushButton(icon("delete"), qtTrId("sources.remove_button"), this);
    for (QPushButton* button : {importButton, m_filter, m_remove})
        button->setAutoDefault(false);
    actions->addWidget(importButton);
    actions->addStretch();
    actions->addWidget(m_filter);
    actions->addWidget(m_remove);
    layout->addLayout(actions);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(qtTrId("common.close"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(importButton, &QPushButton::clicked, this, [this] {
        accept();
        emit importRequested();
    });
    connect(m_filter, &QPushButton::clicked, this, [this] {
        const QList<qint64> ids = selection();
        if (ids.size() != 1)
            return;
        Filters filters = m_workspace->filters();
        filters.sourceId = ids.first();
        m_workspace->setFilters(filters);
        accept();
    });
    connect(m_remove, &QPushButton::clicked, this, [this] {
        const QList<qint64> ids = selection();
        if (ids.isEmpty())
            return;
        const int count = static_cast<int>(ids.size());
        const qint64 first = *std::min_element(ids.cbegin(), ids.cend());
        QStringList following;
        for (const Source& source : m_workspace->sources())
            if (source.duplicates > 0 && source.id > first && !ids.contains(source.id))
                following << source.name;
        QString explanation = qtTrId("sources.remove_explanation", count);
        if (!following.isEmpty())
            explanation += QStringLiteral("\n\n")
                           + qtTrId("sources.remove_duplicates_warning", static_cast<int>(following.size()))
                                 .arg(following.join(QStringLiteral(", ")));
        if (!MessageDialog::confirm(this, qtTrId("sources.remove_question", count), explanation,
                                    qtTrId("sources.remove"), MessageDialog::Type::Warning))
            return;
        accept();
        emit removalRequested(ids);
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this] {
        const qsizetype count = selection().size();
        m_filter->setEnabled(count == 1);
        m_remove->setEnabled(count > 0 && !m_workspace->busy());
    });
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QWidget::customContextMenuRequested, this, [this](const QPoint& position) {
        if (!m_table->indexAt(position).isValid())
            return;
        QMenu menu(this);
        for (QPushButton* button : {m_filter, m_remove})
            menu.addAction(button->icon(), button->text(), button, &QPushButton::click)->setEnabled(button->isEnabled());
        menu.exec(m_table->viewport()->mapToGlobal(position));
    });
    connect(new QShortcut(QKeySequence::Delete, m_table), &QShortcut::activated, m_remove, &QPushButton::click);
    populate();
}

QList<qint64> SourcesDialog::selection() const
{
    QList<qint64> ids;
    for (const QModelIndex& index : m_table->selectionModel()->selectedRows())
        ids.append(m_table->item(index.row(), 0)->data(Qt::UserRole).toLongLong());
    return ids;
}

void SourcesDialog::populate()
{
    const QList<Source>& sources = m_workspace->sources();
    qint64 events = 0;
    m_table->setRowCount(static_cast<int>(sources.size()));
    for (int row = 0; row < sources.size(); ++row) {
        const Source& s = sources[row];
        events += s.events;
        auto count = [](qint64 value) {
            auto* element = new QTableWidgetItem(formatNumber(value));
            element->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            return element;
        };
        auto* name = new QTableWidgetItem(icon("logs"), s.name);
        name->setData(Qt::UserRole, s.id);
        m_table->setItem(row, 0, name);
        m_table->setItem(row, 1, new QTableWidgetItem(s.machine));
        m_table->setItem(row, 2,
                         new QTableWidgetItem(s.importedAt.isValid() ? formatDateTime(s.importedAt.toLocalTime())
                                                                     : QString()));
        auto* size = new QTableWidgetItem(s.size > 0 ? formatSize(s.size) : QString());
        size->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_table->setItem(row, 3, size);
        m_table->setItem(row, 4, count(s.records));
        m_table->setItem(row, 5, count(s.events));
        m_table->setItem(row, 6, count(s.duplicates));
        auto* path = new QTableWidgetItem(QDir::toNativeSeparators(s.path));
        path->setToolTip(path->text());
        m_table->setItem(row, 7, path);
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_summary->setText(sources.isEmpty() ? qtTrId("sources.no_log_imported")
                                         : QStringLiteral("%1 · %2").arg(
                                               qtTrId("common.log_count", static_cast<int>(sources.size())),
                                               qtTrId("sources.events_kept_count", pluralCount(events))));
    m_filter->setEnabled(false);
    m_remove->setEnabled(false);
}

} // namespace lh::ui
