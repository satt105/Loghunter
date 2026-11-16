// About: version, publisher, components and licenses.
#pragma once

#include <QDialog>

namespace lh::ui {

class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget* parent = nullptr);

private:
    void showLicenses();
};

} // namespace lh::ui
