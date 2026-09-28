#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlTableModel>
#include <functional>
#include <opencv2/opencv.hpp>

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void on_btnClick_clicked();
    void onReplyFinished(QNetworkReply *reply);
    void on_btnhttp_clicked();
    void on_btnTryOn_clicked();
    void on_btnCapture_clicked();
    void on_btnGenerate_clicked();
    void on_btnGenerateAndTryOn_clicked();
    void onSubmitFinished(QNetworkReply *reply);
    void pollHistory();
    void on_btnHistory_clicked();
    void on_btnClearMask_clicked();
    void on_btnSelectShirt_clicked();
    void on_btnSaveResult_clicked();
    void updateLoadingText();

    void onStartComfyUI();
    void onStopComfyUI();
    void onCheckComfyUI();
    void onRestoreDefaultBody();

private:
    void setupLayout();
    void setupMenuBar();
    void setLoading(bool loading);

    void ensureComfyUIRunning(std::function<void()> callback);
    bool startComfyProcess();
    bool isComfyRunning();

    bool containsChinese(const QString &text);
    void translateToEnglish(const QString &chinese);
    void startGenerate(const QString &prompt);

    QString findLatestMask();
    void doTryOn(const QString &bodyPath, const QString &maskPath);

    // ★ 结果后处理
    QPixmap postProcessImage(const QPixmap &input);

    Ui::MainWindow *ui;
    cv::VideoCapture cap;
    QTimer *timer;
    QNetworkAccessManager *manager;

    QString currentPromptId;
    QTimer *pollTimer;
    QString lastGeneratedImagePath;
    QString currentShirtPath;
    QPixmap currentResultPixmap;

    QString currentBodyPath;
    QString currentMaskPath;

    bool pendingAutoTryOn = false;
    QString pendingMaskPath;

    QTimer *loadingTimer;
    int loadingDots;
    QString loadingBaseText;

    QProcess *comfyProcess = nullptr;

    QSqlDatabase db;
    void initDatabase();
    void insertRecord(const QString &type,
                      const QString &prompt,
                      const QString &resultPath);

    QString buildWorkflowJson(const QString &prompt);
    void downloadImage(const QString &filename,
                       const QString &subfolder,
                       bool isTryOn = false);
};

#endif // MAINWINDOW_H