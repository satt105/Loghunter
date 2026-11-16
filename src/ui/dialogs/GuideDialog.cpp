#include "GuideDialog.h"

#include <QDialogButtonBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QVBoxLayout>

namespace lh::ui {

namespace {

QString code(const char* text)
{
    return QStringLiteral("<pre>%1</pre>").arg(QString::fromUtf8(text).toHtmlEscaped());
}

} // namespace

QString GuideDialog::content()
{
    QString html;
    html += qtTrId("guide.introduction");

    html += qtTrId("guide.step1");
    html += code(R"(auditpol /set /subcategory:"{0CCE921D-69AE-11D9-BED3-505054503030}" /success:enable /failure:enable)");
    html += qtTrId("guide.step1_permissions");
    html += code(R"(auditpol /set /subcategory:"{0CCE9231-69AE-11D9-BED3-505054503030}" /success:enable)");
    html += qtTrId("guide.step1_shares");
    html += code(R"(auditpol /set /subcategory:"{0CCE9244-69AE-11D9-BED3-505054503030}" /success:enable /failure:enable)");

    html += qtTrId("guide.step2");
    html += code(R"($folder = 'D:\Shares'
$acl = Get-Acl -Path $folder -Audit
$everyone = New-Object System.Security.Principal.SecurityIdentifier('S-1-1-0')
$rights = 'Write, Delete, DeleteSubdirectoriesAndFiles, ChangePermissions, TakeOwnership'
$rule = New-Object System.Security.AccessControl.FileSystemAuditRule(
    $everyone, $rights, 'ContainerInherit, ObjectInherit', 'None', 'Success')
$acl.AddAuditRule($rule)
Set-Acl -Path $folder -AclObject $acl)");

    html += qtTrId("guide.step3");
    html += code("wevtutil sl Security /ms:2147483648");

    html += qtTrId("guide.step4");
    html += code(R"(wevtutil epl Security C:\Export\Security.evtx)");
    html += qtTrId("guide.step4_archives");

    html += qtTrId("guide.step5");

    html += qtTrId("guide.events_used");
    return html;
}

GuideDialog::GuideDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle(qtTrId("guide.title"));
    resize(780, 680);
    auto* layout = new QVBoxLayout(this);
    auto* text = new QTextBrowser(this);
    text->setOpenExternalLinks(true);
    text->setHtml(content());
    layout->addWidget(text, 1);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Close)->setText(qtTrId("common.close"));
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
}

} // namespace lh::ui
