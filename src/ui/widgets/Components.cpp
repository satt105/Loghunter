#include "Components.h"

#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QToolButton>
#include <QVBoxLayout>

namespace lh::ui {

namespace {

bool isThemeChange(const QEvent* event)
{
    switch (event->type()) {
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::StyleChange:
    case QEvent::ThemeChange:
        return true;
    default:
        return false;
    }
}

class SeparatorLine : public QWidget
{
public:
    SeparatorLine(Qt::Orientation orientation, QWidget* parent) : QWidget(parent)
    {
        if (orientation == Qt::Horizontal)
            setFixedHeight(1);
        else
            setFixedWidth(1);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), theme::border(QApplication::palette()));
    }
};

} // namespace

// ── Label ────────────────────────────────────────────────────────────────────

Label::Label(const QString& text, Role role, QWidget* parent) : QLabel(text, parent), m_role(role)
{
    setTextFormat(Qt::PlainText);
    apply();
}

void Label::setColor(const QColor& color)
{
    m_color = color;
    apply();
}

void Label::setElidedText(const QString& text)
{
    m_elided = true;
    m_fullText = text;
    setSizePolicy(QSizePolicy::Ignored, sizePolicy().verticalPolicy());
    elide();
    updateGeometry();
}

QSize Label::minimumSizeHint() const
{
    if (!m_elided)
        return QLabel::minimumSizeHint();
    return {fontMetrics().horizontalAdvance(QStringLiteral("…")) * 4, QLabel::minimumSizeHint().height()};
}

QSize Label::sizeHint() const
{
    if (!m_elided)
        return QLabel::sizeHint();
    const QMargins margins = contentsMargins();
    return {fontMetrics().horizontalAdvance(m_fullText) + margins.left() + margins.right() + 2 * margin(),
            QLabel::sizeHint().height()};
}

void Label::changeEvent(QEvent* event)
{
    QLabel::changeEvent(event);
    if (!m_applying && isThemeChange(event))
        apply();
    if (m_elided && event->type() == QEvent::FontChange)
        elide();
}

void Label::resizeEvent(QResizeEvent* event)
{
    QLabel::resizeEvent(event);
    if (m_elided)
        elide();
}

void Label::elide()
{
    const int width = contentsRect().width() - 2 * margin();
    const QString elided = fontMetrics().elidedText(m_fullText, Qt::ElideMiddle, std::max(0, width));
    if (text() != elided)
        QLabel::setText(elided);
    setToolTip(elided == m_fullText ? QString() : m_fullText);
}

void Label::apply()
{
    m_applying = true;
    const QFont base = QApplication::font();
    QFont font = base;
    bool secondary = false;
    switch (m_role) {
    case Role::Normal:
        break;
    case Role::Secondary:
        secondary = true;
        break;
    case Role::Small:
        font = theme::font(base, 0.92);
        secondary = true;
        break;
    case Role::Title:
        font = theme::font(base, 1.1, QFont::DemiBold);
        break;
    case Role::PageTitle:
        font = theme::font(base, 1.55, QFont::DemiBold);
        break;
    case Role::Value:
        font = theme::font(base, 1.7, QFont::DemiBold);
        break;
    case Role::Section:
        font = theme::font(base, 0.92, QFont::DemiBold);
        secondary = true;
        break;
    case Role::Large:
        font = theme::font(base, 2.0, QFont::Bold);
        break;
    }
    setFont(font);
    const QPalette reference = QApplication::palette();
    const QColor color =
        m_color.isValid() ? m_color : (secondary ? theme::secondaryText(reference) : theme::text(reference));
    QPalette palette = this->palette();
    palette.setColor(QPalette::WindowText, color);
    palette.setColor(QPalette::Text, color);
    setPalette(palette);
    m_applying = false;
}

// ── Card ─────────────────────────────────────────────────────────────────────

Card::Card(const QString& title, QWidget* parent) : QFrame(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 14, 16, 16);
    layout->setSpacing(10);

    m_header = new QWidget(this);
    auto* header = new QHBoxLayout(m_header);
    header->setContentsMargins(0, 0, 0, 0);
    header->setSpacing(8);
    auto* titles = new QVBoxLayout;
    titles->setSpacing(1);
    m_title = new Label(title, Label::Role::Title, m_header);
    m_subtitle = new Label({}, Label::Role::Small, m_header);
    m_subtitle->hide();
    titles->addWidget(m_title);
    titles->addWidget(m_subtitle);
    header->addLayout(titles, 1);
    m_actions = new QHBoxLayout;
    m_actions->setSpacing(6);
    header->addLayout(m_actions);
    m_header->setVisible(!title.isEmpty());
    layout->addWidget(m_header);

    m_content = new QVBoxLayout;
    m_content->setContentsMargins(0, 0, 0, 0);
    m_content->setSpacing(8);
    layout->addLayout(m_content, 1);
}

void Card::setTitle(const QString& title)
{
    m_title->setText(title);
    m_header->setVisible(!title.isEmpty() || m_actions->count() > 0);
}

void Card::setSubtitle(const QString& subtitle)
{
    m_subtitle->setText(subtitle);
    m_subtitle->setVisible(!subtitle.isEmpty());
}

void Card::setMargins(int left, int top, int right, int bottom)
{
    layout()->setContentsMargins(left, top, right, bottom);
}

void Card::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette& pal = palette();
    painter.setPen(QPen(theme::border(pal), 1));
    painter.setBrush(theme::cardBackground(pal));
    painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 8, 8);
}

// ── IndicatorTile ────────────────────────────────────────────────────────────

IndicatorTile::IndicatorTile(const char* icon, const QString& label, QWidget* parent)
    : QWidget(parent), m_icon(icon), m_label(label)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setAttribute(Qt::WA_Hover);
}

void IndicatorTile::setValue(const QString& value, const QString& detail)
{
    m_value = value;
    m_detail = detail;
    setToolTip(detail.isEmpty() ? QString()
                                : qtTrId("common.label_value").arg(m_label, value) + QLatin1Char('\n') + detail);
    update();
}

void IndicatorTile::setColor(const QColor& color)
{
    m_color = color;
    update();
}

void IndicatorTile::setClickable(bool clickable)
{
    m_clickable = clickable;
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
    setFocusPolicy(clickable ? Qt::StrongFocus : Qt::NoFocus);
    update();
}

QSize IndicatorTile::sizeHint() const
{
    const QFontMetrics metrics(font());
    return {220, std::max(84, metrics.height() * 5)};
}

QSize IndicatorTile::minimumSizeHint() const
{
    return {170, sizeHint().height()};
}

void IndicatorTile::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette& pal = palette();
    const bool active = m_clickable && (m_hover || hasFocus());
    const QColor color = m_color.isValid() ? m_color : theme::brand(pal);

    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(active ? theme::blend(theme::border(pal), color, 0.55) : theme::border(pal), 1));
    painter.setBrush(active ? theme::hoverBackground(pal) : theme::cardBackground(pal));
    painter.drawRoundedRect(frame, 8, 8);

    const double side = 40;
    const QRectF box(16, (height() - side) / 2, side, side);
    QColor iconBackground = color;
    iconBackground.setAlphaF(theme::isDark() ? 0.22F : 0.13F);
    painter.setPen(Qt::NoPen);
    painter.setBrush(iconBackground);
    painter.drawRoundedRect(box, 10, 10);
    icon(m_icon, color).paint(&painter, box.adjusted(9, 9, -9, -9).toRect());

    const QFont base = QApplication::font();
    const int left = static_cast<int>(box.right()) + 14;
    const int availableWidth = width() - left - 14;
    const QFont labelFont = theme::font(base, 0.92);
    const QFont detailFont = theme::font(base, 0.88);
    double factor = 1.55;
    QFont valueFont = theme::font(base, factor, QFont::DemiBold);
    while (factor > 1.1 && QFontMetrics(valueFont).horizontalAdvance(m_value) > availableWidth) {
        factor -= 0.1;
        valueFont = theme::font(base, factor, QFont::DemiBold);
    }
    const QFontMetrics labelMetrics(labelFont);
    const QFontMetrics valueMetrics(valueFont);
    const QFontMetrics detailMetrics(detailFont);
    const int textHeight = labelMetrics.height() + valueMetrics.height() + (m_detail.isEmpty() ? 0 : detailMetrics.height());
    int y = (height() - textHeight) / 2;

    painter.setFont(labelFont);
    painter.setPen(theme::secondaryText(pal));
    painter.drawText(QRect(left, y, availableWidth, labelMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                     labelMetrics.elidedText(m_label, Qt::ElideRight, availableWidth));
    y += labelMetrics.height();
    painter.setFont(valueFont);
    painter.setPen(theme::text(pal));
    painter.drawText(QRect(left, y, availableWidth, valueMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                     valueMetrics.elidedText(m_value, Qt::ElideRight, availableWidth));
    y += valueMetrics.height();
    if (!m_detail.isEmpty()) {
        painter.setFont(detailFont);
        painter.setPen(theme::secondaryText(pal));
        painter.drawText(QRect(left, y, availableWidth, detailMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
                         detailMetrics.elidedText(m_detail, Qt::ElideRight, availableWidth));
    }
}

void IndicatorTile::enterEvent(QEnterEvent* event)
{
    m_hover = true;
    update();
    QWidget::enterEvent(event);
}

void IndicatorTile::leaveEvent(QEvent* event)
{
    m_hover = false;
    update();
    QWidget::leaveEvent(event);
}

void IndicatorTile::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_clickable && event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit clicked();
    QWidget::mouseReleaseEvent(event);
}

void IndicatorTile::keyPressEvent(QKeyEvent* event)
{
    if (m_clickable && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter
                        || event->key() == Qt::Key_Space)) {
        emit clicked();
        return;
    }
    QWidget::keyPressEvent(event);
}

// ── EmptyState ───────────────────────────────────────────────────────────────

EmptyState::EmptyState(const char* icon, const QString& title, const QString& message, QWidget* parent)
    : QWidget(parent), m_iconName(icon)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(8);
    layout->addStretch(1);

    m_icon = new QLabel(this);
    m_icon->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_icon);
    layout->addSpacing(4);

    m_title = new Label(title, Label::Role::Title, this);
    m_title->setAlignment(Qt::AlignCenter);
    m_title->setWordWrap(true);
    layout->addWidget(m_title);

    m_message = new Label(message, Label::Role::Secondary, this);
    m_message->setAlignment(Qt::AlignCenter);
    m_message->setWordWrap(true);
    m_message->setMaximumWidth(520);
    m_message->setVisible(!message.isEmpty());
    auto* messageRow = new QHBoxLayout;
    messageRow->addStretch();
    messageRow->addWidget(m_message);
    messageRow->addStretch();
    layout->addLayout(messageRow);

    m_buttons = new QHBoxLayout;
    m_buttons->setSpacing(8);
    m_buttons->addStretch();
    m_buttons->addStretch();
    layout->addSpacing(8);
    layout->addLayout(m_buttons);
    layout->addStretch(2);
    refreshIcon();
}

void EmptyState::changeText(const QString& title, const QString& message)
{
    m_title->setText(title);
    m_message->setText(message);
    m_message->setVisible(!message.isEmpty());
}

QPushButton* EmptyState::addButton(const QString& text, const char* iconName, bool primary)
{
    QPushButton* button = actionButton(text, iconName, this, primary);
    m_buttons->insertWidget(m_buttons->count() - 1, button);
    return button;
}

void EmptyState::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (isThemeChange(event))
        refreshIcon();
}

void EmptyState::refreshIcon()
{
    const QColor color = theme::secondaryText(QApplication::palette());
    m_icon->setPixmap(icon(m_iconName, color).pixmap(QSize(44, 44), devicePixelRatioF()));
}

// ── LoadingIndicator ─────────────────────────────────────────────────────────

LoadingIndicator::LoadingIndicator(QWidget* target) : QWidget(target)
{
    hide();
    target->installEventFilter(this);
    m_delay.setSingleShot(true);
    m_delay.setInterval(250);
    connect(&m_delay, &QTimer::timeout, this, [this] {
        if (!m_active || !parentWidget())
            return;
        setGeometry(parentWidget()->rect());
        raise();
        show();
        m_animation.start();
    });
    m_animation.setInterval(16);
    connect(&m_animation, &QTimer::timeout, this, [this] {
        m_angle = (m_angle + 7) % 360;
        update();
    });
}

void LoadingIndicator::start()
{
    m_active = true;
    if (!isVisible() && !m_delay.isActive())
        m_delay.start();
}

void LoadingIndicator::stop()
{
    m_active = false;
    m_delay.stop();
    m_animation.stop();
    hide();
}

bool LoadingIndicator::eventFilter(QObject* object, QEvent* event)
{
    if (object == parentWidget() && event->type() == QEvent::Resize)
        setGeometry(parentWidget()->rect());
    return QWidget::eventFilter(object, event);
}

void LoadingIndicator::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette& pal = palette();
    QColor veil = theme::cardBackground(pal);
    veil.setAlphaF(0.55F);
    painter.fillRect(rect(), veil);

    const QPointF center = QRectF(rect()).center();
    const double radius = 14;
    QColor track = theme::text(pal);
    track.setAlphaF(0.12F);
    painter.setPen(QPen(track, 3));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(center, radius, radius);
    painter.setPen(QPen(theme::brand(pal), 3, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(QRectF(center.x() - radius, center.y() - radius, 2 * radius, 2 * radius), -m_angle * 16, 100 * 16);
}

// ── SearchField ──────────────────────────────────────────────────────────────

SearchField::SearchField(const QString& placeholder, QWidget* parent) : QLineEdit(parent)
{
    setPlaceholderText(placeholder);
    setClearButtonEnabled(true);
    addAction(icon("search"), QLineEdit::LeadingPosition);
    m_timer.setSingleShot(true);
    m_timer.setInterval(300);
    connect(this, &QLineEdit::textEdited, this, [this](const QString& text) {
        if (text.isEmpty()) {
            m_timer.stop();
            emit searchChanged(QString());
        } else {
            m_timer.start();
        }
    });
    connect(this, &QLineEdit::returnPressed, this, [this] {
        m_timer.stop();
        emit searchChanged(text().trimmed());
    });
    connect(&m_timer, &QTimer::timeout, this, [this] { emit searchChanged(text().trimmed()); });
}

void SearchField::changeText(const QString& newText)
{
    m_timer.stop();
    if (text() != newText)
        setText(newText);
}

void SearchField::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape && !text().isEmpty()) {
        clear();
        m_timer.stop();
        emit searchChanged(QString());
        return;
    }
    QLineEdit::keyPressEvent(event);
}

// ── LinkButton ───────────────────────────────────────────────────────────────

LinkButton::LinkButton(const QString& text, QWidget* parent) : QLabel(parent), m_text(text)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    apply(false);
}

void LinkButton::changeText(const QString& text)
{
    m_text = text;
    setText(text);
}

void LinkButton::changeEvent(QEvent* event)
{
    QLabel::changeEvent(event);
    if (!m_applying && isThemeChange(event))
        apply(underMouse());
}

void LinkButton::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
        emit clicked();
    QLabel::mouseReleaseEvent(event);
}

void LinkButton::enterEvent(QEnterEvent* event)
{
    apply(true);
    QLabel::enterEvent(event);
}

void LinkButton::leaveEvent(QEvent* event)
{
    apply(false);
    QLabel::leaveEvent(event);
}

void LinkButton::apply(bool hover)
{
    m_applying = true;
    QPalette palette = this->palette();
    palette.setColor(QPalette::WindowText, theme::accent(QApplication::palette()));
    setPalette(palette);
    QFont textFont = font();
    textFont.setUnderline(hover);
    setFont(textFont);
    setText(m_text);
    m_applying = false;
}

// ── IconBadge ────────────────────────────────────────────────────────────────

IconBadge::IconBadge(const char* iconName, const QColor& color, int side, QWidget* parent)
    : QWidget(parent), m_icon(iconName), m_color(color)
{
    setFixedSize(side, side);
}

void IconBadge::set(const char* iconName, const QColor& color)
{
    m_icon = iconName;
    m_color = color;
    update();
}

void IconBadge::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor background = m_color;
    background.setAlphaF(theme::isDark() ? 0.22F : 0.13F);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    const double radius = width() * 0.27;
    painter.drawRoundedRect(QRectF(rect()), radius, radius);
    const int margin = width() / 4;
    icon(m_icon, m_color).paint(&painter, rect().adjusted(margin, margin, -margin, -margin));
}

// ── DialogFooter ─────────────────────────────────────────────────────────────

DialogFooter::DialogFooter(QWidget* parent) : QWidget(parent)
{
    m_buttons = new QHBoxLayout(this);
    m_buttons->setContentsMargins(24, 14, 24, 14);
    m_buttons->setSpacing(8);
    m_buttons->addStretch();
}

QPushButton* DialogFooter::addButton(const QString& text, bool primary)
{
    auto* button = new QPushButton(text, this);
    button->setMinimumSize(std::max(100, button->sizeHint().width() + 16), 32);
    button->setDefault(primary);
    m_buttons->addWidget(button);
    return button;
}

void DialogFooter::clear()
{
    while (m_buttons->count() > 1) {
        QLayoutItem* element = m_buttons->takeAt(1);
        if (QWidget* widget = element->widget())
            widget->deleteLater();
        delete element;
    }
}

void DialogFooter::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.fillRect(rect(), theme::pageBackground(palette()));
    painter.fillRect(QRect(0, 0, width(), 1), theme::border(palette()));
}

QWidget* separator(Qt::Orientation orientation, QWidget* parent)
{
    return new SeparatorLine(orientation, parent);
}

QPushButton* actionButton(const QString& text, const char* iconName, QWidget* parent, bool primary)
{
    auto* button = new QPushButton(text, parent);
    if (iconName)
        button->setIcon(icon(iconName));
    button->setAutoDefault(false);
    button->setDefault(primary);
    button->setMinimumHeight(32);
    return button;
}

QToolButton* toolButton(const char* iconName, const QString& text, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setIcon(icon(iconName));
    button->setText(text);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setAutoRaise(true);
    return button;
}

Card* cardWith(const QString& title, QWidget* content, QWidget* parent, const QString& subtitle)
{
    auto* card = new Card(title, parent);
    card->setSubtitle(subtitle);
    card->content()->addWidget(content, 1);
    return card;
}

void clearLayout(QLayout* layout)
{
    while (QLayoutItem* element = layout->takeAt(0)) {
        if (QWidget* widget = element->widget()) {
            widget->hide();
            widget->deleteLater();
        }
        delete element;
    }
}

} // namespace lh::ui
