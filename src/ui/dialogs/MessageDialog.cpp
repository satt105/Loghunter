#include "MessageDialog.h"

#include "core/Alerts.h"
#include "ui/Theme.h"
#include "ui/widgets/Components.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace lh::ui {

namespace {

const char* iconName(MessageDialog::Type type)
{
    switch (type) {
    case MessageDialog::Type::Question:
        return "help";
    case MessageDialog::Type::Warning:
    case MessageDialog::Type::Error:
        return "warning";
    case MessageDialog::Type::Success:
        return "check";
    case MessageDialog::Type::Information:
        break;
    }
    return "info";
}

QColor tint(MessageDialog::Type type)
{
    switch (type) {
    case MessageDialog::Type::Warning:
        return theme::severityColor(Severity::Medium);
    case MessageDialog::Type::Error:
        return theme::severityColor(Severity::Critical);
    case MessageDialog::Type::Success:
        return theme::successColor();
    case MessageDialog::Type::Information:
    case MessageDialog::Type::Question:
        break;
    }
    return theme::brand(QApplication::palette());
}

} // namespace

MessageDialog::MessageDialog(Type type, const QString& title, const QString& text, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(QApplication::applicationDisplayName());
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->setSizeConstraint(QLayout::SetFixedSize);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(24, 24, 28, 26);
    body->setSpacing(16);
    body->addWidget(new IconBadge(iconName(type), tint(type), 44, this), 0, Qt::AlignTop);
    m_texts = new QVBoxLayout;
    m_texts->setSpacing(6);
    const int width = std::max(360, fontMetrics().averageCharWidth() * 58);
    auto* titleLabel = new Label(title, Label::Role::Title, this);
    auto* textLabel = new Label(text, Label::Role::Normal, this);
    textLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    for (Label* labelWidget : {titleLabel, textLabel}) {
        labelWidget->setWordWrap(true);
        labelWidget->setFixedWidth(width);
        labelWidget->setMinimumHeight(labelWidget->heightForWidth(width));
        m_texts->addWidget(labelWidget);
    }
    textLabel->setVisible(!text.isEmpty());
    m_texts->addStretch();
    body->addLayout(m_texts);
    layout->addLayout(body);

    m_footer = new DialogFooter(this);
    layout->addWidget(m_footer);
}

QPushButton* MessageDialog::addButton(const QString& text, bool primary)
{
    QPushButton* button = m_footer->addButton(text, primary);
    connect(button, &QPushButton::clicked, this, [this, button] {
        m_clicked = button;
        accept();
    });
    if (primary)
        button->setFocus();
    return button;
}

void MessageDialog::addContent(QWidget* content)
{
    m_texts->insertSpacing(m_texts->count() - 1, 6);
    m_texts->insertWidget(m_texts->count() - 1, content);
}

void MessageDialog::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), theme::cardBackground(palette()));
}

void MessageDialog::inform(QWidget* parent, const QString& title, const QString& text)
{
    MessageDialog box(Type::Information, title, text, parent);
    box.addButton(qtTrId("message.ok"), true);
    box.exec();
}

void MessageDialog::warn(QWidget* parent, const QString& title, const QString& text)
{
    MessageDialog box(Type::Warning, title, text, parent);
    box.addButton(qtTrId("message.ok"), true);
    box.exec();
}

void MessageDialog::showError(QWidget* parent, const QString& title, const QString& text)
{
    MessageDialog box(Type::Error, title, text, parent);
    box.addButton(qtTrId("common.close"), true);
    box.exec();
}

bool MessageDialog::confirm(QWidget* parent, const QString& title, const QString& text, const QString& acceptText,
                            Type type)
{
    MessageDialog box(type, title, text, parent);
    QPushButton* yesButton = box.addButton(acceptText, true);
    box.addButton(qtTrId("common.cancel"));
    box.exec();
    return box.clickedButton() == yesButton;
}

QString MessageDialog::prompt(QWidget* parent, const QString& title, const QString& text, const QString& acceptText)
{
    MessageDialog box(Type::Question, title, text, parent);
    auto* field = new QLineEdit(&box);
    field->setMinimumHeight(30);
    box.addContent(field);
    QPushButton* acceptButton = box.addButton(acceptText, true);
    box.addButton(qtTrId("common.cancel"));
    acceptButton->setEnabled(false);
    connect(field, &QLineEdit::textChanged, acceptButton,
            [acceptButton](const QString& input) { acceptButton->setEnabled(!input.trimmed().isEmpty()); });
    field->setFocus();
    box.exec();
    return box.clickedButton() == acceptButton ? field->text().trimmed() : QString();
}

} // namespace lh::ui
