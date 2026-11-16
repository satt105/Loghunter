#include "FilterBar.h"

#include "Components.h"
#include "core/Actions.h"
#include "core/Catalog.h"
#include "core/DateTime.h"
#include "ui/Workspace.h"
#include "ui/Theme.h"

#include <QEnterEvent>
#include <QHBoxLayout>
#include <QPainter>
#include <QWidgetItem>

namespace lh::ui {

// ── FlowLayout ───────────────────────────────────────────────────────────────

FlowLayout::FlowLayout(QWidget* parent, int spacing) : QLayout(parent), m_spacing(spacing)
{
    setContentsMargins(0, 0, 0, 0);
}

FlowLayout::~FlowLayout()
{
    while (QLayoutItem* element = takeAt(0))
        delete element;
}

void FlowLayout::addItem(QLayoutItem* element)
{
    m_elements.append(element);
}

QLayoutItem* FlowLayout::itemAt(int index) const
{
    return index >= 0 && index < m_elements.size() ? m_elements[index] : nullptr;
}

QLayoutItem* FlowLayout::takeAt(int index)
{
    return index >= 0 && index < m_elements.size() ? m_elements.takeAt(index) : nullptr;
}

int FlowLayout::heightForWidth(int width) const
{
    return arrange(QRect(0, 0, width, 0), true);
}

void FlowLayout::setGeometry(const QRect& rectangle)
{
    QLayout::setGeometry(rectangle);
    arrange(rectangle, false);
}

QSize FlowLayout::sizeHint() const
{
    return minimumSize();
}

QSize FlowLayout::minimumSize() const
{
    QSize size;
    for (const QLayoutItem* element : m_elements)
        size = size.expandedTo(element->minimumSize());
    const QMargins margins = contentsMargins();
    return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
}

int FlowLayout::arrange(const QRect& rectangle, bool simulation) const
{
    const QMargins margins = contentsMargins();
    const QRect useful = rectangle.adjusted(margins.left(), margins.top(), -margins.right(), -margins.bottom());
    int x = useful.x();
    int y = useful.y();
    int rowHeight = 0;
    for (QLayoutItem* element : m_elements) {
        if (element->isEmpty())
            continue;
        const QSize size = element->sizeHint();
        if (x + size.width() > useful.right() + 1 && rowHeight > 0) {
            x = useful.x();
            y += rowHeight + m_spacing;
            rowHeight = 0;
        }
        if (!simulation)
            element->setGeometry(QRect(QPoint(x, y), QSize(std::min(size.width(), useful.width()), size.height())));
        x += size.width() + m_spacing;
        rowHeight = std::max(rowHeight, size.height());
    }
    return y + rowHeight - rectangle.y() + margins.bottom();
}

// ── Chip ─────────────────────────────────────────────────────────────────────

namespace {

constexpr int MAX_TEXT_WIDTH = 340;

} // namespace

Chip::Chip(const QString& text, const QColor& color, QWidget* parent) : QAbstractButton(parent), m_color(color)
{
    setText(text);
    setToolTip(text + QLatin1Char('\n') + qtTrId("filters.remove_help"));
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::TabFocus);
}

QSize Chip::sizeHint() const
{
    const QFontMetrics metrics(font());
    const int textWidth = std::min(metrics.horizontalAdvance(text()), MAX_TEXT_WIDTH);
    const int dot = m_color.isValid() ? 14 : 0;
    return {12 + dot + textWidth + 8 + 12 + 8, metrics.height() + 10};
}

void Chip::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QPalette& pal = palette();
    const QColor accent = theme::accent(pal);
    const bool active = m_hover || hasFocus();
    const QRectF frame = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    painter.setPen(QPen(theme::blend(theme::border(pal), accent, active ? 0.6 : 0.3), 1));
    painter.setBrush(theme::blend(theme::cardBackground(pal), accent, active ? 0.16 : 0.08));
    painter.drawRoundedRect(frame, frame.height() / 2, frame.height() / 2);

    int x = 12;
    if (m_color.isValid()) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(m_color);
        painter.drawEllipse(QPointF(x + 4, frame.center().y()), 4, 4);
        x += 14;
    }
    const QFontMetrics metrics(font());
    const int textWidth = width() - x - 28;
    painter.setPen(theme::text(pal));
    painter.drawText(QRect(x, 0, textWidth, height()), Qt::AlignLeft | Qt::AlignVCenter,
                     metrics.elidedText(text(), Qt::ElideMiddle, textWidth));

    const QPointF center(width() - 16, frame.center().y());
    painter.setPen(QPen(active ? theme::text(pal) : theme::secondaryText(pal), 1.4, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(center + QPointF(-3.5, -3.5), center + QPointF(3.5, 3.5));
    painter.drawLine(center + QPointF(3.5, -3.5), center + QPointF(-3.5, 3.5));
}

void Chip::enterEvent(QEnterEvent* event)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(event);
}

void Chip::leaveEvent(QEvent* event)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(event);
}

// ── FilterBar ────────────────────────────────────────────────────────────────

FilterBar::FilterBar(Workspace* workspace, QWidget* parent) : QWidget(parent), m_workspace(workspace)
{
    m_layout = new FlowLayout(this, 6);
    m_layout->setContentsMargins(24, 0, 24, 10);
    connect(workspace, &Workspace::filtersChanged, this, &FilterBar::rebuild);
    connect(workspace, &Workspace::dataModified, this, &FilterBar::rebuild);
    rebuild();
}

void FilterBar::add(const QString& text, std::function<void(Filters&)> removal, const QColor& color)
{
    auto* chip = new Chip(text, color, this);
    connect(chip, &Chip::clicked, this, [this, removal = std::move(removal)] {
        Filters filters = m_workspace->filters();
        removal(filters);
        if (filters.user.isEmpty() && filters.file.isEmpty() && filters.ip.isEmpty())
            filters.exact = false;
        m_workspace->setFilters(filters);
    });
    m_layout->addWidget(chip);
}

void FilterBar::rebuild()
{
    clearLayout(m_layout);

    const Filters& f = m_workspace->filters();
    if (!f.text.trimmed().isEmpty())
        add(qtTrId("filters.chip_search").arg(f.text.trimmed()), [](Filters& x) { x.text.clear(); });
    if (!f.rule.isEmpty())
        add(qtTrId("filters.chip_rule").arg(f.ruleTitle.isEmpty() ? f.rule : f.ruleTitle), [](Filters& x) {
            x.rule.clear();
            x.ruleTitle.clear();
        });
    if (f.group != 0)
        add(qtTrId("filters.chip_group").arg(f.groupLabel), [](Filters& x) {
            x.group = 0;
            x.groupLabel.clear();
        });
    if (!f.events.trimmed().isEmpty())
        add(qtTrId("filters.chip_events").arg(f.events.trimmed()),
            [](Filters& x) { x.events.clear(); });
    if (f.level != 0)
        add(qtTrId("filters.chip_level").arg(levelFilterLabel(f.level)), [](Filters& x) { x.level = 0; });
    if (!f.channel.isEmpty())
        add(qtTrId("filters.chip_windows_log").arg(shortChannelName(f.channel)),
            [](Filters& x) { x.channel.clear(); });
    if (!f.ip.trimmed().isEmpty())
        add(qtTrId("filters.chip_ip").arg(f.ip.trimmed()), [](Filters& x) { x.ip.clear(); });
    if (f.hasExactPeriod())
        add(qtTrId("filters.chip_timeline").arg(periodDescription(f.startUtc, f.endUtc, m_workspace->timeZone())),
            [](Filters& x) {
                x.startUtc = 0;
                x.endUtc = 0;
            });
    if (!f.user.trimmed().isEmpty())
        add((f.exact ? qtTrId("filters.chip_user_exact") : qtTrId("filters.chip_user"))
                .arg(f.user.trimmed()),
            [](Filters& x) { x.user.clear(); });
    if (!f.category.isEmpty())
        add(
            qtTrId("filters.chip_action").arg(categoryLabel(f.category)), [](Filters& x) { x.category.clear(); },
            theme::categoryColor(f.category));
    if (!f.file.trimmed().isEmpty())
        add(
            (f.exact ? qtTrId("filters.chip_file_exact") : qtTrId("filters.chip_file")).arg(f.file.trimmed()),
            [](Filters& x) { x.file.clear(); });
    if (!f.folder.isEmpty())
        add(qtTrId("filters.chip_folder").arg(f.folder), [](Filters& x) { x.folder.clear(); });
    if (!f.process.trimmed().isEmpty())
        add(qtTrId("filters.chip_program").arg(f.process.trimmed()), [](Filters& x) { x.process.clear(); });
    if (!f.machine.trimmed().isEmpty())
        add(qtTrId("filters.chip_computer").arg(f.machine.trimmed()), [](Filters& x) { x.machine.clear(); });
    if (f.sourceId > 0) {
        QString name = qtTrId("filters.number").arg(f.sourceId);
        for (const Source& source : m_workspace->sources())
            if (source.id == f.sourceId)
                name = source.label();
        add(qtTrId("filters.chip_log").arg(name), [](Filters& x) { x.sourceId = 0; });
    }
    if (f.since.isValid() || f.until.isValid()) {
        QString text;
        if (f.since.isValid() && f.until.isValid())
            text = f.since == f.until
                       ? qtTrId("filters.chip_on").arg(formatDate(f.since))
                       : qtTrId("filters.chip_period").arg(formatDate(f.since), formatDate(f.until));
        else if (f.since.isValid())
            text = qtTrId("filters.chip_from").arg(formatDate(f.since));
        else
            text = qtTrId("filters.chip_until").arg(formatDate(f.until));
        add(text, [](Filters& x) {
            x.since = {};
            x.until = {};
        });
    }
    if (f.hourRange())
        add(qtTrId("filters.chip_hours").arg(f.startHour).arg(f.endHour), [](Filters& x) {
            x.startHour = -1;
            x.endHour = -1;
        });
    if (f.accounts == AccountFilter::Users)
        add(qtTrId("filters.user_accounts"), [](Filters& x) { x.accounts = AccountFilter::All; });
    else if (f.accounts == AccountFilter::System)
        add(qtTrId("filters.system_accounts"), [](Filters& x) { x.accounts = AccountFilter::All; });
    if (f.includeHandle)
        add(qtTrId("filters.chip_simple_opens"), [](Filters& x) { x.includeHandle = false; });

    const bool anyActive = m_layout->count() > 0;
    if (anyActive) {
        auto* remove = new LinkButton(qtTrId("filters.clear_all"), this);
        remove->setMinimumHeight(fontMetrics().height() + 10);
        connect(remove, &LinkButton::clicked, m_workspace, &Workspace::resetFilters);
        m_layout->addWidget(remove);
    }
    setVisible(m_active && anyActive);
    updateGeometry();
}

void FilterBar::setActive(bool active)
{
    m_active = active;
    setVisible(m_active && m_layout->count() > 0);
}

} // namespace lh::ui
