#include "wardrobemanagerdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QComboBox>
#include <QLabel>
#include <QFileInfo>
#include <QPainter>
#include <QScreen>

WardrobeManagerDialog::WardrobeManagerDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("👕 我的衣服库");
    QScreen *screen = QGuiApplication::primaryScreen();
    QRect screenGeometry = screen->availableGeometry();
    resize(qMin(1100, screenGeometry.width() - 100), qMin(750, screenGeometry.height() - 100));
    setStyleSheet("QDialog { background-color: #F9FAFB; }");

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    // ==================== 第一行：添加按钮 + 搜索框 ====================
    QHBoxLayout *row1 = new QHBoxLayout();

    // 全新的添加按钮：使用标准的半角 '+' 和清晰的文字
    QPushButton *btnQuickAdd = new QPushButton("+ 添加衣服", this);
    btnQuickAdd->setMinimumHeight(42);
    btnQuickAdd->setMinimumWidth(130);
    btnQuickAdd->setCursor(Qt::PointingHandCursor);
    btnQuickAdd->setStyleSheet(
        "QPushButton { "
        "   background-color: #8B5CF6; color: white; border-radius: 8px; "
        "   font-size: 15px; font-weight: bold; border: none; padding: 0 20px;"
        "} "
        "QPushButton:hover { background-color: #7C3AED; }"
        );
    row1->addWidget(btnQuickAdd);

    row1->addStretch(); // 中间留白

    // 宽敞的搜索框
    m_search = new QLineEdit(this);
    m_search->setPlaceholderText("🔍 搜索衣服名称或标签...");
    m_search->setMinimumHeight(42);
    m_search->setMinimumWidth(300);
    m_search->setStyleSheet(
        "QLineEdit { border: 1px solid #D1D5DB; border-radius: 21px; padding: 0 20px; background: white; font-size: 14px; } "
        "QLineEdit:focus { border: 2px solid #8B5CF6; }"
        );
    connect(m_search, &QLineEdit::textChanged, this, [=]() { refreshGrid(); });
    row1->addWidget(m_search);

    mainLayout->addLayout(row1);

    // ==================== 第二行：分类标签 ====================
    QHBoxLayout *row2 = new QHBoxLayout();
    QStringList cats = WardrobeManager::getCategories();
    for (const QString &c : cats) {
        QPushButton *btn = new QPushButton(c, this);
        btn->setCheckable(true);
        btn->setMinimumHeight(34);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { background-color: #FFFFFF; color: #374151; border: 1px solid #E5E7EB; "
            "padding: 0 18px; border-radius: 17px; font-size: 14px; } "
            "QPushButton:hover { background-color: #F3F4F6; } "
            "QPushButton:checked { background-color: #EDE9FE; color: #6D28D9; border: 1px solid #8B5CF6; font-weight: bold; }"
            );
        if (c == "全部") btn->setChecked(true);
        connect(btn, &QPushButton::clicked, this, [=]() {
            m_currentCategory = c;
            for (auto *b : findChildren<QPushButton*>()) {
                if (cats.contains(b->text())) b->setChecked(b->text() == c);
            }
            refreshGrid();
        });
        row2->addWidget(btn);
    }
    row2->addStretch();
    mainLayout->addLayout(row2);

    // ==================== 画廊区 ====================
    m_grid = new QListWidget(this);
    m_grid->setViewMode(QListView::IconMode);
    m_grid->setIconSize(QSize(240, 240));
    m_grid->setGridSize(QSize(260, 300));
    m_grid->setResizeMode(QListView::Adjust);
    m_grid->setSpacing(15);
    m_grid->setStyleSheet(
        "QListWidget { background-color: transparent; border: none; outline: none; } "
        "QListWidget::item { border: 2px solid transparent; border-radius: 12px; background-color: white; padding: 10px; } "
        "QListWidget::item:hover { border: 2px solid #DDD6FE; } "
        "QListWidget::item:selected { border: 2px solid #8B5CF6; background-color: #F5F3FF; }"
        );
    mainLayout->addWidget(m_grid, 1);

    // ==================== 底部操作栏 ====================
    QHBoxLayout *btnLay = new QHBoxLayout();
    QPushButton *btnEdit = new QPushButton("✏️ 编辑", this);
    QPushButton *btnDel = new QPushButton("🗑 删除", this);
    QPushButton *btnClose = new QPushButton("关闭", this);

    btnEdit->setMinimumHeight(40); btnEdit->setMinimumWidth(100);
    btnEdit->setStyleSheet("QPushButton { background-color: #F3F4F6; color: #374151; border-radius: 8px; font-weight: bold; font-size: 14px; } QPushButton:hover { background-color: #E5E7EB; }");

    btnDel->setMinimumHeight(40); btnDel->setMinimumWidth(100);
    btnDel->setStyleSheet("QPushButton { background-color: #FEE2E2; color: #DC2626; border-radius: 8px; font-weight: bold; font-size: 14px; } QPushButton:hover { background-color: #FECACA; }");

    btnClose->setMinimumHeight(40); btnClose->setMinimumWidth(80);
    btnClose->setStyleSheet("QPushButton { background-color: transparent; color: #6B7280; border: none; font-size: 14px; } QPushButton:hover { color: #374151; }");

    connect(btnClose, &QPushButton::clicked, this, &QDialog::accept);
    btnLay->addWidget(btnEdit);
    btnLay->addWidget(btnDel);
    btnLay->addStretch();
    btnLay->addWidget(btnClose);
    mainLayout->addLayout(btnLay);

    // ==================== 业务逻辑绑定 ====================
    auto doAdd = [=]() {
        QString file = QFileDialog::getOpenFileName(this, "选择衣服图片", "C:/Users/31593/Documents/hy/images/", "Images (*.png *.jpg *.jpeg)");
        if (file.isEmpty()) return;

        QDialog dlg(this); dlg.setWindowTitle("添加新衣服"); dlg.resize(400, 320);
        QVBoxLayout *lay = new QVBoxLayout(&dlg); QFormLayout *form = new QFormLayout();
        QLineEdit *eName = new QLineEdit(QFileInfo(file).baseName(), &dlg); eName->setMinimumHeight(36);
        QComboBox *eCat = new QComboBox(&dlg); eCat->addItems(WardrobeManager::getCategories()); eCat->setEditable(true); eCat->setCurrentText("上衣"); eCat->setMinimumHeight(36);
        QLineEdit *eColor = new QLineEdit(&dlg); eColor->setMinimumHeight(36);
        QLineEdit *eTags = new QLineEdit(&dlg); eTags->setPlaceholderText("如：白色,休闲"); eTags->setMinimumHeight(36);
        form->addRow("名称：", eName); form->addRow("分类：", eCat); form->addRow("主色：", eColor); form->addRow("标签：", eTags);
        lay->addLayout(form); lay->addStretch();
        QHBoxLayout *btns = new QHBoxLayout(); QPushButton *ok = new QPushButton("保存", &dlg); QPushButton *cancel = new QPushButton("取消", &dlg);
        ok->setStyleSheet("background-color: #8B5CF6; color: white; padding: 8px 20px; border-radius: 6px; font-weight: bold;"); cancel->setStyleSheet("background-color: #F3F4F6; color: #374151; padding: 8px 20px; border-radius: 6px;");
        connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept); connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
        btns->addStretch(); btns->addWidget(cancel); btns->addWidget(ok); lay->addLayout(btns);

        if (dlg.exec() == QDialog::Accepted) {
            int id = WardrobeManager::addItem(file, eName->text().trimmed(), eCat->currentText().trimmed(), eColor->text().trimmed(), eTags->text().trimmed());
            if(id > 0) { QMessageBox::information(this, "保存成功", "衣服已成功添加到衣服库！"); refreshGrid(); }
            else { QMessageBox::warning(this, "失败", "添加失败，请检查图片是否被占用。"); }
        }
    };
    connect(btnQuickAdd, &QPushButton::clicked, this, doAdd);

    connect(btnEdit, &QPushButton::clicked, this, [=]() {
        QListWidgetItem *cur = m_grid->currentItem();
        if (!cur) { QMessageBox::information(this, "提示", "请先选择要编辑的衣服。"); return; }

        WardrobeItem it = WardrobeManager::getItemById(cur->data(Qt::UserRole).toInt());
        if (it.id < 0) return;

        QDialog dlg(this); dlg.setWindowTitle("编辑衣服"); dlg.resize(400, 320);
        QVBoxLayout *lay = new QVBoxLayout(&dlg); QFormLayout *form = new QFormLayout();
        QLineEdit *eName = new QLineEdit(it.name, &dlg); eName->setMinimumHeight(36);
        QComboBox *eCat = new QComboBox(&dlg); eCat->addItems(WardrobeManager::getCategories()); eCat->setEditable(true); eCat->setCurrentText(it.category); eCat->setMinimumHeight(36);
        QLineEdit *eColor = new QLineEdit(it.color, &dlg); eColor->setMinimumHeight(36);
        QLineEdit *eTags = new QLineEdit(it.tags, &dlg); eTags->setMinimumHeight(36);
        form->addRow("名称：", eName); form->addRow("分类：", eCat); form->addRow("主色：", eColor); form->addRow("标签：", eTags);
        lay->addLayout(form); lay->addStretch();
        QHBoxLayout *btns = new QHBoxLayout(); QPushButton *ok = new QPushButton("保存", &dlg); QPushButton *cancel = new QPushButton("取消", &dlg);
        ok->setStyleSheet("background-color: #8B5CF6; color: white; padding: 8px 20px; border-radius: 6px; font-weight: bold;"); cancel->setStyleSheet("background-color: #F3F4F6; color: #374151; padding: 8px 20px; border-radius: 6px;");
        connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept); connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
        btns->addStretch(); btns->addWidget(cancel); btns->addWidget(ok); lay->addLayout(btns);

        if (dlg.exec() == QDialog::Accepted) {
            QString newName = eName->text().trimmed();
            if (newName.isEmpty()) { QMessageBox::warning(this, "提示", "名字不能为空！"); return; }

            if (WardrobeManager::updateItem(it.id, newName, eCat->currentText().trimmed(), eColor->text().trimmed(), eTags->text().trimmed())) {
                QMessageBox::information(this, "保存成功", "衣服信息已更新！");
                refreshGrid();
            } else {
                QMessageBox::critical(this, "保存失败", "底层数据库写入失败，可能是文件被占用。");
            }
        }
    });

    connect(btnDel, &QPushButton::clicked, this, [=]() {
        QListWidgetItem *cur = m_grid->currentItem(); if (!cur) { QMessageBox::information(this, "提示", "请先选择要删除的衣服。"); return; }
        WardrobeItem it = WardrobeManager::getItemById(cur->data(Qt::UserRole).toInt()); if (it.id < 0) return;
        if (QMessageBox::question(this, "确认删除", QString("确定要彻底删除「%1」吗？").arg(it.name), QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes) {
            if (WardrobeManager::removeItem(it.id)) refreshGrid();
            else QMessageBox::warning(this, "失败", "删除失败，文件可能被其他程序占用。");
        }
    });

    refreshGrid();
}

void WardrobeManagerDialog::refreshGrid()
{
    m_grid->clear();
    QVector<WardrobeItem> items = m_search->text().trimmed().isEmpty() ? WardrobeManager::getItemsByCategory(m_currentCategory) : WardrobeManager::searchItems(m_search->text().trimmed());

    if (items.isEmpty()) {
        QListWidgetItem *empty = new QListWidgetItem("📭 没有找到相关衣服\n点击左上角「+ 添加衣服」导入您的第一件衣物吧！");
        empty->setTextAlignment(Qt::AlignCenter);
        empty->setFlags(Qt::NoItemFlags);
        m_grid->addItem(empty);
        return;
    }

    for (const auto &it : items) {
        QPixmap pix(it.thumbPath.isEmpty() ? it.imagePath : it.thumbPath);
        QPixmap roundedPix(240, 240);
        roundedPix.fill(Qt::transparent);

        if(!pix.isNull()) {
            QPainter painter(&roundedPix);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(QColor("#F3F4F6"));
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(0, 0, 240, 240, 10, 10);
            QPixmap scaled = pix.scaled(230, 230, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter.drawPixmap((240 - scaled.width()) / 2, (240 - scaled.height()) / 2, scaled);
            painter.end();
        } else {
            roundedPix.fill(QColor("#E5E7EB"));
        }

        QString label = it.name + "\n[" + it.category + "]";
        QListWidgetItem *item = new QListWidgetItem(QIcon(roundedPix), label, m_grid);
        item->setData(Qt::UserRole, it.id);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    }
}