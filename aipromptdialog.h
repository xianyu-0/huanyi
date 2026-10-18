#ifndef AIPROMPTDIALOG_H
#define AIPROMPTDIALOG_H

#include <QDialog>
#include <QLineEdit>

class AiPromptDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AiPromptDialog(QWidget *parent = nullptr);
    QString getPrompt() const;

private:
    QLineEdit *m_edit;
};

#endif // AIPROMPTDIALOG_H