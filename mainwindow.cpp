#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "maskeditordialog.h"

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
#include <QTableView>
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

// ==================== 构造函数 ====================
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    setupLayout();
    setupMenuBar();

    QFile qssFile("C:/Users/31593/Documents/hy/style.qss");
    if (qssFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString qss = QString::fromUtf8(qssFile.readAll());
        this->setStyleSheet(qss);
        qssFile.close();
    }

    QIcon icon("C:/Users/31593/Documents/hy/images/app_icon.png");
    if (!icon.isNull()) this->setWindowIcon(icon);

    QShortcut *shortcut = new QShortcut(QKeySequence("Ctrl+Return"), this);
    connect(shortcut, &QShortcut::activated, this, &MainWindow::on_btnGenerate_clicked);

    initDatabase();

    currentBodyPath = "images/body.jpg";
    currentMaskPath = "";

    loadingTimer = new QTimer(this);
    loadingDots = 0;
    connect(loadingTimer, &QTimer::timeout, this, &MainWindow::updateLoadingText);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [=]() {
        cv::Mat frame;
        cap >> frame;
        if (frame.empty()) return;

        cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);
        QImage img(frame.data, frame.cols, frame.rows,
                   frame.step, QImage::Format_RGB888);

        QPixmap pix = QPixmap::fromImage(img).scaled(
            ui->labelImage->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);

        ui->labelImage->setPixmap(pix);
        currentResultPixmap = pix;
    });

    manager = new QNetworkAccessManager(this);
    connect(manager, &QNetworkAccessManager::finished,
            this, &MainWindow::onReplyFinished);

    pollTimer = new QTimer(this);
    connect(pollTimer, &QTimer::timeout, this, &MainWindow::pollHistory);

    updateSmartUI();
}

// ==================== 析构 ====================
MainWindow::~MainWindow()
{
    if (cap.isOpened()) cap.release();
    if (db.isOpen()) db.close();
    delete ui;
}

// ==================== ComfyUI 状态检查 ====================
bool MainWindow::isComfyRunning()
{
    QNetworkAccessManager mgr;
    QNetworkRequest req(QUrl("http://127.0.0.1:8188/system_stats"));
    req.setTransferTimeout(1000);

    QNetworkReply *reply = mgr.get(req);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    return ok;
}

// ==================== 释放显存 ====================
void MainWindow::freeComfyMemory()
{
    qDebug() << "🧹 请求 ComfyUI 释放显存...";

    QNetworkRequest request(QUrl("http://127.0.0.1:8188/free"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    QJsonObject body;
    body["unload_models"] = true;
    body["free_memory"] = true;

    QNetworkReply *reply = manager->post(request, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [=]() {
        qDebug() << "✅ ComfyUI 显存已释放";
        reply->deleteLater();
    });

    QEventLoop loop;
    QTimer::singleShot(1500, &loop, &QEventLoop::quit);
    loop.exec();
}

// ==================== 启动 ComfyUI（--novram 模式） ====================
bool MainWindow::startComfyProcess()
{
    QString pythonPath = "D:/ComfyUI-aki/ComfyUI-aki-v3/python/python.exe";
    QString workingDir = "D:/ComfyUI-aki/ComfyUI-aki-v3";
    QString mainPy = "ComfyUI/main.py";

    if (!QFile::exists(pythonPath)) {
        QMessageBox::warning(this, "错误",
                             "找不到 Python 解释器：\n" + pythonPath);
        return false;
    }

    if (comfyProcess) delete comfyProcess;
    comfyProcess = new QProcess(this);
    comfyProcess->setWorkingDirectory(workingDir);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("HF_ENDPOINT", "https://hf-mirror.com");
    env.insert("HF_HUB_ENABLE_HF_TRANSFER", "1");
    comfyProcess->setProcessEnvironment(env);

    connect(comfyProcess, &QProcess::readyReadStandardOutput, this, [=]() {
        QByteArray out = comfyProcess->readAllStandardOutput();
        qDebug() << "[ComfyUI]" << QString::fromUtf8(out).trimmed();
    });
    connect(comfyProcess, &QProcess::readyReadStandardError, this, [=]() {
        QByteArray out = comfyProcess->readAllStandardError();
        qDebug() << "[ComfyUI ERR]" << QString::fromUtf8(out).trimmed();
    });

    QStringList args;
    args << "-s" << mainPy
         << "--listen"
         << "--windows-standalone-build"
         << "--novram";

    comfyProcess->start(pythonPath, args);
    return comfyProcess->waitForStarted(5000);
}

// ==================== 确保 ComfyUI 运行 ====================
void MainWindow::ensureComfyUIRunning(std::function<void()> callback)
{
    if (isComfyRunning()) {
        qDebug() << "✅ ComfyUI 已运行";
        callback();
        return;
    }

    int ret = QMessageBox::question(this, "ComfyUI 未启动",
                                    "检测到 ComfyUI 未运行。\n\n是否自动启动 ComfyUI？\n"
                                    "（首次启动约 2~3 分钟，已启用最省显存模式）",
                                    QMessageBox::Yes | QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    ui->labelImage->clear();
    ui->labelImage->setText("⏳ ComfyUI 正在启动，请稍候...\n（--novram 模式，速度较慢但更稳）");
    ui->labelImage->repaint();
    QApplication::processEvents();

    if (!startComfyProcess()) {
        QMessageBox::warning(this, "启动失败",
                             "无法启动 ComfyUI 进程，请检查路径。");
        return;
    }

    QTimer *checkTimer = new QTimer(this);
    int *retryCount = new int(0);
    int maxRetries = 180;

    connect(checkTimer, &QTimer::timeout, this, [=]() mutable {
        (*retryCount)++;

        if (isComfyRunning()) {
            checkTimer->stop();
            checkTimer->deleteLater();
            delete retryCount;

            ui->labelImage->clear();
            ui->labelImage->setText("✅ ComfyUI 已就绪，开始处理...");
            ui->labelImage->repaint();
            QApplication::processEvents();

            QTimer::singleShot(500, this, callback);
            return;
        }

        if (*retryCount >= maxRetries) {
            checkTimer->stop();
            checkTimer->deleteLater();
            delete retryCount;

            QMessageBox::warning(this, "启动超时",
                                 "ComfyUI 超过 3 分钟未响应。\n"
                                 "请检查“应用程序输出”面板的 [ComfyUI] 日志。");
            return;
        }

        ui->labelImage->setText(
            QString("⏳ ComfyUI 正在启动，请耐心等待...\n"
                    "已等待 %1 秒 / 最多 180 秒")
                .arg(*retryCount));
    });

    checkTimer->start(1000);
}

// ==================== 菜单项 ====================
void MainWindow::onStartComfyUI()
{
    ensureComfyUIRunning([=]() {
        QMessageBox::information(this, "成功",
                                 "ComfyUI 已就绪。\n浏览器访问：http://127.0.0.1:8188");
    });
}

void MainWindow::onStopComfyUI()
{
    if (!isComfyRunning()) {
        QMessageBox::information(this, "提示", "ComfyUI 当前未运行。");
        return;
    }

    int ret = QMessageBox::question(this, "确认",
                                    "确定要停止 ComfyUI 吗？",
                                    QMessageBox::Yes | QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    if (comfyProcess && comfyProcess->state() != QProcess::NotRunning) {
        comfyProcess->terminate();
        if (!comfyProcess->waitForFinished(5000)) {
            comfyProcess->kill();
        }
    } else {
        QProcess::execute("taskkill", QStringList() << "/F" << "/IM" << "python.exe");
    }

    ui->labelImage->clear();
    ui->labelImage->setText("⏹ ComfyUI 已停止");
}

void MainWindow::onCheckComfyUI()
{
    if (isComfyRunning()) {
        QMessageBox::information(this, "ComfyUI 状态",
                                 "✅ ComfyUI 正在运行\n端口：8188");
    } else {
        QMessageBox::warning(this, "ComfyUI 状态",
                             "❌ ComfyUI 未运行\n\n点击任意生成按钮时会自动提示启动。");
    }
}

void MainWindow::onRestoreDefaultBody()
{
    QMessageBox::information(this, "提示",
                             "请使用【开始试穿】流程，程序会自动使用最新抓拍的人物图。");
}

// ==================== 菜单栏 ====================
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

    comfyMenu->addSeparator();

    QAction *actCheck = comfyMenu->addAction("🔍 检查状态");
    connect(actCheck, &QAction::triggered, this, &MainWindow::onCheckComfyUI);

    QMenu *toolMenu = menuBar->addMenu("工具(&T)");

    QAction *actClear = toolMenu->addAction("🧹 清除所有缓存");
    connect(actClear, &QAction::triggered, this, [=]() {
        currentBodyPath = "images/body.jpg";
        currentMaskPath = "";
        lastGeneratedImagePath = "";
        currentShirtPath = "";
        lastBodyPathForMask = "";
        lastMaskForReuse = "";
        currentPromptForRegen = "";
        currentStage = 0;
        pendingTryOnAfterGenerate = false;
        updateSmartUI();
        ui->labelImage->setText("🧹 缓存已清除\n\n点击按钮重新开始");
        QMessageBox::information(this, "完成", "已清除人物/遮罩/服装缓存。");
    });

    toolMenu->addSeparator();

    QAction *actHttpTest = toolMenu->addAction("🌐 测试 HTTP");
    connect(actHttpTest, &QAction::triggered, this, &MainWindow::on_btnhttp_clicked);

    QMenu *helpMenu = menuBar->addMenu("帮助(&H)");

    QAction *actAbout = helpMenu->addAction("ℹ️ 关于");
    connect(actAbout, &QAction::triggered, this, [=]() {
        QMessageBox::about(this, "关于",
                           "<h2>幻衣 - AI 虚拟试穿系统</h2>"
                           "<p>基于 Qt + OpenCV + ComfyUI + CatVTON 开发。</p>"
                           "<p><b>使用流程：</b><br>"
                           "1. 点击【开始试穿】<br>"
                           "2. 选择人物来源（抓拍 / 默认）<br>"
                           "3. 涂抹需要替换的衣服区域<br>"
                           "4. 选择服装来源（AI生成 / 已有服装）<br>"
                           "5. AI 生成完成后可选择【满意并试穿】或【重新生成】<br>"
                           "6. 试穿完成查看前后对比图</p>"
                           "<p><b>已启用最省显存模式（--novram）</b></p>");
    });
}

// ==================== 极简布局 ====================
void MainWindow::setupLayout()
{
    QWidget *central = centralWidget();
    if (central->layout()) delete central->layout();

    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(12);

    ui->labelImage->setMinimumHeight(420);
    ui->labelImage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->labelImage->setAlignment(Qt::AlignCenter);
    ui->labelImage->setStyleSheet(
        "QLabel { background-color: #FAFAFA;"
        "         border: 1px solid #E5E7EB; border-radius: 12px;"
        "         color: #6B7280; font-size: 15px; }");
    ui->labelImage->setText("👋 欢迎使用幻衣\n\n点击下方按钮开始体验 AI 试穿");
    mainLayout->addWidget(ui->labelImage, 1);

    ui->lineEditPrompt->setVisible(false);
    ui->btnGenerate->setVisible(false);
    ui->btnTryOn->setVisible(false);
    ui->btnClick->setVisible(false);
    ui->btnHistory->setVisible(false);
    ui->btnhttp->setVisible(false);

    lblStatus = new QLabel(central);
    lblStatus->setAlignment(Qt::AlignCenter);
    lblStatus->setMinimumHeight(48);
    lblStatus->setStyleSheet(
        "QLabel { background-color: #EFF6FF; color: #2563EB;"
        "         font-size: 14px; border-radius: 8px; padding: 10px; }");
    lblStatus->setText("💡 现在请：点击下方按钮开始试穿");
    mainLayout->addWidget(lblStatus);

    btnSmartAction = new QPushButton("🚀 开始试穿", central);
    btnSmartAction->setMinimumHeight(64);
    btnSmartAction->setStyleSheet(
        "QPushButton {"
        "   background-color: #8B5CF6; color: white;"
        "   font-weight: bold; font-size: 17px; border-radius: 10px;"
        "}"
        "QPushButton:hover   { background-color: #7C3AED; }"
        "QPushButton:pressed { background-color: #6D28D9; }"
        "QPushButton:disabled{ background-color: #D1D5DB; }");
    connect(btnSmartAction, &QPushButton::clicked,
            this, &MainWindow::onSmartActionClicked);
    mainLayout->addWidget(btnSmartAction);

    QHBoxLayout *subRow = new QHBoxLayout();
    subRow->setSpacing(10);

    QPushButton *btnStop = new QPushButton("⏹ 停止当前任务", central);
    btnStop->setMinimumHeight(46);
    btnStop->setStyleSheet(
        "QPushButton { background-color: #F3F4F6; color: #374151;"
        "   font-size: 14px; border-radius: 8px; }"
        "QPushButton:hover { background-color: #E5E7EB; }");
    connect(btnStop, &QPushButton::clicked, this, [=]() {
        if (pollTimer->isActive()) {
            pollTimer->stop();
            pollCount = 0;
            setLoading(false);
            pendingAutoTryOn = false;
            pendingTryOnAfterGenerate = false;
            currentStage = 0;
            updateSmartUI();
            lblStatus->setText("⏹ 任务已手动停止");
            lblStatus->setStyleSheet(
                "QLabel { background-color: #F3F4F6; color: #6B7280;"
                "         font-size: 14px; border-radius: 8px; padding: 10px; }");
            qDebug() << "⏹ 用户停止任务";
        } else if (pendingTryOnAfterGenerate) {
            pendingTryOnAfterGenerate = false;
            currentStage = 0;
            updateSmartUI();
            lblStatus->setText("⏹ 已取消试穿，可点击按钮重新开始");
            lblStatus->setStyleSheet(
                "QLabel { background-color: #F3F4F6; color: #6B7280;"
                "         font-size: 14px; border-radius: 8px; padding: 10px; }");
        } else {
            QMessageBox::information(this, "提示", "当前没有正在执行的任务。");
        }
    });
    subRow->addWidget(btnStop, 1);

    QPushButton *btnHistory = new QPushButton("📋 历史记录", central);
    btnHistory->setMinimumHeight(46);
    btnHistory->setStyleSheet(
        "QPushButton { background-color: #F3F4F6; color: #374151;"
        "   font-size: 14px; border-radius: 8px; }"
        "QPushButton:hover { background-color: #E5E7EB; }");
    connect(btnHistory, &QPushButton::clicked,
            this, &MainWindow::on_btnHistory_clicked);
    subRow->addWidget(btnHistory, 1);

    mainLayout->addLayout(subRow);

    this->setWindowTitle("幻衣 - AI 虚拟试穿系统");
    this->setMinimumSize(900, 720);
}

// ==================== 智能 UI 刷新 ====================
void MainWindow::updateSmartUI()
{
    if (!btnSmartAction || !lblStatus) return;

    switch (currentStage) {
    case 0:
        if (pendingTryOnAfterGenerate) {
            btnSmartAction->setText("👕 继续试穿");
            btnSmartAction->setEnabled(true);
            lblStatus->setText("✅ 服装已生成！查看效果后点击【继续试穿】");
            lblStatus->setStyleSheet(
                "QLabel { background-color: #D1FAE5; color: #065F46;"
                "         font-size: 14px; border-radius: 8px; padding: 10px; }");
        } else {
            btnSmartAction->setText("🚀 开始试穿");
            btnSmartAction->setEnabled(true);
            lblStatus->setText("💡 现在请：点击下方按钮开始试穿");
            lblStatus->setStyleSheet(
                "QLabel { background-color: #EFF6FF; color: #2563EB;"
                "         font-size: 14px; border-radius: 8px; padding: 10px; }");
        }
        break;

    case 3:
        btnSmartAction->setText("⏳ 正在处理，请稍候...");
        btnSmartAction->setEnabled(false);
        break;
    }
}

// ==================== 智能按钮点击 ====================
void MainWindow::onSmartActionClicked()
{
    // ★ 优先检查：是否有待试穿的服装
    if (pendingTryOnAfterGenerate) {
        pendingTryOnAfterGenerate = false;
        currentStage = 3;
        updateSmartUI();
        lblStatus->setText("⏳ 正在试穿，请稍候...");
        lblStatus->setStyleSheet(
            "QLabel { background-color: #FEF3C7; color: #92400E;"
            "         font-size: 14px; border-radius: 8px; padding: 10px; }");

        freeComfyMemory();
        QTimer::singleShot(3000, this, [=]() {
            doTryOn(currentBodyPath, pendingMaskPath);
        });
        return;
    }

    if (currentStage == 3) return;

    QMessageBox box(this);
    box.setWindowTitle("开始试穿");
    box.setIcon(QMessageBox::Question);
    box.setText("请选择人物图片来源：");
    box.setInformativeText("📸 实时抓拍：打开摄像头拍摄\n"
                           "🖼 使用默认图：直接使用 images/body.jpg");

    QPushButton *btnCapture = box.addButton("📸 实时抓拍", QMessageBox::YesRole);
    QPushButton *btnDefault = box.addButton("🖼 使用默认图", QMessageBox::NoRole);
    QPushButton *btnCancel  = box.addButton("取消", QMessageBox::RejectRole);
    box.setDefaultButton(btnCapture);
    box.exec();

    if (box.clickedButton() == btnCancel) {
        return;
    }

    if (box.clickedButton() == btnCapture) {
        cap.open(0);
        if (!cap.isOpened()) {
            QMessageBox::warning(this, "错误", "无法打开摄像头，请检查设备。");
            return;
        }
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 640);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 480);
        timer->start(30);

        lblStatus->setText("💡 摄像头已开启，站好后再次点击【抓拍并继续】");
        lblStatus->setStyleSheet(
            "QLabel { background-color: #FEF3C7; color: #92400E;"
            "         font-size: 14px; border-radius: 8px; padding: 10px; }");
        btnSmartAction->setText("📸 抓拍并继续");

        disconnect(btnSmartAction, &QPushButton::clicked,
                   this, &MainWindow::onSmartActionClicked);
        connect(btnSmartAction, &QPushButton::clicked, this, [=]() {
            disconnect(btnSmartAction, &QPushButton::clicked, nullptr, nullptr);
            connect(btnSmartAction, &QPushButton::clicked,
                    this, &MainWindow::onSmartActionClicked);

            on_btnCapture_clicked();
        });
        return;
    }

    // ============ 使用默认图 ============
    if (box.clickedButton() == btnDefault) {
        QString defaultBody = "images/body.jpg";
        if (!QFile::exists(defaultBody)) {
            QMessageBox::warning(this, "错误",
                                 "找不到默认人物图：\n" + defaultBody +
                                     "\n\n请先准备好一张人物照片放到该路径。");
            return;
        }
        currentBodyPath = defaultBody;
        qDebug() << "🖼 使用默认人物图:" << currentBodyPath;

        QPixmap pix(defaultBody);
        if (!pix.isNull()) {
            ui->labelImage->setPixmap(pix.scaled(
                ui->labelImage->size(),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation));
        }

        // ★ 检查是否可以复用上次遮罩
        bool reuseMask = false;
        if (!lastMaskForReuse.isEmpty() && QFile::exists(lastMaskForReuse)
            && lastBodyPathForMask == defaultBody) {
            int ret = QMessageBox::question(this, "复用上次遮罩？",
                                            "检测到上次使用默认人物图时已画过遮罩。\n"
                                            "是否直接复用该遮罩，跳过涂抹步骤？",
                                            QMessageBox::Yes | QMessageBox::No);
            if (ret == QMessageBox::Yes) {
                currentMaskPath = lastMaskForReuse;
                reuseMask = true;
                qDebug() << "♻️ 复用上次遮罩:" << currentMaskPath;
            }
        }

        if (!reuseMask) {
            lblStatus->setText("✏️ 第 1 步：涂抹需要替换的衣服区域");
            lblStatus->setStyleSheet(
                "QLabel { background-color: #FEF3C7; color: #92400E;"
                "         font-size: 14px; border-radius: 8px; padding: 10px; }");

            QString ts = QDateTime::currentDateTime().toString("yyyyMMddhhmmss");

            QMessageBox askMask(this);
            askMask.setWindowTitle("第 1 步 / 共 2 步");
            askMask.setIcon(QMessageBox::Information);
            askMask.setText("✏️ 请涂抹需要替换的衣服区域");
            askMask.setInformativeText(
                "接下来会打开涂抹窗口。\n"
                "用鼠标涂抹人物身上想要替换成新衣服的部位（如躯干、肩膀）。");
            askMask.addButton("开始涂抹", QMessageBox::AcceptRole);
            askMask.exec();

            MaskEditorDialog dlg(defaultBody, this);
            if (dlg.exec() != QDialog::Accepted) {
                updateSmartUI();
                ui->labelImage->setText("✅ 已取消涂抹\n\n可点击按钮重新开始");
                return;
            }
            cv::Mat mask = dlg.getMask();
            if (mask.empty() || cv::countNonZero(mask) == 0) {
                QMessageBox::warning(this, "未涂抹", "您没有涂抹任何区域。");
                updateSmartUI();
                return;
            }

            cv::Mat frame = cv::imread(defaultBody.toStdString());
            if (frame.empty()) {
                QMessageBox::warning(this, "错误", "无法读取默认人物图");
                updateSmartUI();
                return;
            }

            cv::Mat rgba;
            cv::cvtColor(frame, rgba, cv::COLOR_BGR2BGRA);
            for (int y = 0; y < rgba.rows; ++y)
                for (int x = 0; x < rgba.cols; ++x)
                    rgba.at<cv::Vec4b>(y, x)[3] = 255 - mask.at<uchar>(y, x);

            QString maskFile = QString("images/captured_mask_%1.png").arg(ts);
            cv::imwrite(maskFile.toStdString(), rgba);
            currentMaskPath = maskFile;

            // ★ 记录供下次复用
            lastBodyPathForMask = defaultBody;
            lastMaskForReuse = maskFile;

            qDebug() << "💾 遮罩已保存:" << currentMaskPath;

            doAfterMaskReady(frame, defaultBody, ts);
        } else {
            // 复用路径：直接进入服装选择
            doAfterMaskReady(cv::Mat(), defaultBody,
                             QDateTime::currentDateTime().toString("yyyyMMddhhmmss"));
        }
    }
}

// ==================== 抓拍流程 ====================
void MainWindow::on_btnCapture_clicked()
{
    if (!cap.isOpened()) {
        QMessageBox::information(this, "提示", "请先点击【开始试穿】选择实时抓拍。");
        return;
    }

    cv::Mat frame;
    cap >> frame;
    if (frame.empty()) {
        QMessageBox::warning(this, "错误", "抓取画面失败，请重试。");
        return;
    }

    timer->stop();
    cap.release();

    if (frame.cols > 512 || frame.rows > 512) {
        double scale = 512.0 / std::max(frame.cols, frame.rows);
        cv::resize(frame, frame, cv::Size(), scale, scale);
    }

    QString ts = QDateTime::currentDateTime().toString("yyyyMMddhhmmss");
    QString bodyFile = QString("images/captured_body_%1.jpg").arg(ts);
    cv::imwrite(bodyFile.toStdString(), frame);
    currentBodyPath = bodyFile;
    currentMaskPath = "";

    qDebug() << "📸 抓拍成功:" << currentBodyPath;

    // ★ 相似度检测：与上次人物图对比，> 0.85 弹窗询问复用
    bool reuseMask = false;
    if (!lastMaskForReuse.isEmpty() && QFile::exists(lastMaskForReuse)
        && !lastBodyPathForMask.isEmpty() && QFile::exists(lastBodyPathForMask)) {
        cv::Mat oldBody = cv::imread(lastBodyPathForMask.toStdString());
        cv::Mat newBody = cv::imread(bodyFile.toStdString());
        if (!oldBody.empty() && !newBody.empty()) {
            if (oldBody.size() != newBody.size()) {
                cv::resize(oldBody, oldBody, newBody.size());
            }
            double sim = imageSimilarity(oldBody, newBody);
            qDebug() << "人物相似度:" << sim;

            if (sim > 0.85) {
                int ret = QMessageBox::question(this, "复用上次遮罩？",
                                                QString("本次抓拍与上次相似度较高（%1%）。\n"
                                                        "是否直接复用上次遮罩，跳过涂抹步骤？")
                                                    .arg(int(sim * 100)),
                                                QMessageBox::Yes | QMessageBox::No);
                if (ret == QMessageBox::Yes) {
                    currentMaskPath = lastMaskForReuse;
                    reuseMask = true;
                    qDebug() << "♻️ 复用上次遮罩:" << currentMaskPath;
                }
            }
        }
    }

    if (reuseMask) {
        doAfterMaskReady(frame, bodyFile, ts);
        return;
    }

    lblStatus->setText("✏️ 第 1 步：涂抹需要替换的衣服区域");
    lblStatus->setStyleSheet(
        "QLabel { background-color: #FEF3C7; color: #92400E;"
        "         font-size: 14px; border-radius: 8px; padding: 10px; }");

    QMessageBox askMask(this);
    askMask.setWindowTitle("第 1 步 / 共 2 步");
    askMask.setIcon(QMessageBox::Information);
    askMask.setText("✏️ 请涂抹需要替换的衣服区域");
    askMask.setInformativeText(
        "接下来会打开涂抹窗口。\n"
        "用鼠标涂抹人物身上想要替换成新衣服的部位（如躯干、肩膀）。");
    askMask.addButton("开始涂抹", QMessageBox::AcceptRole);
    askMask.exec();

    MaskEditorDialog dlg(bodyFile, this);
    if (dlg.exec() != QDialog::Accepted) {
        updateSmartUI();
        ui->labelImage->setText("✅ 已取消涂抹\n\n可点击按钮重新开始");
        return;
    }
    cv::Mat mask = dlg.getMask();
    if (mask.empty() || cv::countNonZero(mask) == 0) {
        QMessageBox::warning(this, "未涂抹", "您没有涂抹任何区域。");
        updateSmartUI();
        return;
    }

    cv::Mat rgba;
    cv::cvtColor(frame, rgba, cv::COLOR_BGR2BGRA);
    for (int y = 0; y < rgba.rows; ++y)
        for (int x = 0; x < rgba.cols; ++x)
            rgba.at<cv::Vec4b>(y, x)[3] = 255 - mask.at<uchar>(y, x);

    QString maskFile = QString("images/captured_mask_%1.png").arg(ts);
    cv::imwrite(maskFile.toStdString(), rgba);
    currentMaskPath = maskFile;

    // ★ 记录供下次复用
    lastBodyPathForMask = bodyFile;
    lastMaskForReuse = maskFile;

    qDebug() << "💾 遮罩已保存:" << currentMaskPath;

    doAfterMaskReady(frame, bodyFile, ts);
}

// ==================== 涂抹完成后：弹出分支 B ====================
void MainWindow::doAfterMaskReady(const cv::Mat &frame,
                                  const QString &bodyFile,
                                  const QString &ts)
{
    Q_UNUSED(frame)
    Q_UNUSED(bodyFile)
    Q_UNUSED(ts)

    lblStatus->setText("👕 第 2 步：请选择服装来源");
    lblStatus->setStyleSheet(
        "QLabel { background-color: #EFF6FF; color: #2563EB;"
        "         font-size: 14px; border-radius: 8px; padding: 10px; }");

    QMessageBox box(this);
    box.setWindowTitle("选择服装来源");
    box.setIcon(QMessageBox::Question);
    box.setText("请选择服装来源：");
    box.setInformativeText("✨ AI 生成：输入描述词，让 AI 帮您生成服装\n"
                           "👕 已有服装：从本地图片中选择一件衣服");

    QPushButton *btnAI     = box.addButton("✨ AI 生成", QMessageBox::YesRole);
    QPushButton *btnLocal  = box.addButton("👕 已有服装", QMessageBox::NoRole);
    QPushButton *btnCancel = box.addButton("取消", QMessageBox::RejectRole);
    box.setDefaultButton(btnAI);
    box.exec();

    if (box.clickedButton() == btnCancel) {
        updateSmartUI();
        ui->labelImage->setText("✅ 遮罩已保存\n\n可点击按钮重新开始");
        return;
    }

    if (box.clickedButton() == btnAI) {
        bool ok = false;
        QString prompt = QInputDialog::getText(
            this,
            "输入服装描述",
            "请输入服装描述（支持中文）：\n"
            "例如：时尚白色短衬衫 / 红色连衣裙 / 蓝色牛仔外套",
            QLineEdit::Normal,
            "时尚白色短衬衫",
            &ok);

        if (!ok || prompt.trimmed().isEmpty()) {
            updateSmartUI();
            ui->labelImage->setText("✅ 遮罩已保存\n\n可点击按钮重新开始");
            return;
        }

        prompt = prompt.trimmed();
        currentPromptForRegen = prompt;   // ★ 记录，重新生成时用

        qDebug() << "===== AI 生成服装 =====";
        qDebug() << "用户描述:" << prompt;

        currentStage = 3;
        updateSmartUI();
        lblStatus->setText("⏳ 第 3 步：AI 正在生成服装，请稍候...");
        lblStatus->setStyleSheet(
            "QLabel { background-color: #FEF3C7; color: #92400E;"
            "         font-size: 14px; border-radius: 8px; padding: 10px; }");

        ensureComfyUIRunning([=]() {
            pendingAutoTryOn = true;
            pendingMaskPath = currentMaskPath;

            if (containsChinese(prompt)) {
                translateToEnglish(prompt);
            } else {
                startGenerate(prompt);
            }
        });
        return;
    }

    if (box.clickedButton() == btnLocal) {
        doTryOnWithLocalShirt();
        return;
    }
}

// ==================== 分支 B-2：使用本地已有服装 ====================
void MainWindow::doTryOnWithLocalShirt()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, "选择服装图片",
        "C:/Users/31593/Documents/hy/images/",
        "图片文件 (*.png *.jpg *.jpeg *.bmp)");

    if (filePath.isEmpty()) {
        updateSmartUI();
        ui->labelImage->setText("✅ 遮罩已保存\n\n可点击按钮重新开始");
        return;
    }

    currentShirtPath = filePath;
    lastGeneratedImagePath = "";

    qDebug() << "===== 使用已有服装 =====";
    qDebug() << "服装图:" << filePath;

    currentStage = 3;
    updateSmartUI();
    lblStatus->setText("⏳ 第 3 步：正在使用已有服装试穿，请稍候...");
    lblStatus->setStyleSheet(
        "QLabel { background-color: #FEF3C7; color: #92400E;"
        "         font-size: 14px; border-radius: 8px; padding: 10px; }");

    ensureComfyUIRunning([=]() {
        freeComfyMemory();
        doTryOn(currentBodyPath, currentMaskPath);
    });
}

// ==================== 查找最新遮罩 ====================
QString MainWindow::findLatestMask()
{
    QString comfyInput = "D:/ComfyUI-aki/ComfyUI-aki-v3/ComfyUI/input/";

    if (!currentMaskPath.isEmpty() && QFile::exists(currentMaskPath)) {
        return currentMaskPath;
    }

    QDir clipspaceDir(comfyInput + "clipspace/");
    if (clipspaceDir.exists()) {
        QFileInfoList files = clipspaceDir.entryInfoList(
            QStringList() << "clipspace-painted-masked-*.png",
            QDir::Files, QDir::Time);
        if (!files.isEmpty()) {
            return files.first().absoluteFilePath();
        }
    }

    return "";
}

// ==================== 一键 AI 试穿（已弃用） ====================
void MainWindow::on_btnGenerateAndTryOn_clicked()
{
    QMessageBox::information(this, "提示",
                             "请使用【开始试穿】流程。");
}

// ==================== 虚拟试穿（已弃用） ====================
void MainWindow::on_btnTryOn_clicked()
{
    QMessageBox::information(this, "提示",
                             "请使用【开始试穿】流程。");
}

// ==================== 通用试穿逻辑 ====================
void MainWindow::doTryOn(const QString &bodyPath, const QString &maskPath)
{
    currentTaskType = 1;

    if (bodyPath.isEmpty() || !QFile::exists(bodyPath)) {
        QMessageBox::warning(this, "错误",
                             "找不到人物图，请先【开始试穿】！");
        return;
    }
    if (maskPath.isEmpty() || !QFile::exists(maskPath)) {
        QMessageBox::warning(this, "错误",
                             "找不到遮罩图，请先涂抹衣服区域！");
        return;
    }

    QString comfyInput = "D:/ComfyUI-aki/ComfyUI-aki-v3/ComfyUI/input/";

    auto forceCopy = [](const QString &src, const QString &dst) -> bool {
        QFile::remove(dst);
        return QFile::copy(src, dst);
    };

    if (!forceCopy(bodyPath, comfyInput + "body.jpg")) {
        QMessageBox::warning(this, "错误", "无法复制人物图：" + bodyPath);
        return;
    }

    if (!forceCopy(maskPath, comfyInput + "body_mask.png")) {
        QMessageBox::warning(this, "错误", "无法复制遮罩：" + maskPath);
        return;
    }

    QString shirtSrc = !lastGeneratedImagePath.isEmpty()
                           ? lastGeneratedImagePath
                           : (currentShirtPath.isEmpty()
                                  ? QString("images/shirt.png")
                                  : currentShirtPath);

    if (!QFile::exists(shirtSrc)) {
        QMessageBox::warning(this, "错误", "找不到服装图：" + shirtSrc);
        currentStage = 0;
        updateSmartUI();
        return;
    }

    if (!forceCopy(shirtSrc, comfyInput + "shirt.png")) {
        QMessageBox::warning(this, "错误", "无法复制服装图：" + shirtSrc);
        currentStage = 0;
        updateSmartUI();
        return;
    }

    qDebug() << "试穿: 人物=" << bodyPath << " 服装=" << shirtSrc;

    QFile file("C:/Users/31593/Documents/hy/tryon_workflow_api.json");
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "错误", "读不到 tryon_workflow_api.json");
        currentStage = 0;
        updateSmartUI();
        return;
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();

    if (!root.contains("15") || !root["15"].toObject().contains("class_type")) {
        QMessageBox::warning(this, "错误",
                             "tryon_workflow_api.json 中缺少节点 15 (CatVTONNode)，"
                             "或节点没有 class_type 字段。\n\n"
                             "请使用 ComfyUI 的 'Save (API Format)' 重新导出试穿工作流。");
        currentStage = 0;
        updateSmartUI();
        return;
    }

    QJsonObject node19 = root["19"].toObject();
    QJsonObject inputs19 = node19["inputs"].toObject();
    inputs19["image"] = "shirt.png";
    node19["inputs"] = inputs19;
    root["19"] = node19;

    QJsonObject node20 = root["20"].toObject();
    QJsonObject inputs20 = node20["inputs"].toObject();
    inputs20["image"] = "body_mask.png";
    node20["inputs"] = inputs20;
    root["20"] = node20;

    QJsonObject node15 = root["15"].toObject();
    QJsonObject inputs15 = node15["inputs"].toObject();
    inputs15["seed"] = (qint64)QRandomGenerator::global()->bounded(1, 1000000000);
    inputs15["mask_grow"] = 40;
    inputs15["steps"] = 30;
    inputs15["mixed_precision"] = "fp16";
    node15["inputs"] = inputs15;
    root["15"] = node15;

    QJsonObject wrapper;
    wrapper["prompt"] = root;
    wrapper["client_id"] = "qt_client";
    QString json = QJsonDocument(wrapper).toJson(QJsonDocument::Compact);

    QNetworkRequest request(QUrl("http://127.0.0.1:8188/prompt"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    setLoading(true);
    loadingBaseText = "👕 正在试穿";
    QApplication::processEvents();

    QNetworkReply *reply = manager->post(request, json.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() != QNetworkReply::NoError) {
            setLoading(false);
            currentStage = 0;
            updateSmartUI();
            QMessageBox::warning(this, "网络错误",
                                 "无法连接 ComfyUI：" + reply->errorString());
            reply->deleteLater();
            return;
        }

        QByteArray resp = reply->readAll();
        QJsonDocument d = QJsonDocument::fromJson(resp);
        QJsonObject obj = d.object();
        if (!obj.contains("prompt_id")) {
            setLoading(false);
            currentStage = 0;
            updateSmartUI();
            QMessageBox::warning(this, "提交失败", "ComfyUI 拒绝了请求。");
            reply->deleteLater();
            return;
        }
        currentPromptId = obj["prompt_id"].toString();
        pollCount = 0;
        pollTimer->start(1000);
        reply->deleteLater();
    });
}

// ==================== HTTP 完成 ====================
void MainWindow::onReplyFinished(QNetworkReply *reply)
{
    if (reply->url().toString().contains("httpbin")) {
        QByteArray data = reply->readAll();
        ui->labelImage->setWordWrap(true);
        ui->labelImage->setText(QString::fromUtf8(data));
    }
    reply->deleteLater();
}

void MainWindow::on_btnhttp_clicked()
{
    QNetworkRequest request(QUrl("https://httpbin.org/get"));
    manager->get(request);
    ui->labelImage->setText("请求中...");
}

// ==================== 清除遮罩（兼容） ====================
void MainWindow::on_btnClearMask_clicked()
{
    QString comfyInput = "D:/ComfyUI-aki/ComfyUI-aki-v3/ComfyUI/input/";
    QDir clipspaceDir(comfyInput + "clipspace/");
    QFileInfoList maskFiles = clipspaceDir.entryInfoList(
        QStringList() << "clipspace-painted-masked-*.png", QDir::Files);

    int count = 0;
    for (const QFileInfo &fi : maskFiles) {
        if (QFile::remove(fi.absoluteFilePath())) count++;
    }
    if (QFile::remove(comfyInput + "body_mask.png")) count++;
    if (QFile::remove("images/body_mask.png")) count++;
    if (QFile::remove("images/captured_mask.png")) count++;

    currentMaskPath = "";

    QMessageBox::information(this, "清除成功",
                             QString("已清除 %1 个遮罩文件。").arg(count));

    ui->labelImage->clear();
    ui->labelImage->setText("✅ 旧遮罩已清除");
}

// ==================== 选择服装图（已弃用） ====================
void MainWindow::on_btnSelectShirt_clicked()
{
    QMessageBox::information(this, "提示",
                             "请使用【开始试穿】流程，选择【已有服装】分支。");
}

// ==================== 保存结果图 ====================
void MainWindow::on_btnSaveResult_clicked()
{
    if (currentResultPixmap.isNull()) {
        QMessageBox::warning(this, "无结果", "当前没有可保存的结果图。");
        return;
    }

    QString defaultPath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                          + "/huanyi_result_"
                          + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")
                          + ".png";

    QString savePath = QFileDialog::getSaveFileName(
        this, "保存结果图", defaultPath,
        "PNG 图片 (*.png);;JPG 图片 (*.jpg)");

    if (savePath.isEmpty()) return;

    if (currentResultPixmap.save(savePath)) {
        QMessageBox::information(this, "保存成功",
                                 "结果图已保存到：\n" + savePath);
    }
}

// ==================== 中文检测 & 翻译 ====================
bool MainWindow::containsChinese(const QString &text)
{
    for (QChar c : text) {
        if (c.unicode() >= 0x4E00 && c.unicode() <= 0x9FFF) return true;
    }
    return false;
}

void MainWindow::translateToEnglish(const QString &chinese)
{
    setLoading(true);
    loadingBaseText = "🌐 正在翻译";

    QUrl url("https://api.mymemory.translated.net/get");
    QUrlQuery query;
    query.addQueryItem("q", chinese);
    query.addQueryItem("langpair", "zh-CN|en");
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "HuanYiApp/1.0");

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        setLoading(false);

        if (reply->error() != QNetworkReply::NoError) {
            pendingAutoTryOn = false;
            currentStage = 0;
            updateSmartUI();
            QMessageBox::warning(this, "翻译失败",
                                 "无法连接翻译服务：" + reply->errorString());
            reply->deleteLater();
            return;
        }

        QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
        QJsonObject obj = doc.object();
        QString translated;
        if (obj.contains("responseData")) {
            translated = obj["responseData"].toObject()["translatedText"].toString();
        }

        if (translated.isEmpty()) {
            pendingAutoTryOn = false;
            currentStage = 0;
            updateSmartUI();
            QMessageBox::warning(this, "翻译失败", "无法翻译，请直接输入英文。");
            reply->deleteLater();
            return;
        }

        ui->labelImage->clear();
        ui->labelImage->setText(
            QString("翻译结果：%1\n\n⏳ 正在生成...").arg(translated));
        ui->labelImage->repaint();

        startGenerate(translated);
        reply->deleteLater();
    });
}

// ==================== AI 生图（已弃用） ====================
void MainWindow::on_btnGenerate_clicked()
{
    QMessageBox::information(this, "提示",
                             "请使用【开始试穿】流程。");
}

void MainWindow::startGenerate(const QString &prompt)
{
    currentTaskType = 0;

    QString json = buildWorkflowJson(prompt);
    if (json.isEmpty()) {
        currentStage = 0;
        updateSmartUI();
        return;
    }

    QNetworkRequest request(QUrl("http://127.0.0.1:8188/prompt"));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    setLoading(true);
    loadingBaseText = "⏳ AI 正在生成";
    QApplication::processEvents();

    QNetworkReply *reply = manager->post(request, json.toUtf8());
    connect(reply, &QNetworkReply::finished, this, [=]() {
        if (reply->error() != QNetworkReply::NoError) {
            setLoading(false);
            pendingAutoTryOn = false;
            currentStage = 0;
            updateSmartUI();
            QMessageBox::warning(this, "网络错误",
                                 "无法连接 ComfyUI：" + reply->errorString());
            reply->deleteLater();
            return;
        }

        QByteArray resp = reply->readAll();
        QJsonDocument d = QJsonDocument::fromJson(resp);
        QJsonObject obj = d.object();
        if (!obj.contains("prompt_id")) {
            setLoading(false);
            pendingAutoTryOn = false;
            currentStage = 0;
            updateSmartUI();
            QMessageBox::warning(this, "提交失败", "ComfyUI 拒绝了请求。");
            reply->deleteLater();
            return;
        }
        currentPromptId = obj["prompt_id"].toString();
        pollCount = 0;
        pollTimer->start(1000);
        reply->deleteLater();
    });
}

// ==================== 构造生图请求体 ====================
QString MainWindow::buildWorkflowJson(const QString &prompt)
{
    QFile file("C:/Users/31593/Documents/hy/workflow_api.json");
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "错误", "读不到 workflow_api.json");
        return "";
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) {
        QMessageBox::warning(this, "错误", "workflow_api.json 不是合法 JSON");
        return "";
    }
    QJsonObject root = doc.object();

    if (!root.contains("3") || !root["3"].toObject().contains("class_type")) {
        QMessageBox::warning(this, "错误",
                             "workflow_api.json 中缺少节点 3（KSampler），"
                             "或节点没有 class_type 字段。\n\n"
                             "请使用 ComfyUI 的 'Save (API Format)' 重新导出。");
        return "";
    }
    if (!root.contains("6") || !root["6"].toObject().contains("class_type")) {
        QMessageBox::warning(this, "错误",
                             "workflow_api.json 中缺少节点 6（CLIPTextEncode）。");
        return "";
    }

    QJsonObject node6 = root["6"].toObject();
    QJsonObject inputs6 = node6["inputs"].toObject();
    inputs6["text"] = prompt;
    node6["inputs"] = inputs6;
    root["6"] = node6;

    QJsonObject node3 = root["3"].toObject();
    QJsonObject inputs3 = node3["inputs"].toObject();
    inputs3["seed"] = (qint64)QRandomGenerator::global()->bounded(1, 1000000000);
    node3["inputs"] = inputs3;
    root["3"] = node3;

    QJsonObject wrapper;
    wrapper["prompt"] = root;
    wrapper["client_id"] = "qt_client";

    return QJsonDocument(wrapper).toJson(QJsonDocument::Compact);
}

void MainWindow::onSubmitFinished(QNetworkReply *reply)
{
    reply->deleteLater();
}

// ==================== 轮询 ====================
void MainWindow::pollHistory()
{
    pollCount++;
    int elapsed = pollCount;

    if (lblStatus && currentStage == 3) {
        lblStatus->setText(
            QString("⏳ 已等待 %1 秒，AI 正在处理...（最长等待 10 分钟）")
                .arg(elapsed));
    }

    if (pollCount > 600) {
        pollTimer->stop();
        pollCount = 0;
        setLoading(false);
        pendingAutoTryOn = false;
        pendingTryOnAfterGenerate = false;
        currentStage = 0;
        updateSmartUI();
        lblStatus->setText("⏱ 任务超时已中止（超过 10 分钟）");
        lblStatus->setStyleSheet(
            "QLabel { background-color: #FEE2E2; color: #991B1B;"
            "         font-size: 14px; border-radius: 8px; padding: 10px; }");
        QMessageBox::warning(this, "超时",
                             "ComfyUI 处理超时（超过 10 分钟）。\n"
                             "可能是显存不足或工作流卡住了。\n"
                             "请检查下方 [ComfyUI] 日志。");
        return;
    }

    QString url = QString("http://127.0.0.1:8188/history/%1").arg(currentPromptId);
    QNetworkRequest request((QUrl(url)));

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();

        if (!root.contains(currentPromptId)) {
            reply->deleteLater();
            return;
        }

        QJsonObject job = root[currentPromptId].toObject();
        QJsonObject status = job["status"].toObject();
        QString statusStr = status["status_str"].toString();
        bool completed = status["completed"].toBool();

        qDebug() << "任务状态:" << statusStr << " 完成:" << completed;

        if (statusStr == "error" || !completed) {
            pollTimer->stop();
            pollCount = 0;
            setLoading(false);
            pendingAutoTryOn = false;
            pendingTryOnAfterGenerate = false;

            QString errMsg = "任务执行失败";
            QJsonArray messages = status["messages"].toArray();
            for (const auto &msg : messages) {
                QJsonArray arr = msg.toArray();
                if (arr.size() >= 2 && arr[0].toString() == "execution_error") {
                    errMsg = arr[1].toObject()["exception_message"].toString();
                    break;
                }
            }

            currentStage = 0;
            updateSmartUI();
            lblStatus->setText("❌ 失败：" + errMsg.left(40));
            lblStatus->setStyleSheet(
                "QLabel { background-color: #FEE2E2; color: #991B1B;"
                "         font-size: 14px; border-radius: 8px; padding: 10px; }");

            QMessageBox::warning(this, "生成失败",
                                 "任务失败：\n\n" + errMsg +
                                     "\n\n常见原因：显存不足 / 工作流断线 / 模型缺失");
            reply->deleteLater();
            return;
        }

        QJsonObject outputs = job["outputs"].toObject();
        QString outputNode;
        bool isTryOn = false;

        if (currentTaskType == 1) {
            if (outputs.contains("17")) { outputNode = "17"; isTryOn = true; }
        } else {
            if (outputs.contains("9")) { outputNode = "9"; isTryOn = false; }
        }

        if (!outputNode.isEmpty()) {
            QJsonObject saveNode = outputs[outputNode].toObject();
            QJsonArray images = saveNode["images"].toArray();
            if (!images.isEmpty()) {
                QJsonObject img = images[0].toObject();
                pollTimer->stop();
                pollCount = 0;
                downloadImage(img["filename"].toString(),
                              img["subfolder"].toString(),
                              isTryOn);
                reply->deleteLater();
                return;
            }
        }

        pollTimer->stop();
        pollCount = 0;
        setLoading(false);
        pendingAutoTryOn = false;
        pendingTryOnAfterGenerate = false;
        currentStage = 0;
        updateSmartUI();
        QMessageBox::warning(this, "无输出", "任务完成但未生成图片。");
        reply->deleteLater();
    });
}

// ==================== 结果后处理 ====================
QPixmap MainWindow::postProcessImage(const QPixmap &input)
{
    if (input.isNull()) return input;

    QImage qimg = input.toImage().convertToFormat(QImage::Format_RGB888);
    cv::Mat mat(qimg.height(), qimg.width(), CV_8UC3,
                (void*)qimg.bits(), qimg.bytesPerLine());
    mat = mat.clone();
    cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);

    cv::Mat blurred, sharpened;
    cv::GaussianBlur(mat, blurred, cv::Size(0, 0), 3);
    cv::addWeighted(mat, 1.5, blurred, -0.5, 0, sharpened);

    cv::Mat hsv;
    cv::cvtColor(sharpened, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> channels;
    cv::split(hsv, channels);
    channels[1] *= 1.10;
    cv::merge(channels, hsv);
    cv::cvtColor(hsv, sharpened, cv::COLOR_HSV2BGR);

    cv::cvtColor(sharpened, sharpened, cv::COLOR_BGR2RGB);
    QImage resultImg(sharpened.data, sharpened.cols, sharpened.rows,
                     (int)sharpened.step, QImage::Format_RGB888);
    return QPixmap::fromImage(resultImg.copy());
}

// ==================== 下载结果（含四个新功能） ====================
void MainWindow::downloadImage(const QString &filename,
                               const QString &subfolder,
                               bool isTryOn)
{
    QString url = QString("http://127.0.0.1:8188/view?filename=%1&subfolder=%2&type=output")
    .arg(filename, subfolder);

    QNetworkRequest request((QUrl(url)));
    QNetworkReply *reply = manager->get(request);

    connect(reply, &QNetworkReply::finished, this, [=]() {
        QByteArray data = reply->readAll();
        if (data.isEmpty()) {
            setLoading(false);
            pendingAutoTryOn = false;
            pendingTryOnAfterGenerate = false;
            currentStage = 0;
            updateSmartUI();
            reply->deleteLater();
            return;
        }

        QString savePath = isTryOn ? "images/tryon_result.png" : "images/generated.png";
        QFile file(savePath);
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.close();
        }

        if (!isTryOn) lastGeneratedImagePath = savePath;

        setLoading(false);

        QPixmap pix;
        pix.loadFromData(data);

        if (isTryOn && !pix.isNull()) {
            pix = postProcessImage(pix);
        }

        if (!pix.isNull()) {
            QPixmap scaled = pix.scaled(
                ui->labelImage->size(),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation);
            ui->labelImage->setPixmap(scaled);
            currentResultPixmap = scaled;
        }

        // ==================== 试穿完成 ====================
        if (isTryOn) {
            insertRecord("试穿", "CatVTON 虚拟试穿", savePath);
            qDebug() << "✅ 试穿完成";

            currentStage = 0;
            pendingTryOnAfterGenerate = false;
            updateSmartUI();
            lblStatus->setText("✅ 试穿完成！");
            lblStatus->setStyleSheet(
                "QLabel { background-color: #D1FAE5; color: #065F46;"
                "         font-size: 14px; border-radius: 8px; padding: 10px; }");

            // ★ 弹前后对比图
            QTimer::singleShot(500, this, [=]() {
                showComparisonDialog();
            });
        }
        // ==================== 生图完成 ====================
        else {
            insertRecord("生图", currentPromptForRegen, savePath);

            if (pendingAutoTryOn) {
                pendingAutoTryOn = false;

                qDebug() << "🎨 生图完成，弹窗询问用户是否满意";

                // ★ 弹窗：满意并试穿 / 重新生成 / 取消
                QMessageBox box(this);
                box.setWindowTitle("AI 服装生成完成");
                box.setIcon(QMessageBox::Question);
                box.setText("对生成的衣服满意吗？");
                box.setInformativeText(
                    "👕 满意并试穿：保存到衣服库并继续试穿\n"
                    "🔄 重新生成：换个描述词重新画一张\n"
                    "❌ 取消：结束本次流程");

                QPushButton *btnOk     = box.addButton("👕 满意并试穿", QMessageBox::YesRole);
                QPushButton *btnRegen  = box.addButton("🔄 重新生成", QMessageBox::NoRole);
                QPushButton *btnCancel = box.addButton("❌ 取消", QMessageBox::RejectRole);
                box.setDefaultButton(btnOk);
                box.exec();

                if (box.clickedButton() == btnOk) {
                    // ★ 保存到衣服库 + 等待用户点【继续试穿】
                    saveToWardrobe(savePath);
                    pendingTryOnAfterGenerate = true;

                    currentStage = 0;
                    updateSmartUI();
                    lblStatus->setText("✅ 服装已保存到衣服库！点击【继续试穿】");
                    lblStatus->setStyleSheet(
                        "QLabel { background-color: #D1FAE5; color: #065F46;"
                        "         font-size: 14px; border-radius: 8px; padding: 10px; }");
                }
                else if (box.clickedButton() == btnRegen) {
                    // ★ 重新生成
                    bool ok = false;
                    QString newPrompt = QInputDialog::getText(
                        this, "重新生成服装",
                        "请输入新的服装描述（支持中文）：",
                        QLineEdit::Normal,
                        currentPromptForRegen,
                        &ok);

                    if (!ok || newPrompt.trimmed().isEmpty()) {
                        currentStage = 0;
                        updateSmartUI();
                        ui->labelImage->setText("✅ 已取消重新生成");
                        return;
                    }

                    newPrompt = newPrompt.trimmed();
                    currentPromptForRegen = newPrompt;

                    currentStage = 3;
                    updateSmartUI();
                    lblStatus->setText("⏳ 正在重新生成服装，请稍候...");
                    lblStatus->setStyleSheet(
                        "QLabel { background-color: #FEF3C7; color: #92400E;"
                        "         font-size: 14px; border-radius: 8px; padding: 10px; }");

                    pendingAutoTryOn = true;
                    pendingMaskPath = currentMaskPath;

                    if (containsChinese(newPrompt)) {
                        translateToEnglish(newPrompt);
                    } else {
                        startGenerate(newPrompt);
                    }
                }
                else {
                    // 取消
                    currentStage = 0;
                    updateSmartUI();
                    ui->labelImage->setText("✅ 已取消\n\n可点击按钮重新开始");
                }
            } else {
                currentStage = 0;
                updateSmartUI();
            }
        }

        reply->deleteLater();
    });
}

// ==================== ★ 保存到衣服库 ====================
void MainWindow::saveToWardrobe(const QString &imagePath)
{
    QDir().mkpath("images/wardrobe");

    QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss");
    QString dst = QString("images/wardrobe/shirt_%1.png").arg(ts);

    if (QFile::copy(imagePath, dst)) {
        qDebug() << "👕 已保存到衣服库:" << dst;
    } else {
        qDebug() << "⚠️ 保存衣服库失败";
    }
}

// ==================== ★ 前后对比弹窗 ====================
void MainWindow::showComparisonDialog()
{
    QString beforePath = currentBodyPath;
    QString afterPath  = "images/tryon_result.png";

    if (!QFile::exists(beforePath) || !QFile::exists(afterPath)) {
        qDebug() << "对比图缺失，跳过";
        return;
    }

    cv::Mat before = cv::imread(beforePath.toStdString());
    cv::Mat after  = cv::imread(afterPath.toStdString());
    if (before.empty() || after.empty()) return;

    // 统一高度为 480
    int targetH = 480;
    double s1 = (double)targetH / before.rows;
    double s2 = (double)targetH / after.rows;
    cv::resize(before, before, cv::Size(), s1, s1);
    cv::resize(after,  after,  cv::Size(), s2, s2);

    // 水平拼接
    cv::Mat combined;
    cv::hconcat(before, after, combined);

    // 中间加紫色分隔线
    cv::line(combined,
             cv::Point(before.cols, 0),
             cv::Point(before.cols, combined.rows),
             cv::Scalar(139, 92, 246), 4);

    // 转为 QPixmap 前先克隆一份 RGB 用于显示/保存
    cv::Mat rgb;
    cv::cvtColor(combined, rgb, cv::COLOR_BGR2RGB);
    QImage qimg(rgb.data, rgb.cols, rgb.rows,
                (int)rgb.step, QImage::Format_RGB888);
    QPixmap pix = QPixmap::fromImage(qimg.copy());

    QDialog dlg(this);
    dlg.setWindowTitle("试穿前后对比");
    dlg.resize(1000, 620);

    QVBoxLayout *layout = new QVBoxLayout(&dlg);

    QLabel *title = new QLabel("左：原始照片       右：试穿效果", &dlg);
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(
        "QLabel { font-size: 15px; font-weight: bold;"
        "         color: #8B5CF6; padding: 10px; }");
    layout->addWidget(title);

    QLabel *imgLabel = new QLabel(&dlg);
    imgLabel->setAlignment(Qt::AlignCenter);
    imgLabel->setPixmap(pix.scaled(960, 500,
                                   Qt::KeepAspectRatio, Qt::SmoothTransformation));
    layout->addWidget(imgLabel, 1);

    QHBoxLayout *btns = new QHBoxLayout();
    QPushButton *btnSave = new QPushButton("💾 保存对比图", &dlg);
    QPushButton *btnOk   = new QPushButton("✅ 完成", &dlg);

    QString btnStyle =
        "QPushButton { background-color: #8B5CF6; color: white;"
        "   padding: 10px 20px; border-radius: 6px; font-size: 14px; }"
        "QPushButton:hover { background-color: #7C3AED; }";
    btnSave->setStyleSheet(btnStyle);
    btnOk->setStyleSheet(btnStyle);

    connect(btnSave, &QPushButton::clicked, [&]() {
        QString savePath = QFileDialog::getSaveFileName(
            &dlg, "保存对比图",
            QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                + "/compare_"
                + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss")
                + ".png",
            "PNG 图片 (*.png);;JPG 图片 (*.jpg)");
        if (!savePath.isEmpty()) {
            // combined 是 BGR，直接 imwrite 就是正确的
            cv::imwrite(savePath.toStdString(), combined);
            QMessageBox::information(&dlg, "已保存", "对比图已保存到：\n" + savePath);
        }
    });

    connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);

    btns->addStretch();
    btns->addWidget(btnSave);
    btns->addWidget(btnOk);
    btns->addStretch();
    layout->addLayout(btns);

    dlg.exec();
}

// ==================== 历史记录 ====================
void MainWindow::on_btnHistory_clicked()
{
    QDialog *dialog = new QDialog(this);
    dialog->setWindowTitle("历史记录");
    dialog->resize(900, 500);

    QVBoxLayout *layout = new QVBoxLayout(dialog);

    QSqlTableModel *model = new QSqlTableModel(dialog, db);
    model->setTable("records");
    model->setEditStrategy(QSqlTableModel::OnManualSubmit);
    model->select();

    model->setHeaderData(0, Qt::Horizontal, "ID");
    model->setHeaderData(1, Qt::Horizontal, "类型");
    model->setHeaderData(2, Qt::Horizontal, "描述");
    model->setHeaderData(3, Qt::Horizontal, "结果图");
    model->setHeaderData(4, Qt::Horizontal, "时间");

    QTableView *view = new QTableView(dialog);
    view->setModel(model);
    view->resizeColumnsToContents();

    layout->addWidget(view);
    dialog->setLayout(layout);
    dialog->exec();

    delete dialog;
}

// ==================== 加载状态切换 ====================
void MainWindow::setLoading(bool loading)
{
    if (loading) {
        loadingDots = 0;
        loadingBaseText = "⏳ AI 正在处理";
        ui->labelImage->clear();
        ui->labelImage->setText("⏳ AI 正在处理...");
        ui->labelImage->repaint();
        QApplication::processEvents();
        loadingTimer->start(300);
    } else {
        loadingTimer->stop();
    }
}

// ==================== 加载动画 ====================
void MainWindow::updateLoadingText()
{
    loadingDots = (loadingDots + 1) % 4;
    QString dots = QString(".").repeated(loadingDots);
    ui->labelImage->setText(loadingBaseText + dots);
}

// ==================== 数据库 ====================
void MainWindow::initDatabase()
{
    QString dbPath = QDir::currentPath() + "/hy_data.db";
    db = QSqlDatabase::addDatabase("QSQLITE");
    db.setDatabaseName(dbPath);
    if (!db.open()) return;
    QSqlQuery query;
    query.exec("CREATE TABLE IF NOT EXISTS records ("
               "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "  type TEXT, prompt TEXT, result_path TEXT, created_at TEXT)");
}

void MainWindow::insertRecord(const QString &type,
                              const QString &prompt,
                              const QString &resultPath)
{
    QSqlQuery query;
    query.prepare("INSERT INTO records (type, prompt, result_path, created_at) "
                  "VALUES (:type, :prompt, :path, :time)");
    query.bindValue(":type", type);
    query.bindValue(":prompt", prompt);
    query.bindValue(":path", resultPath);
    query.bindValue(":time", QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss"));
    query.exec();
}

// ==================== 摄像头（由智能按钮接管） ====================
void MainWindow::on_btnClick_clicked()
{
    // 已由智能按钮接管
}

// ==================== 图像相似度 ====================
double MainWindow::imageSimilarity(const cv::Mat &a, const cv::Mat &b)
{
    if (a.empty() || b.empty()) return 0;
    if (a.size() != b.size() || a.type() != b.type()) return 0;

    cv::Mat grayA, grayB;
    cv::cvtColor(a, grayA, cv::COLOR_BGR2GRAY);
    cv::cvtColor(b, grayB, cv::COLOR_BGR2GRAY);

    cv::Mat diff;
    cv::absdiff(grayA, grayB, diff);
    cv::Scalar meanScalar = cv::mean(diff);
    double meanDiff = meanScalar[0];

    return 1.0 - (meanDiff / 255.0);
}

// ==================== GrabCut 分割 ====================
cv::Mat MainWindow::generateSmartMask(const cv::Mat &frame)
{
    if (frame.empty()) return cv::Mat();

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    cv::CascadeClassifier bodyCascade;
    bodyCascade.load("haarcascade_mcs_upperbody.xml");
    std::vector<cv::Rect> bodies;
    if (!bodyCascade.empty()) {
        bodyCascade.detectMultiScale(gray, bodies, 1.1, 3, 0, cv::Size(80, 80));
    }

    cv::CascadeClassifier faceCascade;
    faceCascade.load("haarcascade_frontalface_default.xml");
    std::vector<cv::Rect> faces;
    if (!faceCascade.empty()) {
        faceCascade.detectMultiScale(gray, faces, 1.1, 4, 0, cv::Size(30, 30));
    }

    cv::Rect bodyRect;
    bool hasRect = false;

    if (!bodies.empty()) {
        bodyRect = bodies[0];
        for (const auto &b : bodies)
            if (b.area() > bodyRect.area()) bodyRect = b;
        hasRect = true;
        qDebug() << "✅ 上半身检测成功";
    } else if (!faces.empty()) {
        cv::Rect faceRect = faces[0];
        for (const auto &f : faces)
            if (f.area() > faceRect.area()) faceRect = f;

        bodyRect.width = (int)(faceRect.width * 1.8);
        bodyRect.height = (int)(faceRect.height * 1.8);
        bodyRect.x = faceRect.x + faceRect.width / 2 - bodyRect.width / 2;
        bodyRect.y = faceRect.y + (int)(faceRect.height * 1.0);
        hasRect = true;
        qDebug() << "⚠️ 人脸推算";
    } else {
        qDebug() << "❌ 未检测到人体";
        return cv::Mat();
    }

    bodyRect.x = std::max(0, bodyRect.x);
    bodyRect.y = std::max(0, bodyRect.y);
    bodyRect.width = std::min(bodyRect.width, frame.cols - bodyRect.x);
    bodyRect.height = std::min(bodyRect.height, frame.rows - bodyRect.y);

    cv::Mat grabMask = cv::Mat::zeros(frame.size(), CV_8UC1);
    cv::Mat bgdModel, fgdModel;

    cv::Rect gcRect = bodyRect;
    gcRect.x += (int)(gcRect.width * 0.05);
    gcRect.y += (int)(gcRect.height * 0.05);
    gcRect.width -= (int)(gcRect.width * 0.10);
    gcRect.height -= (int)(gcRect.height * 0.10);

    try {
        cv::grabCut(frame, grabMask, gcRect,
                    bgdModel, fgdModel,
                    5, cv::GC_INIT_WITH_RECT);
    } catch (cv::Exception &e) {
        qDebug() << "❌ GrabCut 失败:" << e.what();
        return cv::Mat();
    }

    cv::Mat fgMask = cv::Mat::zeros(frame.size(), CV_8UC1);
    fgMask.setTo(255, grabMask == cv::GC_FGD);
    fgMask.setTo(255, grabMask == cv::GC_PR_FGD);

    cv::Mat kernelClose = cv::getStructuringElement(
        cv::MORPH_ELLIPSE, cv::Size(15, 15));
    cv::morphologyEx(fgMask, fgMask, cv::MORPH_CLOSE, kernelClose);

    cv::Mat kernelOpen = cv::getStructuringElement(
        cv::MORPH_ELLIPSE, cv::Size(7, 7));
    cv::morphologyEx(fgMask, fgMask, cv::MORPH_OPEN, kernelOpen);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(fgMask, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    if (!contours.empty()) {
        int maxIdx = 0;
        double maxArea = 0;
        for (size_t i = 0; i < contours.size(); ++i) {
            double area = cv::contourArea(contours[i]);
            if (area > maxArea) {
                maxArea = area;
                maxIdx = (int)i;
            }
        }
        cv::Mat cleanMask = cv::Mat::zeros(frame.size(), CV_8UC1);
        cv::drawContours(cleanMask, contours, maxIdx,
                         cv::Scalar(255), cv::FILLED);
        fgMask = cleanMask;
    }

    cv::GaussianBlur(fgMask, fgMask, cv::Size(21, 21), 0);
    return fgMask;
}