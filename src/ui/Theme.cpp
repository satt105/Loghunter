#include "Theme.h"

#include "Settings.h"
#include "core/Actions.h"
#include "core/Session.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QStyle>
#include <QStyleFactory>
#include <QStyleHints>
#include <QTimer>

#include <algorithm>

namespace lh::ui::theme {

namespace {

constexpr double MIN_FONT_SIZE = 10.5;

ThemeMode g_mode = ThemeMode::System;
bool g_applied = false;
bool g_dark = false;

QPalette buildPalette(bool dark)
{
    struct Colors
    {
        QColor window, base, alternate, text, secondary, button, light, medium, dark, accent;
    };
    const Colors c = dark ? Colors{QColor(0x1c, 0x1e, 0x22), QColor(0x25, 0x28, 0x2d), QColor(0x2a, 0x2d, 0x33),
                                   QColor(0xec, 0xee, 0xf1), QColor(0x98, 0x9d, 0xa6), QColor(0x2e, 0x31, 0x37),
                                   QColor(0x3d, 0x41, 0x48), QColor(0x34, 0x37, 0x3e), QColor(0x12, 0x13, 0x16),
                                   QColor(0x5b, 0x9c, 0xff)}
                              : Colors{QColor(0xf4, 0xf5, 0xf8), QColor(0xff, 0xff, 0xff), QColor(0xf7, 0xf8, 0xfa),
                                       QColor(0x1b, 0x1d, 0x22), QColor(0x69, 0x6e, 0x78), QColor(0xff, 0xff, 0xff),
                                       QColor(0xff, 0xff, 0xff), QColor(0xd4, 0xd7, 0xdd), QColor(0xa6, 0xaa, 0xb2),
                                       QColor(0x25, 0x63, 0xeb)};
    QPalette p;
    p.setColor(QPalette::Window, c.window);
    p.setColor(QPalette::WindowText, c.text);
    p.setColor(QPalette::Base, c.base);
    p.setColor(QPalette::AlternateBase, c.alternate);
    p.setColor(QPalette::ToolTipBase, c.base);
    p.setColor(QPalette::ToolTipText, c.text);
    p.setColor(QPalette::PlaceholderText, c.secondary);
    p.setColor(QPalette::Text, c.text);
    p.setColor(QPalette::Button, c.button);
    p.setColor(QPalette::ButtonText, c.text);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Light, c.light);
    p.setColor(QPalette::Midlight, blend(c.light, c.medium, 0.5));
    p.setColor(QPalette::Mid, c.medium);
    p.setColor(QPalette::Dark, c.dark);
    p.setColor(QPalette::Shadow, Qt::black);
    p.setColor(QPalette::Highlight, c.accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::Link, c.accent);
    p.setColor(QPalette::LinkVisited, c.accent.darker(120));
    p.setColor(QPalette::Accent, c.accent);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, c.secondary);
    return p;
}

void checkSchema()
{
    const Qt::ColorScheme isSystem = QGuiApplication::styleHints()->colorScheme();
    const bool dark = g_mode == ThemeMode::Dark
                      || (g_mode == ThemeMode::System && isSystem == Qt::ColorScheme::Dark);
    if (g_applied && dark == g_dark)
        return;
    if (!g_applied)
        QApplication::setStyle(QStyleFactory::create(QStringLiteral("fusion")));
    QApplication::setPalette(buildPalette(dark));
    qApp->setStyleSheet(styleSheet(QApplication::palette()));
    g_applied = true;
    g_dark = dark;
}

bool darkPalette(const QPalette& palette)
{
    return palette.color(QPalette::Window).lightness() < 128;
}

QString arrowPath(const char* name, const char* points, const QColor& color)
{
    const QString folder = dataFolder() + QStringLiteral("/icons");
    QDir().mkpath(folder);
    const QString path = QStringLiteral("%1/%2-%3.svg").arg(folder, QLatin1String(name), color.name(QColor::HexRgb).mid(1));
    if (!QFileInfo::exists(path)) {
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\" fill=\"none\" "
                                      "stroke=\"%1\" stroke-width=\"2.4\" stroke-linecap=\"round\" "
                                      "stroke-linejoin=\"round\"><polyline points=\"%2\"/></svg>")
                           .arg(color.name(QColor::HexRgb), QLatin1String(points))
                           .toUtf8());
    }
    return QDir::fromNativeSeparators(path);
}

} // namespace

void initialize()
{
    QFont base = QApplication::font();
    if (QFontDatabase::hasFamily(QStringLiteral("Segoe UI Variable Text")))
        base.setFamily(QStringLiteral("Segoe UI Variable Text"));
    if (base.pointSizeF() > 0 && base.pointSizeF() < MIN_FONT_SIZE)
        base.setPointSizeF(MIN_FONT_SIZE);
    QApplication::setFont(base);
    const int value = settings::read(settings::key::THEME, 0).toInt();
    apply(static_cast<ThemeMode>(qBound(0, value, 2)));
    QObject::connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, qApp, [] {
        QTimer::singleShot(0, qApp, checkSchema);
    });
}

void apply(ThemeMode mode)
{
    g_mode = mode;
    settings::write(settings::key::THEME, static_cast<int>(mode));
    const Qt::ColorScheme schema = mode == ThemeMode::Light  ? Qt::ColorScheme::Light
                                   : mode == ThemeMode::Dark ? Qt::ColorScheme::Dark
                                                             : Qt::ColorScheme::Unknown;
    QGuiApplication::styleHints()->setColorScheme(schema);
    checkSchema();
}

ThemeMode mode()
{
    return g_mode;
}

bool isDark()
{
    return darkPalette(QApplication::palette());
}

QColor pageBackground(const QPalette& palette)
{
    return palette.color(QPalette::Window);
}

QColor cardBackground(const QPalette& palette)
{
    const QColor window = palette.color(QPalette::Window);
    const QColor base = palette.color(QPalette::Base);
    if (darkPalette(palette))
        return base.lightness() > window.lightness() ? base : blend(window, Qt::white, 0.05);
    return base;
}

QColor hoverBackground(const QPalette& palette)
{
    return blend(cardBackground(palette), text(palette), 0.06);
}

QColor selectionBackground(const QPalette& palette)
{
    return blend(cardBackground(palette), accent(palette), darkPalette(palette) ? 0.30 : 0.14);
}

QColor border(const QPalette& palette)
{
    return blend(cardBackground(palette), text(palette), darkPalette(palette) ? 0.17 : 0.12);
}

QColor text(const QPalette& palette)
{
    return palette.color(QPalette::WindowText);
}

QColor secondaryText(const QPalette& palette)
{
    return blend(text(palette), cardBackground(palette), 0.40);
}

QColor accent(const QPalette& palette)
{
    QColor color = palette.color(QPalette::Accent);
    if (darkPalette(palette) && color.lightness() < 120)
        color = color.lighter(160);
    return color;
}

QColor brand(const QPalette& palette)
{
    return darkPalette(palette) ? QColor(0x60, 0xa5, 0xfa) : QColor(0x25, 0x63, 0xeb);
}

QColor severityColor(Severity severity)
{
    const bool dark = isDark();
    switch (severity) {
    case Severity::Critical:
        return dark ? QColor(0xf8, 0x71, 0x71) : QColor(0xdc, 0x26, 0x26);
    case Severity::High:
        return dark ? QColor(0xfb, 0x92, 0x3c) : QColor(0xea, 0x58, 0x0c);
    case Severity::Medium:
        return dark ? QColor(0xfb, 0xbf, 0x24) : QColor(0xd9, 0x77, 0x06);
    case Severity::Low:
        break;
    }
    return dark ? QColor(0x60, 0xa5, 0xfa) : QColor(0x25, 0x63, 0xeb);
}

QColor levelColor(int rank)
{
    switch (rank) {
    case 3:
        return severityColor(Severity::Critical);
    case 2:
        return severityColor(Severity::High);
    case 1:
        return severityColor(Severity::Medium);
    default:
        return QColor(0x94, 0xa3, 0xb8);
    }
}

QColor successColor()
{
    return isDark() ? QColor(0x4a, 0xde, 0x80) : QColor(0x16, 0xa3, 0x4a);
}

QColor categoryColor(const QString& key)
{
    return QColor(lh::categoryColor(key));
}

QString styleSheet(const QPalette& palette)
{
    const auto name = [](const QColor& color) { return color.name(QColor::HexArgb); };
    const bool dark = darkPalette(palette);
    const QColor page = pageBackground(palette);
    const QColor card = cardBackground(palette);
    const QColor hover = hoverBackground(palette);
    const QColor selection = selectionBackground(palette);
    const QColor stroke = border(palette);
    const QColor strongStroke = blend(stroke, text(palette), 0.25);
    const QColor primary = text(palette);
    const QColor secondary = secondaryText(palette);
    const QColor tint = accent(palette);
    const QColor hoverTint = dark ? tint.lighter(112) : tint.darker(112);
    const QColor grip = blend(card, primary, 0.22);
    const QColor gripHover = blend(card, primary, 0.38);
    const QColor alternate = palette.color(QPalette::AlternateBase);
    const QColor disabled = blend(page, card, 0.5);
    const QString downArrow = arrowPath("arrow-down", "6 9 12 15 18 9", secondary);
    const QString upArrow = arrowPath("arrow-up", "6 15 12 9 18 15", secondary);
    const QString disabledDownArrow = arrowPath("arrow-down", "6 9 12 15 18 9", strongStroke);

    QString styleSheet;
    styleSheet += QStringLiteral(
                      "QMenuBar { background: %1; border-bottom: 1px solid %2; padding: 3px 8px; }"
                      "QMenuBar::item { background: transparent; color: %3; padding: 6px 14px; margin: 1px 2px;"
                      " border-radius: 6px; }"
                      "QMenuBar::item:selected { background: %4; }"
                      "QMenuBar::item:pressed { background: %5; }"
                      "QMenu { background: %6; color: %3; border: 1px solid %2; border-radius: 8px; padding: 6px 0px; }"
                      "QMenu::item { padding: 7px 36px 7px 40px; margin: 1px 6px; border-radius: 6px; }"
                      "QMenu::item:selected { background: %5; }"
                      "QMenu::item:disabled { color: %7; }"
                      "QMenu::icon { left: 14px; }"
                      "QMenu::indicator { left: 14px; width: 16px; height: 16px; }"
                      "QMenu::separator { height: 1px; background: %2; margin: 5px 14px; }"
                      "QMenu::right-arrow { right: 12px; }"
                      "QToolTip { background: %6; color: %3; border: 1px solid %2; padding: 6px 8px; border-radius: 6px; }")
                      .arg(name(page), name(stroke), name(primary), name(hover), name(selection), name(card), name(secondary));

    styleSheet += QStringLiteral(
                      "QPushButton { background: %1; color: %2; border: 1px solid %3; border-radius: 6px;"
                      " padding: 5px 14px; min-height: 20px; }"
                      "QPushButton:hover { background: %4; border-color: %5; }"
                      "QPushButton:pressed { background: %6; }"
                      "QPushButton:default { background: %7; color: white; border-color: %7; }"
                      "QPushButton:default:hover { background: %8; border-color: %8; }"
                      "QPushButton:disabled { color: %9; background: %10; border-color: %3; }"
                      "QPushButton::menu-indicator { subcontrol-position: right center; subcontrol-origin: padding;"
                      " right: 6px; }"
                      "QToolButton { background: transparent; color: %2; border: 1px solid transparent; border-radius: 6px;"
                      " padding: 4px 8px; }"
                      "QToolButton:hover { background: %4; }"
                      "QToolButton:pressed, QToolButton:checked { background: %6; }"
                      "QToolButton:disabled { color: %9; }"
                      "QToolButton::menu-indicator { image: none; width: 0px; }"
                      "QToolButton[style=\"border\"] { background: %1; border-color: %3; padding: 5px 12px; }"
                      "QToolButton[style=\"border\"]:hover { background: %4; border-color: %5; }"
                      "QToolButton[style=\"border\"]:checked { background: %6; border-color: %7; }"
                      "QToolButton[style=\"accent\"] { background: %7; color: white; border-color: %7; padding: 5px 12px; }"
                      "QToolButton[style=\"accent\"]:hover { background: %8; border-color: %8; }"
                      "QToolButton[style=\"accent\"]:disabled { background: %3; color: %9; border-color: %3; }")
                      .arg(name(card), name(primary), name(stroke), name(hover), name(strongStroke), name(selection),
                           name(tint), name(hoverTint), name(secondary), name(disabled));

    styleSheet += QStringLiteral(
                      "QLineEdit, QAbstractSpinBox, QComboBox { background: %1; color: %2; border: 1px solid %3;"
                      " border-radius: 6px; padding: 4px 8px; min-height: 22px; selection-background-color: %4;"
                      " selection-color: white; }"
                      "QLineEdit:focus, QAbstractSpinBox:focus, QComboBox:focus { border-color: %4; }"
                      "QLineEdit:disabled, QAbstractSpinBox:disabled, QComboBox:disabled { color: %5; background: %6; }"
                      "QComboBox::drop-down, QDateTimeEdit::drop-down { border: none; width: 24px;"
                      " subcontrol-origin: padding; subcontrol-position: right center; }"
                      "QComboBox::down-arrow, QDateTimeEdit::down-arrow { image: url(%8); width: 14px; height: 14px; }"
                      "QComboBox::down-arrow:disabled, QDateTimeEdit::down-arrow:disabled { image: url(%10); }"
                      "QComboBox QAbstractItemView { background: %1; color: %2; border: 1px solid %3; border-radius: 6px;"
                      " padding: 4px; outline: 0; selection-background-color: %7; selection-color: %2; }"
                      "QComboBox QAbstractItemView::item { padding: 5px 8px; min-height: 22px; border-radius: 4px; }"
                      "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { border: none; width: 20px;"
                      " background: transparent; subcontrol-origin: border; }"
                      "QAbstractSpinBox::up-button { subcontrol-position: top right; }"
                      "QAbstractSpinBox::down-button { subcontrol-position: bottom right; }"
                      "QAbstractSpinBox::up-arrow { image: url(%9); width: 10px; height: 10px; }"
                      "QAbstractSpinBox::down-arrow { image: url(%8); width: 10px; height: 10px; }"
                      "QAbstractSpinBox::up-arrow:disabled, QAbstractSpinBox::up-arrow:off { image: url(%10); }"
                      "QAbstractSpinBox::down-arrow:disabled, QAbstractSpinBox::down-arrow:off { image: url(%10); }"
                      "QCheckBox, QRadioButton { spacing: 8px; color: %2; }"
                      "QCheckBox::indicator, QRadioButton::indicator { width: 16px; height: 16px; }"
                      "QGroupBox { border: 1px solid %3; border-radius: 8px; margin-top: 16px; padding: 12px 8px 6px 8px; }"
                      "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; left: 10px;"
                      " padding: 0px 4px; color: %5; }")
                      .arg(name(card), name(primary), name(stroke), name(tint), name(secondary), name(disabled),
                           name(selection), downArrow, upArrow)
                      .arg(disabledDownArrow);

    styleSheet += QStringLiteral(
                      "QHeaderView { background: %1; border: none; }"
                      "QHeaderView::section { background: %1; color: %2; border: none; border-bottom: 1px solid %3;"
                      " padding: 6px 8px; font-weight: 600; }"
                      "QHeaderView::section:hover { background: %4; color: %5; }"
                      "QHeaderView::up-arrow, QHeaderView::down-arrow { subcontrol-origin: padding;"
                      " subcontrol-position: top center; width: 12px; height: 12px; top: -3px; }"
                      "QHeaderView::up-arrow { image: url(%10); }"
                      "QHeaderView::down-arrow { image: url(%11); }"
                      "QAbstractItemView { outline: 0; }"
                      "QTableView, QTreeView, QListView { background: %6; alternate-background-color: %7; color: %5;"
                      " border: none; gridline-color: %3; selection-background-color: %8; selection-color: %5; }"
                      "QTableView::item, QTreeView::item, QListView::item { padding: 2px 6px; border: none; }"
                      "QTableView::item:hover, QTreeView::item:hover, QListView::item:hover { background: %4; }"
                      "QTableView::item:selected, QTreeView::item:selected, QListView::item:selected { background: %8;"
                      " color: %5; }"
                      "QAbstractScrollArea::corner { background: transparent; }"
                      "QScrollArea { border: none; background: transparent; }"
                      "QPlainTextEdit, QTextEdit { background: transparent; border: none; color: %5;"
                      " selection-background-color: %9; selection-color: white; }")
                      .arg(name(page), name(secondary), name(stroke), name(hover), name(primary), name(card), name(alternate),
                           name(selection), name(tint))
                      .arg(upArrow, downArrow);

    styleSheet += QStringLiteral(
                      "QScrollBar:vertical { background: transparent; width: 12px; margin: 2px; }"
                      "QScrollBar::handle:vertical { background: %1; border-radius: 4px; min-height: 30px; }"
                      "QScrollBar::handle:vertical:hover { background: %2; }"
                      "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; border: none;"
                      " background: none; }"
                      "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
                      "QScrollBar:horizontal { background: transparent; height: 12px; margin: 2px; }"
                      "QScrollBar::handle:horizontal { background: %1; border-radius: 4px; min-width: 30px; }"
                      "QScrollBar::handle:horizontal:hover { background: %2; }"
                      "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; border: none;"
                      " background: none; }"
                      "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }"
                      "QTabWidget::pane { border: none; }"
                      "QTabBar { background: transparent; }"
                      "QTabBar::tab { background: transparent; color: %3; border: none;"
                      " border-bottom: 2px solid transparent; padding: 8px 14px; margin-right: 4px; }"
                      "QTabBar::tab:hover { color: %4; }"
                      "QTabBar::tab:selected { color: %4; border-bottom-color: %5; }"
                      "QSplitter::handle { background: transparent; }"
                      "QProgressBar { background: %6; border: none; border-radius: 4px; text-align: center; color: %4; }"
                      "QProgressBar::chunk { background: %5; border-radius: 4px; }"
                      "QStatusBar { background: %7; border-top: 1px solid %6; color: %3; }"
                      "QStatusBar::item { border: none; }"
                      "QStatusBar QLabel { color: %3; }"
                      "QStatusBar QToolButton { padding: 2px 6px; color: %3; }")
                      .arg(name(grip), name(gripHover), name(secondary), name(primary), name(tint), name(stroke),
                           name(page));
    return styleSheet;
}

QFont font(const QFont& base, double factor, QFont::Weight weight)
{
    QFont result = base;
    if (base.pointSizeF() > 0)
        result.setPointSizeF(base.pointSizeF() * factor);
    else
        result.setPixelSize(std::max(1, static_cast<int>(base.pixelSize() * factor)));
    result.setWeight(weight);
    return result;
}

} // namespace lh::ui::theme
