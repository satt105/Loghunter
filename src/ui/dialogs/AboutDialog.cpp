#include "AboutDialog.h"

#include "core/Session.h"
#include "loghunter_version.h"
#include "ui/widgets/Components.h"

#include <sqlite3.h>

#include <QApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QFile>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSysInfo>
#include <QUrl>
#include <QVBoxLayout>

namespace lh::ui {

namespace {

QString compilerName()
{
#if defined(_MSC_VER)
    return QStringLiteral("MSVC %1").arg(_MSC_FULL_VER);
#elif defined(__clang__)
    return QStringLiteral("Clang %1.%2.%3").arg(__clang_major__).arg(__clang_minor__).arg(__clang_patchlevel__);
#elif defined(__GNUC__)
    return QStringLiteral("GCC %1.%2.%3").arg(__GNUC__).arg(__GNUC_MINOR__).arg(__GNUC_PATCHLEVEL__);
#else
    return qtTrId("about.unknown");
#endif
}

} // namespace

AboutDialog::AboutDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(qtTrId("about.title"));
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 16);
    layout->setSpacing(14);

    auto* header = new QHBoxLayout;
    header->setSpacing(18);
    auto* logo = new QLabel(this);
    QIcon icon;
    for (const int size : {64, 128, 256})
        icon.addFile(QStringLiteral(":/images/logo-%1.png").arg(size));
    logo->setPixmap(icon.pixmap(QSize(80, 80), devicePixelRatioF()));
    header->addWidget(logo, 0, Qt::AlignTop);
    auto* texts = new QVBoxLayout;
    texts->setSpacing(2);
    texts->addWidget(new Label(QStringLiteral(LOGHUNTER_NAME), Label::Role::Large, this));
    texts->addWidget(
        new Label(qtTrId("common.version").arg(QStringLiteral(LOGHUNTER_VERSION)), Label::Role::Title, this));
    auto* description = new Label(qtTrId("about.description"),
                                  Label::Role::Secondary, this);
    description->setWordWrap(true);
    texts->addSpacing(6);
    texts->addWidget(description);
    header->addLayout(texts, 1);
    layout->addLayout(header);

    layout->addWidget(separator(Qt::Horizontal, this));
    auto* information = new Label({}, Label::Role::Normal, this);
    information->setTextFormat(Qt::RichText);
    information->setTextInteractionFlags(Qt::TextSelectableByMouse);
    information->setText(QStringLiteral("<table cellspacing='0' cellpadding='3'>"
                                        "<tr><td>%1</td><td><b>%2</b></td></tr>"
                                        "<tr><td>%3</td><td>%4</td></tr>"
                                        "<tr><td>%12</td><td>© 2026 %4</td></tr>"
                                        "<tr><td>%13</td><td>GNU GPL v3</td></tr>"
                                        "<tr><td>%5</td><td>Qt %6 · SQLite %7 · miniz 3.1.2</td></tr>"
                                        "<tr><td>%8</td><td>%9</td></tr>"
                                        "<tr><td>%10</td><td>%11</td></tr></table>")
                             .arg(qtTrId("about.publisher"), QStringLiteral(LOGHUNTER_PUBLISHER).toHtmlEscaped(),
                                  qtTrId("about.author"), QStringLiteral(LOGHUNTER_AUTHOR).toHtmlEscaped(),
                                  qtTrId("about.components"), QString::fromLatin1(qVersion()),
                                  QString::fromLatin1(sqlite3_libversion()), qtTrId("about.build"),
                                  compilerName() + QStringLiteral(" · ") + QSysInfo::buildCpuArchitecture())
                             .arg(qtTrId("about.system"), QSysInfo::prettyProductName().toHtmlEscaped(),
                                  qtTrId("about.copyright"), qtTrId("about.license")));
    layout->addWidget(information);

    auto* buttons = new QDialogButtonBox(this);
    auto* licenses = buttons->addButton(qtTrId("about.licenses_button"), QDialogButtonBox::ActionRole);
    auto* qt = buttons->addButton(qtTrId("about.about_qt"), QDialogButtonBox::ActionRole);
    auto* log = buttons->addButton(qtTrId("about.logs_folder"), QDialogButtonBox::ActionRole);
    log->setToolTip(qtTrId("about.logs_folder_help"));
    auto* close = buttons->addButton(qtTrId("common.close"), QDialogButtonBox::AcceptRole);
    close->setDefault(true);
    connect(licenses, &QPushButton::clicked, this, &AboutDialog::showLicenses);
    connect(qt, &QPushButton::clicked, this, [] { QApplication::aboutQt(); });
    connect(log, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl::fromLocalFile(dataFolder())); });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    layout->addWidget(buttons);
}

void AboutDialog::showLicenses()
{
    QDialog dialog(this);
    dialog.setWindowTitle(qtTrId("about.licenses"));
    dialog.resize(720, 560);
    auto* layout = new QVBoxLayout(&dialog);
    auto* text = new QPlainTextEdit(&dialog);
    text->setReadOnly(true);
    text->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    QString content;
    for (const QString& path : {QStringLiteral(":/documents/NOTICE"), QStringLiteral(":/documents/LICENSE")}) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly))
            content += QString::fromUtf8(file.readAll()) + QStringLiteral("\n\n");
    }
    text->setPlainText(content.isEmpty() ? qtTrId("about.licenses_unavailable") : content);
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    text->setFont(font);
    layout->addWidget(text);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
    buttons->button(QDialogButtonBox::Close)->setText(qtTrId("common.close"));
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog.exec();
}

} // namespace lh::ui
