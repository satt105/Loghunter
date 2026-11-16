#include "Page.h"

#include "ui/Workspace.h"

#include <QTimer>

namespace lh::ui {

Page::Page(Workspace* workspace, QWidget* parent) : QWidget(parent), m_workspace(workspace)
{
    connect(workspace, &Workspace::sessionChanged, this, &Page::clear);
    connect(workspace, &Workspace::sessionChanged, this, &Page::invalidate);
    connect(workspace, &Workspace::dataModified, this, &Page::invalidate);
    connect(workspace, &Workspace::filtersChanged, this, &Page::invalidate);
    connect(workspace, &Workspace::parametersChanged, this, &Page::invalidate);
}

void Page::invalidate()
{
    m_needsRefresh = true;
    if (!isVisible() || m_scheduled)
        return;
    m_scheduled = true;
    QTimer::singleShot(0, this, [this] {
        m_scheduled = false;
        if (m_needsRefresh && isVisible()) {
            m_needsRefresh = false;
            refresh();
        }
    });
}

void Page::refreshIfNeeded()
{
    if (m_needsRefresh) {
        m_needsRefresh = false;
        refresh();
    }
}

void Page::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_needsRefresh)
        invalidate();
}

} // namespace lh::ui
