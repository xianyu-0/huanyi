#ifndef MASKEDITORDIALOG_H
#define MASKEDITORDIALOG_H

#include <QDialog>
#include <QImage>
#include <QPoint>
#include <QLabel>
#include <QSlider>
#include <QMouseEvent>
#include <opencv2/opencv.hpp>

// 自定义画板，负责自适应显示图片和绘制
class MaskCanvas : public QLabel {
    Q_OBJECT
public:
    explicit MaskCanvas(QWidget *parent = nullptr);
    void setImages(const QImage &orig, const QImage &mask);
    void setBrushSize(int size) { m_brushSize = size; }
    QImage getMask() const { return m_mask; }
    void clearMask();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QImage m_original;
    QImage m_mask;
    int m_brushSize = 30;
    bool m_isDrawing = false;
    QPointF m_lastPos; // 记录原图坐标

    // 核心：将鼠标在屏幕上的坐标，逆向映射回原图的真实坐标
    QPointF mapToImage(const QPointF &labelPos, bool &isValid) const;
};

class MaskEditorDialog : public QDialog
{
    Q_OBJECT
public:
    explicit MaskEditorDialog(const QString &imagePath, QWidget *parent = nullptr);
    ~MaskEditorDialog();
    cv::Mat getMask() const;

protected:
    void wheelEvent(QWheelEvent *event) override;

private:
    MaskCanvas *canvas;
    QSlider *slider;
    QImage m_originalImage;
};

#endif // MASKEDITORDIALOG_H