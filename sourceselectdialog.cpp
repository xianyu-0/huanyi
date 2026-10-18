#include "sourceselectdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>

SourceSelectDialog::SourceSelectDialog(int wardrobeCount, QWidget *parent) : QDialog(parent)
{
    setWindowTitle("第 2 步：请选择服装来源");
    setFixedSize(560, 440);
    setStyleSheet("QDialog { background-color: #F9FAFB; }");

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(20, 20, 20, 20);
    root->setSpacing(15);

    auto createCard = [&](const QString &title, const QString &desc, const QString &btnText, Choice choice) {
        QFrame *card = new QFrame(this);
        card->setStyleSheet("QFrame { background-color: #FFFFFF; border: 1px solid #E5E7EB; border-radius: 12px; } "
                            "QFrame:hover { border: 2px solid #8B5CF6; }");
        QHBoxLayout *lay = new QHBoxLayout(card);
        lay->setContentsMargins(20, 15, 20, 15);

        QVBoxLayout *textLay = new QVBoxLayout();
        QLabel *lTitle = new QLabel(title, card);
        lTitle->setStyleSheet("font-size: 16px; font-weight: bold; color: #374151; border: none;");

        QLabel *lDesc = new QLabel(desc, card);
        lDesc->setStyleSheet("font-size: 13px; color: #6B7280; border: none;");
        lDesc->setWordWrap(true); // 自动换行

        textLay->addWidget(lTitle);
        textLay->addWidget(lDesc);

        QPushButton *btn = new QPushButton(btnText, card);
        // ★ 加大按钮最小宽度，确保中文能完全显示
        btn->setMinimumSize(130, 42);
        btn->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        btn->setStyleSheet("QPushButton { background-color: #F3F4F6; color: #374151; border-radius: 8px; font-weight: bold; border: none; font-size: 14px; } "
                           "QPushButton:hover { background-color: #8B5CF6; color: white; }");

        connect(btn, &QPushButton::clicked, this, [=]() { m_choice = choice; accept(); });

        lay->addLayout(textLay, 1);
        lay->addWidget(btn);
        return card;
    };

    QString wardrobeDesc = wardrobeCount > 0
                               ? QString("挑选您已保存的衣物，即点即试。(当前有 %1 件)").arg(wardrobeCount)
                               : "您的衣服库还是空的，快去添加吧！";

    // ★ 去除了容易乱码的特殊符号，改用纯中文
    root->addWidget(createCard("从衣服库选（推荐）", wardrobeDesc, "去选择", Wardrobe));
    root->addWidget(createCard("AI 生成新衣服", "输入描述词，让 AI 为您创作一件新衣。", "开始生成", AIGenerate));
    root->addWidget(createCard("从电脑选择图片", "临时使用一张本地图片，试穿后不自动入库。", "浏览文件", LocalFile));

    QHBoxLayout *btnLay = new QHBoxLayout();
    QPushButton *btnCancel = new QPushButton("取消", this);
    btnCancel->setFixedSize(80, 36);
    btnCancel->setStyleSheet("QPushButton { background-color: transparent; color: #6B7280; border: none; font-size: 14px; } "
                             "QPushButton:hover { color: #EF4444; }");
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    btnLay->addStretch();
    btnLay->addWidget(btnCancel);
    root->addLayout(btnLay);
}