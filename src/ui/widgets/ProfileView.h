// Profile of a user, a file or a folder: summary and timeline.
#pragma once

#include "core/Filters.h"
#include "core/Queries.h"
#include "ui/Executor.h"

#include <QWidget>

class QSortFilterProxyModel;
class QStackedWidget;
class QTabBar;
class QTableView;
class QToolButton;

namespace lh::ui {

class ColumnFitter;
class SearchField;
class Workspace;
class EmptyState;
class Label;
class DonutChart;
class TimelineChart;
class RankingChart;
class LoadingIndicator;
class TableModel;
class EventTable;
class IndicatorTile;

class ProfileView : public QWidget
{
    Q_OBJECT

public:
    enum class Type { User, File, Folder };
    ProfileView(Workspace* workspace, Type type, QWidget* parent = nullptr);

    void display(const QString& value);
    [[nodiscard]] const QString& value() const { return m_value; }
    void refresh();
    [[nodiscard]] Filters scope() const;
    void setListVisible(bool visible);

signals:
    void valueChanged();
    void listToggled();

private:
    void showProfile(const queries::Profile& profile);
    void createFilesTab();
    void loadTable();
    void loadFiles();
    void fileMenu(const QPoint& position);

    Workspace* m_workspace;
    Type m_type;
    QString m_value;
    QString m_tableKey;
    TokenPtr m_token;

    QStackedWidget* m_stack;
    EmptyState* m_empty;
    QToolButton* m_listButton;
    Label* m_title;
    Label* m_subtitle;
    QTabBar* m_tabs;
    QStackedWidget* m_pages;
    QWidget* m_summary;
    LoadingIndicator* m_loading;
    IndicatorTile* m_tiles[4];
    TimelineChart* m_timeline;
    DonutChart* m_distribution;
    RankingChart* m_rankingA;
    RankingChart* m_rankingB;
    QWidget* m_cardB;
    EventTable* m_table;
    QWidget* m_filesTab = nullptr;
    SearchField* m_fileSearch = nullptr;
    Label* m_fileCount = nullptr;
    TableModel* m_filesModel = nullptr;
    QSortFilterProxyModel* m_filesProxy = nullptr;
    QTableView* m_filesView = nullptr;
    ColumnFitter* m_fileColumns = nullptr;
    LoadingIndicator* m_filesLoading = nullptr;
    TokenPtr m_filesToken;
    QString m_filesKey;
};

} // namespace lh::ui
