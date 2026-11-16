#include "RulesDialog.h"

#include "core/Format.h"
#include "ui/Icons.h"
#include "ui/Settings.h"
#include "ui/Theme.h"
#include "ui/dialogs/MessageDialog.h"
#include "ui/widgets/Components.h"
#include "ui/widgets/TableModel.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPushButton>
#include <QShortcut>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace lh::ui {

namespace {

constexpr int ROLE_ENTRY = Qt::UserRole;
constexpr int ROLE_SORT = Qt::UserRole + 1;
constexpr int MAX_REJECTIONS_SHOWN = 5;

enum { COLUMN_RULE, COLUMN_LEVEL, COLUMN_ORIGIN, COLUMN_STATE, COLUMN_COUNT };
enum { FILTER_ALL, FILTER_BUILTIN, FILTER_ADDED, FILTER_UNUSED };

class SortedItem : public QTableWidgetItem
{
public:
    using QTableWidgetItem::QTableWidgetItem;

    bool operator<(const QTableWidgetItem& other) const override
    {
        const QVariant key = data(ROLE_SORT);
        const QVariant otherKey = other.data(ROLE_SORT);
        if (key.isValid() && otherKey.isValid())
            return key.toInt() < otherKey.toInt();
        return text().localeAwareCompare(other.text()) < 0;
    }
};

QString levelLabel(sigma::Level level)
{
    switch (level) {
    case sigma::Level::Critical:
        return qtTrId("alerts.severity_critical");
    case sigma::Level::High:
        return qtTrId("alerts.severity_high");
    case sigma::Level::Medium:
        return qtTrId("alerts.severity_medium");
    case sigma::Level::Low:
        return qtTrId("alerts.severity_low");
    case sigma::Level::Information:
        break;
    }
    return qtTrId("rules.level_informational");
}

QString unusableLabel(sigma::Entry::State state)
{
    switch (state) {
    case sigma::Entry::State::NotApplicable:
        return qtTrId("rules.state_not_applicable");
    case sigma::Entry::State::Deprecated:
        return qtTrId("rules.state_deprecated");
    case sigma::Entry::State::Duplicate:
        return qtTrId("rules.state_duplicate");
    case sigma::Entry::State::Invalid:
    case sigma::Entry::State::Usable:
        break;
    }
    return qtTrId("rules.state_invalid");
}

} // namespace

RulesDialog::RulesDialog(const AlertParameters& parameters, QWidget* parent)
    : QDialog(parent), m_folder(sigma::userFolder()),
      m_disabled(parameters.disabledRules.cbegin(), parameters.disabledRules.cend())
{
    setWindowTitle(qtTrId("rules.title"));
    resize(940, 640);
    auto* layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    m_summary = new Label({}, Label::Role::Title, this);
    layout->addWidget(m_summary);
    auto* explanation = new Label(qtTrId("rules.explanation"), Label::Role::Small, this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    if (!parameters.ruleAnalysis) {
        auto* notice = new Label(qtTrId("rules.analysis_disabled"), Label::Role::Normal, this);
        notice->setWordWrap(true);
        notice->setColor(theme::severityColor(Severity::Medium));
        layout->addWidget(notice);
    }

    auto* filters = new QHBoxLayout;
    m_search = new SearchField(qtTrId("rules.search_placeholder"), this);
    m_filter = new QComboBox(this);
    m_filter->addItem(qtTrId("rules.filter_all"), FILTER_ALL);
    m_filter->addItem(qtTrId("rules.filter_builtin"), FILTER_BUILTIN);
    m_filter->addItem(qtTrId("rules.filter_added"), FILTER_ADDED);
    m_filter->addItem(qtTrId("rules.filter_unused"), FILTER_UNUSED);
    filters->addWidget(m_search, 1);
    filters->addWidget(m_filter);
    layout->addLayout(filters);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(COLUMN_COUNT);
    m_table->setHorizontalHeaderLabels({qtTrId("rules.column_rule"), qtTrId("rules.column_level"),
                                        qtTrId("rules.column_origin"), qtTrId("rules.column_state")});
    configureTable(m_table);
    m_table->setTextElideMode(Qt::ElideRight);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(COLUMN_RULE, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSortIndicator(COLUMN_RULE, Qt::AscendingOrder);
    layout->addWidget(m_table, 1);

    m_description = new Label({}, Label::Role::Normal, this);
    m_description->setWordWrap(true);
    m_description->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_description->setMinimumHeight(fontMetrics().height() * 2);
    m_description->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_note = new Label({}, Label::Role::Small, this);
    m_note->setWordWrap(true);
    layout->addWidget(m_description);
    layout->addWidget(m_note);

    auto* actions = new QHBoxLayout;
    auto* addFiles = new QPushButton(icon("plus"), qtTrId("rules.add_files"), this);
    auto* addFolder = new QPushButton(icon("folder"), qtTrId("rules.add_folder"), this);
    auto* enableAll = new QPushButton(qtTrId("rules.enable_all"), this);
    auto* disableAll = new QPushButton(qtTrId("rules.disable_all"), this);
    m_remove = new QPushButton(icon("delete"), qtTrId("rules.remove_button"), this);
    m_remove->setToolTip(qtTrId("rules.remove_help"));
    for (QPushButton* button : {enableAll, disableAll})
        button->setToolTip(qtTrId("rules.shown_rules_help"));
    for (QPushButton* button : {addFiles, addFolder, enableAll, disableAll, m_remove})
        button->setAutoDefault(false);
    actions->addWidget(addFiles);
    actions->addWidget(addFolder);
    actions->addStretch();
    actions->addWidget(enableAll);
    actions->addWidget(disableAll);
    actions->addWidget(m_remove);
    layout->addLayout(actions);

    auto* footer = new QHBoxLayout;
    m_level = new QComboBox(this);
    for (const sigma::Level level : {sigma::Level::Information, sigma::Level::Low, sigma::Level::Medium,
                                     sigma::Level::High, sigma::Level::Critical})
        m_level->addItem(levelLabel(level), static_cast<int>(level));
    m_level->setCurrentIndex(std::max(0, m_level->findData(parameters.minimumRuleLevel)));
    m_level->setToolTip(qtTrId("rules.minimum_level_help"));
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(qtTrId("common.close"));
    buttons->button(QDialogButtonBox::Close)->setAutoDefault(false);
    footer->addWidget(new QLabel(qtTrId("rules.minimum_level"), this));
    footer->addWidget(m_level);
    footer->addStretch();
    footer->addWidget(buttons);
    layout->addLayout(footer);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_search, &SearchField::searchChanged, this, &RulesDialog::applyFilter);
    connect(m_filter, &QComboBox::currentIndexChanged, this, &RulesDialog::applyFilter);
    connect(m_level, &QComboBox::currentIndexChanged, this, &RulesDialog::refreshRows);
    connect(enableAll, &QPushButton::clicked, this, [this] { setShownEnabled(true); });
    connect(disableAll, &QPushButton::clicked, this, [this] { setShownEnabled(false); });
    connect(m_remove, &QPushButton::clicked, this, &RulesDialog::removeSelection);
    connect(new QShortcut(QKeySequence::Delete, m_table), &QShortcut::activated, m_remove, &QPushButton::click);
    connect(addFiles, &QPushButton::clicked, this, [this] {
        const QStringList paths =
            QFileDialog::getOpenFileNames(this, qtTrId("rules.choose_files"),
                                          settings::rememberedFolder(settings::key::RULES_FOLDER),
                                          qtTrId("rules.file_filter"));
        if (paths.isEmpty())
            return;
        settings::rememberFolder(settings::key::RULES_FOLDER, paths.first());
        addRules(paths);
    });
    connect(addFolder, &QPushButton::clicked, this, [this] {
        const QString folder = QFileDialog::getExistingDirectory(
            this, qtTrId("rules.choose_folder"), settings::rememberedFolder(settings::key::RULES_FOLDER));
        if (folder.isEmpty())
            return;
        settings::rememberFolder(settings::key::RULES_FOLDER, folder);
        addRules({folder});
    });
    connect(m_table, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* item) {
        if (m_updating || item->column() != COLUMN_RULE)
            return;
        const sigma::Entry& entry = entryAt(item->row());
        if (item->checkState() == Qt::Checked)
            m_disabled.remove(entry.id);
        else
            m_disabled.insert(entry.id);
        m_updating = true;
        refreshRow(item->row());
        m_updating = false;
        updateSummary();
        updateDetail();
    });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, &RulesDialog::updateDetail);
    reload();
    m_table->selectRow(0);
}

// ── Result ───────────────────────────────────────────────────────────────────

QStringList RulesDialog::disabledRules() const
{
    QStringList rules;
    for (const sigma::Entry& entry : m_entries)
        if (entry.state == sigma::Entry::State::Usable && m_disabled.contains(entry.id))
            rules << entry.id;
    rules.sort();
    return rules;
}

int RulesDialog::minimumLevel() const
{
    return m_level->currentData().toInt();
}

// ── List ─────────────────────────────────────────────────────────────────────

void RulesDialog::reload()
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    m_updating = true;
    m_table->setSortingEnabled(false);
    m_table->setCurrentItem(nullptr);
    m_table->clearSelection();
    m_table->clearContents();
    m_entries = sigma::library(m_folder);
    m_table->setRowCount(static_cast<int>(m_entries.size()));
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const sigma::Entry& entry = m_entries[static_cast<size_t>(row)];
        auto* rule = new SortedItem(entry.title);
        rule->setData(ROLE_ENTRY, row);
        rule->setToolTip(entry.title);
        if (entry.state != sigma::Entry::State::Usable)
            rule->setFlags(rule->flags() & ~Qt::ItemIsUserCheckable);
        auto* level = new SortedItem(dot(theme::severityColor(severityFromLevel(static_cast<int>(entry.level)))),
                                     levelLabel(entry.level));
        level->setData(ROLE_SORT, static_cast<int>(entry.level));
        m_table->setItem(row, COLUMN_RULE, rule);
        m_table->setItem(row, COLUMN_LEVEL, level);
        m_table->setItem(row, COLUMN_ORIGIN, new SortedItem(entry.builtin ? qtTrId("rules.origin_builtin")
                                                                           : qtTrId("rules.origin_added")));
        m_table->setItem(row, COLUMN_STATE, new SortedItem);
    }
    m_updating = false;
    refreshRows();
    QApplication::restoreOverrideCursor();
}

void RulesDialog::refreshRows()
{
    m_updating = true;
    m_table->setSortingEnabled(false);
    for (int row = 0; row < m_table->rowCount(); ++row)
        refreshRow(row);
    m_table->setSortingEnabled(true);
    m_updating = false;
    applyFilter();
    updateSummary();
}

void RulesDialog::refreshRow(int row)
{
    const sigma::Entry& entry = entryAt(row);
    std::array<QTableWidgetItem*, COLUMN_COUNT> items{};
    for (int column = 0; column < COLUMN_COUNT; ++column)
        items[static_cast<size_t>(column)] = m_table->item(row, column);
    const bool enabled = entry.state == sigma::Entry::State::Usable && !m_disabled.contains(entry.id);
    items[COLUMN_RULE]->setCheckState(enabled ? Qt::Checked : Qt::Unchecked);
    items[COLUMN_STATE]->setText(stateLabel(entry));
    const QBrush brush = active(entry) ? QBrush() : QBrush(theme::secondaryText(palette()));
    for (QTableWidgetItem* item : items)
        item->setForeground(brush);
}

void RulesDialog::applyFilter()
{
    for (int row = 0; row < m_table->rowCount(); ++row)
        m_table->setRowHidden(row, !shown(entryAt(row)));
    updateDetail();
}

void RulesDialog::updateSummary()
{
    const auto activeCount =
        std::count_if(m_entries.cbegin(), m_entries.cend(), [this](const sigma::Entry& e) { return active(e); });
    m_summary->setText(
        QStringLiteral("%1 · %2").arg(qtTrId("rules.count", pluralCount(static_cast<qint64>(m_entries.size()))),
                                      qtTrId("rules.used_count", pluralCount(activeCount))));
}

void RulesDialog::updateDetail()
{
    m_remove->setEnabled(!selectedFiles().isEmpty());
    const int row = m_table->currentRow();
    if (row < 0 || m_table->isRowHidden(row) || !m_table->item(row, COLUMN_RULE)->isSelected()) {
        m_description->clear();
        m_note->clear();
        return;
    }
    const sigma::Entry& entry = entryAt(row);
    m_description->setText(entry.description.isEmpty() ? qtTrId("rules.no_description")
                                                       : entry.description.simplified());
    QStringList notes;
    const QString explanation = stateExplanation(entry);
    if (!explanation.isEmpty())
        notes << explanation;
    if (!entry.error.isEmpty())
        notes << qtTrId("rules.technical_detail").arg(entry.error);
    if (!entry.author.isEmpty())
        notes << qtTrId("rules.author").arg(entry.author);
    m_note->setText(notes.join(QStringLiteral(" · ")));
}

void RulesDialog::setShownEnabled(bool enabled)
{
    for (int row = 0; row < m_table->rowCount(); ++row) {
        const sigma::Entry& entry = entryAt(row);
        if (m_table->isRowHidden(row) || entry.state != sigma::Entry::State::Usable)
            continue;
        if (enabled)
            m_disabled.remove(entry.id);
        else
            m_disabled.insert(entry.id);
    }
    refreshRows();
}

// ── Files ────────────────────────────────────────────────────────────────────

void RulesDialog::addRules(const QStringList& paths)
{
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const sigma::Addition addition = sigma::addRules(m_folder, paths);
    QApplication::restoreOverrideCursor();
    if (addition.added + addition.updated > 0) {
        m_filesChanged = true;
        m_search->changeText({});
        m_filter->setCurrentIndex(m_filter->findData(FILTER_ADDED));
        reload();
    }
    QStringList lines;
    if (addition.added > 0)
        lines << qtTrId("rules.added_count", addition.added);
    if (addition.updated > 0)
        lines << qtTrId("rules.updated_count", addition.updated);
    if (addition.unchanged > 0)
        lines << qtTrId("rules.unchanged_count", addition.unchanged);
    if (!addition.rejected.isEmpty()) {
        lines << qtTrId("rules.rejected_count", pluralCount(addition.rejected.size()));
        for (const sigma::RejectedFile& file : addition.rejected.mid(0, MAX_REJECTIONS_SHOWN)) {
            QString reason = unusableLabel(file.state);
            if (!file.error.isEmpty())
                reason += QStringLiteral(" (%1)").arg(file.error);
            lines << qtTrId("common.label_value").arg(QFileInfo(file.path).fileName(), reason);
        }
        if (addition.rejected.size() > MAX_REJECTIONS_SHOWN)
            lines << QStringLiteral("…");
    }
    if (lines.isEmpty())
        lines << qtTrId("rules.nothing_found");
    const QString text = lines.join(QLatin1Char('\n'));
    if (addition.added + addition.updated == 0 && !addition.rejected.isEmpty())
        MessageDialog::warn(this, qtTrId("rules.addition_title"), text);
    else
        MessageDialog::inform(this, qtTrId("rules.addition_title"), text);
}

void RulesDialog::removeSelection()
{
    const QStringList files = selectedFiles();
    if (files.isEmpty())
        return;
    const int count = static_cast<int>(files.size());
    if (!MessageDialog::confirm(this, qtTrId("rules.remove_question", count), qtTrId("rules.remove_explanation", count),
                                qtTrId("rules.remove"), MessageDialog::Type::Warning))
        return;
    for (const QString& file : files)
        m_filesChanged = sigma::removeRule(m_folder, file) || m_filesChanged;
    reload();
}

// ── State of a rule ──────────────────────────────────────────────────────────

const sigma::Entry& RulesDialog::entryAt(int row) const
{
    return m_entries[static_cast<size_t>(m_table->item(row, COLUMN_RULE)->data(ROLE_ENTRY).toInt())];
}

bool RulesDialog::active(const sigma::Entry& entry) const
{
    return entry.state == sigma::Entry::State::Usable && !m_disabled.contains(entry.id)
           && static_cast<int>(entry.level) >= minimumLevel();
}

bool RulesDialog::shown(const sigma::Entry& entry) const
{
    switch (m_filter->currentData().toInt()) {
    case FILTER_BUILTIN:
        if (!entry.builtin)
            return false;
        break;
    case FILTER_ADDED:
        if (entry.builtin)
            return false;
        break;
    case FILTER_UNUSED:
        if (active(entry))
            return false;
        break;
    default:
        break;
    }
    const QString text = m_search->text().trimmed();
    return text.isEmpty() || entry.title.contains(text, Qt::CaseInsensitive)
           || entry.description.contains(text, Qt::CaseInsensitive)
           || entry.author.contains(text, Qt::CaseInsensitive) || entry.id.contains(text, Qt::CaseInsensitive);
}

QString RulesDialog::stateLabel(const sigma::Entry& entry) const
{
    if (entry.state != sigma::Entry::State::Usable)
        return unusableLabel(entry.state);
    if (m_disabled.contains(entry.id))
        return qtTrId("rules.state_disabled");
    return active(entry) ? qtTrId("rules.state_enabled") : qtTrId("rules.state_below_level");
}

QString RulesDialog::stateExplanation(const sigma::Entry& entry) const
{
    switch (entry.state) {
    case sigma::Entry::State::NotApplicable:
        return qtTrId("rules.not_applicable_explanation");
    case sigma::Entry::State::Deprecated:
        return qtTrId("rules.deprecated_explanation");
    case sigma::Entry::State::Invalid:
        return qtTrId("rules.invalid_explanation");
    case sigma::Entry::State::Duplicate:
        return qtTrId("rules.duplicate_explanation");
    case sigma::Entry::State::Usable:
        break;
    }
    return m_disabled.contains(entry.id) || active(entry) ? QString() : qtTrId("rules.below_level_explanation");
}

QStringList RulesDialog::selectedFiles() const
{
    QStringList files;
    for (const QModelIndex& index : m_table->selectionModel()->selectedRows()) {
        const sigma::Entry& entry = entryAt(index.row());
        if (!entry.builtin)
            files << entry.path;
    }
    return files;
}

} // namespace lh::ui
