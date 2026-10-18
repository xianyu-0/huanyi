#ifndef WARDROBEPICKERDIALOG_H
#define WARDROBEPICKERDIALOG_H

#include <QDialog>
#include "wardrobemanager.h"

class QListWidget;
class QLineEdit;
class QLabel;

class WardrobePickerDialog : public QDialog
{
    Q_OBJECT
public:
    explicit WardrobePickerDialog(QWidget *parent = nullptr);
    WardrobeItem selectedItem() const { return m_selected; }

private:
    void refreshGrid();
    QListWidget *m_grid = nullptr;
    QLineEdit *m_search = nullptr;
    QLabel *m_hint = nullptr;
    QString m_currentCategory = "全部";
    WardrobeItem m_selected;
};

#endif // WARDROBEPICKERDIALOG_H