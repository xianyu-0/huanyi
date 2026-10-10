#include "maskeditordialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QLabel>
#include <QMessageBox>

// ============ MaskCanvas ============
MaskCanvas::MaskCanvas(QWidget *parent) : QWidget(parent)
{
    setMouseTracking(true);
    setAttribute(Qt::WA_StaticContents);
}

bool MaskCanvas::loadImage(const QString &path)
{
    QImage img;
    if (!img.load(path)) {
        // 中文路径兼容
        cv::Mat mat = cv::imread(path.toLocal8Bit().toStdString());
        if (!mat.empty()) {
            cv::cvtColor(mat, mat, cv::COLOR_BGR2RGB);
            img = QImage(mat.data, mat.cols, mat.rows,
                         (int)mat.step, QImage::Format_RGB888).copy();
        }
    }
    if (img.isNull()) return false;

    baseImage = img.convertToFormat(QImage::Format_RGB888);
    maskImage = QImage(baseImage.size(), QImage::Format_ARGB32);
    maskImage.fill(Qt::transparent);

    setFixedSize(baseImage.size());
    update();
    return true;
}

void MaskCanvas::setBrushSize(int size) { brushSize = size; }

void MaskCanvas::clearMask()
{
    maskImage.fill(Qt::transparent);
    update();
}

void MaskCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        drawing = true;
        lastPoint = event->pos();

        QPainter painter(&maskImage);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(Qt::white);
        painter.drawEllipse(lastPoint, brushSize / 2, brushSize / 2);
        update();
    }
}

void MaskCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (drawing) {
        QPainter painter(&maskImage);
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen(Qt::white, brushSize, Qt::SolidLine,
                 Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.drawLine(lastPoint, event->pos());
        lastPoint = event->pos();
        update();
    }
}

void MaskCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) drawing = false;
}

void MaskCanvas::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.drawImage(0, 0, baseImage);

    // 叠加半透明紫色显示涂抹区域
    QImage overlay = maskImage;
    QPainter op(&overlay);
    op.setCompositionMode(QPainter::CompositionMode_SourceIn);
    op.fillRect(overlay.rect(), QColor(139, 92, 246, 150));
    op.end();

    painter.drawImage(0, 0, overlay);
}

QImage MaskCanvas::getMaskImage() const
{
    QImage result(maskImage.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < maskImage.height(); ++y) {
        const QRgb *src = reinterpret_cast<const QRgb*>(maskImage.constScanLine(y));
        uchar *dst = result.scanLine(y);
        for (int x = 0; x < maskImage.width(); ++x) {
            dst[x] = (qAlpha(src[x]) > 128) ? 255 : 0;
        }
    }
    return result;
}

// ============ MaskEditorDialog ============
MaskEditorDialog::MaskEditorDialog(const QString &imagePath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("✏️ 手动涂抹遮罩");
    resize(900, 720);

    canvas = new MaskCanvas(this);
    if (!canvas->loadImage(imagePath)) {
        QMessageBox::warning(this, "错误", "无法加载图片：" + imagePath);
    }

    QLabel *tip = new QLabel(
        "🖌 按住鼠标左键涂抹<b>需要被替换的衣服区域</b>（会显示为紫色）<br>"
        "只需涂衣服部分，脸和背景不用涂", this);
    tip->setStyleSheet("QLabel { color: #4B5563; padding: 8px; }");

    QLabel *lblSize = new QLabel("画笔大小：", this);
    sizeSlider = new QSlider(Qt::Horizontal, this);
    sizeSlider->setRange(5, 120);
    sizeSlider->setValue(30);
    connect(sizeSlider, &QSlider::valueChanged,
            this, &MaskEditorDialog::onBrushSizeChanged);

    QPushButton *btnClear = new QPushButton("🧹 清空", this);
    connect(btnClear, &QPushButton::clicked, this, [=]() {
        canvas->clearMask();
    });

    QPushButton *btnCancel = new QPushButton("取消", this);
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    QPushButton *btnOk = new QPushButton("✅ 确定", this);
    btnOk->setDefault(true);
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);

    QHBoxLayout *ctrlRow = new QHBoxLayout();
    ctrlRow->addWidget(lblSize);
    ctrlRow->addWidget(sizeSlider, 1);
    ctrlRow->addWidget(btnClear);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch();
    btnRow->addWidget(btnCancel);
    btnRow->addWidget(btnOk);

    QScrollArea *scroll = new QScrollArea(this);
    scroll->setWidget(canvas);
    scroll->setWidgetResizable(false);
    scroll->setAlignment(Qt::AlignCenter);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(tip);
    mainLayout->addWidget(scroll, 1);
    mainLayout->addLayout(ctrlRow);
    mainLayout->addLayout(btnRow);
}

void MaskEditorDialog::onBrushSizeChanged(int value)
{
    canvas->setBrushSize(value);
}

cv::Mat MaskEditorDialog::getMask() const
{
    QImage maskImg = canvas->getMaskImage();
    if (maskImg.isNull()) return cv::Mat();

    cv::Mat mask(maskImg.height(), maskImg.width(), CV_8UC1,
                 (void*)maskImg.constBits(), (size_t)maskImg.bytesPerLine());
    return mask.clone();
}