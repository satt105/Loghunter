// Basic visual components, drawn from the current palette (light or dark theme).
#pragma once

#include <QColor>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QTimer>

class QHBoxLayout;
class QLayout;
class QPushButton;
class QToolButton;
class QVBoxLayout;

namespace lh::ui {

class Label : public QLabel
{
    Q_OBJECT

public:
    enum class Role { Normal, Secondary, Small, Title, PageTitle, Value, Section, Large };
    explicit Label(const QString& text = {}, Role role = Role::Normal, QWidget* parent = nullptr);
    void setColor(const QColor& color);
    void setElidedText(const QString& text);
    [[nodiscard]] QSize minimumSizeHint() const override;
    [[nodiscard]] QSize sizeHint() const override;

protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void apply();
    void elide();
    Role m_role;
    QColor m_color;
    bool m_applying = false;
    bool m_elided = false;
    QString m_fullText;
};

class Card : public QFrame
{
    Q_OBJECT

public:
    explicit Card(const QString& title = {}, QWidget* parent = nullptr);
    void setTitle(const QString& title);
    void setSubtitle(const QString& subtitle);
    [[nodiscard]] QHBoxLayout* actions() const { return m_actions; }
    [[nodiscard]] QVBoxLayout* content() const { return m_content; }
    void setMargins(int left, int top, int right, int bottom);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QWidget* m_header;
    Label* m_title;
    Label* m_subtitle;
    QHBoxLayout* m_actions;
    QVBoxLayout* m_content;
};

class IndicatorTile : public QWidget
{
    Q_OBJECT

public:
    IndicatorTile(const char* icon, const QString& label, QWidget* parent = nullptr);
    void setValue(const QString& value, const QString& detail = {});
    void setColor(const QColor& color);
    void setClickable(bool clickable);
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    const char* m_icon;
    QString m_label;
    QString m_value;
    QString m_detail;
    QColor m_color;
    bool m_clickable = false;
    bool m_hover = false;
};

class EmptyState : public QWidget
{
    Q_OBJECT

public:
    EmptyState(const char* icon, const QString& title, const QString& message = {}, QWidget* parent = nullptr);
    void changeText(const QString& title, const QString& message);
    QPushButton* addButton(const QString& text, const char* icon = nullptr, bool primary = false);

protected:
    void changeEvent(QEvent* event) override;

private:
    void refreshIcon();
    const char* m_iconName;
    QLabel* m_icon;
    Label* m_title;
    Label* m_message;
    QHBoxLayout* m_buttons;
};

class LoadingIndicator : public QWidget
{
    Q_OBJECT

public:
    explicit LoadingIndicator(QWidget* target);
    void start();
    void stop();

protected:
    bool eventFilter(QObject* object, QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QTimer m_delay;
    QTimer m_animation;
    int m_angle = 0;
    bool m_active = false;
};

class SearchField : public QLineEdit
{
    Q_OBJECT

public:
    explicit SearchField(const QString& placeholder, QWidget* parent = nullptr);
    void changeText(const QString& text);

signals:
    void searchChanged(const QString& text);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    QTimer m_timer;
};

class LinkButton : public QLabel
{
    Q_OBJECT

public:
    explicit LinkButton(const QString& text, QWidget* parent = nullptr);
    void changeText(const QString& text);

signals:
    void clicked();

protected:
    void changeEvent(QEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    void apply(bool hover);
    QString m_text;
    bool m_applying = false;
};

class IconBadge : public QWidget
{
    Q_OBJECT

public:
    IconBadge(const char* icon, const QColor& color, int side, QWidget* parent = nullptr);
    void set(const char* icon, const QColor& color);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    const char* m_icon;
    QColor m_color;
};

class DialogFooter : public QWidget
{
    Q_OBJECT

public:
    explicit DialogFooter(QWidget* parent = nullptr);
    QPushButton* addButton(const QString& text, bool primary = false);
    void clear();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QHBoxLayout* m_buttons;
};

QWidget* separator(Qt::Orientation orientation = Qt::Horizontal, QWidget* parent = nullptr);
QPushButton* actionButton(const QString& text, const char* icon, QWidget* parent, bool primary = false);
QToolButton* toolButton(const char* icon, const QString& text, QWidget* parent);
Card* cardWith(const QString& title, QWidget* content, QWidget* parent, const QString& subtitle = {});
void clearLayout(QLayout* layout);

} // namespace lh::ui
