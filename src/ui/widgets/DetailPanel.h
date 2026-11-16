// Detail of an event in three views (summary, JSON, raw XML) and investigation actions.
#pragma once

#include "core/Queries.h"

#include <QFrame>

#include <optional>

class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTabBar;
class QToolButton;
class QVBoxLayout;

namespace lh::ui {

class Workspace;
class Label;

class DetailPanel : public QFrame
{
    Q_OBJECT

public:
    explicit DetailPanel(Workspace* workspace, QWidget* parent = nullptr);
    void display(qint64 identifier);
    [[nodiscard]] QSize sizeHint() const override { return {380, 500}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void addField(QVBoxLayout* target, const QString& label, const QString& value, bool remember = true);
    void addSection(QVBoxLayout* target, const QString& title);
    void clearFields();
    void showView(int index);
    void copy();

    Workspace* m_workspace;
    QStackedWidget* m_stack;
    QTabBar* m_tabs;
    QStackedWidget* m_views;
    QWidget* m_content;
    Label* m_action;
    Label* m_timestamp;
    QVBoxLayout* m_fields;
    QVBoxLayout* m_alerts;
    QVBoxLayout* m_data;
    QPlainTextEdit* m_json;
    QPlainTextEdit* m_xml;
    QPushButton* m_investigateFile;
    QPushButton* m_investigateUser;
    std::optional<queries::EventDetail> m_detail;
    QString m_rawXml;
    bool m_xmlLoaded = false;
    QList<QPair<QString, QString>> m_values;
};

} // namespace lh::ui
