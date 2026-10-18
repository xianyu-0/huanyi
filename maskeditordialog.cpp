#include "maskeditordialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QPainter>
#include <QWheelEvent>
#include <QMessageBox>

// ==================== MaskCanvas (自适应画板) ====================
MaskCanvas::MaskCanvas(QWidget *parent) : QLabel(parent)
{
    setMinimumSize(400, 300); // 保证弹窗初始不会太小
    setCursor(Qt::CrossCursor);
    setStyleSheet("background-color: #E5E7EB;"); // 灰色背景，用于衬托图片边界
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAlignment(Qt::AlignCenter);
}

void MaskCanvas::setImages(const QImage &orig, const QImage &mask)
{
    m_original = orig.convertToFormat(QImage::Format_RGB32);
    m_mask = mask.convertToFormat(QImage::Format_ARGB32);
    update();
}

void MaskCanvas::clearMask()
{
    m_mask.fill(Qt::transparent);
    update();
}

void MaskCanvas::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform); // 开启平滑缩放

    if (m_original.isNull()) return;

    // 1. 计算等比缩放比例和居中偏移量
    double scaleW = (double)width() / m_original.width();
    double scaleH = (double)height() / m_original.height();
    double scale = qMin(scaleW, scaleH);

    int drawW = m_original.width() * scale;
    int drawH = m_original.height() * scale;
    int ox = (width() - drawW) / 2;
    int oy = (height() - drawH) / 2;

    // 2. 画原图
    painter.drawImage(QRect(ox, oy, drawW, drawH), m_original);

    // 3. 画红色遮罩
    if (!m_mask.isNull()) {
        painter.drawImage(QRect(ox, oy, drawW, drawH), m_mask);
    }
}

QPointF MaskCanvas::mapToImage(const QPointF &labelPos, bool &isValid) const
{
    isValid = false;
    if (m_original.isNull()) return QPointF();

    double scaleW = (double)width() / m_original.width();
    double scaleH = (double)height() / m_original.height();
    double scale = qMin(scaleW, scaleH);

    int drawW = m_original.width() * scale;
    int drawH = m_original.height() * scale;
    int ox = (width() - drawW) / 2;
    int oy = (height() - drawH) / 2;

    // 检查鼠标是否在图片有效区域内
    if (labelPos.x() < ox || labelPos.x() > ox + drawW || labelPos.y() < oy || labelPos.y() > oy + drawH) {
        return QPointF();
    }

    isValid = true;
    // 逆向映射回原图真实坐标
    return QPointF((labelPos.x() - ox) / scale, (labelPos.y() - oy) / scale);
}

void MaskCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        bool valid = false;
        m_lastPos = mapToImage(event->pos(), valid);
        if (valid) {
            m_isDrawing = true;
            QPainter painter(&m_mask);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(255, 0, 0, 150)); // 半透明红
            painter.drawEllipse(m_lastPos, m_brushSize / 2.0, m_brushSize / 2.0);
            update();
        }
    }
}

void MaskCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDrawing) {
        bool valid = false;
        QPointF currentPos = mapToImage(event->pos(), valid);
        if (valid) {
            QPainter painter(&m_mask);
            painter.setRenderHint(QPainter::Antialiasing);
            QPen pen(QColor(255, 0, 0, 150), m_brushSize, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen);
            painter.drawLine(m_lastPos, currentPos);
            m_lastPos = currentPos;
            update();
        }
    }
}

void MaskCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_isDrawing = false;
    }
}

// ==================== MaskEditorDialog (弹窗外壳) ====================
MaskEditorDialog::MaskEditorDialog(const QString &imagePath, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle("✏️ 涂抹需要替换的衣服区域");
    resize(1000, 800); // 给一个足够大的初始窗口
    setMinimumSize(700, 500);
    setStyleSheet("QDialog { background-color: #F9FAFB; }");

    // 1. 加载图片
    QImage img(imagePath);
    if (img.isNull()) {
        QMessageBox::critical(this, "错误", "无法加载图片！");
        return;
    }

    // 限制最大分辨率，防止极大图导致卡顿
    if (img.width() > 1600 || img.height() > 1600) {
        img = img.scaled(1600, 1600, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    m_originalImage = img;

    // 2. 初始化全透明遮罩层
    QImage mask(m_originalImage.size(), QImage::Format_ARGB32);
    mask.fill(Qt::transparent);

    // 3. 构建 UI
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(15);

    // 顶部提示栏
    QLabel *tipLabel = new QLabel("✏️ <b>按住鼠标左键</b> 涂抹衣服区域 (<span style='color:red;'>红色为将被替换的部分</span>)，滚轮可调整笔刷大小。", this);
    tipLabel->setStyleSheet("background-color: #FFFFFF; border-radius: 8px; padding: 12px; font-size: 14px; color: #374151; border: 1px solid #E5E7EB;");
    tipLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(tipLabel, 0); // 0表示不拉伸

    // 核心画板
    canvas = new MaskCanvas(this);
    canvas->setImages(m_originalImage, mask);
    mainLayout->addWidget(canvas, 1); // 1表示占据所有可用拉伸空间

    // 底部控制栏
    QHBoxLayout *bottomLayout = new QHBoxLayout();

    QLabel *sizeLabel = new QLabel("笔刷大小:", this);
    sizeLabel->setStyleSheet("font-size: 14px; color: #374151; font-weight: bold;");

    slider = new QSlider(Qt::Horizontal, this);
    slider->setRange(5, 100);
    slider->setValue(30);
    slider->setMinimumWidth(200);
    slider->setStyleSheet(R"(
        QSlider::groove:horizontal { height: 6px; background: #E5E7EB; border-radius: 3px; }
        QSlider::handle:horizontal { width: 16px; height: 16px; margin: -5px 0; border-radius: 8px; background: #8B5CF6; }
    )");
    connect(slider, &QSlider::valueChanged, this, [this](int val) {
        canvas->setBrushSize(val);
    });

    QPushButton *btnClear = new QPushButton("🗑 清除重涂", this);
    btnClear->setMinimumHeight(40);
    btnClear->setStyleSheet("QPushButton { background-color: #FEE2E2; color: #DC2626; border-radius: 6px; padding: 0 20px; font-weight: bold; } QPushButton:hover { background-color: #FCA5A5; }");
    connect(btnClear, &QPushButton::clicked, canvas, &MaskCanvas::clearMask);

    QPushButton *btnSave = new QPushButton("✅ 确认并保存", this);
    btnSave->setMinimumHeight(40);
    btnSave->setStyleSheet("QPushButton { background-color: #8B5CF6; color: white; border-radius: 6px; padding: 0 25px; font-weight: bold; } QPushButton:hover { background-color: #7C3AED; }");
    connect(btnSave, &QPushButton::clicked, this, &QDialog::accept);

    QPushButton *btnCancel = new QPushButton("取消", this);
    btnCancel->setMinimumHeight(40);
    btnCancel->setStyleSheet("QPushButton { background-color: transparent; color: #6B7280; border: none; font-size: 14px; padding: 0 15px; } QPushButton:hover { color: #374151; }");
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    bottomLayout->addWidget(sizeLabel);
    bottomLayout->addWidget(slider);
    bottomLayout->addStretch();
    bottomLayout->addWidget(btnClear);
    bottomLayout->addWidget(btnSave);
    bottomLayout->addWidget(btnCancel);

    mainLayout->addLayout(bottomLayout, 0); // 0表示死死钉在底部
}

MaskEditorDialog::~MaskEditorDialog() {}

void MaskEditorDialog::wheelEvent(QWheelEvent *event)
{
    int delta = event->angleDelta().y() / 120;
    int newSize = qBound(5, slider->value() + delta * 5, 100);

    // 同步更新滑块和画板
    slider->blockSignals(true);
    slider->setValue(newSize);
    slider->blockSignals(false);

    canvas->setBrushSize(newSize);
    event->accept();
}

cv::Mat MaskEditorDialog::getMask() const
{
    if (!canvas) return cv::Mat();

    // 从画板中提取原图尺寸的遮罩
    QImage maskImg = canvas->getMask();
    cv::Mat mask(maskImg.height(), maskImg.width(), CV_8UC1, cv::Scalar(0));

    for (int y = 0; y < maskImg.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb*>(maskImg.constScanLine(y));
        uchar *maskLine = mask.ptr<uchar>(y);
        for (int x = 0; x < maskImg.width(); ++x) {
            if (qAlpha(line[x]) > 0) {
                maskLine[x] = 255;
            }
        }
    }
    return mask;
}