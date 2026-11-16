#include "Sidebar.h"

#include "Components.h"
#include "loghunter_version.h"
#include "ui/Icons.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QButtonGroup>
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QVBoxLayout>

namespace lh::ui {

// ── NavigationItem ───────────────────────────────────────────────────────────

NavigationItem::NavigationItem(const char* icon, const QString& label, QWidget* parent)
    : QAbstractButton(parent), m_icon(icon)
{
    setText(label);
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
    setAttribute(Qt::WA_Hover);
}

void NavigationItem::setBadge(const QString& text, const QColor& color)
{
    m_badge = text;
    m_badgeColor = color;
    update();
}

QSize NavigationItem::sizeHint() const
{
    return {200, std::max(38, fontMetrics().height() + 20)};
}

void NavigationItem::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette& pal = palette();
    const QRectF frame = QRectF(rect()).adjusted(8, 2, -8, -2);
    const QColor accent = theme::accent(pal);
    if (isChecked()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme::selectionBackground(pal));
        painter.drawRoundedRect(frame, 6, 6);
        painter.setBrush(accent);
        painter.drawRoundedRect(QRectF(frame.left(), frame.center().y() - 8, 3, 16), 1.5, 1.5);
    } else if (m_hover || hasFocus()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(theme::hoverBackground(pal));
        painter.drawRoundedRect(frame, 6, 6);
    }

    const QColor textColor = isChecked() ? theme::text(pal) : theme::blend(theme::text(pal), theme::pageBackground(pal), 0.18);
    const int side = 18;
    const QRect iconZone(static_cast<int>(frame.left()) + 14, (height() - side) / 2, side, side);
    ui::icon(m_icon, isChecked() ? accent : textColor).paint(&painter, iconZone);

    QFont textFont = font();
    textFont.setWeight(isChecked() ? QFont::DemiBold : QFont::Normal);
    painter.setFont(textFont);
    painter.setPen(textColor);
    int right = static_cast<int>(frame.right()) - 10;
    if (!m_badge.isEmpty()) {
        QFont smaller = font();
        smaller.setPointSizeF(font().pointSizeF() * 0.85);
        smaller.setWeight(QFont::DemiBold);
        const QFontMetrics metrics(smaller);
        const int width = std::max(metrics.horizontalAdvance(m_badge) + 12, metrics.height() + 4);
        const QRectF dot(right - width, height() / 2.0 - (metrics.height() + 2) / 2.0, width, metrics.height() + 2);
        const QColor background = m_badgeColor.isValid() ? m_badgeColor : accent;
        painter.setPen(Qt::NoPen);
        painter.setBrush(background);
        painter.drawRoundedRect(dot, dot.height() / 2, dot.height() / 2);
        painter.setFont(smaller);
        painter.setPen(background.lightness() > 150 ? QColor(0x11, 0x18, 0x27) : QColor(Qt::white));
        painter.drawText(dot, Qt::AlignCenter, m_badge);
        painter.setFont(textFont);
        painter.setPen(textColor);
        right = static_cast<int>(dot.left()) - 6;
    }
    const int left = iconZone.right() + 12;
    painter.drawText(QRect(left, 0, right - left, height()), Qt::AlignLeft | Qt::AlignVCenter,
                     QFontMetrics(textFont).elidedText(text(), Qt::ElideRight, right - left));
}

void NavigationItem::enterEvent(QEnterEvent* event)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(event);
}

void NavigationItem::leaveEvent(QEvent* event)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(event);
}

// ── Sidebar ──────────────────────────────────────────────────────────────────

Sidebar::Sidebar(QWidget* parent) : QWidget(parent)
{
    setFixedWidth(232);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 16, 1, 12);
    layout->setSpacing(2);

    auto* identity = new QHBoxLayout;
    identity->setContentsMargins(20, 0, 16, 18);
    identity->setSpacing(10);
    auto* logo = new QLabel(this);
    QIcon applicationIcon;
    for (const int size : {32, 48, 64})
        applicationIcon.addFile(QStringLiteral(":/images/logo-%1.png").arg(size));
    logo->setPixmap(applicationIcon.pixmap(QSize(28, 28), devicePixelRatioF()));
    identity->addWidget(logo);
    identity->addWidget(new Label(QStringLiteral(LOGHUNTER_NAME), Label::Role::Title, this), 1);
    layout->addLayout(identity);

    m_elements = new QVBoxLayout;
    m_elements->setSpacing(2);
    layout->addLayout(m_elements);
    layout->addStretch();
    auto* version =
        new Label(qtTrId("common.version").arg(QStringLiteral(LOGHUNTER_VERSION)), Label::Role::Small, this);
    version->setContentsMargins(22, 0, 0, 0);
    layout->addWidget(version);
}

int Sidebar::add(const char* iconName, const QString& label, const QString& tooltip)
{
    auto* element = new NavigationItem(iconName, label, this);
    element->setToolTip(tooltip);
    const int index = static_cast<int>(m_list.size());
    m_list.append(element);
    m_elements->addWidget(element);
    connect(element, &QAbstractButton::clicked, this, [this, index] {
        select(index);
        emit pageRequested(index);
    });
    return index;
}

void Sidebar::select(int index)
{
    for (int i = 0; i < m_list.size(); ++i)
        m_list[i]->setChecked(i == index);
}

void Sidebar::setBadge(int index, const QString& text, const QColor& color)
{
    if (index >= 0 && index < m_list.size())
        m_list[index]->setBadge(text, color);
}

void Sidebar::setItemVisible(int index, bool visible)
{
    if (index >= 0 && index < m_list.size())
        m_list[index]->setVisible(visible);
}

void Sidebar::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    const QPalette& pal = palette();
    painter.fillRect(rect(), theme::blend(theme::pageBackground(pal), theme::cardBackground(pal), 0.55));
    painter.fillRect(QRect(width() - 1, 0, 1, height()), theme::border(pal));
}

} // namespace lh::ui
