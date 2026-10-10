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
#include <opencv2/opencv.hpp>
#include <functional>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void on_btnClick_clicked();
    void on_btnCapture_clicked();
    void on_btnGenerate_clicked();
    void on_btnTryOn_clicked();
    void on_btnGenerateAndTryOn_clicked();
    void on_btnClearMask_clicked();
    void on_btnSelectShirt_clicked();
    void on_btnSaveResult_clicked();
    void on_btnHistory_clicked();
    void on_btnhttp_clicked();

    void onReplyFinished(QNetworkReply *reply);
    void onSubmitFinished(QNetworkReply *reply);

    void pollHistory();
    void updateLoadingText();

    void onStartComfyUI();
    void onStopComfyUI();
    void onCheckComfyUI();
    void onRestoreDefaultBody();

    void onSmartActionClicked();

private:
    Ui::MainWindow *ui;

    QNetworkAccessManager *manager = nullptr;
    QSqlDatabase db;

    cv::VideoCapture cap;
    QTimer *timer = nullptr;

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

    // ★ 新增：遮罩复用
    QString lastBodyPathForMask;      // 上次用于涂抹的人物图
    QString lastMaskForReuse;         // 上次生成的遮罩路径

    // ★ 新增：重新生成上下文
    QString currentPromptForRegen;    // 当前描述词（重新生成时复用）

    QPushButton *btnSmartAction = nullptr;
    QLabel *lblStatus = nullptr;

    void setupLayout();
    void setupMenuBar();
    void setLoading(bool loading);
    void initDatabase();
    void insertRecord(const QString &type, const QString &prompt, const QString &resultPath);

    double imageSimilarity(const cv::Mat &a, const cv::Mat &b);
    cv::Mat generateSmartMask(const cv::Mat &frame);
    QString findLatestMask();
    void doTryOn(const QString &bodyPath, const QString &maskPath);
    bool containsChinese(const QString &text);
    void translateToEnglish(const QString &chinese);
    void startGenerate(const QString &prompt);
    QString buildWorkflowJson(const QString &prompt);
    QPixmap postProcessImage(const QPixmap &input);
    void downloadImage(const QString &filename, const QString &subfolder, bool isTryOn);

    bool isComfyRunning();
    void freeComfyMemory();
    bool startComfyProcess();
    void ensureComfyUIRunning(std::function<void()> callback);

    void updateSmartUI();

    void doAfterMaskReady(const cv::Mat &frame, const QString &bodyFile, const QString &ts);
    void doTryOnWithLocalShirt();

    // ★ 新增
    void saveToWardrobe(const QString &imagePath);
    void showComparisonDialog();
};

#endif // MAINWINDOW_H