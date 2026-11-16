#include "Application.h"

#include "Settings.h"
#include "Theme.h"
#include "core/Session.h"
#include "core/DateTime.h"
#include "dialogs/MessageDialog.h"
#include "loghunter_version.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QLibraryInfo>
#include <QLocale>
#include <QMutex>
#include <QPushButton>
#include <QSysInfo>
#include <QTranslator>
#include <QUrl>

#include <algorithm>
#include <cstring>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>

#include <cwchar>
#include <iterator>
#endif

namespace lh::ui {

namespace {

constexpr qint64 MAX_LOG_SIZE = 2 * 1024 * 1024;
constexpr int KEPT_REPORTS = 5;

QFile* g_log = nullptr;
QMutex g_logLock;
QtMessageHandler g_previousHandler = nullptr;
QString g_loadedLanguage;
QString g_loadedDateFormat;

QString crashFolder()
{
    return dataFolder() + QStringLiteral("/crashes");
}

// ── Application log ──────────────────────────────────────────────────────────

void writeMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const char* level = "INFO";
    switch (type) {
    case QtDebugMsg:
        level = "DEBUG";
        break;
    case QtInfoMsg:
        level = "INFO";
        break;
    case QtWarningMsg:
        level = "WARN";
        break;
    case QtCriticalMsg:
        level = "ERROR";
        break;
    case QtFatalMsg:
        level = "FATAL";
        break;
    }
    const bool category = context.category && std::strcmp(context.category, "default") != 0;
    const QString row = QStringLiteral("%1 [%2] %3%4\n")
                            .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), QLatin1String(level),
                                 category ? QLatin1String(context.category) + QStringLiteral(": ") : QString(),
                                 message);
    {
        QMutexLocker lock(&g_logLock);
        if (g_log) {
            g_log->write(row.toUtf8());
            g_log->flush();
        }
    }
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

void openLog()
{
    const QString folder = dataFolder();
    const QString path = folder + QStringLiteral("/loghunter.log");
    if (QFileInfo(path).size() > MAX_LOG_SIZE) {
        const QString oldPath = folder + QStringLiteral("/loghunter.1.log");
        QFile::remove(oldPath);
        QFile::rename(path, oldPath);
    }
    auto* file = new QFile(path);
    if (file->open(QIODevice::Append | QIODevice::Text))
        g_log = file;
    else
        delete file;
    g_previousHandler = qInstallMessageHandler(writeMessage);
}

// ── Crash reports (Windows) ──────────────────────────────────────────────────

#ifdef Q_OS_WIN
wchar_t g_crashPath[MAX_PATH] = {};

LONG WINAPI handleCrash(EXCEPTION_POINTERS* exception)
{
    SYSTEMTIME instant;
    GetLocalTime(&instant);
    wchar_t path[MAX_PATH + 64];
    std::swprintf(path, std::size(path), L"%ls\\LogHunter-%04u%02u%02u-%02u%02u%02u.dmp", g_crashPath,
                  instant.wYear, instant.wMonth, instant.wDay, instant.wHour, instant.wMinute, instant.wSecond);
    const HANDLE file =
        CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION information{GetCurrentThreadId(), exception, FALSE};
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                          static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory),
                          &information, nullptr, nullptr);
        CloseHandle(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void installCrashHandler()
{
    const QString folder = crashFolder();
    QDir().mkpath(folder);
    const std::wstring native = QDir::toNativeSeparators(folder).toStdWString();
    wcsncpy_s(g_crashPath, native.c_str(), _TRUNCATE);
    SetUnhandledExceptionFilter(handleCrash);
}
#endif

// ── Translations ─────────────────────────────────────────────────────────────

constexpr char REFERENCE_LANGUAGE[] = "fr";
constexpr char COMPILED_LANGUAGES[] = ":/translations";

class SharedTranslator : public QTranslator
{
public:
    using QTranslator::QTranslator;

    QString translate(const char* context, const char* source, const char* precision, int n) const override
    {
        QMutexLocker lock(&m_lock);
        return QTranslator::translate(context, source, precision, n);
    }

private:
    mutable QMutex m_lock;
};

QString languageFile(const QString& code)
{
    return QStringLiteral("%1/loghunter_%2.qm").arg(QLatin1String(COMPILED_LANGUAGES), code);
}

void installTranslation(QApplication& application, const QString& path)
{
    auto* translator = new SharedTranslator(&application);
    if (translator->load(path)) {
        application.installTranslator(translator);
        return;
    }
    delete translator;
    qWarning().noquote() << QStringLiteral("Unreadable language file: %1").arg(path);
}

QString effectiveLanguage()
{
    const QStringList availableCodes = availableLanguages();
    const QString chosen = chosenLanguage();
    if (availableCodes.contains(chosen))
        return chosen;
    for (const QString& language : QLocale::system().uiLanguages(QLocale::TagSeparator::Underscore)) {
        if (availableCodes.contains(language))
            return language;
        const QString code = language.section(QLatin1Char('_'), 0, 0);
        if (availableCodes.contains(code))
            return code;
        const QString regional = code + QLatin1Char('_');
        for (const QString& available : availableCodes) {
            if (available.startsWith(regional))
                return available;
        }
    }
    return QLatin1String(REFERENCE_LANGUAGE);
}

void loadTranslations(QApplication& application)
{
    const QString language = effectiveLanguage();
    g_loadedLanguage = language;
    QLocale::setDefault(QLocale(language));
    installTranslation(application, languageFile(QLatin1String(REFERENCE_LANGUAGE)));
    if (language != QLatin1String(REFERENCE_LANGUAGE))
        installTranslation(application, languageFile(language));

    const QStringList qtFolders{QLibraryInfo::path(QLibraryInfo::TranslationsPath),
                                QCoreApplication::applicationDirPath() + QStringLiteral("/translations")};
    auto* qtTranslator = new SharedTranslator(&application);
    for (const QString& folder : qtFolders) {
        if (qtTranslator->load(QStringLiteral("qt_") + language, folder)
            || qtTranslator->load(QStringLiteral("qtbase_") + language, folder)) {
            application.installTranslator(qtTranslator);
            return;
        }
    }
    delete qtTranslator;
    qInfo().noquote() << QStringLiteral("Qt translations not found for “%1”: standard components stay in English.")
                             .arg(language);
}

} // namespace

void initializeApplication(QApplication& application)
{
    QApplication::setOrganizationName(QStringLiteral(LOGHUNTER_PUBLISHER));
    QApplication::setApplicationName(QStringLiteral(LOGHUNTER_NAME));
    QApplication::setApplicationVersion(QStringLiteral(LOGHUNTER_VERSION));
    QIcon icon;
    for (const int size : {16, 24, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/images/logo-%1.png").arg(size));
    QApplication::setWindowIcon(icon);

    openLog();
#ifdef Q_OS_WIN
    installCrashHandler();
#endif
    qInfo().noquote() << QStringLiteral("Starting %1 %2 (Qt %3, %4)")
                             .arg(QStringLiteral(LOGHUNTER_NAME), QStringLiteral(LOGHUNTER_VERSION),
                                  QString::fromLatin1(qVersion()), QSysInfo::prettyProductName());
    loadTranslations(application);
    g_loadedDateFormat = chosenDateFormat();
    setDateFormat(g_loadedDateFormat);
    theme::initialize();
    QObject::connect(&application, &QCoreApplication::aboutToQuit, [] { qInfo() << "Closing the application"; });
}

void reportPreviousCrash(QWidget* parent)
{
    const QDir folder(crashFolder());
    const QFileInfoList reports = folder.entryInfoList({QStringLiteral("*.dmp")}, QDir::Files, QDir::Time);
    if (reports.isEmpty())
        return;
    for (qsizetype i = KEPT_REPORTS; i < reports.size(); ++i)
        QFile::remove(reports[i].absoluteFilePath());
    const QDateTime lastReported = settings::read(settings::key::LAST_CRASH).toDateTime();
    const QDateTime mostRecent = reports.first().lastModified();
    if (lastReported.isValid() && mostRecent <= lastReported)
        return;
    settings::write(settings::key::LAST_CRASH, mostRecent);
    qWarning() << "Crash during the previous session:" << reports.first().fileName();

    MessageDialog box(MessageDialog::Type::Warning, qtTrId("application.crash_title"),
                      qtTrId("application.crash_explanation"),
                      parent);
    QPushButton* reportButton = box.addButton(qtTrId("application.report_issue"), true);
    QPushButton* openButton = box.addButton(qtTrId("application.open_folder"));
    box.addButton(qtTrId("common.close"));
    box.exec();
    if (box.clickedButton() == reportButton || box.clickedButton() == openButton)
        QDesktopServices::openUrl(QUrl::fromLocalFile(dataFolder()));
    if (box.clickedButton() == reportButton)
        QDesktopServices::openUrl(QUrl(QStringLiteral(LOGHUNTER_WEBSITE "/issues/new")));
}

QStringList availableLanguages()
{
    QStringList languages{QLatin1String(REFERENCE_LANGUAGE)};
    const QStringList files =
        QDir(QLatin1String(COMPILED_LANGUAGES)).entryList({QStringLiteral("loghunter_*.qm")}, QDir::Files);
    for (const QString& file : files) {
        const QString code = file.mid(10).chopped(3);
        if (!languages.contains(code))
            languages << code;
    }
    std::sort(languages.begin() + 1, languages.end(), [](const QString& a, const QString& b) {
        return languageName(a).localeAwareCompare(languageName(b)) < 0;
    });
    return languages;
}

QString languageName(const QString& code)
{
    const QLocale locale(code);
    QString name = locale.nativeLanguageName();
    if (!code.contains(QLatin1Char('_'))) {
        QHash<QString, int> occurrences;
        const QList<QLocale> variants =
            QLocale::matchingLocales(locale.language(), QLocale::AnyScript, QLocale::AnyTerritory);
        for (const QLocale& variant : variants) {
            const QString candidate = variant.nativeLanguageName();
            if (!candidate.isEmpty() && ++occurrences[candidate] > occurrences.value(name))
                name = candidate;
        }
    }
    if (name.isEmpty() || locale.language() == QLocale::C)
        return code;
    name[0] = name[0].toUpper();
    return name;
}

QString chosenLanguage()
{
    return settings::read(settings::key::LANGUAGE).toString();
}

void chooseLanguage(const QString& code)
{
    if (code.isEmpty())
        settings::remove(settings::key::LANGUAGE);
    else
        settings::write(settings::key::LANGUAGE, code);
}

QString chosenDateFormat()
{
    return settings::read(settings::key::DATE_FORMAT).toString();
}

void chooseDateFormat(const QString& format)
{
    if (format.isEmpty())
        settings::remove(settings::key::DATE_FORMAT);
    else
        settings::write(settings::key::DATE_FORMAT, format);
}

bool restartRequired()
{
    return effectiveLanguage() != g_loadedLanguage || chosenDateFormat() != g_loadedDateFormat;
}

} // namespace lh::ui
