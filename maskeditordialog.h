#ifndef MASKEDITORDIALOG_H
#define MASKEDITORDIALOG_H

#include <QDialog>
#include <QWidget>
#include <QImage>
#include <QSlider>
#include <QPushButton>
#include <QScrollArea>
#include <opencv2/opencv.hpp>

// ========== 涂抹画布 ==========
class MaskCanvas : public QWidget
{
    Q_OBJECT
public:
    explicit MaskCanvas(QWidget *parent = nullptr);

    bool loadImage(const QString &path);
    void setBrushSize(int size);
    void clearMask();

    QImage getMaskImage() const;   // 白=涂抹区域，黑=保留区域
    QImage getOriginalImage() const { return baseImage; }

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QImage baseImage;    // 原图
    QImage maskImage;    // ARGB32，涂抹处白色不透明
    QPoint lastPoint;
    int brushSize = 30;
    bool drawing = false;
};

// ========== 涂抹对话框 ==========
class MaskEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MaskEditorDialog(const QString &imagePath, QWidget *parent = nullptr);

    cv::Mat getMask() const;   // 返回 CV_8UC1，白=涂抹区域

private slots:
    void onBrushSizeChanged(int value);

private:
    MaskCanvas *canvas;
    QSlider *sizeSlider;
};

#endif // MASKEDITORDIALOG_H