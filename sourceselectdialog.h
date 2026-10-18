#ifndef SOURCESELECTDIALOG_H
#define SOURCESELECTDIALOG_H

#include <QDialog>

class SourceSelectDialog : public QDialog
{
    Q_OBJECT // ★ 必须有这个宏
public:
    enum Choice { None, Wardrobe, AIGenerate, LocalFile };
    explicit SourceSelectDialog(int wardrobeCount, QWidget *parent = nullptr);
    Choice getChoice() const { return m_choice; }

private:
    Choice m_choice = None;
};

#endif // SOURCESELECTDIALOG_H