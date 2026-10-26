#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>
#include <QProcess>
#include <QSqlDatabase>
#include <QLabel>
#include <QPushButton>
#include <QWebSocket>
#include <opencv2/opencv.hpp>
#include <functional>

// ==================== 用户角色枚举 ====================
enum UserRole { Role_Buyer, Role_Seller };

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(UserRole role = Role_Buyer, QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    // ========== 试穿主流程 ==========
    void onSmartActionClicked();       // 主操作按钮：开始试穿 / 抓拍 / 继续试穿
    void on_btnCapture_clicked();      // 抓拍
    void showComparisonDialog();       // 查看试穿前后对比
    void onBackToHistoryClicked();     // 从历史大图返回列表
    void onUseHistoryImageClicked();   // 用历史图作为服装重新试穿
    void on_btnHistory_clicked();      // 打开历史记录中心
    void on_btnSaveResult_clicked();   // 保存结果图（菜单栏触发）

    // ========== 任务轮询与加载动效 ==========
    void pollHistory();                // 轮询 ComfyUI history
    void updateLoadingText();          // 加载动画点点点

    // ========== ComfyUI 进程控制（菜单栏触发） ==========
    void onStartComfyUI();
    void onStopComfyUI();
    void onCheckComfyUI();

    // ========== WebSocket 回调 ==========
    void onWsConnected();
    void onWsDisconnected();
    void onWsTextMessage(const QString &msg);

private:
    // ==================== 角色与网络 ====================
    UserRole m_currentRole;                    // 买家 / 卖家
    QNetworkAccessManager *manager = nullptr;  // 全部 HTTP 请求
    QSqlDatabase db;                           // SQLite 历史记录

    // ==================== 摄像头与计时器 ====================
    cv::VideoCapture cap;                      // 摄像头
    QTimer *timer = nullptr;                   // 摄像头预览刷新
    QTimer *animTimer = nullptr;               // 引擎启动 / 进度动画
    QTimer *pollTimer = nullptr;               // HTTP 轮询（WebSocket 断线时兜底）
    QTimer *loadingTimer = nullptr;            // "AI 正在处理..." 省略号动画
    int loadingDots = 0;
    QString loadingBaseText;

    // ==================== ComfyUI 进程 ====================
    QProcess *comfyProcess = nullptr;

    // ==================== 图像与任务状态 ====================
    QPixmap currentResultPixmap;               // 当前结果图（保存菜单用）
    QString currentBodyPath;                   // 当前人物图
    QString currentMaskPath;                   // 当前遮罩
    QString currentShirtPath;                  // 当前服装（衣橱 / 本地 / 历史）
    QString lastGeneratedImagePath;            // 最近生成的服装图（供试穿复用）
    QString currentPromptId;                   // ComfyUI 任务 ID
    QString pendingMaskPath;                   // 待试穿用的遮罩
    bool pendingAutoTryOn = false;             // 生图完自动进入试穿
    bool pendingTryOnAfterGenerate = false;    // 生图完等待用户点"继续试穿"
    int currentTaskType = 0;                   // 0=生图, 1=试穿
    int pollCount = 0;
    int currentStage = 0;                      // 0=空闲, 2=引擎启动, 3=任务中
    int engineWaitSeconds = 0;

    // ==================== 遮罩复用 ====================
    QString lastBodyPathForMask;               // 上次画遮罩时的身体图
    QString lastMaskForReuse;                  // 可复用的遮罩
    QString currentPromptForRegen;             // 重新生成时的 prompt

    // ==================== UI 控件（苹果紫版全部保留） ====================
    QLabel *lblImage = nullptr;                // 右侧主画布（白卡 20px 圆角 + 阴影）
    QLabel *lblStatus = nullptr;               // 顶部状态胶囊（白底半透明）
    QPushButton *btnSmartAction = nullptr;     // 主操作按钮（苹果紫胶囊；卖家隐藏但指针有效）
    QPushButton *btnShowCompare = nullptr;     // 查看对比（试穿完成后出现）
    QPushButton *btnWardrobe = nullptr;        // 衣橱按钮（苹果紫版隐藏占位，功能已移入侧栏）
    QWidget *bottomSubRowWidget = nullptr;     // 底部子行（高度 0，兼容占位，防旧调用崩溃）
    QPushButton *btnBackToHistory = nullptr;   // 历史接管时：返回列表
    QPushButton *btnUseHistoryImg = nullptr;   // 历史接管时：用此图试穿

    // ==================== 历史大图状态 ====================
    bool m_isViewingHistoryImage = false;
    QString m_historyImgPath;

    // ==================== WebSocket ====================
    QWebSocket *comfyWebSocket = nullptr;
    QString wsClientId;
    bool wsConnected = false;
    int currentNodeProgress = 0;
    int currentNodeMax = 0;

    // ==================== 布局与状态 ====================
    void setupLayout();                        // 苹果紫版：侧栏 + 主区（本次重构重点）
    void setupMenuBar();
    void resetToHomeScreen();                  // 苹果紫版：欢迎页（本次重构重点）
    void updateSmartUI();
    void updateProgressUI();

    // ==================== 数据库 ====================
    void initDatabase();
    void insertRecord(const QString &type, const QString &prompt, const QString &resultPath);

    // ==================== 加载 / 错误提示 ====================
    void setLoading(bool loading);
    void showFriendlyError(const QString &title, const QString &errorDetails);
    void showCenterLoading(const QString &title, const QString &subtitle);
    void updateCenterProgress(int percent, const QString &msg);

    // ==================== 图像处理 ====================
    double imageSimilarity(const cv::Mat &a, const cv::Mat &b);
    QString findLatestMask();
    QString protectFaceInMask(const QString &bodyPath, const QString &maskPath);
    QPixmap postProcessImage(const QPixmap &input);

    // ==================== 试穿流程 ====================
    void doAfterMaskReady();
    void doTryOn(const QString &bodyPath, const QString &maskPath);
    void doTryOnWithLocalShirt();
    void doTryOnWithWardrobe();
    void openWardrobeManager();

    // ==================== 生图流程 ====================
    void startGenerate(const QString &prompt);
    QString buildWorkflowJson(const QString &prompt);
    void downloadImage(const QString &filename, const QString &subfolder, bool isTryOn);

    // ==================== 翻译 ====================
    bool containsChinese(const QString &text);
    void translateToEnglish(const QString &chinese);
    void tryMyMemoryTranslate(const QString &text);
    void tryYoudaoTranslate(const QString &text);

    // ==================== ComfyUI 引擎 ====================
    bool isComfyRunning();
    void freeComfyMemory();
    bool startComfyProcess();
    void ensureComfyUIRunning(std::function<void()> callback);

    // ==================== WebSocket 管理 ====================
    void connectWebSocket();
    void disconnectWebSocket();
    void ensureWebSocketReady();

    // ==================== 分类识别 ====================
    QString detectCategoryFromPrompt(const QString &prompt);
};

#endif // MAINWINDOW_H