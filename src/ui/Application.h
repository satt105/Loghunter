// Application setup: identity, log, crashes, translation and theme.
#pragma once

#include <QStringList>

class QApplication;
class QWidget;

namespace lh::ui {

void initializeApplication(QApplication& application);

void reportPreviousCrash(QWidget* parent);

QStringList availableLanguages();
QString languageName(const QString& code);
QString chosenLanguage();
void chooseLanguage(const QString& code);

QString chosenDateFormat();
void chooseDateFormat(const QString& format);

bool restartRequired();

} // namespace lh::ui
