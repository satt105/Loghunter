// Sidebar for navigating between pages.
#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QList>
#include <QWidget>

class QVBoxLayout;

namespace lh::ui {

class NavigationItem : public QAbstractButton
{
    Q_OBJECT

public:
    NavigationItem(const char* icon, const QString& label, QWidget* parent = nullptr);
    void setBadge(const QString& text, const QColor& color);
    [[nodiscard]] QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    const char* m_icon;
    QString m_badge;
    QColor m_badgeColor;
    bool m_hover = false;
};

class Sidebar : public QWidget
{
    Q_OBJECT

public:
    explicit Sidebar(QWidget* parent = nullptr);
    int add(const char* icon, const QString& label, const QString& tooltip);
    void select(int index);
    void setBadge(int index, const QString& text, const QColor& color = {});
    void setItemVisible(int index, bool visible);
    [[nodiscard]] QSize sizeHint() const override { return {232, 600}; }

signals:
    void pageRequested(int index);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVBoxLayout* m_elements;
    QList<NavigationItem*> m_list;
};

} // namespace lh::ui
