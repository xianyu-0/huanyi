#ifndef WARDROBEMANAGERDIALOG_H
#define WARDROBEMANAGERDIALOG_H

#include <QDialog>
#include "wardrobemanager.h"

class QListWidget;
class QLineEdit;

class WardrobeManagerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WardrobeManagerDialog(QWidget *parent = nullptr);

private:
    void refreshGrid();
    QListWidget *m_grid = nullptr;
    QLineEdit *m_search = nullptr;
    QString m_currentCategory = "全部";
};

#endif // WARDROBEMANAGERDIALOG_H