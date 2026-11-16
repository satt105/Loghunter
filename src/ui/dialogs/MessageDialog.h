// Message boxes in the application colors: information, question, warning or error.
#pragma once

#include <QDialog>

class QPushButton;
class QVBoxLayout;

namespace lh::ui {

class DialogFooter;

class MessageDialog : public QDialog
{
    Q_OBJECT

public:
    enum class Type { Information, Question, Warning, Error, Success };

    MessageDialog(Type type, const QString& title, const QString& text, QWidget* parent = nullptr);

    QPushButton* addButton(const QString& text, bool primary = false);
    void addContent(QWidget* content);
    [[nodiscard]] QPushButton* clickedButton() const { return m_clicked; }

    static void inform(QWidget* parent, const QString& title, const QString& text);
    static void warn(QWidget* parent, const QString& title, const QString& text);
    static void showError(QWidget* parent, const QString& title, const QString& text);
    static bool confirm(QWidget* parent, const QString& title, const QString& text, const QString& acceptText,
                        Type type = Type::Question);
    static QString prompt(QWidget* parent, const QString& title, const QString& text, const QString& acceptText);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QVBoxLayout* m_texts;
    DialogFooter* m_footer;
    QPushButton* m_clicked = nullptr;
};

} // namespace lh::ui
