// Entry point of the LogHunter graphical application.

#include "core/Session.h"
#include "ui/Application.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QTimer>

int main(int argc, char* argv[])
{
    QApplication application(argc, argv);
    lh::ui::initializeApplication(application);
    lh::Session::cleanOrphanSessions();

    lh::ui::MainWindow window;
    window.show();

    QStringList paths;
    for (const QString& argument : QCoreApplication::arguments().mid(1))
        if (!argument.startsWith(QLatin1Char('-')))
            paths << argument;
    QTimer::singleShot(0, &window, [&window, paths] {
        lh::ui::reportPreviousCrash(&window);
        if (!paths.isEmpty())
            window.openPaths(paths);
    });
    return QApplication::exec();
}
