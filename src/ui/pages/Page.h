// Base class of the main window pages.
#pragma once

#include <QWidget>

namespace lh::ui {

class Workspace;

class Page : public QWidget
{
    Q_OBJECT

public:
    explicit Page(Workspace* workspace, QWidget* parent = nullptr);

    [[nodiscard]] virtual QString title() const = 0;
    [[nodiscard]] virtual const char* iconName() const = 0;
    [[nodiscard]] virtual bool usesFilters() const { return true; }

protected:
    virtual void refresh() = 0;
    virtual void clear() {}
    void invalidate();
    void refreshIfNeeded();

    void showEvent(QShowEvent* event) override;

    Workspace* m_workspace;

private:
    bool m_needsRefresh = true;
    bool m_scheduled = false;
};

} // namespace lh::ui
