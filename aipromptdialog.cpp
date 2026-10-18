#include "aipromptdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

AiPromptDialog::AiPromptDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("✨ AI 生成服装");
    setFixedSize(460, 280);
    setStyleSheet("QDialog { background-color: #FFFFFF; }");

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(12);

    QLabel *lTip = new QLabel("请输入服装描述（支持中文）：", this);
    lTip->setStyleSheet("font-size: 14px; color: #374151; font-weight: bold;");
    root->addWidget(lTip);

    m_edit = new QLineEdit(this);
    m_edit->setText("时尚白色短衬衫");
    m_edit->setMinimumHeight(40);
    m_edit->setStyleSheet("QLineEdit { border: 1px solid #D1D5DB; border-radius: 8px; padding: 0 10px; font-size: 14px; }"
                          "QLineEdit:focus { border: 2px solid #8B5CF6; }");
    root->addWidget(m_edit);

    QLabel *lQuick = new QLabel("💡 灵感快捷词：", this);
    lQuick->setStyleSheet("font-size: 13px; color: #6B7280;");
    root->addWidget(lQuick);

    QHBoxLayout *quickLay = new QHBoxLayout();
    QStringList quicks = {"夏季碎花裙", "复古牛仔外套", "商务白衬衫", "嘻哈卫衣"};
    for (const QString &q : quicks) {
        QPushButton *btn = new QPushButton(q, this);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet("QPushButton { background-color: #F3F4F6; color: #4B5563; border: none; padding: 6px 10px; border-radius: 4px; font-size: 12px; }"
                           "QPushButton:hover { background-color: #EDE9FE; color: #6D28D9; }");
        connect(btn, &QPushButton::clicked, this, [=]() { m_edit->setText(q); });
        quickLay->addWidget(btn);
    }
    quickLay->addStretch();
    root->addLayout(quickLay);

    QLabel *lWarn = new QLabel("⚠️ 生成约需 20~40 秒，请保持网络畅通。", this);
    lWarn->setStyleSheet("font-size: 12px; color: #D97706;");
    root->addWidget(lWarn);
    root->addStretch();

    QHBoxLayout *btnLay = new QHBoxLayout();
    QPushButton *btnCancel = new QPushButton("取消", this);
    QPushButton *btnOk = new QPushButton("开始生成", this);

    btnCancel->setFixedSize(90, 40);
    btnCancel->setStyleSheet("QPushButton { background-color: #F3F4F6; color: #374151; border-radius: 8px; font-size: 14px; }");
    btnOk->setFixedSize(110, 40);
    btnOk->setStyleSheet("QPushButton { background-color: #8B5CF6; color: white; border-radius: 8px; font-size: 14px; font-weight: bold; }"
                         "QPushButton:hover { background-color: #7C3AED; }");

    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);

    btnLay->addStretch();
    btnLay->addWidget(btnCancel);
    btnLay->addWidget(btnOk);
    root->addLayout(btnLay);
}

QString AiPromptDialog::getPrompt() const {
    return m_edit->text().trimmed();
}