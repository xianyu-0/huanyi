#include "wardrobepickerdialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QScreen>

WardrobePickerDialog::WardrobePickerDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("👕 选择衣服（双击直接试穿）");
    QScreen *screen = QGuiApplication::primaryScreen(); QRect screenGeometry = screen->availableGeometry();
    resize(qMin(1100, screenGeometry.width() - 100), qMin(750, screenGeometry.height() - 100));
    setStyleSheet("QDialog { background-color: #F9FAFB; }");

    QVBoxLayout *root = new QVBoxLayout(this); root->setContentsMargins(20, 20, 20, 20); root->setSpacing(15);

    QHBoxLayout *catRow = new QHBoxLayout();
    QStringList cats = WardrobeManager::getCategories();
    for (const QString &c : cats) {
        QPushButton *btn = new QPushButton(c, this); btn->setCheckable(true);
        btn->setStyleSheet("QPushButton { background-color: #FFFFFF; color: #374151; border: 1px solid #E5E7EB; padding: 6px 16px; border-radius: 15px; font-size: 14px; } QPushButton:hover { background-color: #F3F4F6; } QPushButton:checked { background-color: #8B5CF6; color: white; border: none; }");
        if (c == "全部") btn->setChecked(true);
        connect(btn, &QPushButton::clicked, this, [=]() { m_currentCategory = c; for (auto *b : findChildren<QPushButton*>()) if (cats.contains(b->text())) b->setChecked(b->text() == c); refreshGrid(); });
        catRow->addWidget(btn);
    }
    catRow->addStretch();

    m_search = new QLineEdit(this); m_search->setPlaceholderText("🔍 搜索..."); m_search->setMinimumHeight(36); m_search->setMinimumWidth(200);
    m_search->setStyleSheet("QLineEdit { border: 1px solid #D1D5DB; border-radius: 18px; padding: 0 15px; background: white; }");
    connect(m_search, &QLineEdit::textChanged, this, [=]() { refreshGrid(); });
    catRow->addWidget(m_search);
    root->addLayout(catRow);

    m_grid = new QListWidget(this);
    m_grid->setViewMode(QListView::IconMode); m_grid->setIconSize(QSize(240, 240)); m_grid->setGridSize(QSize(260, 300));
    m_grid->setResizeMode(QListView::Adjust); m_grid->setSpacing(15);
    m_grid->setStyleSheet("QListWidget { background-color: transparent; border: none; outline: none; } QListWidget::item { border: 2px solid transparent; border-radius: 12px; background-color: white; padding: 10px; } QListWidget::item:hover { border: 2px solid #DDD6FE; } QListWidget::item:selected { border: 2px solid #8B5CF6; background-color: #F5F3FF; }");

    connect(m_grid, &QListWidget::itemClicked, this, [=](QListWidgetItem *item) {
        m_selected = WardrobeManager::getItemById(item->data(Qt::UserRole).toInt()); m_hint->setText("已选：" + m_selected.name);
    });
    connect(m_grid, &QListWidget::itemDoubleClicked, this, [=](QListWidgetItem *item) {
        m_selected = WardrobeManager::getItemById(item->data(Qt::UserRole).toInt()); accept();
    });
    root->addWidget(m_grid, 1);

    m_hint = new QLabel("💡 双击衣服图片可直接开始试穿！", this); m_hint->setAlignment(Qt::AlignCenter);
    m_hint->setStyleSheet("background-color: #EFF6FF; color: #2563EB; padding: 10px; border-radius: 8px; font-weight: bold;");
    root->addWidget(m_hint);

    QHBoxLayout *btnLay = new QHBoxLayout();
    QPushButton *btnCancel = new QPushButton("取消", this); QPushButton *btnOk = new QPushButton("✅ 确定选择", this);
    btnCancel->setFixedSize(100, 40); btnCancel->setStyleSheet("QPushButton { background-color: #F3F4F6; color: #374151; border-radius: 8px; }");
    btnOk->setFixedSize(120, 40); btnOk->setStyleSheet("QPushButton { background-color: #8B5CF6; color: white; border-radius: 8px; font-weight: bold; } QPushButton:hover { background-color: #7C3AED; }");

    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, this, [=]() {
        if (m_selected.id < 0) QMessageBox::warning(this, "提示", "请先选择一件衣服"); else accept();
    });
    btnLay->addStretch(); btnLay->addWidget(btnCancel); btnLay->addWidget(btnOk);
    root->addLayout(btnLay);

    refreshGrid();
}

void WardrobePickerDialog::refreshGrid()
{
    m_grid->clear();
    QVector<WardrobeItem> items = m_search->text().trimmed().isEmpty() ? WardrobeManager::getItemsByCategory(m_currentCategory) : WardrobeManager::searchItems(m_search->text().trimmed());
    if (items.isEmpty()) {
        QListWidgetItem *empty = new QListWidgetItem("📭 这里还没有衣服哦~");
        empty->setTextAlignment(Qt::AlignCenter); empty->setFlags(Qt::NoItemFlags); m_grid->addItem(empty); return;
    }

    for (const auto &it : items) {
        QPixmap pix(it.thumbPath.isEmpty() ? it.imagePath : it.thumbPath); QPixmap roundedPix(240, 240); roundedPix.fill(Qt::transparent);
        if(!pix.isNull()) {
            QPainter painter(&roundedPix); painter.setRenderHint(QPainter::Antialiasing); painter.setBrush(QColor("#F3F4F6")); painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(0, 0, 240, 240, 10, 10);
            QPixmap scaled = pix.scaled(230, 230, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            painter.drawPixmap((240 - scaled.width()) / 2, (240 - scaled.height()) / 2, scaled); painter.end();
        } else { roundedPix.fill(QColor("#E5E7EB")); }
        QString label = it.name + "\n[" + it.category + "]";
        QListWidgetItem *item = new QListWidgetItem(QIcon(roundedPix), label, m_grid);
        item->setData(Qt::UserRole, it.id); item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    }
}