// Reminder of the active filters as chips removable with one click.
#pragma once

#include "core/Filters.h"

#include <QAbstractButton>
#include <QLayout>
#include <QWidget>

#include <functional>

namespace lh::ui {

class Workspace;
class LinkButton;

class FlowLayout : public QLayout
{
public:
    explicit FlowLayout(QWidget* parent = nullptr, int spacing = 6);
    ~FlowLayout() override;

    void addItem(QLayoutItem* element) override;
    [[nodiscard]] int count() const override { return static_cast<int>(m_elements.size()); }
    [[nodiscard]] QLayoutItem* itemAt(int index) const override;
    QLayoutItem* takeAt(int index) override;
    [[nodiscard]] Qt::Orientations expandingDirections() const override { return {}; }
    [[nodiscard]] bool hasHeightForWidth() const override { return true; }
    [[nodiscard]] int heightForWidth(int width) const override;
    void setGeometry(const QRect& rectangle) override;
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSize() const override;

private:
    int arrange(const QRect& rectangle, bool simulation) const;
    QList<QLayoutItem*> m_elements;
    int m_spacing;
};

class Chip : public QAbstractButton
{
    Q_OBJECT

public:
    Chip(const QString& text, const QColor& color, QWidget* parent = nullptr);
    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QColor m_color;
    bool m_hover = false;
};

class FilterBar : public QWidget
{
    Q_OBJECT

public:
    explicit FilterBar(Workspace* workspace, QWidget* parent = nullptr);
    void setActive(bool active);

private:
    void rebuild();
    void add(const QString& text, std::function<void(Filters&)> removal, const QColor& color = {});

    Workspace* m_workspace;
    FlowLayout* m_layout;
    bool m_active = true;
};

} // namespace lh::ui
