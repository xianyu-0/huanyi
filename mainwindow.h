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

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onSmartActionClicked();
    void on_btnCapture_clicked();
    void showComparisonDialog();
    void onBackToHistoryClicked();
    void onUseHistoryImageClicked();
    void on_btnHistory_clicked();
    void on_btnSaveResult_clicked();

    void pollHistory();
    void updateLoadingText();

    void onStartComfyUI();
    void onStopComfyUI();
    void onCheckComfyUI();

    void onWsConnected();
    void onWsDisconnected();
    void onWsTextMessage(const QString &msg);

private:
    QNetworkAccessManager *manager = nullptr;
    QSqlDatabase db;

    cv::VideoCapture cap;
    QTimer *timer = nullptr;
    QTimer *animTimer = nullptr;
    QTimer *pollTimer = nullptr;
    QTimer *loadingTimer = nullptr;
    int loadingDots = 0;
    QString loadingBaseText;

    QProcess *comfyProcess = nullptr;

    QPixmap currentResultPixmap;
    QString currentBodyPath;
    QString currentMaskPath;
    QString currentShirtPath;
    QString lastGeneratedImagePath;
    QString currentPromptId;
    QString pendingMaskPath;
    bool pendingAutoTryOn = false;
    bool pendingTryOnAfterGenerate = false;
    int currentTaskType = 0;
    int pollCount = 0;
    int currentStage = 0;
    int engineWaitSeconds = 0;

    QString lastBodyPathForMask;
    QString lastMaskForReuse;
    QString currentPromptForRegen;

    // 纯代码 UI 控件
    QLabel *lblImage = nullptr;
    QLabel *lblStatus = nullptr;
    QPushButton *btnSmartAction = nullptr;
    QPushButton *btnShowCompare = nullptr;
    QPushButton *btnWardrobe = nullptr;
    QWidget *bottomSubRowWidget = nullptr;
    QPushButton *btnBackToHistory = nullptr;
    QPushButton *btnUseHistoryImg = nullptr;

    bool m_isViewingHistoryImage = false;
    QString m_historyImgPath;

    QWebSocket *comfyWebSocket = nullptr;
    QString wsClientId;
    bool wsConnected = false;
    int currentNodeProgress = 0;
    int currentNodeMax = 0;

    void setupLayout();
    void setupMenuBar();
    void resetToHomeScreen();
    void updateSmartUI();
    void updateProgressUI();

    void initDatabase();
    void insertRecord(const QString &type, const QString &prompt, const QString &resultPath);

    void setLoading(bool loading);
    void showFriendlyError(const QString &title, const QString &errorDetails);
    void showCenterLoading(const QString &title, const QString &subtitle);
    void updateCenterProgress(int percent, const QString &msg);

    double imageSimilarity(const cv::Mat &a, const cv::Mat &b);
    QString findLatestMask();
    QString protectFaceInMask(const QString &bodyPath, const QString &maskPath);
    void doTryOn(const QString &bodyPath, const QString &maskPath);
    void doAfterMaskReady();
    QPixmap postProcessImage(const QPixmap &input);

    void doTryOnWithLocalShirt();
    void doTryOnWithWardrobe();
    void openWardrobeManager();
    void startGenerate(const QString &prompt);
    QString buildWorkflowJson(const QString &prompt);
    void downloadImage(const QString &filename, const QString &subfolder, bool isTryOn);

    bool containsChinese(const QString &text);
    void translateToEnglish(const QString &chinese);
    void tryMyMemoryTranslate(const QString &text);
    void tryYoudaoTranslate(const QString &text);

    bool isComfyRunning();
    void freeComfyMemory();
    bool startComfyProcess();
    void ensureComfyUIRunning(std::function<void()> callback);

    void connectWebSocket();
    void disconnectWebSocket();
    void ensureWebSocketReady();

    QString detectCategoryFromPrompt(const QString &prompt);
};

#endif // MAINWINDOW_H