#include "mainwindow.h"

#include "wardrobemanager.h"
#include "sourceselectdialog.h"
#include "aipromptdialog.h"
#include "wardrobepickerdialog.h"
#include "wardrobemanagerdialog.h"
#include "maskeditordialog.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include <QMessageBox>
#include <QInputDialog>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QDir>
#include <QFileInfoList>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlTableModel>
#include <QDateTime>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QApplication>
#include <QFileDialog>
#include <QShortcut>
#include <QIcon>
#include <QStandardPaths>
#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QEventLoop>
#include <QTimer>
#include <QWebSocket>
#include <QUuid>
#include <QDesktopServices>
#include <QFileInfo>
#include <QThread>
#include <QPainter>
#include <QScreen>
#include <QBuffer>

// ==================== 工具函数 ====================
static inline void applyMaskToAlpha(cv::Mat &bgra, const cv::Mat &mask)
{
    std::vector<cv::Mat> ch;
    cv::split(bgra, ch);
    cv::bitwise_not(mask, ch[3]);
    cv::merge(ch, bgra);
}

// ==================== 构造函数 ====================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupLayout();
    QFile qssFile("C:/Users/31593/Documents/hy/style.qss");
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString qss = QString::fromUtf8(qssFile.readAll());
        this->setStyleSheet(qss);
        qssFile.close();
    }
    QString msgBoxStyle = R"(
        QMessageBox { background-color: #FFFFFF; }
        QMessageBox QLabel { font-size: 14px; color: #374151; min-width: 260px; }
        QMessageBox QPushButton {
            min-width: 110px; min-height: 38px; padding: 8px 22px; font-size: 14px;
            border: none; border-radius: 8px; background-color: #8B5CF6; color: #FFFFFF;
        }
        QMessageBox QPushButton:hover   { background-color: #7C3AED; }
        QMessageBox QPushButton:pressed { background-color: #6D28D9; }
    )";
    qApp->setStyleSheet(qApp->styleSheet() + msgBoxStyle);
    QIcon icon("C:/Users/31593/Documents/hy/images/app_icon.png");
    if (!icon.isNull()) this->setWindowIcon(icon);
    QShortcut *shortcut = new QShortcut(QKeySequence("Ctrl+Return"), this);
    connect(shortcut, &QShortcut::activated, this, &MainWindow::onSmartActionClicked);
    initDatabase();
    WardrobeManager::init();
    QDir().mkpath("images/history");
    currentBodyPath = "images/body.jpg";
    currentMaskPath = "";
    loadingTimer = new QTimer(this);
    loadingDots = 0;
    connect(loadingTimer, &QTimer::timeout, this, &MainWindow::updateLoadingText);
    animTimer = new QTimer(this);
    animTimer->setInterval(150);
    connect(animTimer, &QTimer::timeout, this, [=]() {
        if (currentStage == 2) {
            static int dots = 0;
            dots = (dots + 1) % 4;
            QString stepStr = "";
            int sec = engineWaitSeconds;
            if (sec < 5) stepStr = "【步骤 1/3】正在初始化 Python 环境";
            else if (sec < 25) stepStr = "【步骤 2/3】正在加载 AI 大模型 (约需 20~60 秒)";
            else stepStr = QString("【步骤 3/3】正在分配显存... 已等待 %1 秒").arg(sec);
            showCenterLoading("AI 引擎正在启动" + QString(".").repeated(dots), stepStr);
        }
        else if (currentStage == 3 && currentNodeMax > 0) {
            updateProgressUI();
        }
    });
    animTimer->start();
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [=]() {
        cv::Mat frame;
        cap >> frame;
        if (frame.empty()) return;
        cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);
        QImage img(frame.data, frame.cols, frame.rows, frame.step, QImage::Format_RGB888);
        QPixmap pix = QPixmap::fromImage(img).scaled(lblImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        lblImage->setPixmap(pix);
        currentResultPixmap = pix;
    });
    manager = new QNetworkAccessManager(this);
    pollTimer = new QTimer(this);
    pollTimer->setInterval(4000);
    connect(pollTimer, &QTimer::timeout, this, &MainWindow::pollHistory);
    wsClientId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    connectWebSocket();
    updateSmartUI();
    QTimer::singleShot(500, this, [=]() { startComfyProcess(); });
}

MainWindow::~MainWindow()
{
    disconnectWebSocket();
    if (cap.isOpened()) cap.release();
    if (db.isOpen()) db.close();
}

// ==================== 纯代码构建高定 UI ====================
void MainWindow::setupLayout()
{
    QWidget *central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);
    lblImage = new QLabel(central);
    lblImage->setMinimumHeight(420);
    lblImage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    lblImage->setAlignment(Qt::AlignCenter);
    lblImage->setStyleSheet("QLabel { background-color: #FAFAFA; border: 2px dashed #E5E7EB; border-radius: 16px; color: #6B7280; font-size: 15px; }");
    mainLayout->addWidget(lblImage, 1);
    lblStatus = new QLabel(central);
    lblStatus->setAlignment(Qt::AlignCenter);
    lblStatus->setMinimumHeight(48);
    lblStatus->setStyleSheet("QLabel { background-color: #EFF6FF; color: #2563EB; font-size: 14px; border-radius: 8px; padding: 10px; }");
    mainLayout->addWidget(lblStatus);
    btnShowCompare = new QPushButton("📸 查看试穿前后对比", central);
    btnShowCompare->setMinimumHeight(50);
    btnShowCompare->setStyleSheet("QPushButton { background-color: #10B981; color: white; font-weight: bold; font-size: 16px; border-radius: 10px; } QPushButton:hover { background-color: #059669; }");
    btnShowCompare->setVisible(false);
    connect(btnShowCompare, &QPushButton::clicked, this, &MainWindow::showComparisonDialog);
    mainLayout->addWidget(btnShowCompare);
    btnSmartAction = new QPushButton("🚀 开始试穿", central);
    btnSmartAction->setMinimumHeight(64);
    btnSmartAction->setStyleSheet("QPushButton { background-color: #8B5CF6; color: white; font-weight: bold; font-size: 17px; border-radius: 10px; } QPushButton:hover { background-color: #7C3AED; } QPushButton:disabled { background-color: #D1D5DB; }");
    connect(btnSmartAction, &QPushButton::clicked, this, &MainWindow::onSmartActionClicked);
    mainLayout->addWidget(btnSmartAction);
    btnBackToHistory = new QPushButton("⬅️ 返回历史列表", central);
    btnUseHistoryImg = new QPushButton("🚀 用此图开始试穿", central);
    btnBackToHistory->setMinimumHeight(50);
    btnBackToHistory->setStyleSheet("QPushButton { background-color: #F3F4F6; color: #374151; border-radius: 8px; font-weight: bold; font-size: 16px; } QPushButton:hover { background-color: #E5E7EB; }");
    btnUseHistoryImg->setMinimumHeight(50);
    btnUseHistoryImg->setStyleSheet("QPushButton { background-color: #8B5CF6; color: white; border-radius: 8px; font-weight: bold; font-size: 16px; } QPushButton:hover { background-color: #7C3AED; }");
    btnBackToHistory->setVisible(false); btnUseHistoryImg->setVisible(false);
    QHBoxLayout *historyActionRow = new QHBoxLayout();
    historyActionRow->addWidget(btnBackToHistory); historyActionRow->addWidget(btnUseHistoryImg);
    mainLayout->addLayout(historyActionRow);
    connect(btnBackToHistory, &QPushButton::clicked, this, &MainWindow::onBackToHistoryClicked);
    connect(btnUseHistoryImg, &QPushButton::clicked, this, &MainWindow::onUseHistoryImageClicked);
    bottomSubRowWidget = new QWidget(central);
    QHBoxLayout *subRow = new QHBoxLayout(bottomSubRowWidget);
    subRow->setContentsMargins(0,0,0,0); subRow->setSpacing(10);
    btnWardrobe = new QPushButton("👕 我的衣服库", bottomSubRowWidget);
    btnWardrobe->setMinimumHeight(46);
    btnWardrobe->setStyleSheet("QPushButton { background-color: #EDE9FE; color: #6D28D9; font-size: 14px; border-radius: 8px; font-weight: bold; } QPushButton:hover { background-color: #DDD6FE; }");
    connect(btnWardrobe, &QPushButton::clicked, this, &MainWindow::openWardrobeManager);
    subRow->addWidget(btnWardrobe, 1);
    QPushButton *btnHistoryUI = new QPushButton("📋 历史记录", bottomSubRowWidget);
    btnHistoryUI->setMinimumHeight(46);
    btnHistoryUI->setStyleSheet("QPushButton { background-color: #F3F4F6; color: #374151; font-size: 14px; border-radius: 8px; } QPushButton:hover { background-color: #E5E7EB; }");
    connect(btnHistoryUI, &QPushButton::clicked, this, &MainWindow::on_btnHistory_clicked);
    subRow->addWidget(btnHistoryUI, 1);
    QPushButton *btnStop = new QPushButton("⏹ 停止当前任务", bottomSubRowWidget);
    btnStop->setMinimumHeight(46);
    btnStop->setStyleSheet("QPushButton { background-color: #F3F4F6; color: #374151; font-size: 14px; border-radius: 8px; } QPushButton:hover { background-color: #E5E7EB; }");
    connect(btnStop, &QPushButton::clicked, this, [=]() {
        if (pollTimer->isActive()) {
            pollTimer->stop(); pollCount = 0; pendingAutoTryOn = false; pendingTryOnAfterGenerate = false;
            currentStage = 0; updateSmartUI(); lblStatus->setText("⏹ 任务已手动停止");
            lblStatus->setStyleSheet("QLabel { background-color: #F3F4F6; color: #6B7280; font-size: 14px; border-radius: 8px; padding: 10px; }");
        } else if (pendingTryOnAfterGenerate) {
            pendingTryOnAfterGenerate = false; currentStage = 0; updateSmartUI(); lblStatus->setText("⏹ 已取消试穿");
        } else {
            lblStatus->setText("💡 当前没有正在执行的任务哦~");
            lblStatus->setStyleSheet("QLabel { background-color: #EFF6FF; color: #2563EB; font-size: 14px; border-radius: 8px; padding: 10px; }");
            QTimer::singleShot(3000, this, [=]() { updateSmartUI(); });
        }
    });
    subRow->addWidget(btnStop, 1);
    mainLayout->addWidget(bottomSubRowWidget);
    this->setWindowTitle("幻衣 - AI 虚拟试穿系统"); this->setMinimumSize(900, 720);
    setupMenuBar();
    resetToHomeScreen();
}

void MainWindow::setupMenuBar()
{
    QMenuBar *menuBar = this->menuBar();
    QMenu *fileMenu = menuBar->addMenu("文件(&F)");
    QAction *actSave = fileMenu->addAction("💾 保存结果图");
    connect(actSave, &QAction::triggered, this, &MainWindow::on_btnSaveResult_clicked);
    fileMenu->addSeparator();
    QAction *actExit = fileMenu->addAction("🚪 退出");
    connect(actExit, &QAction::triggered, this, &QWidget::close);
    QMenu *comfyMenu = menuBar->addMenu("ComfyUI");
    QAction *actStart = comfyMenu->addAction("▶️ 启动 ComfyUI");
    connect(actStart, &QAction::triggered, this, &MainWindow::onStartComfyUI);
    QAction *actStop = comfyMenu->addAction("⏹ 停止 ComfyUI");
    connect(actStop, &QAction::triggered, this, &MainWindow::onStopComfyUI);
    QAction *actCheck = comfyMenu->addAction("🔍 检查状态");
    connect(actCheck, &QAction::triggered, this, &MainWindow::onCheckComfyUI);
    QMenu *toolMenu = menuBar->addMenu("工具(&T)");
    QAction *actWardrobe = toolMenu->addAction("👕 我的衣服库");
    connect(actWardrobe, &QAction::triggered, this, &MainWindow::openWardrobeManager);
    QMenu *helpMenu = menuBar->addMenu("帮助(&H)");

    QAction *actAbout = helpMenu->addAction("ℹ️ 关于");
    connect(actAbout, &QAction::triggered, this, [=]() {
        QDialog dlg(this);
        dlg.setWindowTitle("关于幻衣");
        dlg.setFixedSize(420, 260);
        dlg.setStyleSheet("QDialog { background-color: #FFFFFF; }");

        QVBoxLayout *layout = new QVBoxLayout(&dlg);
        layout->setContentsMargins(20, 20, 20, 20);

        QLabel *titleLabel = new QLabel("👕 幻衣 - AI 虚拟试穿系统", &dlg);
        titleLabel->setAlignment(Qt::AlignCenter);
        titleLabel->setStyleSheet("font-size: 20px; font-weight: bold; color: #8B5CF6; margin-top: 10px;");

        QLabel *descLabel = new QLabel("基于 <b>Qt + OpenCV + ComfyUI + CatVTON</b> 开发。\n\n为您提供一站式、高保真的线上虚拟试穿体验。", &dlg);
        descLabel->setAlignment(Qt::AlignCenter);
        descLabel->setWordWrap(true);
        descLabel->setStyleSheet("font-size: 14px; color: #4B5563; line-height: 1.6; margin-top: 15px;");

        QPushButton *btnOk = new QPushButton("确定", &dlg);
        btnOk->setFixedSize(120, 42);
        btnOk->setStyleSheet("QPushButton { background-color: #8B5CF6; color: white; border-radius: 8px; font-size: 15px; font-weight: bold; } QPushButton:hover { background-color: #7C3AED; }");
        connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);

        layout->addWidget(titleLabel);
        layout->addWidget(descLabel);
        layout->addStretch();
        layout->addWidget(btnOk, 0, Qt::AlignCenter);

        dlg.exec();
    });
}

// ==================== 状态与动效 ====================
void MainWindow::resetToHomeScreen() {
    lblImage->setText(R"(
        <div style='text-align: center; font-family: "Microsoft YaHei", sans-serif; margin-top: 50px;'>
            <div style='margin-bottom: 20px;'>
                <span style='font-size: 90px;'>👗</span>
            </div>
            <h1 style='margin: 0; font-size: 42px; font-weight: 900; color: #8B5CF6; letter-spacing: 3px;'>幻衣 · AI 虚拟试穿</h1>
            <p style='color: #6B7280; font-size: 18px; margin-top: 15px; font-weight: 300;'>无需试衣间，一键预见最美的自己</p>
            <table width='100%' style='margin-top: 60px;'><tr><td align='center'>
                <table><tr>
                    <td style='padding: 10px 20px; background: #F3E8FF; border-radius: 10px;'><span style='font-size: 20px;'>✨</span> <span style='color: #7E22CE; font-weight: bold; margin-left: 5px;'>AI 智能生成</span></td>
                    <td width='20'></td>
                    <td style='padding: 10px 20px; background: #D1FAE5; border-radius: 10px;'><span style='font-size: 20px;'>⚡</span> <span style='color: #047857; font-weight: bold; margin-left: 5px;'>秒级换装</span></td>
                </tr></table>
            </td></tr></table>
        </div>
    )");
    m_isViewingHistoryImage = false; btnBackToHistory->setVisible(false); btnUseHistoryImg->setVisible(false);
    btnSmartAction->setVisible(true); bottomSubRowWidget->setVisible(true); btnShowCompare->setVisible(false);
    updateSmartUI();
}

void MainWindow::showCenterLoading(const QString &title, const QString &subtitle) {
    QString html = QString(R"(
        <div style='text-align: center; margin-top: 90px; font-family: "Microsoft YaHei", sans-serif;'>
            <div style='display: inline-block; padding: 25px; border-radius: 50%; background: linear-gradient(135deg, #F3E8FF 0%, #E0F2FE 100%); box-shadow: 0 10px 25px rgba(139,92,246,0.15); margin-bottom: 25px;'>
                <span style='font-size: 50px;'>⚙️</span>
            </div>
            <h2 style='color: #1E40AF; font-size: 26px; font-weight: bold; margin-bottom: 10px;'>%1</h2>
            <p style='color: #6B7280; font-size: 16px; font-weight: bold;'>%2</p>
        </div>
    )").arg(title, subtitle);
    lblImage->setText(html); lblImage->repaint(); QApplication::processEvents();
}

void MainWindow::updateCenterProgress(int percent, const QString &msg) {
    int offset = (QDateTime::currentMSecsSinceEpoch() / 10) % 100;
    QString html = QString(R"(
        <div style='text-align: center; margin-top: 90px; font-family: "Microsoft YaHei", sans-serif;'>
            <h2 style='color: #8B5CF6; font-size: 24px; font-weight: bold; margin-bottom: 25px;'>%1</h2>
            <table width='480' align='center' style='background-color: #F3F4F6; border-radius: 11px; overflow: hidden;'>
                <tr><td>
                    <div style='width: %2%; height: 22px; background: linear-gradient(90deg, #8B5CF6, #D946EF, #8B5CF6); background-size: 200%% 100%%; background-position: %3%% 0; border-radius: 11px;'></div>
                </td></tr>
            </table>
            <h3 style='color: #D946EF; font-size: 38px; font-weight: 900; margin-top: 20px;'>%2<span style='font-size: 20px; color: #6B7280;'>%</span></h3>
            <p style='color: #9CA3AF; font-size: 14px; margin-top: 10px;'>请稍候，AI 正在全力以赴为您创作...</p>
        </div>
    )").arg(msg).arg(percent).arg(offset);
    lblImage->setText(html); lblImage->repaint();
}

void MainWindow::updateProgressUI() {
    if (!lblStatus || currentStage != 3) return;
    if (currentNodeMax > 0) {
        int percent = currentNodeProgress * 100 / currentNodeMax;
        updateCenterProgress(percent, "AI 正在全速运转");
        if (currentTaskType == 0) {
            if (percent < 25) {
                lblStatus->setText("💡【步骤 1/3】AI 正在深度解析描述词...");
                lblStatus->setStyleSheet("QLabel { background-color: #FEF3C7; color: #92400E; font-size: 15px; font-weight: bold; border-radius: 8px; padding: 10px; }");
            } else if (percent < 75) {
                lblStatus->setText("🎨【步骤 2/3】正在精心绘制服装轮廓与细节...");
                lblStatus->setStyleSheet("QLabel { background-color: #DBEAFE; color: #1E40AF; font-size: 15px; font-weight: bold; border-radius: 8px; padding: 10px; }");
            } else {
                lblStatus->setText("✨【步骤 3/3】正在渲染高清光影，马上就好...");
                lblStatus->setStyleSheet("QLabel { background-color: #D1FAE5; color: #065F46; font-size: 15px; font-weight: bold; border-radius: 8px; padding: 10px; }");
            }
        } else {
            if (percent < 25) {
                lblStatus->setText("📐【步骤 1/3】正在精准解析您的身体轮廓...");
                lblStatus->setStyleSheet("QLabel { background-color: #FEF3C7; color: #92400E; font-size: 15px; font-weight: bold; border-radius: 8px; padding: 10px; }");
            } else if (percent < 75) {
                lblStatus->setText("👗【步骤 2/3】正在为您穿上新衣并处理自然褶皱...");
                lblStatus->setStyleSheet("QLabel { background-color: #DBEAFE; color: #1E40AF; font-size: 15px; font-weight: bold; border-radius: 8px; padding: 10px; }");
            } else {
                lblStatus->setText("🖼️【步骤 3/3】正在融合背景，生成最终试穿效果...");
                lblStatus->setStyleSheet("QLabel { background-color: #D1FAE5; color: #065F46; font-size: 15px; font-weight: bold; border-radius: 8px; padding: 10px; }");
            }
        }
    }
}

void MainWindow::updateSmartUI() {
    if (!btnSmartAction || !lblStatus || m_isViewingHistoryImage) return;
    switch (currentStage) {
    case 0:
        btnShowCompare->setVisible(false);
        if (pendingTryOnAfterGenerate) {
            btnSmartAction->setText("👕 继续试穿"); btnSmartAction->setEnabled(true);
            lblStatus->setText("✅ 服装已生成！查看效果后点击【继续试穿】");
            lblStatus->setStyleSheet("QLabel { background-color: #D1FAE5; color: #065F46; font-size: 14px; border-radius: 8px; padding: 10px; }");
        } else {
            btnSmartAction->setText("🚀 开始试穿"); btnSmartAction->setEnabled(true);
            lblStatus->setText("💡 现在请：点击下方按钮开始试穿");
            lblStatus->setStyleSheet("QLabel { background-color: #EFF6FF; color: #2563EB; font-size: 14px; border-radius: 8px; padding: 10px; }");
        }
        break;
    case 3:
        btnShowCompare->setVisible(false);
        btnSmartAction->setText("⏳ 正在处理，请稍候..."); btnSmartAction->setEnabled(false); break;
    }
}

// ==================== 主流程交互 ====================
void MainWindow::onSmartActionClicked() {
    if (pendingTryOnAfterGenerate) {
        pendingTryOnAfterGenerate = false; currentStage = 3; updateSmartUI();
        lblStatus->setText("⏳ 正在准备试穿...");
        freeComfyMemory(); QTimer::singleShot(3000, this, [=]() { doTryOn(currentBodyPath, pendingMaskPath); });
        return;
    }
    if (currentStage == 3) return;
    QDialog dlg(this); dlg.setWindowTitle("开始试穿"); dlg.setFixedSize(360, 320); dlg.setStyleSheet("QDialog { background-color: #FFFFFF; }");
    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    QLabel *iconLabel = new QLabel("📸", &dlg); iconLabel->setAlignment(Qt::AlignCenter); iconLabel->setStyleSheet("font-size: 48px; margin-top: 10px;");
    QLabel *textLabel = new QLabel("请选择人物图片来源", &dlg); textLabel->setAlignment(Qt::AlignCenter); textLabel->setStyleSheet("font-size: 16px; font-weight: bold; margin-bottom: 15px; color: #374151;");
    QPushButton *btnCapture = new QPushButton("📷 实时抓拍", &dlg); btnCapture->setMinimumHeight(46); btnCapture->setStyleSheet("QPushButton { background-color: #8B5CF6; color: white; border-radius: 8px; font-size: 15px; font-weight: bold; } QPushButton:hover { background-color: #7C3AED; }");
    QPushButton *btnDefault = new QPushButton("🖼 使用默认图", &dlg); btnDefault->setMinimumHeight(46); btnDefault->setStyleSheet("QPushButton { background-color: #F3F4F6; color: #374151; border-radius: 8px; font-size: 15px; font-weight: bold; } QPushButton:hover { background-color: #E5E7EB; }");
    QPushButton *btnCancel = new QPushButton("取消", &dlg); btnCancel->setMinimumHeight(40); btnCancel->setStyleSheet("QPushButton { background-color: transparent; color: #9CA3AF; border: none; font-size: 14px; }");
    layout->addWidget(iconLabel); layout->addWidget(textLabel); layout->addWidget(btnCapture); layout->addWidget(btnDefault); layout->addWidget(btnCancel);
    int ret = 0;
    connect(btnCapture, &QPushButton::clicked, [&](){ ret = 1; dlg.accept(); });
    connect(btnDefault, &QPushButton::clicked, [&](){ ret = 2; dlg.accept(); });
    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    dlg.exec();
    if (dlg.result() != QDialog::Accepted) return;
    if (ret == 1) {
        cap.open(0); if (!cap.isOpened()) { QMessageBox::warning(this, "错误", "无法打开摄像头"); return; }
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 640); cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480); timer->start(30);
        lblStatus->setText("💡 摄像头已开启，站好后再次点击【抓拍并继续】"); lblStatus->setStyleSheet("QLabel { background-color: #FEF3C7; color: #92400E; font-size: 14px; border-radius: 8px; padding: 10px; }");
        btnSmartAction->setText("📸 抓拍并继续");
        disconnect(btnSmartAction, &QPushButton::clicked, this, &MainWindow::onSmartActionClicked);
        connect(btnSmartAction, &QPushButton::clicked, this, [=]() {
            disconnect(btnSmartAction, &QPushButton::clicked, nullptr, nullptr);
            connect(btnSmartAction, &QPushButton::clicked, this, &MainWindow::onSmartActionClicked);
            on_btnCapture_clicked();
        });
        return;
    }
    if (ret == 2) {
        QString defaultBody = "images/body.jpg";
        if (!QFile::exists(defaultBody)) { QMessageBox::warning(this, "错误", "找不到默认人物图"); return; }
        currentBodyPath = defaultBody; QPixmap pix(defaultBody); if (!pix.isNull()) lblImage->setPixmap(pix.scaled(lblImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        bool reuseMask = false;
        if (!lastMaskForReuse.isEmpty() && QFile::exists(lastMaskForReuse) && !lastBodyPathForMask.isEmpty()) {
            QDialog askDlg(this); askDlg.setWindowTitle("复用上次遮罩"); askDlg.setFixedSize(320, 180);
            QVBoxLayout *aLayout = new QVBoxLayout(&askDlg);
            QLabel *aLbl = new QLabel("检测到上次已画过遮罩，是否直接复用？", &askDlg); aLbl->setAlignment(Qt::AlignCenter); aLbl->setWordWrap(true); aLbl->setStyleSheet("font-size: 15px; color: #374151; font-weight: bold; margin: 10px;");
            QHBoxLayout *btnLayout = new QHBoxLayout();
            QPushButton *btnYes = new QPushButton("复用", &askDlg); btnYes->setStyleSheet("background-color: #8B5CF6; color: white; border-radius: 6px; padding: 8px 20px; font-weight: bold;");
            QPushButton *btnNo = new QPushButton("重新画", &askDlg); btnNo->setStyleSheet("background-color: #F3F4F6; color: #374151; border-radius: 6px; padding: 8px 20px; font-weight: bold;");
            btnLayout->addWidget(btnNo); btnLayout->addWidget(btnYes); aLayout->addWidget(aLbl); aLayout->addLayout(btnLayout);
            connect(btnYes, &QPushButton::clicked, &askDlg, &QDialog::accept); connect(btnNo, &QPushButton::clicked, &askDlg, &QDialog::reject);
            if (askDlg.exec() == QDialog::Accepted) { currentMaskPath = lastMaskForReuse; reuseMask = true; }
        }
        if (!reuseMask) {
            lblStatus->setText("✏️ 第 1 步：涂抹需要替换的衣服区域"); lblStatus->setStyleSheet("QLabel { background-color: #FEF3C7; color: #92400E; font-size: 14px; border-radius: 8px; padding: 10px; }");
            QString ts = QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
            MaskEditorDialog maskDlg(defaultBody, this);
            if (maskDlg.exec() != QDialog::Accepted) { updateSmartUI(); lblImage->setText("✅ 已取消涂抹"); return; }
            cv::Mat mask = maskDlg.getMask(); if (mask.empty() || cv::countNonZero(mask) == 0) { QMessageBox::warning(this, "未涂抹", "没有涂抹任何区域"); updateSmartUI(); return; }
            cv::Mat frame = cv::imread(defaultBody.toStdString()); cv::Mat rgba; cv::cvtColor(frame, rgba, cv::COLOR_BGR2BGRA);
            applyMaskToAlpha(rgba, mask); QString maskFile = QString("images/captured_mask_%1.png").arg(ts);
            cv::imwrite(maskFile.toStdString(), rgba); currentMaskPath = maskFile; lastBodyPathForMask = defaultBody; lastMaskForReuse = maskFile;
            doAfterMaskReady();
        } else {
            doAfterMaskReady();
        }
    }
}

void MainWindow::on_btnCapture_clicked() {
    if (!cap.isOpened()) { QMessageBox::information(this, "提示", "请先点击【开始试穿】"); return; }
    cv::Mat frame; cap >> frame; if (frame.empty()) { QMessageBox::warning(this, "错误", "抓取画面失败"); return; }
    timer->stop(); cap.release();
    if (frame.cols > 512 || frame.rows > 512) { double scale = 512.0 / std::max(frame.cols, frame.rows); cv::resize(frame, frame, cv::Size(), scale, scale); }
    QString ts = QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
    QString bodyFile = QString("images/captured_body_%1.jpg").arg(ts); cv::imwrite(bodyFile.toStdString(), frame);
    currentBodyPath = bodyFile; currentMaskPath = "";
    bool reuseMask = false;
    if (!lastMaskForReuse.isEmpty() && QFile::exists(lastMaskForReuse) && !lastBodyPathForMask.isEmpty() && QFile::exists(lastBodyPathForMask)) {
        cv::Mat oldBody = cv::imread(lastBodyPathForMask.toStdString()); cv::Mat newBody = cv::imread(bodyFile.toStdString());
        if (!oldBody.empty() && !newBody.empty()) {
            if (oldBody.size() != newBody.size()) cv::resize(oldBody, oldBody, newBody.size());
            double sim = imageSimilarity(oldBody, newBody);
            if (sim > 0.65) {
                if (QMessageBox::question(this, "复用上次遮罩？", QString("相似度 %1%，是否复用？").arg(int(sim*100)), QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes) {
                    currentMaskPath = lastMaskForReuse; reuseMask = true;
                }
            }
        }
    }
    if (reuseMask) { doAfterMaskReady(); return; }
    lblStatus->setText("✏️ 第 1 步：涂抹需要替换的衣服区域"); lblStatus->setStyleSheet("QLabel { background-color: #FEF3C7; color: #92400E; font-size: 14px; border-radius: 8px; padding: 10px; }");
    MaskEditorDialog dlg(bodyFile, this); if (dlg.exec() != QDialog::Accepted) { updateSmartUI(); return; }
    cv::Mat mask = dlg.getMask(); if (mask.empty() || cv::countNonZero(mask) == 0) { QMessageBox::warning(this, "未涂抹", "没有涂抹"); updateSmartUI(); return; }
    cv::Mat rgba; cv::cvtColor(frame, rgba, cv::COLOR_BGR2BGRA); applyMaskToAlpha(rgba, mask);
    QString maskFile = QString("images/captured_mask_%1.png").arg(ts); cv::imwrite(maskFile.toStdString(), rgba);
    currentMaskPath = maskFile; lastBodyPathForMask = bodyFile; lastMaskForReuse = maskFile;
    doAfterMaskReady();
}

void MainWindow::doAfterMaskReady() {
    int count = WardrobeManager::getItemCount(); SourceSelectDialog dialog(count, this);
    if (dialog.exec() != QDialog::Accepted || dialog.getChoice() == SourceSelectDialog::None) { updateSmartUI(); lblImage->setText("✅ 遮罩已保存"); return; }
    if (dialog.getChoice() == SourceSelectDialog::Wardrobe) { doTryOnWithWardrobe(); return; }
    if (dialog.getChoice() == SourceSelectDialog::AIGenerate) {
        AiPromptDialog aiDlg(this); if (aiDlg.exec() != QDialog::Accepted) { updateSmartUI(); lblImage->setText("✅ 已取消生成"); return; }
        QString prompt = aiDlg.getPrompt(); if (prompt.isEmpty()) { updateSmartUI(); return; }
        currentPromptForRegen = prompt; currentStage = 3; updateSmartUI();
        lblStatus->setText("⏳ 第 3 步：AI 正在准备生成服装...");
        lblStatus->setStyleSheet("QLabel { background-color: #FEF3C7; color: #92400E; font-size: 14px; border-radius: 8px; padding: 10px; }");
        ensureComfyUIRunning([=]() {
            pendingAutoTryOn = true; pendingMaskPath = currentMaskPath;
            if (containsChinese(prompt)) translateToEnglish(prompt); else startGenerate(prompt);
        });
        return;
    }
    if (dialog.getChoice() == SourceSelectDialog::LocalFile) { doTryOnWithLocalShirt(); return; }
}

void MainWindow::openWardrobeManager() { WardrobeManagerDialog dlg(this); dlg.exec(); }

void MainWindow::doTryOnWithWardrobe() {
    WardrobePickerDialog picker(this); if (picker.exec() != QDialog::Accepted) { updateSmartUI(); lblImage->setText("✅ 已取消选择"); return; }
    WardrobeItem item = picker.selectedItem(); if (item.id < 0 || !QFile::exists(item.imagePath)) { QMessageBox::warning(this, "错误", "衣服文件丢失"); return; }
    currentShirtPath = item.imagePath; lastGeneratedImagePath = ""; currentStage = 3; updateSmartUI();
    lblStatus->setText(QString("⏳ 第 3 步：正在准备试穿「%1」...").arg(item.name));
    lblStatus->setStyleSheet("QLabel { background-color: #FEF3C7; color: #92400E; font-size: 14px; border-radius: 8px; padding: 10px; }");
    ensureComfyUIRunning([=]() { freeComfyMemory(); doTryOn(currentBodyPath, currentMaskPath); });
}

void MainWindow::doTryOnWithLocalShirt() {
    QString filePath = QFileDialog::getOpenFileName(this, "选择服装图片", "C:/Users/31593/Documents/hy/images/", "图片文件 (*.png *.jpg *.jpeg *.bmp)");
    if (filePath.isEmpty()) { updateSmartUI(); lblImage->setText("✅ 已取消选择"); return; }
    int ret = QMessageBox::question(this, "是否存入衣服库？", "临时试穿不保存图片，下次需重新找文件。\n是否将这张图存入衣服库，方便以后一键复用？", QMessageBox::Yes | QMessageBox::No);
    if (ret == QMessageBox::Yes) {
        bool ok = false; QString name = QFileInfo(filePath).baseName();
        QString input = QInputDialog::getText(this, "存入衣服库", "请输入衣服名称（默认取文件名）：", QLineEdit::Normal, name, &ok);
        if (ok && !input.trimmed().isEmpty()) {
            QString autoCategory = detectCategoryFromPrompt(QFileInfo(filePath).baseName() + " " + input);
            int id = WardrobeManager::addItem(filePath, input.trimmed(), autoCategory);
            if (id > 0) { WardrobeItem it = WardrobeManager::getItemById(id); filePath = it.imagePath; }
        }
    }
    currentShirtPath = filePath; lastGeneratedImagePath = ""; currentStage = 3; updateSmartUI();
    lblStatus->setText("⏳ 第 3 步：正在使用本地图片准备试穿..."); lblStatus->setStyleSheet("QLabel { background-color: #FEF3C7; color: #92400E; font-size: 14px; border-radius: 8px; padding: 10px; }");
    ensureComfyUIRunning([=]() { freeComfyMemory(); doTryOn(currentBodyPath, currentMaskPath); });
}

// ==================== 网络请求 ====================
void MainWindow::doTryOn(const QString &bodyPath, const QString &maskPath) {
    currentTaskType = 1;
    if (bodyPath.isEmpty() || !QFile::exists(bodyPath)) { QMessageBox::warning(this, "错误", "找不到人物图"); return; }
    if (maskPath.isEmpty() || !QFile::exists(maskPath)) { QMessageBox::warning(this, "错误", "找不到遮罩图"); return; }
    QString comfyInput = "D:/ComfyUI-aki/ComfyUI-aki-v3/ComfyUI/input/";
    auto forceCopy = [](const QString &src, const QString &dst) -> bool { QFile::remove(dst); return QFile::copy(src, dst); };

    QString protectedMask = protectFaceInMask(bodyPath, maskPath);
    if (!forceCopy(bodyPath, comfyInput + "body.jpg")) { QMessageBox::warning(this, "错误", "无法复制人物"); return; }

    // ★ 恢复原始逻辑：直接复制遮罩图，不再做任何强制羽化处理
    if (!forceCopy(protectedMask, comfyInput + "body_mask.png")) { QMessageBox::warning(this, "错误", "无法复制遮罩"); return; }

    QString shirtSrc = !lastGeneratedImagePath.isEmpty() ? lastGeneratedImagePath : (currentShirtPath.isEmpty() ? QString("images/shirt.png") : currentShirtPath);
    if (!QFile::exists(shirtSrc)) { QMessageBox::warning(this, "错误", "找不到服装图"); currentStage=0; updateSmartUI(); return; }
    if (!forceCopy(shirtSrc, comfyInput + "shirt.png")) { QMessageBox::warning(this, "错误", "无法复制服装"); currentStage=0; updateSmartUI(); return; }

    QFile file("C:/Users/31593/Documents/hy/tryon_workflow_api.json");
    if (!file.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "错误", "读不到 tryon_workflow_api.json"); currentStage=0; updateSmartUI(); return; }
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object(); file.close();

    if (root.contains("19")) { QJsonObject n = root["19"].toObject(); QJsonObject i = n["inputs"].toObject(); i["image"] = "shirt.png"; n["inputs"] = i; root["19"] = n; }
    if (root.contains("20")) { QJsonObject n = root["20"].toObject(); QJsonObject i = n["inputs"].toObject(); i["image"] = "body_mask.png"; n["inputs"] = i; root["20"] = n; }
    if (root.contains("15")) {
        QJsonObject n = root["15"].toObject();
        QJsonObject i = n["inputs"].toObject();
        i["seed"] = (qint64)QRandomGenerator::global()->bounded(1, 1000000000);
        n["inputs"] = i;
        root["15"] = n;
    }

    ensureWebSocketReady(); QJsonObject wrapper; wrapper["prompt"] = root; wrapper["client_id"] = wsClientId;
    QString json = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
    QNetworkRequest request(QUrl("http://127.0.0.1:8188/prompt")); request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    showCenterLoading("正在准备试穿", "正在与 AI 引擎建立连接...");
    QNetworkReply *reply = manager->post(request, json.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() != QNetworkReply::NoError) { currentStage=0; updateSmartUI(); showFriendlyError("试穿失败", reply->errorString()); reply->deleteLater(); return; }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (!obj.contains("prompt_id")) { currentStage=0; updateSmartUI(); showFriendlyError("提交失败", "AI 引擎拒绝了请求，可能是参数格式不正确。"); reply->deleteLater(); return; }
        currentPromptId = obj["prompt_id"].toString(); pollCount = 0;
        if (!wsConnected) pollTimer->start(); else QTimer::singleShot(15000, this, [=]() { if (currentStage == 3 && !pollTimer->isActive()) pollTimer->start(); });
        reply->deleteLater();
    });
}

void MainWindow::startGenerate(const QString &prompt) {
    currentTaskType = 0; QString json = buildWorkflowJson(prompt);
    if (json.isEmpty()) { currentStage=0; updateSmartUI(); return; }
    QNetworkRequest request(QUrl("http://127.0.0.1:8188/prompt")); request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    showCenterLoading("✨ AI 正在绘制服装", "正在解析您的描述，马上开始...");
    QNetworkReply *reply = manager->post(request, json.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() != QNetworkReply::NoError) { pendingAutoTryOn = false; currentStage=0; updateSmartUI(); showFriendlyError("生成失败", reply->errorString()); reply->deleteLater(); return; }
        QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
        if (!obj.contains("prompt_id")) { pendingAutoTryOn = false; currentStage=0; updateSmartUI(); showFriendlyError("提交失败", "AI 引擎拒绝执行，请检查工作流配置。"); reply->deleteLater(); return; }
        currentPromptId = obj["prompt_id"].toString(); pollCount = 0;
        if (!wsConnected) pollTimer->start(); else QTimer::singleShot(15000, this, [=]() { if (currentStage == 3 && !pollTimer->isActive()) pollTimer->start(); });
        reply->deleteLater();
    });
}

QString MainWindow::buildWorkflowJson(const QString &prompt) {
    QFile file("C:/Users/31593/Documents/hy/workflow_api.json");
    if (!file.open(QIODevice::ReadOnly)) { QMessageBox::warning(this, "错误", "读不到 workflow_api.json"); return ""; }
    QJsonObject root = QJsonDocument::fromJson(file.readAll()).object(); file.close();
    if (root.contains("6")) {
        QJsonObject node6 = root["6"].toObject(); QJsonObject inputs6 = node6["inputs"].toObject();
        inputs6["text"] = prompt + ", masterpiece, best quality, 8k resolution, ultra-detailed, photorealistic, soft lighting";
        node6["inputs"] = inputs6; root["6"] = node6;
    }
    if (root.contains("3")) {
        QJsonObject node3 = root["3"].toObject(); QJsonObject inputs3 = node3["inputs"].toObject();
        inputs3["seed"] = (qint64)QRandomGenerator::global()->bounded(1, 1000000000);
        inputs3["batch_size"] = 1;
        node3["inputs"] = inputs3; root["3"] = node3;
    }
    ensureWebSocketReady();
    QJsonObject wrapper; wrapper["prompt"] = root; wrapper["client_id"] = wsClientId;
    return QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
}

// ==================== 结果处理与轮询 ====================
void MainWindow::pollHistory() {
    pollCount++;
    if (pollCount > 150) { pollTimer->stop(); pollCount = 0; setLoading(false); pendingAutoTryOn = false; pendingTryOnAfterGenerate = false; currentStage=0; updateSmartUI(); showFriendlyError("生成超时", "AI 引擎处理时间超过 10 分钟，请检查显存。"); return; }
    QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(QString("http://127.0.0.1:8188/history/%1").arg(currentPromptId))));
    connect(reply, &QNetworkReply::finished, this, [=]() {
        QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object(); if (!root.contains(currentPromptId)) { reply->deleteLater(); return; }
        QJsonObject job = root[currentPromptId].toObject(); QJsonObject status = job["status"].toObject();
        if (status["status_str"].toString() == "error" || !status["completed"].toBool()) {
            pollTimer->stop(); pollCount = 0; setLoading(false); pendingAutoTryOn = false; pendingTryOnAfterGenerate = false;
            QString errMsg = "任务执行失败";
            for (const auto &msg : status["messages"].toArray()) { QJsonArray arr = msg.toArray(); if (arr.size() >= 2 && arr[0].toString() == "execution_error") { errMsg = arr[1].toObject()["exception_message"].toString(); break; } }
            currentStage=0; updateSmartUI(); showFriendlyError("生成失败", errMsg); reply->deleteLater(); return;
        }
        QJsonObject outputs = job["outputs"].toObject(); QString outputNode; bool isTryOn = false;
        if (currentTaskType == 1) { if (outputs.contains("17")) { outputNode = "17"; isTryOn = true; } }
        else { if (outputs.contains("9")) { outputNode = "9"; isTryOn = false; } }
        if (outputNode.isEmpty()) {
            for (auto it = outputs.begin(); it != outputs.end(); ++it) {
                if (!it.value().toObject()["images"].toArray().isEmpty()) {
                    outputNode = it.key(); isTryOn = (currentTaskType == 1); break;
                }
            }
        }
        if (!outputNode.isEmpty()) {
            QJsonArray images = outputs[outputNode].toObject()["images"].toArray();
            if (!images.isEmpty()) {
                QJsonObject img = images[0].toObject();
                pollTimer->stop(); pollCount = 0;
                downloadImage(img["filename"].toString(), img["subfolder"].toString(), isTryOn);
                reply->deleteLater(); return;
            }
        }
        pollTimer->stop(); pollCount = 0; setLoading(false); pendingAutoTryOn = false; pendingTryOnAfterGenerate = false; currentStage=0; updateSmartUI(); showFriendlyError("无输出", "任务已经完成，但 AI 没有生成任何图片。"); reply->deleteLater();
    });
}

void MainWindow::downloadImage(const QString &filename, const QString &subfolder, bool isTryOn) {
    QNetworkReply *reply = manager->get(QNetworkRequest(QUrl(QString("http://127.0.0.1:8188/view?filename=%1&subfolder=%2&type=output").arg(filename, subfolder))));
    connect(reply, &QNetworkReply::finished, this, [=]() {
        QByteArray data = reply->readAll();
        if (data.isEmpty()) {
            setLoading(false); pendingAutoTryOn = false; pendingTryOnAfterGenerate = false; currentStage=0; updateSmartUI();
            showFriendlyError("下载失败", "无法获取生成的图片，可能是网络波动。"); reply->deleteLater(); return;
        }
        QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
        QString savePath = isTryOn ? QString("images/history/tryon_%1.png").arg(ts) : QString("images/history/gen_%1.png").arg(ts);

        QImage img = QImage::fromData(data);

        QFile file(savePath); if (file.open(QIODevice::WriteOnly)) { file.write(data); file.close(); }

        lastGeneratedImagePath = savePath;
        currentResultPixmap = QPixmap::fromImage(img);

        currentStage = 0;
        setLoading(false);

        QPixmap pix = QPixmap::fromImage(img);
        if (isTryOn && !pix.isNull()) pix = postProcessImage(pix);

        if (!pix.isNull()) {
            lblImage->setPixmap(pix.scaled(lblImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
            currentResultPixmap = pix;
            lblImage->repaint();
            QApplication::processEvents();
        }

        if (isTryOn) {
            insertRecord("试穿", "CatVTON 虚拟试穿", savePath);
            pendingTryOnAfterGenerate = false;
            updateSmartUI();
            lblStatus->setText("✅ 试穿完成！您可以点击下方按钮查看对比。");
            lblStatus->setStyleSheet("QLabel { background-color: #D1FAE5; color: #065F46; font-size: 14px; border-radius: 8px; padding: 10px; }");
            btnShowCompare->setVisible(true);
        }
        else {
            insertRecord("生图", currentPromptForRegen, savePath);
            if (pendingAutoTryOn) {
                pendingAutoTryOn = false;
                lblStatus->setText("✅ 服装生成完毕，请选择下一步操作。");
                lblStatus->setStyleSheet("QLabel { background-color: #D1FAE5; color: #065F46; font-size: 14px; border-radius: 8px; padding: 10px; }");
                QApplication::processEvents();

                QMessageBox box(this); box.setWindowTitle("AI 服装生成完成"); box.setText("对生成的衣服满意吗？"); box.setInformativeText("您可以存入衣服库方便以后复用，或直接试穿。");
                QPushButton *btnTry = box.addButton("👕 直接试穿", QMessageBox::YesRole);
                QPushButton *btnSave = box.addButton("💾 存入衣柜", QMessageBox::ActionRole);
                QPushButton *btnRegen = box.addButton("🔄 重新生成", QMessageBox::NoRole);
                QPushButton *btnCancel = box.addButton("❌ 取消", QMessageBox::RejectRole);
                box.exec();
                if (box.clickedButton() == btnTry) {
                    pendingTryOnAfterGenerate = true; currentStage=0; updateSmartUI();
                    lblStatus->setText("✅ 点击【继续试穿】"); lblStatus->setStyleSheet("QLabel { background-color: #D1FAE5; color: #065F46; font-size: 14px; border-radius: 8px; padding: 10px; }");
                } else if (box.clickedButton() == btnSave) {
                    QString autoCategory = detectCategoryFromPrompt(currentPromptForRegen);
                    bool ok = false;
                    QString titleStr = QString("系统自动识别分类为：【%1】").arg(autoCategory);
                    QString name = QInputDialog::getText(this, "存入衣服库", titleStr + "\n请输入衣服名称：", QLineEdit::Normal, currentPromptForRegen.left(10), &ok);
                    if (ok && !name.trimmed().isEmpty()) {
                        int id = WardrobeManager::addItem(savePath, name.trimmed(), autoCategory);
                        if (id > 0) { WardrobeItem it = WardrobeManager::getItemById(id); currentShirtPath = it.imagePath; }
                    }
                    pendingTryOnAfterGenerate = true; currentStage=0; updateSmartUI();
                    lblStatus->setText("✅ 已存入库，点击【继续试穿】"); lblStatus->setStyleSheet("QLabel { background-color: #D1FAE5; color: #065F46; font-size: 14px; border-radius: 8px; padding: 10px; }");
                } else if (box.clickedButton() == btnRegen) {
                    bool ok = false; QString newPrompt = QInputDialog::getText(this, "重新生成", "描述词：", QLineEdit::Normal, currentPromptForRegen, &ok);
                    if (ok && !newPrompt.trimmed().isEmpty()) {
                        currentPromptForRegen = newPrompt; currentStage=3; updateSmartUI();
                        pendingAutoTryOn = true; pendingMaskPath = currentMaskPath;
                        if (containsChinese(newPrompt)) translateToEnglish(newPrompt); else startGenerate(newPrompt);
                    } else { currentStage=0; updateSmartUI(); }
                } else { currentStage=0; updateSmartUI(); }
            } else { currentStage=0; updateSmartUI(); }
        }
        reply->deleteLater();
    });
}

// ==================== 高级图像处理与引擎控制 ====================
QPixmap MainWindow::postProcessImage(const QPixmap &input) {
    if (input.isNull()) return input;
    QImage qimg = input.toImage().convertToFormat(QImage::Format_RGB888); cv::Mat mat(qimg.height(), qimg.width(), CV_8UC3, (void*)qimg.bits(), qimg.bytesPerLine());
    mat = mat.clone(); cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);
    cv::Mat blurred, sharpened; cv::GaussianBlur(mat, blurred, cv::Size(0, 0), 3); cv::addWeighted(mat, 1.5, blurred, -0.5, 0, sharpened);
    cv::Mat hsv; cv::cvtColor(sharpened, hsv, cv::COLOR_BGR2HSV); std::vector<cv::Mat> channels; cv::split(hsv, channels); channels[1] *= 1.10; cv::merge(channels, hsv); cv::cvtColor(hsv, sharpened, cv::COLOR_HSV2BGR);
    cv::cvtColor(sharpened, sharpened, cv::COLOR_BGR2RGB); QImage res(sharpened.data, sharpened.cols, sharpened.rows, (int)sharpened.step, QImage::Format_RGB888); return QPixmap::fromImage(res.copy());
}

QString MainWindow::protectFaceInMask(const QString &bodyPath, const QString &maskPath) {
    cv::Mat bodyMat = cv::imread(bodyPath.toStdString()); cv::Mat maskRaw = cv::imread(maskPath.toStdString(), cv::IMREAD_UNCHANGED);
    if (bodyMat.empty() || maskRaw.empty()) return maskPath;
    static cv::CascadeClassifier faceCasc;
    if (faceCasc.empty()) { QStringList searchPaths = {"haarcascade_frontalface_default.xml", "C:/Users/31593/Documents/hy/haarcascade_frontalface_default.xml", "D:/opencv/build/etc/haarcascades/haarcascade_frontalface_default.xml"}; for (const auto &p : searchPaths) if (QFile::exists(p) && faceCasc.load(p.toStdString())) break; }
    if (faceCasc.empty()) return maskPath;
    cv::Mat gray; cv::cvtColor(bodyMat, gray, cv::COLOR_BGR2GRAY); std::vector<cv::Rect> faces; faceCasc.detectMultiScale(gray, faces, 1.1, 4, 0, cv::Size(30, 30));
    if (faces.empty()) return maskPath;
    cv::Rect faceRect = faces[0]; for (const auto &f : faces) if (f.area() > faceRect.area()) faceRect = f;
    int expandX = std::max((int)(faceRect.width * 0.6), 100); int expandYUp = std::max((int)(faceRect.height * 0.6), 100); int expandYDown = std::max((int)(faceRect.height * 0.8), 150);
    int newX = std::max(0, faceRect.x - expandX); int newY = std::max(0, faceRect.y - expandYUp); int newW = faceRect.width + expandX * 2; int newH = faceRect.height + expandYUp + expandYDown;
    if (newX + newW > bodyMat.cols) newW = bodyMat.cols - newX; if (newY + newH > bodyMat.rows) newH = bodyMat.rows - newY;
    cv::Rect protectRect(newX, newY, newW, newH); if (maskRaw.size() != bodyMat.size()) cv::resize(maskRaw, maskRaw, bodyMat.size());
    cv::Mat alpha; if (maskRaw.channels() == 4) { std::vector<cv::Mat> ch; cv::split(maskRaw, ch); alpha = ch[3].clone(); } else if (maskRaw.channels() == 3) { cv::Mat g; cv::cvtColor(maskRaw, g, cv::COLOR_BGR2GRAY); cv::bitwise_not(g, alpha); } else alpha = maskRaw.clone();
    cv::rectangle(alpha, protectRect, cv::Scalar(255), cv::FILLED); cv::GaussianBlur(alpha, alpha, cv::Size(21, 21), 0);
    cv::Mat out; cv::cvtColor(bodyMat, out, cv::COLOR_BGR2BGRA); std::vector<cv::Mat> outCh; cv::split(out, outCh); outCh[3] = alpha; cv::merge(outCh, out);
    QString protectedPath = maskPath; protectedPath.replace(".png", "_faceprotected.png"); cv::imwrite(protectedPath.toStdString(), out); return protectedPath;
}

double MainWindow::imageSimilarity(const cv::Mat &a, const cv::Mat &b) {
    if (a.empty() || b.empty() || a.size() != b.size() || a.type() != b.type()) return 0;
    cv::Mat sA, sB; cv::resize(a, sA, cv::Size(64, 64), 0, 0, cv::INTER_AREA); cv::resize(b, sB, cv::Size(64, 64), 0, 0, cv::INTER_AREA);
    cv::Mat gA, gB; cv::cvtColor(sA, gA, cv::COLOR_BGR2GRAY); cv::cvtColor(sB, gB, cv::COLOR_BGR2GRAY);
    return 1.0 - (cv::mean(cv::abs(gA - gB))[0] / 255.0);
}

bool MainWindow::isComfyRunning() {
    QNetworkRequest req(QUrl("http://127.0.0.1:8188/system_stats")); req.setTransferTimeout(500);
    QNetworkReply *reply = manager->get(req); QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit); loop.exec();
    bool ok = (reply->error() == QNetworkReply::NoError); reply->deleteLater(); return ok;
}

void MainWindow::freeComfyMemory() {
    QNetworkRequest request(QUrl("http://127.0.0.1:8188/free")); request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QJsonObject body; body["unload_models"] = false; body["free_memory"] = true;
    QNetworkReply *reply = manager->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [=]() { reply->deleteLater(); });
    QEventLoop loop; QTimer::singleShot(300, &loop, &QEventLoop::quit); loop.exec();
}

bool MainWindow::startComfyProcess() {
    if (isComfyRunning()) return true;
    QString pythonPath = "D:/ComfyUI-aki/ComfyUI-aki-v3/python/python.exe";
    QString workingDir = "D:/ComfyUI-aki/ComfyUI-aki-v3"; QString mainPy = "ComfyUI/main.py";
    if (!QFile::exists(pythonPath)) return false;
    QProcess::execute("taskkill", QStringList() << "/F" << "/IM" << "python.exe");
    QThread::msleep(1500);
    if (comfyProcess) delete comfyProcess;
    comfyProcess = new QProcess(this); comfyProcess->setWorkingDirectory(workingDir);
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HF_ENDPOINT", "https://hf-mirror.com"); env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1"); env.insert("PYTORCH_NO_CUDA_MEMORY_CACHING", "1");
    comfyProcess->setProcessEnvironment(env);
    comfyProcess->setStandardOutputFile(QProcess::nullDevice());
    comfyProcess->setStandardErrorFile(QProcess::nullDevice());
    comfyProcess->setCreateProcessArgumentsModifier([] (QProcess::CreateProcessArguments *args) {
        args->flags |= CREATE_NO_WINDOW;
    });
    QStringList args;
    args << "-s" << mainPy << "--listen" << "--windows-standalone-build" << "--lowvram" << "--disable-smart-memory"
         << "--use-pytorch-cross-attention" << "--disable-assets-autoscan" << "--disable-manager-ui";
    comfyProcess->start(pythonPath, args);
    return comfyProcess->waitForStarted(5000);
}

void MainWindow::ensureComfyUIRunning(std::function<void()> callback) {
    if (isComfyRunning()) { callback(); return; }
    currentStage = 2; engineWaitSeconds = 0;
    if (!startComfyProcess()) { currentStage=0; showFriendlyError("启动失败", "无法启动 ComfyUI 进程，请检查路径。"); return; }
    QTimer *checkTimer = new QTimer(this); int *retryCount = new int(0);
    connect(checkTimer, &QTimer::timeout, this, [=]() mutable {
        (*retryCount)++;
        engineWaitSeconds = *retryCount;
        if (isComfyRunning()) {
            checkTimer->stop(); checkTimer->deleteLater(); delete retryCount;
            currentStage = 3;
            showCenterLoading("AI 引擎已就绪", "马上开始处理您的请求...");
            QTimer::singleShot(500, this, callback);
        } else if (*retryCount >= 240) {
            checkTimer->stop(); checkTimer->deleteLater(); delete retryCount;
            currentStage = 0; updateSmartUI();
            showFriendlyError("启动超时", "ComfyUI 超过 4 分钟未响应，请检查显存。");
        }
    });
    checkTimer->start(1000);
}

// ==================== ComfyUI 手动控制（菜单） ====================
void MainWindow::onStartComfyUI() {
    if (isComfyRunning()) {
        QMessageBox::information(this, "ComfyUI", "ComfyUI 已在运行，服务正常。");
        return;
    }
    currentStage = 2; engineWaitSeconds = 0; updateSmartUI();
    if (!startComfyProcess()) {
        currentStage = 0; updateSmartUI();
        showFriendlyError("启动失败", "无法启动 ComfyUI 进程，请检查 D:/ComfyUI-aki 路径。");
        return;
    }
    QTimer *checkTimer = new QTimer(this); int *retry = new int(0);
    connect(checkTimer, &QTimer::timeout, this, [=]() mutable {
        (*retry)++; engineWaitSeconds = *retry;
        if (isComfyRunning()) {
            checkTimer->stop(); checkTimer->deleteLater(); delete retry;
            currentStage = 0; updateSmartUI();
            lblStatus->setText("✅ ComfyUI 已启动并就绪");
            lblStatus->setStyleSheet("QLabel { background-color: #D1FAE5; color: #065F46; font-size: 14px; border-radius: 8px; padding: 10px; }");
        } else if (*retry >= 240) {
            checkTimer->stop(); checkTimer->deleteLater(); delete retry;
            currentStage = 0; updateSmartUI();
            showFriendlyError("启动超时", "ComfyUI 超过 4 分钟未响应，请检查显存。");
        }
    });
    checkTimer->start(1000);
}

void MainWindow::onStopComfyUI() {
    if (comfyProcess && comfyProcess->state() != QProcess::NotRunning) {
        comfyProcess->terminate();
        if (!comfyProcess->waitForFinished(3000)) comfyProcess->kill();
        comfyProcess->waitForFinished(1000);
        wsConnected = false;
        QMessageBox::information(this, "ComfyUI", "ComfyUI 已停止。");
    } else {
        QMessageBox::information(this, "ComfyUI", "ComfyUI 当前未运行。");
    }
    currentStage = 0; updateSmartUI();
}

void MainWindow::onCheckComfyUI() {
    if (isComfyRunning())
        QMessageBox::information(this, "ComfyUI", "ComfyUI 正在运行，服务正常。");
    else
        QMessageBox::information(this, "ComfyUI", "ComfyUI 未运行。");
}

// ==================== WebSocket ====================
void MainWindow::ensureWebSocketReady() {
    if (wsConnected) return;
    if (!comfyWebSocket) connectWebSocket();
    else if (comfyWebSocket->state() == QAbstractSocket::UnconnectedState)
        comfyWebSocket->open(QUrl(QString("ws://127.0.0.1:8188/ws?clientId=%1").arg(wsClientId)));
}

void MainWindow::connectWebSocket() {
    if (comfyWebSocket) { comfyWebSocket->deleteLater(); comfyWebSocket = nullptr; }
    comfyWebSocket = new QWebSocket(); comfyWebSocket->setParent(this);
    connect(comfyWebSocket, &QWebSocket::connected, this, [=](){ wsConnected = true; if (pollTimer->isActive()) pollTimer->stop(); });
    connect(comfyWebSocket, &QWebSocket::disconnected, this, [=](){ wsConnected = false; if (currentStage == 3 && !currentPromptId.isEmpty() && !pollTimer->isActive()) pollTimer->start(); });
    connect(comfyWebSocket, &QWebSocket::textMessageReceived, this, &MainWindow::onWsTextMessage);
    comfyWebSocket->open(QUrl(QString("ws://127.0.0.1:8188/ws?clientId=%1").arg(wsClientId)));
}

void MainWindow::disconnectWebSocket() { if (comfyWebSocket) comfyWebSocket->close(); wsConnected = false; }

void MainWindow::onWsConnected() {}
void MainWindow::onWsDisconnected() {}
void MainWindow::onWsTextMessage(const QString &msg) {
    QJsonObject root = QJsonDocument::fromJson(msg.toUtf8()).object(); QString type = root["type"].toString(); QJsonObject data = root["data"].toObject();
    if (!data["prompt_id"].toString().isEmpty() && data["prompt_id"].toString() != currentPromptId) return;
    if (type == "progress") { currentNodeProgress = data["value"].toInt(); currentNodeMax = data["max"].toInt(); updateProgressUI(); }
}

// ==================== 历史记录与对比 ====================
void MainWindow::on_btnHistory_clicked() {
    QDialog *dialog = new QDialog(this); dialog->setWindowTitle("📋 历史记录中心");
    QScreen *screen = QGuiApplication::primaryScreen(); QRect screenGeometry = screen->availableGeometry();
    int w = qMin(1100, screenGeometry.width() - 100); int h = qMin(750, screenGeometry.height() - 100);
    dialog->resize(w, h); dialog->setStyleSheet("QDialog { background-color: #F9FAFB; }");
    QVBoxLayout *mainLayout = new QVBoxLayout(dialog); mainLayout->setContentsMargins(20, 20, 20, 20); mainLayout->setSpacing(15);
    QHBoxLayout *topBar = new QHBoxLayout();
    QPushButton *btnAll = new QPushButton("全部", dialog); QPushButton *btnTry = new QPushButton("仅试穿", dialog); QPushButton *btnGen = new QPushButton("仅生图", dialog);
    QString filterStyle = "QPushButton { background-color: #FFFFFF; color: #374151; border: 1px solid #E5E7EB; padding: 6px 16px; border-radius: 15px; font-size: 14px; } QPushButton:hover { background-color: #F3F4F6; } QPushButton:checked { background-color: #8B5CF6; color: white; border: none; }";
    btnAll->setCheckable(true); btnAll->setChecked(true); btnTry->setCheckable(true); btnGen->setCheckable(true);
    btnAll->setStyleSheet(filterStyle); btnTry->setStyleSheet(filterStyle); btnGen->setStyleSheet(filterStyle);
    topBar->addWidget(btnAll); topBar->addWidget(btnTry); topBar->addWidget(btnGen); topBar->addStretch();
    QLabel *lblCount = new QLabel("共 0 条记录", dialog); lblCount->setStyleSheet("color: #6B7280; font-size: 13px;");
    topBar->addWidget(lblCount); mainLayout->addLayout(topBar);
    QListWidget *listWidget = new QListWidget(dialog);
    listWidget->setViewMode(QListView::IconMode); listWidget->setIconSize(QSize(240, 240)); listWidget->setGridSize(QSize(260, 300));
    listWidget->setResizeMode(QListView::Adjust); listWidget->setSpacing(15);
    listWidget->setStyleSheet("QListWidget { background-color: transparent; border: none; outline: none; } QListWidget::item { border: 2px solid transparent; border-radius: 12px; background-color: white; padding: 10px; } QListWidget::item:hover { border: 2px solid #DDD6FE; } QListWidget::item:selected { border: 2px solid #8B5CF6; background-color: #F5F3FF; }");
    mainLayout->addWidget(listWidget, 1);
    QHBoxLayout *bottomBar = new QHBoxLayout();
    QPushButton *btnView = new QPushButton("👁 查看大图", dialog); QPushButton *btnExport = new QPushButton("💾 导出图片", dialog); QPushButton *btnRetry = new QPushButton("👕 再次试穿", dialog); QPushButton *btnDelete = new QPushButton("🗑 删除记录", dialog); QPushButton *btnClose = new QPushButton("关闭", dialog);
    QString actionStyle = "QPushButton { background-color: #F3F4F6; color: #374151; padding: 10px 20px; border-radius: 8px; font-size: 14px; font-weight: bold; } QPushButton:hover { background-color: #E5E7EB; } QPushButton:disabled { background-color: #F9FAFB; color: #D1D5DB; }";
    QString dangerStyle = "QPushButton { background-color: #FEE2E2; color: #DC2626; padding: 10px 20px; border-radius: 8px; font-size: 14px; font-weight: bold; } QPushButton:hover { background-color: #FECACA; } QPushButton:disabled { background-color: #F9FAFB; color: #D1D5DB; }";
    btnView->setStyleSheet(actionStyle); btnExport->setStyleSheet(actionStyle); btnRetry->setStyleSheet(actionStyle); btnDelete->setStyleSheet(dangerStyle);
    btnClose->setStyleSheet("QPushButton { background-color: transparent; color: #6B7280; font-size: 14px; border: none; } QPushButton:hover { color: #374151; }");
    btnView->setEnabled(false); btnExport->setEnabled(false); btnRetry->setEnabled(false); btnDelete->setEnabled(false);
    bottomBar->addWidget(btnView); bottomBar->addWidget(btnExport); bottomBar->addWidget(btnRetry); bottomBar->addWidget(btnDelete);
    bottomBar->addStretch(); bottomBar->addWidget(btnClose); mainLayout->addLayout(bottomBar);
    auto refreshGrid = [&](const QString &filterType = "全部") {
        listWidget->clear(); int count = 0;
        QSqlQuery query;
        if (filterType == "全部") query.exec("SELECT type, result_path, created_at FROM records ORDER BY id DESC");
        else { query.prepare("SELECT type, result_path, created_at FROM records WHERE type = :t ORDER BY id DESC"); query.bindValue(":t", filterType); query.exec(); }
        while (query.next()) {
            count++;
            QString type = query.value(0).toString(); QString resultPath = query.value(1).toString(); QString time = query.value(2).toString();
            QPixmap pix(resultPath); QPixmap roundedPix(240, 240); roundedPix.fill(Qt::transparent);
            if(!pix.isNull()) {
                QPainter painter(&roundedPix); painter.setRenderHint(QPainter::Antialiasing); painter.setBrush(QColor("#F3F4F6")); painter.setPen(Qt::NoPen);
                painter.drawRoundedRect(0, 0, 240, 240, 10, 10);
                QPixmap scaled = pix.scaled(230, 230, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                painter.drawPixmap((240 - scaled.width()) / 2, (240 - scaled.height()) / 2, scaled); painter.end();
            } else { roundedPix.fill(QColor("#E5E7EB")); }
            QString title = (type == "生图") ? "✨ 生成服装" : "👕 试穿效果";
            QListWidgetItem *item = new QListWidgetItem(QIcon(roundedPix), title + "\n" + time, listWidget);
            item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom); item->setData(Qt::UserRole, resultPath); item->setData(Qt::UserRole + 1, type);
        }
        lblCount->setText(QString("共 %1 条记录").arg(count));
    };
    refreshGrid("全部");
    connect(btnAll, &QPushButton::clicked, this, [=]() { btnAll->setChecked(true); btnTry->setChecked(false); btnGen->setChecked(false); refreshGrid("全部"); });
    connect(btnTry, &QPushButton::clicked, this, [=]() { btnAll->setChecked(false); btnTry->setChecked(true); btnGen->setChecked(false); refreshGrid("试穿"); });
    connect(btnGen, &QPushButton::clicked, this, [=]() { btnAll->setChecked(false); btnTry->setChecked(false); btnGen->setChecked(true); refreshGrid("生图"); });
    connect(listWidget, &QListWidget::itemSelectionChanged, this, [=]() {
        QListWidgetItem *cur = listWidget->currentItem();
        if (!cur) { btnView->setEnabled(false); btnExport->setEnabled(false); btnRetry->setEnabled(false); btnDelete->setEnabled(false); return; }
        btnView->setEnabled(true); btnExport->setEnabled(true); btnDelete->setEnabled(true);
        if (cur->data(Qt::UserRole + 1).toString() == "生图") { btnRetry->setEnabled(true); btnRetry->setStyleSheet("QPushButton { background-color: #8B5CF6; color: white; padding: 10px 20px; border-radius: 8px; font-size: 14px; font-weight: bold; }"); }
        else { btnRetry->setEnabled(false); btnRetry->setStyleSheet(actionStyle); }
    });
    auto doView = [=]() {
        QListWidgetItem *cur = listWidget->currentItem(); if (!cur) return;
        QString path = cur->data(Qt::UserRole).toString();
        dialog->accept();
        resetToHomeScreen();
        m_isViewingHistoryImage = true;
        m_historyImgPath = path;
        QPixmap pix(path);
        lblImage->setPixmap(pix.scaled(lblImage->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        currentResultPixmap = pix;
        lblStatus->setText("🖼 正在查看历史大图，请选择返回或使用此图试穿。");
        lblStatus->setStyleSheet("QLabel { background-color: #EFF6FF; color: #2563EB; font-size: 14px; border-radius: 8px; padding: 10px; }");
        btnSmartAction->setVisible(false);
        bottomSubRowWidget->setVisible(false);
        btnBackToHistory->setVisible(true);
        btnUseHistoryImg->setVisible(true);
    };
    connect(btnView, &QPushButton::clicked, doView);
    connect(listWidget, &QListWidget::itemDoubleClicked, this, doView);
    connect(btnExport, &QPushButton::clicked, this, [=]() {
        QListWidgetItem *cur = listWidget->currentItem(); if (!cur) return;
        QString srcPath = cur->data(Qt::UserRole).toString();
        QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/export_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".png";
        QString savePath = QFileDialog::getSaveFileName(dialog, "导出图片", defaultPath, "图片 (*.png *.jpg)");
        if (!savePath.isEmpty() && QFile::copy(srcPath, savePath)) QMessageBox::information(dialog, "成功", "图片已导出！");
    });
    connect(btnRetry, &QPushButton::clicked, this, [=]() {
        QListWidgetItem *cur = listWidget->currentItem(); if (!cur) return;
        QString path = cur->data(Qt::UserRole).toString();
        currentShirtPath = path; lastGeneratedImagePath = path;
        if (currentBodyPath.isEmpty() || currentMaskPath.isEmpty()) { QMessageBox::warning(dialog, "缺少信息", "缺少人物图或遮罩，请重新开始试穿流程。"); dialog->accept(); return; }
        dialog->accept(); currentStage = 3; updateSmartUI();
        lblStatus->setText("⏳ 正在使用历史服装重新试穿，请稍候..."); lblStatus->setStyleSheet("QLabel { background-color: #FEF3C7; color: #92400E; font-size: 14px; border-radius: 8px; padding: 10px; }");
        ensureComfyUIRunning([=]() { freeComfyMemory(); doTryOn(currentBodyPath, currentMaskPath); });
    });
    connect(btnDelete, &QPushButton::clicked, this, [=]() {
        QListWidgetItem *cur = listWidget->currentItem(); if (!cur) return;
        QString path = cur->data(Qt::UserRole).toString();
        QMessageBox ask(dialog); ask.setWindowTitle("确认删除"); ask.setText("确定要删除这条历史记录吗？"); ask.setInformativeText("是否同时删除本地图片文件？");
        QPushButton *btnYesDelFile = ask.addButton("删除记录和图片", QMessageBox::DestructiveRole);
        QPushButton *btnJustDb = ask.addButton("仅删除记录", QMessageBox::YesRole);
        ask.addButton("取消", QMessageBox::RejectRole); ask.exec();
        bool deleteFile = false; if (ask.clickedButton() == btnYesDelFile) deleteFile = true; else if (ask.clickedButton() != btnJustDb) return;
        QSqlQuery q; q.prepare("DELETE FROM records WHERE result_path = :path"); q.bindValue(":path", path);
        if (q.exec()) { if (deleteFile) QFile::remove(path); if (btnAll->isChecked()) refreshGrid("全部"); else if (btnTry->isChecked()) refreshGrid("试穿"); else refreshGrid("生图"); }
    });
    connect(btnClose, &QPushButton::clicked, dialog, &QDialog::accept);
    dialog->exec(); delete dialog;
}

void MainWindow::showComparisonDialog() {
    QString beforePath = currentBodyPath;
    QString afterPath = lastGeneratedImagePath;
    if (!QFile::exists(beforePath) || !QFile::exists(afterPath)) return;

    // 直接使用 QImage 加载，规避 OpenCV 的拼接拉伸
    QImage imgBefore(beforePath);
    QImage imgAfter(afterPath);
    if (imgBefore.isNull() || imgAfter.isNull()) return;

    int canvasW = 1000;
    int canvasH = 600;
    int halfW = canvasW / 2;

    QImage canvas(canvasW, canvasH, QImage::Format_RGB32);
    canvas.fill(Qt::white);
    QPainter p(&canvas);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // 左侧：等比缩放并居中绘制原图
    QImage scaledBefore = imgBefore.scaled(halfW - 20, canvasH - 20, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    int x1 = (halfW - scaledBefore.width()) / 2;
    int y1 = (canvasH - scaledBefore.height()) / 2;
    p.drawImage(x1, y1, scaledBefore);

    // 右侧：等比缩放并居中绘制试穿图
    QImage scaledAfter = imgAfter.scaled(halfW - 20, canvasH - 20, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    int x2 = halfW + (halfW - scaledAfter.width()) / 2;
    int y2 = (canvasH - scaledAfter.height()) / 2;
    p.drawImage(x2, y2, scaledAfter);

    // 画中间的紫色分隔线
    p.setPen(QPen(QColor(139, 92, 246), 4));
    p.drawLine(halfW, 0, halfW, canvasH);
    p.end();

    QDialog dlg(this); dlg.setWindowTitle("试穿前后对比"); dlg.resize(1050, 700);
    QVBoxLayout *layout = new QVBoxLayout(&dlg);
    QLabel *title = new QLabel("左：原图      右：试穿效果", &dlg);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet("font-size: 15px; font-weight: bold; color: #8B5CF6; padding: 10px;");
    layout->addWidget(title);

    QLabel *imgLabel = new QLabel(&dlg);
    imgLabel->setAlignment(Qt::AlignCenter);
    imgLabel->setPixmap(QPixmap::fromImage(canvas));
    layout->addWidget(imgLabel, 1);

    QHBoxLayout *btns = new QHBoxLayout();
    QPushButton *btnSave = new QPushButton("💾 保存对比图", &dlg);
    QPushButton *btnOk = new QPushButton("✅ 完成", &dlg);
    QString btnStyle = "QPushButton { background-color: #8B5CF6; color: white; padding: 10px 20px; border-radius: 6px; font-size: 14px; } QPushButton:hover { background-color: #7C3AED; }";
    btnSave->setStyleSheet(btnStyle); btnOk->setStyleSheet(btnStyle);

    connect(btnSave, &QPushButton::clicked, [&]() {
        QString savePath = QFileDialog::getSaveFileName(&dlg, "保存", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/compare_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".png", "PNG (*.png)");
        if (!savePath.isEmpty()) {
            canvas.save(savePath);
            QMessageBox::information(&dlg, "已保存", savePath);
        }
    });
    connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);

    btns->addStretch(); btns->addWidget(btnSave); btns->addWidget(btnOk); btns->addStretch();
    layout->addLayout(btns);
    dlg.exec();
}

void MainWindow::onBackToHistoryClicked() { resetToHomeScreen(); on_btnHistory_clicked(); }

void MainWindow::onUseHistoryImageClicked() {
    if (m_historyImgPath.isEmpty()) return;
    currentShirtPath = m_historyImgPath; lastGeneratedImagePath = "";
    resetToHomeScreen();
    lblStatus->setText("✅ 已选择历史服装，请点击【开始试穿】！");
    lblStatus->setStyleSheet("QLabel { background-color: #D1FAE5; color: #065F46; font-size: 14px; border-radius: 8px; padding: 10px; }");
}

// ==================== 翻译辅助与其他 ====================
void MainWindow::initDatabase() {
    db = QSqlDatabase::addDatabase("QSQLITE"); db.setDatabaseName(QDir::currentPath() + "/hy_data.db");
    if (db.open()) { QSqlQuery q; q.exec("CREATE TABLE IF NOT EXISTS records (id INTEGER PRIMARY KEY AUTOINCREMENT, type TEXT, prompt TEXT, result_path TEXT, created_at TEXT)"); }
}

void MainWindow::insertRecord(const QString &type, const QString &prompt, const QString &resultPath) {
    QSqlQuery q; q.prepare("INSERT INTO records (type, prompt, result_path, created_at) VALUES (:t, :p, :r, :c)");
    q.bindValue(":t", type); q.bindValue(":p", prompt); q.bindValue(":r", resultPath); q.bindValue(":c", QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss")); q.exec();
}

void MainWindow::showFriendlyError(const QString &t, const QString &d) {
    QString friendlyMsg = d;
    if (d.contains("ran out of memory") || d.contains("OOM")) friendlyMsg = "显存不足，请关闭其他占用显卡的软件。";
    else if (d.contains("Connection refused") || d.contains("timed out")) friendlyMsg = "无法连接 AI 引擎，请检查后台是否运行。";
    lblStatus->setText("❌ " + friendlyMsg);
    lblStatus->setStyleSheet("QLabel { background-color: #FEE2E2; color: #991B1B; font-size: 14px; border-radius: 8px; padding: 10px; }");
    QTimer::singleShot(4000, this, [=]() { updateSmartUI(); });
}

bool MainWindow::containsChinese(const QString &text) { for (QChar c : text) if (c.unicode() >= 0x4E00 && c.unicode() <= 0x9FFF) return true; return false; }

void MainWindow::translateToEnglish(const QString &chinese) { showCenterLoading("🌐 正在翻译描述词", "正在连接翻译服务..."); tryMyMemoryTranslate(chinese); }

void MainWindow::tryMyMemoryTranslate(const QString &text) {
    QUrl url("https://api.mymemory.translated.net/get"); QUrlQuery query;
    query.addQueryItem("q", text); query.addQueryItem("langpair", "zh-CN|en"); url.setQuery(query);
    QNetworkReply *reply = manager->get(QNetworkRequest(url));
    connect(reply, &QNetworkReply::finished, this, [=]() {
        reply->deleteLater();
        QString trans = QJsonDocument::fromJson(reply->readAll()).object()["responseData"].toObject()["translatedText"].toString();
        if (!trans.isEmpty() && !trans.contains("WARNING")) startGenerate(trans);
        else tryYoudaoTranslate(text);
    });
}

void MainWindow::tryYoudaoTranslate(const QString &text) {
    QNetworkReply *reply = manager->get(QNetworkRequest(QUrl("https://fanyi.youdao.com/translate?&doctype=json&type=AUTO&i=" + QUrl::toPercentEncoding(text))));
    connect(reply, &QNetworkReply::finished, this, [=]() {
        reply->deleteLater();
        QString trans; QJsonArray arr = QJsonDocument::fromJson(reply->readAll()).object()["translateResult"].toArray();
        if (!arr.isEmpty()) trans = arr[0].toArray()[0].toObject()["tgt"].toString();
        if (!trans.isEmpty()) startGenerate(trans);
        else { pendingAutoTryOn = false; currentStage=0; updateSmartUI(); lblStatus->setText("⚠️ 翻译服务全线超时，请直接输入英文描述。"); }
    });
}

void MainWindow::setLoading(bool loading) {
    if (loading) { loadingDots = 0; loadingBaseText = "⏳ AI 正在处理"; loadingTimer->start(300); } else loadingTimer->stop();
}

void MainWindow::updateLoadingText() {
    loadingDots = (loadingDots + 1) % 4; lblImage->setText(loadingBaseText + QString(".").repeated(loadingDots));
}

void MainWindow::on_btnSaveResult_clicked() {
    if (currentResultPixmap.isNull()) { QMessageBox::warning(this, "无结果", "当前没有可保存的结果图。"); return; }
    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) + "/huanyi_result_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss") + ".png";
    QString savePath = QFileDialog::getSaveFileName(this, "保存结果图", defaultPath, "PNG 图片 (*.png);;JPG 图片 (*.jpg)");
    if (!savePath.isEmpty() && currentResultPixmap.save(savePath)) QMessageBox::information(this, "保存成功", "已保存到：\n" + savePath);
}

QString MainWindow::findLatestMask() { return currentMaskPath; }

QString MainWindow::detectCategoryFromPrompt(const QString &prompt) {
    QString lower = prompt.toLower();
    if (lower.contains("shirt") || lower.contains("衬衫") || lower.contains("衬衣") || lower.contains("blouse")) return "衬衫";
    if (lower.contains("dress") || lower.contains("连衣裙") || lower.contains("裙")) return "连衣裙";
    if (lower.contains("jacket") || lower.contains("coat") || lower.contains("外套") ||
        lower.contains("夹克") || lower.contains("大衣") || lower.contains("风衣") ||
        lower.contains("卫衣") || lower.contains("sweatshirt") || lower.contains("hoodie")) return "外套";
    if (lower.contains("t-shirt") || lower.contains("tshirt") || lower.contains("t恤") || lower.contains("短袖")) return "T恤";
    return "其他";
}