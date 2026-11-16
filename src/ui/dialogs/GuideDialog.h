// Guide to configuring file access auditing on Windows.
#pragma once

#include <QDialog>

namespace lh::ui {

class GuideDialog : public QDialog
{
    Q_OBJECT

public:
    explicit GuideDialog(QWidget* parent = nullptr);

private:
    static QString content();
};

} // namespace lh::ui
