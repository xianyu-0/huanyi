#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
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

// ==================== 启动 ComfyUI ====================
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
         << "--windows-standalone-build";

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
                                    "（首次启动约 2~3 分钟）",
                                    QMessageBox::Yes | QMessageBox::No);

    if (ret != QMessageBox::Yes) return;

    ui->labelImage->clear();
    ui->labelImage->setText("⏳ ComfyUI 正在启动，请稍候...\n（首次启动约 2~3 分钟）");
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
                    "已等待 %1 秒 / 最多 180 秒\n"
                    "（首次启动需要加载模型，约 2~3 分钟）")
                .arg(*retryCount));
    });

    checkTimer->start(1000);
}

// ==================== 菜单：ComfyUI 控制 ====================
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

// ==================== 恢复默认人物 ====================
void MainWindow::onRestoreDefaultBody()
{
    currentBodyPath = "images/body.jpg";
    currentMaskPath = "";

    QMessageBox::information(this, "已恢复",
                             "已恢复为默认人物图：images/body.jpg\n\n"
                             "接下来的试穿会使用默认人物。\n"
                             "遮罩会用 ComfyUI 里涂的遮罩。");

    qDebug() << "✅ 已恢复默认人物";
}

// ==================== 菜单栏 ====================
void MainWindow::setupMenuBar()
{
    QMenuBar *menuBar = this->menuBar();

    // 文件
    QMenu *fileMenu = menuBar->addMenu("文件(&F)");

    QAction *actSave = fileMenu->addAction("💾 保存结果图");
    connect(actSave, &QAction::triggered, this, &MainWindow::on_btnSaveResult_clicked);

    QAction *actSelectShirt = fileMenu->addAction("👕 选择服装图");
    connect(actSelectShirt, &QAction::triggered, this, &MainWindow::on_btnSelectShirt_clicked);

    fileMenu->addSeparator();

    QAction *actExit = fileMenu->addAction("🚪 退出");
    connect(actExit, &QAction::triggered, this, &QWidget::close);

    // ComfyUI
    QMenu *comfyMenu = menuBar->addMenu("ComfyUI");

    QAction *actStart = comfyMenu->addAction("▶️ 启动 ComfyUI");
    connect(actStart, &QAction::triggered, this, &MainWindow::onStartComfyUI);

    QAction *actStop = comfyMenu->addAction("⏹ 停止 ComfyUI");
    connect(actStop, &QAction::triggered, this, &MainWindow::onStopComfyUI);

    comfyMenu->addSeparator();

    QAction *actCheck = comfyMenu->addAction("🔍 检查状态");
    connect(actCheck, &QAction::triggered, this, &MainWindow::onCheckComfyUI);

    // 工具
    QMenu *toolMenu = menuBar->addMenu("工具(&T)");

    QAction *actRestoreBody = toolMenu->addAction("🔄 恢复默认人物");
    connect(actRestoreBody, &QAction::triggered, this, &MainWindow::onRestoreDefaultBody);

    QAction *actClearMask = toolMenu->addAction("🧹 清除旧遮罩");
    connect(actClearMask, &QAction::triggered, this, &MainWindow::on_btnClearMask_clicked);

    QAction *actHistory = toolMenu->addAction("📋 历史记录");
    connect(actHistory, &QAction::triggered, this, &MainWindow::on_btnHistory_clicked);

    toolMenu->addSeparator();

    QAction *actHttpTest = toolMenu->addAction("🌐 测试 HTTP");
    connect(actHttpTest, &QAction::triggered, this, &MainWindow::on_btnhttp_clicked);

    // 帮助
    QMenu *helpMenu = menuBar->addMenu("帮助(&H)");

    QAction *actAbout = helpMenu->addAction("ℹ️ 关于");
    connect(actAbout, &QAction::triggered, this, [=]() {
        QMessageBox::about(this, "关于",
                           "<h2>幻衣 - AI 虚拟试穿系统</h2>"
                           "<p>基于 Qt + OpenCV + ComfyUI + CatVTON 开发。</p>"
                           "<p><b>主要功能：</b></p>"
                           "<ul>"
                           "<li>AI 生图（Stable Diffusion）</li>"
                           "<li>虚拟试穿（CatVTON）</li>"
                           "<li>摄像头抓拍试穿</li>"
                           "<li>一键 AI 试穿</li>"
                           "<li>历史记录管理</li>"
                           "</ul>"
                           "<p><b>快捷键：</b> Ctrl+Enter 快速生图</p>");
    });
}

// ==================== 布局 ====================
void MainWindow::setupLayout()
{
    QWidget *central = centralWidget();
    if (central->layout()) delete central->layout();

    QVBoxLayout *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(15);

    ui->labelImage->setMinimumHeight(400);
    ui->labelImage->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->labelImage->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(ui->labelImage, 1);

    ui->lineEditPrompt->setMinimumHeight(45);
    ui->lineEditPrompt->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ui->lineEditPrompt->setPlaceholderText("请输入服装描述（支持中文），Ctrl+Enter 快速生图");
    mainLayout->addWidget(ui->lineEditPrompt, 0);

    ui->btnClick->setText("📷 开始/停止");
    ui->btnTryOn->setText("👕 虚拟试穿");
    ui->btnGenerate->setText("🎨 AI 生图");
    ui->btnHistory->setText("📋 历史记录");
    ui->btnhttp->setText("🌐 测试HTTP");

    QPushButton *btnCapture = new QPushButton("📸 抓拍试穿", central);
    connect(btnCapture, &QPushButton::clicked,
            this, &MainWindow::on_btnCapture_clicked);

    QPushButton *btnAutoTryOn = new QPushButton("✨ 一键 AI 试穿", central);
    btnAutoTryOn->setStyleSheet(
        "QPushButton {"
        "   background-color: #8B5CF6;"
        "   font-weight: bold;"
        "   font-size: 15px;"
        "}"
        "QPushButton:hover { background-color: #7C3AED; }"
        "QPushButton:pressed { background-color: #6D28D9; }"
        );
    connect(btnAutoTryOn, &QPushButton::clicked,
            this, &MainWindow::on_btnGenerateAndTryOn_clicked);

    QHBoxLayout *row1 = new QHBoxLayout();
    row1->setSpacing(12);

    QList<QPushButton*> row1Buttons = {
        ui->btnGenerate, btnCapture, btnAutoTryOn
    };

    for (QPushButton *btn : row1Buttons) {
        if (btn) {
            btn->setMinimumHeight(52);
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            row1->addWidget(btn);
        }
    }
    mainLayout->addLayout(row1, 0);

    QHBoxLayout *row2 = new QHBoxLayout();
    row2->setSpacing(12);

    QList<QPushButton*> row2Buttons = {
        ui->btnClick, ui->btnTryOn, ui->btnHistory
    };

    for (QPushButton *btn : row2Buttons) {
        if (btn) {
            btn->setMinimumHeight(52);
            btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            row2->addWidget(btn);
        }
    }
    mainLayout->addLayout(row2, 0);

    this->setWindowTitle("幻衣 - AI 虚拟试穿系统");
    this->setMinimumSize(1000, 720);
}

// ==================== 加载动画 ====================
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
        ui->labelImage->clear();
    }
}

void MainWindow::updateLoadingText()
{
    loadingDots = (loadingDots + 1) % 4;
    QString dots = QString(".").repeated(loadingDots);
    ui->labelImage->setText(loadingBaseText + dots);
}

// ==================== 析构 ====================
MainWindow::~MainWindow()
{
    if (cap.isOpened()) cap.release();
    if (db.isOpen()) db.close();
    delete ui;
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

// ==================== 摄像头 ====================
void MainWindow::on_btnClick_clicked()
{
    if (!cap.isOpened()) {
        cap.open(0);
        if (!cap.isOpened()) {
            QMessageBox::warning(this, "错误", "无法打开摄像头");
            return;
        }
        timer->start(30);
        ui->btnClick->setText("⏹ 停止");
    } else {
        timer->stop();
        cap.release();
        ui->btnClick->setText("📷 开始/停止");
    }
}

// ==================== ★ 抓拍试穿（椭圆遮罩）====================
void MainWindow::on_btnCapture_clicked()
{
    if (!cap.isOpened()) {
        QMessageBox::information(this, "提示",
                                 "请先点“开始/停止”打开摄像头，站好后，再点“抓拍试穿”。");
        return;
    }

    cv::Mat frame;
    cap >> frame;
    if (frame.empty()) {
        QMessageBox::warning(this, "错误", "抓取画面失败，请稍后重试");
        return;
    }

    timer->stop();
    cap.release();
    ui->btnClick->setText("📷 开始/停止");

    cv::imwrite("images/captured_body.jpg", frame);

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);

    cv::CascadeClassifier faceCascade;
    faceCascade.load("haarcascade_frontalface_default.xml");
    if (faceCascade.empty()) {
        QMessageBox::warning(this, "错误", "加载人脸分类器失败");
        return;
    }

    std::vector<cv::Rect> faces;
    faceCascade.detectMultiScale(gray, faces, 1.1, 4, 0, cv::Size(30, 30));

    if (faces.empty()) {
        QMessageBox::warning(this, "未检测到人脸",
                             "请正对摄像头，保持光线充足，再试一次。");
        return;
    }

    cv::Rect face = faces[0];
    for (const auto &f : faces) {
        if (f.area() > face.area()) face = f;
    }

    int bodyX = face.x + face.width / 2 - (int)(face.width * 1.25);
    int bodyY = face.y + (int)(face.height * 0.7);
    int bodyW = (int)(face.width * 2.5);
    int bodyH = (int)(face.height * 2.8);

    bodyX = std::max(0, bodyX);
    bodyY = std::max(0, bodyY);
    bodyW = std::min(bodyW, frame.cols - bodyX);
    bodyH = std::min(bodyH, frame.rows - bodyY);

    // ★ 用椭圆遮罩替代矩形
    cv::Mat mask = cv::Mat::zeros(frame.size(), CV_8UC1);

    cv::Point center(bodyX + bodyW / 2, bodyY + bodyH / 2);
    cv::Size axes(bodyW / 2, (int)(bodyH / 2 * 0.9));
    cv::ellipse(mask, center, axes, 0, 0, 360, cv::Scalar(255), -1);

    // ★ 高斯模糊边缘，让过渡自然
    cv::GaussianBlur(mask, mask, cv::Size(31, 31), 0);

    // 转成 RGBA（白色区域 alpha=0，要重绘；黑色区域 alpha=255，保留）
    cv::Mat rgba;
    cv::cvtColor(frame, rgba, cv::COLOR_BGR2BGRA);

    for (int y = 0; y < rgba.rows; ++y) {
        for (int x = 0; x < rgba.cols; ++x) {
            uchar m = mask.at<uchar>(y, x);
            rgba.at<cv::Vec4b>(y, x)[3] = 255 - m;
        }
    }

    cv::imwrite("images/captured_mask.png", rgba);

    currentBodyPath = "images/captured_body.jpg";
    currentMaskPath = "images/captured_mask.png";

    qDebug() << "✅ 已抓拍（椭圆遮罩）";

    ensureComfyUIRunning([=]() {
        ui->labelImage->clear();
        ui->labelImage->setText("📸 已抓拍，正在试穿...");
        ui->labelImage->repaint();
        QApplication::processEvents();

        doTryOn(currentBodyPath, currentMaskPath);
    });
}

// ==================== 找最新可用遮罩 ====================
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

// ==================== 一键 AI 试穿 ====================
void MainWindow::on_btnGenerateAndTryOn_clicked()
{
    QString prompt = ui->lineEditPrompt->text().trimmed();

    if (prompt.isEmpty()) {
        QMessageBox::information(this, "提示",
                                 "请输入服装描述，例如：\n红色连衣裙\n蓝色牛仔外套");
        ui->lineEditPrompt->setFocus();
        return;
    }

    QString maskPath = findLatestMask();
    if (maskPath.isEmpty()) {
        QMessageBox::warning(this, "没找到遮罩",
                             "试穿需要遮罩，请先：\n\n"
                             "1. 用“抓拍试穿”自动生成遮罩\n"
                             "或\n"
                             "2. 在 ComfyUI 里手动涂一次遮罩");
        return;
    }

    ensureComfyUIRunning([=]() {
        qDebug() << "===== 一键 AI 试穿开始 =====";

        pendingAutoTryOn = true;
        pendingMaskPath = maskPath;

        if (containsChinese(prompt)) {
            translateToEnglish(prompt);
        } else {
            startGenerate(prompt);
        }
    });
}

// ==================== 虚拟试穿 ====================
void MainWindow::on_btnTryOn_clicked()
{
    QString maskPath = findLatestMask();
    if (maskPath.isEmpty()) {
        QMessageBox::warning(this, "没找到遮罩",
                             "请先：\n1. 用“抓拍试穿”自动生成遮罩\n"
                             "或\n2. 在 ComfyUI 里涂一次遮罩");
        return;
    }

    ensureComfyUIRunning([=]() {
        doTryOn(currentBodyPath, maskPath);
    });
}

// ==================== 通用试穿逻辑（★ 参数优化）====================
void MainWindow::doTryOn(const QString &bodyPath, const QString &maskPath)
{
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

    if (!forceCopy(shirtSrc, comfyInput + "shirt.png")) {
        QMessageBox::warning(this, "错误", "无法复制服装图：" + shirtSrc);
        return;
    }
    qDebug() << "试穿: 人物=" << bodyPath << " 服装=" << shirtSrc;

    QFile file("C:/Users/31593/Documents/hy/tryon_workflow_api.json");
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, "错误", "读不到 tryon_workflow_api.json");
        return;
    }
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();

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

    // ★ 优化参数
    QJsonObject node15 = root["15"].toObject();
    QJsonObject inputs15 = node15["inputs"].toObject();
    inputs15["seed"] = (qint64)QRandomGenerator::global()->bounded(1, 1000000000);
    inputs15["mask_grow"] = 60;              // ★ 40 → 60
    inputs15["steps"] = 50;                  // ★ 40 → 50
    inputs15["mixed_precision"] = "bf16";    // ★ fp16 → bf16
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
            QMessageBox::warning(this, "提交失败", "ComfyUI 拒绝了请求。");
            reply->deleteLater();
            return;
        }
        currentPromptId = obj["prompt_id"].toString();
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

// ==================== 清除遮罩 ====================
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

// ==================== 选择服装图 ====================
void MainWindow::on_btnSelectShirt_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, "选择服装图片",
        "C:/Users/31593/Documents/hy/images/",
        "图片文件 (*.png *.jpg *.jpeg *.bmp)");

    if (filePath.isEmpty()) return;

    currentShirtPath = filePath;
    lastGeneratedImagePath = "";

    QPixmap pix(filePath);
    if (!pix.isNull()) {
        ui->labelImage->clear();
        ui->labelImage->setPixmap(pix.scaled(
            ui->labelImage->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation));
        currentResultPixmap = pix;
    }

    QMessageBox::information(this, "已选择",
                             "已选择服装图：\n" + filePath);
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

// ==================== AI 生图 ====================
void MainWindow::on_btnGenerate_clicked()
{
    QString prompt = ui->lineEditPrompt->text().trimmed();

    if (prompt.isEmpty()) {
        QMessageBox::information(this, "提示", "请输入服装描述");
        ui->lineEditPrompt->setFocus();
        return;
    }

    ensureComfyUIRunning([=]() {
        pendingAutoTryOn = false;

        if (containsChinese(prompt)) {
            translateToEnglish(prompt);
        } else {
            startGenerate(prompt);
        }
    });
}

void MainWindow::startGenerate(const QString &prompt)
{
    QString json = buildWorkflowJson(prompt);
    if (json.isEmpty()) return;

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
            QMessageBox::warning(this, "提交失败", "ComfyUI 拒绝了请求。");
            reply->deleteLater();
            return;
        }
        currentPromptId = obj["prompt_id"].toString();
        pollTimer->start(1000);
        reply->deleteLater();
    });
}

// ==================== 构造请求体 ====================
QString MainWindow::buildWorkflowJson(const QString &prompt)
{
    QFile file("C:/Users/31593/Documents/hy/workflow_api.json");
    if (!file.open(QIODevice::ReadOnly)) return "";
    QByteArray data = file.readAll();
    file.close();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();

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
    QString url = QString("http://127.0.0.1:8188/history/%1").arg(currentPromptId);
    QNetworkRequest request((QUrl(url)));

    QNetworkReply *reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [=]() {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonObject root = doc.object();

        if (root.contains(currentPromptId)) {
            QJsonObject job = root[currentPromptId].toObject();
            QJsonObject outputs = job["outputs"].toObject();

            QString outputNode;
            bool isTryOn = false;

            if (outputs.contains("17")) { outputNode = "17"; isTryOn = true; }
            else if (outputs.contains("9")) { outputNode = "9"; isTryOn = false; }

            if (!outputNode.isEmpty()) {
                QJsonObject saveNode = outputs[outputNode].toObject();
                QJsonArray images = saveNode["images"].toArray();

                if (!images.isEmpty()) {
                    QJsonObject img = images[0].toObject();
                    pollTimer->stop();
                    downloadImage(img["filename"].toString(),
                                  img["subfolder"].toString(),
                                  isTryOn);
                }
            }
        }
        reply->deleteLater();
    });
}

// ==================== ★ 结果后处理（锐化 + 饱和增强）====================
QPixmap MainWindow::postProcessImage(const QPixmap &input)
{
    if (input.isNull()) return input;

    // QPixmap → cv::Mat
    QImage qimg = input.toImage().convertToFormat(QImage::Format_RGB888);
    cv::Mat mat(qimg.height(), qimg.width(), CV_8UC3,
                (void*)qimg.bits(), qimg.bytesPerLine());
    mat = mat.clone();
    cv::cvtColor(mat, mat, cv::COLOR_RGB2BGR);

    // ① 锐化：原图 * 1.5 - 高斯模糊 * 0.5
    cv::Mat blurred, sharpened;
    cv::GaussianBlur(mat, blurred, cv::Size(0, 0), 3);
    cv::addWeighted(mat, 1.5, blurred, -0.5, 0, sharpened);

    // ② 饱和度 +10%
    cv::Mat hsv;
    cv::cvtColor(sharpened, hsv, cv::COLOR_BGR2HSV);
    std::vector<cv::Mat> channels;
    cv::split(hsv, channels);
    channels[1] *= 1.10;
    cv::merge(channels, hsv);
    cv::cvtColor(hsv, sharpened, cv::COLOR_HSV2BGR);

    // cv::Mat → QPixmap
    cv::cvtColor(sharpened, sharpened, cv::COLOR_BGR2RGB);
    QImage resultImg(sharpened.data, sharpened.cols, sharpened.rows,
                     (int)sharpened.step, QImage::Format_RGB888);
    return QPixmap::fromImage(resultImg.copy());
}

// ==================== 下载结果（含后处理）====================
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

        // ★ 试穿结果才做后处理（生图不加锐化）
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

        if (isTryOn) {
            insertRecord("试穿", "CatVTON 虚拟试穿", savePath);
            qDebug() << "✅ 试穿完成（已后处理）";
        } else {
            insertRecord("生图", ui->lineEditPrompt->text(), savePath);

            if (pendingAutoTryOn) {
                pendingAutoTryOn = false;
                qDebug() << "🎨 生图完成，自动试穿";

                ui->labelImage->clear();
                ui->labelImage->setText("🎨 生图完成，正在试穿...");
                ui->labelImage->repaint();
                QApplication::processEvents();

                QTimer::singleShot(800, this, [=]() {
                    doTryOn(currentBodyPath, pendingMaskPath);
                });
            }
        }

        reply->deleteLater();
    });
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