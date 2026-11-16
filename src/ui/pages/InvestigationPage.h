// Users and files: lists, folder tree and detailed profile.
#pragma once

#include "Page.h"

class QTabWidget;

namespace lh::ui {

class InvestigationTab;

class InvestigationPage : public Page
{
    Q_OBJECT

public:
    enum class Tab { Users, Files, Folders };

    explicit InvestigationPage(Workspace* workspace, QWidget* parent = nullptr);
    [[nodiscard]] QString title() const override { return qtTrId("investigation.title"); }
    [[nodiscard]] const char* iconName() const override { return "investigation"; }

    [[nodiscard]] Tab tab() const;
    [[nodiscard]] QString value() const;
    void focusOn(Tab tab, const QString& value);
    void restore(Tab tab, const QString& value);

signals:
    void viewChanged();

protected:
    void refresh() override;
    void clear() override;

private:
    void refreshCurrentTab();

    QTabWidget* m_tabs;
    InvestigationTab* m_views[3];
};

} // namespace lh::ui
