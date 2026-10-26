#include "mainwindow.h"
#include <QApplication>
#include <QDialog>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // ★ 采用自定义大尺寸弹窗，替代原生极小的 QMessageBox
    QDialog dlg;
    dlg.setWindowTitle("欢迎使用幻衣");
    dlg.setFixedSize(480, 320);
    dlg.setStyleSheet("QDialog { background-color: #F5F5F7; }");

    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(16);

    QLabel *title = new QLabel("欢迎使用幻衣系统", &dlg);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 26px; font-weight: 600; color: #1D1D1F;");

    QLabel *subtitle = new QLabel("请选择您的进入身份", &dlg);
    subtitle->setAlignment(Qt::AlignCenter);
    subtitle->setStyleSheet("font-size: 15px; color: #86868B; margin-bottom: 24px;");

    QPushButton *btnBuyer = new QPushButton("🙋‍♀️ 买家 (体验试穿)", &dlg);
    btnBuyer->setFixedHeight(52);
    btnBuyer->setCursor(Qt::PointingHandCursor);
    btnBuyer->setStyleSheet(
        "QPushButton { background-color: #5856D6; color: #FFFFFF; border-radius: 26px; "
        "font-size: 16px; font-weight: 600; } "
        "QPushButton:hover { background-color: #4B49C4; }"
        );

    QPushButton *btnSeller = new QPushButton("💼 卖家 (管理衣服)", &dlg);
    btnSeller->setFixedHeight(52);
    btnSeller->setCursor(Qt::PointingHandCursor);
    btnSeller->setStyleSheet(
        "QPushButton { background-color: #FFFFFF; color: #1D1D1F; border: 1px solid rgba(0,0,0,0.08); "
        "border-radius: 26px; font-size: 16px; font-weight: 500; } "
        "QPushButton:hover { background-color: #E5E5EA; }"
        );

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(btnBuyer);
    layout->addWidget(btnSeller);

    UserRole selectedRole = Role_Buyer;
    QObject::connect(btnBuyer, &QPushButton::clicked, [&]() {
        selectedRole = Role_Buyer;
        dlg.accept();
    });
    QObject::connect(btnSeller, &QPushButton::clicked, [&]() {
        selectedRole = Role_Seller;
        dlg.accept();
    });

    // 如果用户点了右上角 X 或关闭，直接退出
    if (dlg.exec() != QDialog::Accepted) {
        return 0;
    }

    MainWindow w(selectedRole);
    w.show();

    return a.exec();
}