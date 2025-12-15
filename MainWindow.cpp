#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QPainter>
#include <QMouseEvent>

// 工具: Mat -> QImage
QImage matToQImage(const cv::Mat &mat) {
    if(mat.empty()) return QImage();
    if(mat.type() == CV_8UC3) {
        QImage img(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_RGB888);
        return img.rgbSwapped();
    }
    return QImage();
}

// 辅助函数：根据预设比例计算中心裁剪 Rect
cv::Rect calculateRatioCrop(int w, int h, int mode) {
    if (mode == 0 || mode == 99) return cv::Rect(0, 0, w, h);
    double targetRatio = 0.0;
    switch(mode) {
    case 1: targetRatio = 16.0/9.0; break;
    case 2: targetRatio = 9.0/16.0; break;
    case 3: targetRatio = 1.0; break;
    case 4: targetRatio = 4.0/5.0; break;
    case 5: targetRatio = 2.35; break;
    default: return cv::Rect(0, 0, w, h);
    }
    double currentRatio = (double)w / h;
    int newW = w, newH = h;
    if (currentRatio > targetRatio) newW = (int)(h * targetRatio);
    else newH = (int)(w / targetRatio);
    return cv::Rect((w - newW) / 2, (h - newH) / 2, newW, newH);
}

// ================= DropLabel =================
DropLabel::DropLabel(QWidget *parent) : QLabel(parent) {
    setText("\n📂\n点击或拖拽视频文件\n到此处");
    setAlignment(Qt::AlignCenter);
    setObjectName("DropZone");
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet("border: 2px dashed #444; border-radius: 10px; color: #888; font-size: 14px;");
}
void DropLabel::dragEnterEvent(QDragEnterEvent *event) { if (event->mimeData()->hasUrls()) event->acceptProposedAction(); }
void DropLabel::dropEvent(QDropEvent *event) {
    if (!event->mimeData()->urls().isEmpty()) emit fileDropped(event->mimeData()->urls().first().toLocalFile());
}
void DropLabel::mousePressEvent(QMouseEvent *event) { if (event->button() == Qt::LeftButton) emit clicked(); QLabel::mousePressEvent(event); }

// ================= CropEditorDialog (增强交互版) =================
CropEditorDialog::CropEditorDialog(const cv::Mat &frame, QRect currentRect, QWidget *parent)
    : QDialog(parent), m_origFrame(frame), m_mode(ModeNone), m_activeHandle(HandleNone)
{
    setWindowTitle("框选裁剪区域 (支持拖拽和缩放)");
    resize(900, 650);
    setMouseTracking(true); // 开启鼠标追踪以更新光标

    QImage rawImg = matToQImage(frame);
    // 适应屏幕大小
    m_scaleFactor = std::min(850.0 / rawImg.width(), 600.0 / rawImg.height());
    if(m_scaleFactor > 1.0) m_scaleFactor = 1.0;

    m_displayImage = rawImg.scaled(rawImg.width() * m_scaleFactor, rawImg.height() * m_scaleFactor, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    m_offsetX = (width() - m_displayImage.width()) / 2;
    m_offsetY = (height() - m_displayImage.height()) / 2;
    if(m_offsetX < 0) m_offsetX = 0; if(m_offsetY < 0) m_offsetY = 0;

    // 初始化选区
    if (!currentRect.isEmpty() && currentRect.width() > 0) {
        m_selectionRect = QRect(
            currentRect.x() * m_scaleFactor + m_offsetX,
            currentRect.y() * m_scaleFactor + m_offsetY,
            currentRect.width() * m_scaleFactor,
            currentRect.height() * m_scaleFactor
            );
    } else {
        // 默认全选
        m_selectionRect = QRect(m_offsetX, m_offsetY, m_displayImage.width(), m_displayImage.height());
    }

    QPushButton *btnOk = new QPushButton("确认裁剪", this);
    btnOk->setStyleSheet("background-color: #00A8E8; color: black; font-weight: bold; padding: 8px 20px; border-radius: 4px;");
    btnOk->setGeometry(width() - 130, height() - 50, 110, 35);
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
}

QRect CropEditorDialog::getFinalCropRect() {
    double rx = (m_selectionRect.x() - m_offsetX) / m_scaleFactor;
    double ry = (m_selectionRect.y() - m_offsetY) / m_scaleFactor;
    double rw = m_selectionRect.width() / m_scaleFactor;
    double rh = m_selectionRect.height() / m_scaleFactor;

    int x = std::max(0, (int)rx);
    int y = std::max(0, (int)ry);
    int w = std::min(m_origFrame.cols - x, (int)rw);
    int h = std::min(m_origFrame.rows - y, (int)rh);

    if (w <= 0 || h <= 0) return QRect(0,0, m_origFrame.cols, m_origFrame.rows);
    return QRect(x, y, w, h);
}

QRect CropEditorDialog::getImageRect() {
    return QRect(m_offsetX, m_offsetY, m_displayImage.width(), m_displayImage.height());
}

CropEditorDialog::ResizeHandle CropEditorDialog::hitTest(const QPoint &pos) {
    if (!m_selectionRect.isValid()) return HandleNone;

    int tolerance = 10; // 鼠标命中容差
    QRect r = m_selectionRect;

    // 检查四个角
    if (abs(pos.x() - r.left()) < tolerance && abs(pos.y() - r.top()) < tolerance) return HandleTopLeft;
    if (abs(pos.x() - r.right()) < tolerance && abs(pos.y() - r.top()) < tolerance) return HandleTopRight;
    if (abs(pos.x() - r.left()) < tolerance && abs(pos.y() - r.bottom()) < tolerance) return HandleBottomLeft;
    if (abs(pos.x() - r.right()) < tolerance && abs(pos.y() - r.bottom()) < tolerance) return HandleBottomRight;

    // 检查四条边
    if (abs(pos.x() - r.left()) < tolerance && pos.y() > r.top() && pos.y() < r.bottom()) return HandleLeft;
    if (abs(pos.x() - r.right()) < tolerance && pos.y() > r.top() && pos.y() < r.bottom()) return HandleRight;
    if (abs(pos.y() - r.top()) < tolerance && pos.x() > r.left() && pos.x() < r.right()) return HandleTop;
    if (abs(pos.y() - r.bottom()) < tolerance && pos.x() > r.left() && pos.x() < r.right()) return HandleBottom;

    // 检查内部
    if (r.contains(pos)) return HandleInside;

    return HandleNone;
}

void CropEditorDialog::updateCursorIcon(const QPoint &pos) {
    ResizeHandle h = hitTest(pos);
    switch(h) {
    case HandleTopLeft: case HandleBottomRight: setCursor(Qt::SizeFDiagCursor); break;
    case HandleTopRight: case HandleBottomLeft: setCursor(Qt::SizeBDiagCursor); break;
    case HandleTop: case HandleBottom: setCursor(Qt::SizeVerCursor); break;
    case HandleLeft: case HandleRight: setCursor(Qt::SizeHorCursor); break;
    case HandleInside: setCursor(Qt::SizeAllCursor); break;
    default: setCursor(Qt::CrossCursor); break;
    }
}

void CropEditorDialog::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        m_lastMousePos = e->pos();
        ResizeHandle h = hitTest(e->pos());

        if (h != HandleNone && h != HandleInside) {
            m_mode = ModeResizing;
            m_activeHandle = h;
        } else if (h == HandleInside) {
            m_mode = ModeMoving;
        } else {
            m_mode = ModeDrawing; // 点击外部，开始新绘制
            m_startPos = e->pos();
            m_selectionRect = QRect();
        }
        update();
    }
}

void CropEditorDialog::mouseMoveEvent(QMouseEvent *e) {
    if (m_mode == ModeNone) {
        updateCursorIcon(e->pos());
        return;
    }

    QPoint diff = e->pos() - m_lastMousePos;
    QRect imgRect = getImageRect();

    if (m_mode == ModeMoving) {
        // 移动逻辑
        m_selectionRect.translate(diff);
        m_selectionRect = m_selectionRect.intersected(imgRect); // 限制在图片内
    }
    else if (m_mode == ModeResizing) {
        // 调整大小逻辑
        QRect r = m_selectionRect;
        int minSize = 20;

        if (m_activeHandle == HandleLeft || m_activeHandle == HandleTopLeft || m_activeHandle == HandleBottomLeft) {
            int newLeft = std::min(r.right() - minSize, r.left() + diff.x());
            r.setLeft(newLeft);
        }
        if (m_activeHandle == HandleRight || m_activeHandle == HandleTopRight || m_activeHandle == HandleBottomRight) {
            int newRight = std::max(r.left() + minSize, r.right() + diff.x());
            r.setRight(newRight);
        }
        if (m_activeHandle == HandleTop || m_activeHandle == HandleTopLeft || m_activeHandle == HandleTopRight) {
            int newTop = std::min(r.bottom() - minSize, r.top() + diff.y());
            r.setTop(newTop);
        }
        if (m_activeHandle == HandleBottom || m_activeHandle == HandleBottomLeft || m_activeHandle == HandleBottomRight) {
            int newBottom = std::max(r.top() + minSize, r.bottom() + diff.y());
            r.setBottom(newBottom);
        }
        m_selectionRect = r.normalized().intersected(imgRect);
    }
    else if (m_mode == ModeDrawing) {
        // 新建绘制逻辑
        m_selectionRect = QRect(m_startPos, e->pos()).normalized().intersected(imgRect);
    }

    m_lastMousePos = e->pos();
    update();
}

void CropEditorDialog::mouseReleaseEvent(QMouseEvent *) {
    m_mode = ModeNone;
    m_activeHandle = HandleNone;
    // 归一化矩形（防止负宽高）
    m_selectionRect = m_selectionRect.normalized();
}

void CropEditorDialog::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), QColor("#121212")); // 背景

    // 1. 绘制图片 (居中)
    // 重新计算 offset 以防 resize 窗口
    m_offsetX = (width() - m_displayImage.width()) / 2;
    m_offsetY = (height() - m_displayImage.height()) / 2;
    if(m_offsetX < 0) m_offsetX = 0; if(m_offsetY < 0) m_offsetY = 0;

    p.drawImage(m_offsetX, m_offsetY, m_displayImage);

    // 2. 绘制半透明遮罩 (黑色半透)
    // 遮罩区域 = 整个图片区域 - 选框区域
    QRect imgRect = getImageRect();
    QRegion all(imgRect);
    QRegion crop(m_selectionRect);
    QRegion mask = all.subtracted(crop);

    p.setClipRegion(mask);
    p.fillRect(imgRect, QColor(0, 0, 0, 160)); // 遮罩
    p.setClipping(false);

    // 3. 绘制选框边框
    if (m_selectionRect.isValid()) {
        p.setPen(QPen(QColor("#00A8E8"), 2, Qt::SolidLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(m_selectionRect);

        // 4. 绘制控制手柄 (8个点)
        p.setBrush(QColor("#00A8E8"));
        p.setPen(Qt::NoPen);
        int hs = 8; // handle size
        int hs2 = hs/2;

        QVector<QPoint> handles = {
            m_selectionRect.topLeft(), m_selectionRect.topRight(),
            m_selectionRect.bottomLeft(), m_selectionRect.bottomRight(),
            QPoint(m_selectionRect.center().x(), m_selectionRect.top()),
            QPoint(m_selectionRect.center().x(), m_selectionRect.bottom()),
            QPoint(m_selectionRect.left(), m_selectionRect.center().y()),
            QPoint(m_selectionRect.right(), m_selectionRect.center().y())
        };

        for(const QPoint &pt : handles) {
            p.drawRect(pt.x() - hs2, pt.y() - hs2, hs, hs);
        }

        // 5. 绘制尺寸文字
        QString sizeText = QString("%1 x %2").arg((int)(m_selectionRect.width()/m_scaleFactor)).arg((int)(m_selectionRect.height()/m_scaleFactor));
        p.setPen(Qt::white);
        p.drawText(m_selectionRect.topLeft() - QPoint(0, 8), sizeText);
    }
}


// ================= RenderConfigDialog =================
RenderConfigDialog::RenderConfigDialog(QString videoPath, QWidget *parent)
    : QDialog(parent), m_videoPath(videoPath)
{
    setWindowTitle("导出与裁剪设置");
    resize(650, 750);

    m_previewCap.open(videoPath.toStdString());
    if (m_previewCap.isOpened()) {
        m_totalFrames = (int)m_previewCap.get(cv::CAP_PROP_FRAME_COUNT);
        m_srcW = (int)m_previewCap.get(cv::CAP_PROP_FRAME_WIDTH);
        m_srcH = (int)m_previewCap.get(cv::CAP_PROP_FRAME_HEIGHT);
        m_fps = m_previewCap.get(cv::CAP_PROP_FPS);
    } else {
        m_totalFrames = 100; m_fps = 30; m_srcW=1920; m_srcH=1080;
    }

    QVBoxLayout *lay = new QVBoxLayout(this);

    // --- 0. 视频时长预览 ---
    QGroupBox *grpPrev = new QGroupBox("视频时长选择 (拖动滑块查看)");
    QVBoxLayout *lPrev = new QVBoxLayout(grpPrev);

    m_lblVideoPreview = new QLabel;
    m_lblVideoPreview->setAlignment(Qt::AlignCenter);
    m_lblVideoPreview->setStyleSheet("background: #000; border: 1px solid #333;");
    m_lblVideoPreview->setMinimumHeight(240);
    lPrev->addWidget(m_lblVideoPreview);

    QHBoxLayout *hSlider = new QHBoxLayout;
    m_sliderTimeline = new QSlider(Qt::Horizontal);
    m_sliderTimeline->setRange(0, m_totalFrames - 1);
    m_sliderTimeline->setValue(0);
    connect(m_sliderTimeline, &QSlider::valueChanged, this, &RenderConfigDialog::onTimelineChanged);

    m_lblCurrentFrame = new QLabel("第 0 帧");
    m_lblCurrentFrame->setFixedWidth(80);
    m_lblCurrentFrame->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    hSlider->addWidget(m_sliderTimeline);
    hSlider->addWidget(m_lblCurrentFrame);
    lPrev->addLayout(hSlider);

    QHBoxLayout *hSetBtns = new QHBoxLayout;
    QPushButton *btnSetStart = new QPushButton("设为起点 ([)");
    QPushButton *btnSetEnd = new QPushButton("设为终点 (])");
    btnSetStart->setToolTip("将当前预览的帧设为开始帧");
    btnSetEnd->setToolTip("将当前预览的帧设为结束帧");
    connect(btnSetStart, &QPushButton::clicked, this, &RenderConfigDialog::onSetStartClicked);
    connect(btnSetEnd, &QPushButton::clicked, this, &RenderConfigDialog::onSetEndClicked);
    hSetBtns->addStretch(); hSetBtns->addWidget(btnSetStart); hSetBtns->addWidget(btnSetEnd); hSetBtns->addStretch();
    lPrev->addLayout(hSetBtns);

    lay->addWidget(grpPrev);

    // --- 1. 裁剪设置 ---
    QGroupBox *grpCrop = new QGroupBox("1. 裁剪设置");
    QVBoxLayout *lCrop = new QVBoxLayout(grpCrop);

    QHBoxLayout *hTime = new QHBoxLayout;
    m_spinStartFrame = new QSpinBox; m_spinStartFrame->setRange(0, m_totalFrames-1); m_spinStartFrame->setValue(0);
    m_spinEndFrame = new QSpinBox; m_spinEndFrame->setRange(1, m_totalFrames); m_spinEndFrame->setValue(m_totalFrames);
    hTime->addWidget(new QLabel("精确帧范围:")); hTime->addWidget(m_spinStartFrame);
    hTime->addWidget(new QLabel("至")); hTime->addWidget(m_spinEndFrame);
    lCrop->addLayout(hTime);
    m_lblDuration = new QLabel; lCrop->addWidget(m_lblDuration);

    connect(m_spinStartFrame, QOverload<int>::of(&QSpinBox::valueChanged), this, &RenderConfigDialog::updateDurationLabel);
    connect(m_spinEndFrame, QOverload<int>::of(&QSpinBox::valueChanged), this, &RenderConfigDialog::updateDurationLabel);
    updateDurationLabel();

    QHBoxLayout *hRatio = new QHBoxLayout;
    hRatio->addWidget(new QLabel("画幅:"));
    m_cmbCropRatio = new QComboBox;
    m_cmbCropRatio->addItem("原始比例", 0);
    m_cmbCropRatio->addItem("16:9 (横屏)", 1);
    m_cmbCropRatio->addItem("9:16 (抖音/竖屏)", 2);
    m_cmbCropRatio->addItem("1:1 (正方形)", 3);
    m_cmbCropRatio->addItem("4:5 (IG竖屏)", 4);
    m_cmbCropRatio->addItem("2.35:1 (电影)", 5);
    m_cmbCropRatio->addItem("手动框选 (任意比例)...", 99);
    hRatio->addWidget(m_cmbCropRatio);
    lCrop->addLayout(hRatio);

    m_btnEditCrop = new QPushButton("📐 编辑裁剪区域");
    m_btnEditCrop->setStyleSheet("background-color: #444; color: #00A8E8; border: 1px solid #00A8E8;");
    m_btnEditCrop->setVisible(false);
    connect(m_btnEditCrop, &QPushButton::clicked, this, &RenderConfigDialog::openCropEditor);
    lCrop->addWidget(m_btnEditCrop);

    connect(m_cmbCropRatio, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RenderConfigDialog::onCropModeChanged);

    lay->addWidget(grpCrop);

    // 2. 画质
    QGroupBox *grpRes = new QGroupBox("2. 输出画质");
    QVBoxLayout *lRes = new QVBoxLayout(grpRes);
    m_cmbRes = new QComboBox;
    m_cmbRes->addItem("原始分辨率 (最慢)", 0);
    m_cmbRes->addItem("4K UHD (2160p)", 2160);
    m_cmbRes->addItem("Full HD (1080p)", 1080);
    m_cmbRes->addItem("HD (720p)", 720);
    m_cmbRes->setCurrentIndex(2);
    lRes->addWidget(m_cmbRes);
    lay->addWidget(grpRes);

    // 3. 导出内容
    QGroupBox *grpMode = new QGroupBox("3. 导出内容");
    QVBoxLayout *lMode = new QVBoxLayout(grpMode);
    m_rbVideoOnly = new QRadioButton("仅视频"); m_rbVideoOnly->setChecked(true);
    m_rbLivePhoto = new QRadioButton("仅实况照片");
    m_rbBoth = new QRadioButton("视频 + 实况照片");
    lMode->addWidget(m_rbVideoOnly); lMode->addWidget(m_rbLivePhoto); lMode->addWidget(m_rbBoth);
    lay->addWidget(grpMode);

    // 4. 其他
    QHBoxLayout *hFmt = new QHBoxLayout;
    m_cmbFormat = new QComboBox; m_cmbFormat->addItem("MP4", ".mp4"); m_cmbFormat->addItem("MOV", ".mov");
    hFmt->addWidget(new QLabel("格式:")); hFmt->addWidget(m_cmbFormat);
    lay->addLayout(hFmt);

    m_chkOpenCL = new QCheckBox("启用 GPU 加速"); m_chkOpenCL->setChecked(true);
    lay->addWidget(m_chkOpenCL);

    lay->addStretch();

    QHBoxLayout *hBtn = new QHBoxLayout;
    QPushButton *btnCancel = new QPushButton("取消");
    QPushButton *btnOk = new QPushButton("开始渲染");
    btnOk->setStyleSheet("background-color: #00A8E8; color: black; font-weight: bold; padding: 8px;");
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    hBtn->addStretch(); hBtn->addWidget(btnCancel); hBtn->addWidget(btnOk);
    lay->addLayout(hBtn);

    onTimelineChanged(0);
}

RenderConfigDialog::~RenderConfigDialog() {
    if(m_previewCap.isOpened()) m_previewCap.release();
}

void RenderConfigDialog::onTimelineChanged(int value) {
    m_lblCurrentFrame->setText(QString("第 %1 帧").arg(value));
    if (m_previewCap.isOpened()) {
        m_previewCap.set(cv::CAP_PROP_POS_FRAMES, value);
        cv::Mat f;
        if (m_previewCap.read(f)) {
            int w = m_lblVideoPreview->width(); int h = m_lblVideoPreview->height();
            if(w <= 0) w = 400; if(h <= 0) h = 240;
            QImage img = matToQImage(f);
            m_lblVideoPreview->setPixmap(QPixmap::fromImage(img).scaled(QSize(w, h), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }
}

void RenderConfigDialog::onSetStartClicked() { m_spinStartFrame->setValue(m_sliderTimeline->value()); }
void RenderConfigDialog::onSetEndClicked() { m_spinEndFrame->setValue(m_sliderTimeline->value()); }

void RenderConfigDialog::updateDurationLabel() {
    int s = m_spinStartFrame->value(); int e = m_spinEndFrame->value();
    if (s >= e) { m_spinStartFrame->setValue(e-1); s = e-1; }
    int frames = e - s;
    m_lblDuration->setText(QString("长度: %1 帧 (约 %2 秒)").arg(frames).arg(frames/m_fps, 0, 'f', 1));
}

void RenderConfigDialog::onCropModeChanged(int index) {
    int data = m_cmbCropRatio->itemData(index).toInt();
    if (data == 99) {
        m_btnEditCrop->setVisible(true);
        if (m_currentManualRect.isEmpty()) openCropEditor();
    } else {
        m_btnEditCrop->setVisible(false);
    }
}

void RenderConfigDialog::openCropEditor() {
    cv::Mat frame;
    m_previewCap.set(cv::CAP_PROP_POS_FRAMES, m_sliderTimeline->value());
    m_previewCap.read(frame);
    if (frame.empty()) return;

    CropEditorDialog dlg(frame, m_currentManualRect, this);
    if (dlg.exec() == QDialog::Accepted) {
        QRect qResult = dlg.getFinalCropRect();
        m_currentManualRect = qResult;
        m_btnEditCrop->setText(QString("区域: %1x%2 (点击修改)").arg(qResult.width()).arg(qResult.height()));
    }
}

RenderSettings RenderConfigDialog::getSettings() {
    RenderSettings s;
    s.targetHeight = m_cmbRes->currentData().toInt();
    s.exportVideo = m_rbVideoOnly->isChecked() || m_rbBoth->isChecked();
    s.exportLivePhoto = m_rbLivePhoto->isChecked() || m_rbBoth->isChecked();
    s.useOpenCL = m_chkOpenCL->isChecked();
    s.outputFormat = m_cmbFormat->currentData().toString();
    s.startFrame = m_spinStartFrame->value();
    s.endFrame = m_spinEndFrame->value();
    s.cropRatioMode = m_cmbCropRatio->currentData().toInt();
    s.manualCropRect = m_currentManualRect;
    return s;
}

// ================= VideoWriterWorker =================
VideoWriterWorker::VideoWriterWorker(QString path, int w, int h, double fps, bool isMov)
    : m_path(path), m_width(w), m_height(h), m_fps(fps), m_running(true)
{
    if (m_width % 2 != 0) m_width--; // H.264 偶数修正
    if (m_height % 2 != 0) m_height--;
}

void VideoWriterWorker::addFrame(const cv::Mat &frame) {
    QMutexLocker locker(&m_mutex);
    m_queue.enqueue(frame.clone());
    m_condition.wakeOne();
}

void VideoWriterWorker::stop() {
    m_running = false;
    m_condition.wakeAll();
    wait();
}

void VideoWriterWorker::run() {
    int fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1');
    cv::VideoWriter writer;
    writer.open(m_path.toStdString(), fourcc, m_fps, cv::Size(m_width, m_height));
    while (m_running || !m_queue.isEmpty()) {
        cv::Mat frame;
        {
            QMutexLocker locker(&m_mutex);
            while (m_queue.isEmpty() && m_running) m_condition.wait(&m_mutex);
            if (!m_queue.isEmpty()) frame = m_queue.dequeue();
        }
        if (!frame.empty()) {
            if (frame.cols != m_width || frame.rows != m_height) cv::resize(frame, frame, cv::Size(m_width, m_height), 0, 0, cv::INTER_AREA);
            writer.write(frame);
        }
    }
    writer.release();
}

// ================= ProcessorThread =================
void ProcessorThread::setParams(const ProcessParams &params) { m_params = params; }
void ProcessorThread::stop() { m_running = false; }

void ProcessorThread::run() {
    m_running = true;
    cv::VideoCapture cap(m_params.inPath.toStdString());
    if (!cap.isOpened()) { emit errorOccurred("无法打开视频"); return; }
    cv::ocl::setUseOpenCL(m_params.useOpenCL);

    int total = (int)cap.get(cv::CAP_PROP_FRAME_COUNT);
    double fps = cap.get(cv::CAP_PROP_FPS); if (fps<=0) fps=30;

    cv::Rect cropRect = m_params.finalCropRect;
    int finalW = cropRect.width;
    int finalH = cropRect.height;
    if (m_params.targetRes > 0 && m_params.targetRes < finalH) {
        double scale = (double)m_params.targetRes / finalH;
        finalW = (int)(finalW * scale);
        finalH = m_params.targetRes;
    }

    VideoWriterWorker *writer = new VideoWriterWorker(m_params.outPath, finalW, finalH, fps, m_params.isMov);
    writer->start();

    int start = std::max(0, m_params.startFrame);
    int end = std::min(total, m_params.endFrame);
    if (end <= start) end = total;
    int processCount = end - start;
    cap.set(cv::CAP_PROP_POS_FRAMES, start);

    bool infinite = m_params.trailLength >= processCount;
    std::deque<cv::Mat> buffer;
    std::vector<float> weights;
    float fadeStart = std::max(0.05, 1.0 - m_params.fadeStrength);
    for (int i=0; i<m_params.trailLength; ++i) {
        float t = (float)i / (std::max(1, m_params.trailLength - 1));
        weights.push_back(fadeStart + t * (1.0f - fadeStart));
    }

    cv::Mat g_accum;
    cv::UMat u_accum;
    QElapsedTimer timer; timer.start();
    int p_h = 360;
    int p_w = (int)(finalW * ((double)p_h / finalH));

    for (int i=0; i < processCount; ++i) {
        if (!m_running) break;
        cv::Mat rawFrame;
        if (!cap.read(rawFrame)) break;
        cv::Mat frame_cpu = rawFrame(cropRect).clone();

        cv::Mat finalFrame;
        if (infinite) {
            if (m_params.useOpenCL) {
                cv::UMat u_frame = frame_cpu.getUMat(cv::ACCESS_READ);
                if (u_accum.empty()) u_accum = u_frame.clone();
                else cv::max(u_accum, u_frame, u_accum);
                finalFrame = u_accum.getMat(cv::ACCESS_READ);
            } else {
                if (g_accum.empty()) g_accum = frame_cpu.clone();
                else cv::max(g_accum, frame_cpu, g_accum);
                finalFrame = g_accum;
            }
        } else {
            buffer.push_back(frame_cpu.clone());
            if (buffer.size() > (size_t)m_params.trailLength) buffer.pop_front();
            size_t bLen = buffer.size();
            if (bLen > 1) {
                int wOffset = m_params.trailLength - bLen;
                cv::Mat accum;
                cv::convertScaleAbs(buffer[0], accum, weights[wOffset]);
                for (size_t k=1; k<bLen; ++k) {
                    float weight = weights[wOffset+k];
                    if (weight > 0.99f) cv::max(accum, buffer[k], accum);
                    else {
                        cv::Mat tmp; cv::convertScaleAbs(buffer[k], tmp, weight); cv::max(accum, tmp, accum);
                    }
                }
                finalFrame = accum;
            } else { finalFrame = frame_cpu; }
        }
        writer->addFrame(finalFrame);
        if (i % 5 == 0) {
            cv::Mat small; cv::resize(finalFrame, small, cv::Size(p_w, p_h), 0, 0, cv::INTER_NEAREST);
            emit previewUpdated(matToQImage(small));
            double elap = timer.elapsed() / 1000.0;
            double spd = (elap>0) ? (i+1)/elap : 0;
            emit progressUpdated(i+1, processCount, spd);
        }
    }
    cap.release(); writer->stop(); delete writer;
    emit finished(m_params.outPath);
}

// ================= CoverSelectorDialog =================
CoverSelectorDialog::CoverSelectorDialog(QString videoPath, QWidget *parent)
    : QDialog(parent), m_videoPath(videoPath)
{
    setWindowTitle("选择实况封面");
    resize(800, 600);
    m_cap = new cv::VideoCapture(videoPath.toStdString());
    m_totalFrames = (int)m_cap->get(cv::CAP_PROP_FRAME_COUNT);
    m_currentIdx = m_totalFrames - 1;
    QVBoxLayout *lay = new QVBoxLayout(this);
    m_lblPreview = new QLabel; m_lblPreview->setAlignment(Qt::AlignCenter); m_lblPreview->setStyleSheet("background: #000; border: 1px solid #333;"); m_lblPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); lay->addWidget(m_lblPreview);
    QHBoxLayout *ctrl = new QHBoxLayout;
    m_slider = new QSlider(Qt::Horizontal); m_slider->setRange(0, m_totalFrames-1); m_slider->setValue(m_currentIdx); connect(m_slider, &QSlider::valueChanged, this, &CoverSelectorDialog::onSliderValueChanged);
    m_lblInfo = new QLabel(QString::number(m_totalFrames)); ctrl->addWidget(new QLabel("选择封面:")); ctrl->addWidget(m_slider); ctrl->addWidget(m_lblInfo); lay->addLayout(ctrl);
    QHBoxLayout *btns = new QHBoxLayout; QPushButton *btnOk = new QPushButton("确认"); btnOk->setStyleSheet("background-color: #00A8E8; color: black; font-weight: bold; padding: 10px;"); connect(btnOk, &QPushButton::clicked, this, &QDialog::accept); btns->addStretch(); btns->addWidget(btnOk); lay->addLayout(btns);
    updatePreview();
}
CoverSelectorDialog::~CoverSelectorDialog() { delete m_cap; }
void CoverSelectorDialog::onSliderValueChanged(int v) { m_currentIdx = v; m_lblInfo->setText(QString("%1/%2").arg(v).arg(m_totalFrames)); updatePreview(); }
void CoverSelectorDialog::updatePreview() {
    m_cap->set(cv::CAP_PROP_POS_FRAMES, m_currentIdx); cv::Mat f;
    if(m_cap->read(f)) {
        m_selectedFrame = f.clone(); int h = f.rows; int dispH = 500; int dispW = (int)(f.cols * ((double)dispH/h));
        cv::Mat s; cv::resize(f, s, cv::Size(dispW, dispH)); m_lblPreview->setPixmap(QPixmap::fromImage(matToQImage(s)));
    }
}
cv::Mat CoverSelectorDialog::getSelectedImage() { return m_selectedFrame; }

// ================= MainWindow =================
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUi();
    m_processor = new ProcessorThread;
    connect(m_processor, &ProcessorThread::progressUpdated, this, &MainWindow::onProgress);
    connect(m_processor, &ProcessorThread::previewUpdated, this, &MainWindow::onPreviewUpdated);
    connect(m_processor, &ProcessorThread::finished, this, &MainWindow::onProcessingFinished);
    connect(m_processor, &ProcessorThread::errorOccurred, this, [this](QString m){ QMessageBox::critical(this, "Error", m); m_btnStart->setEnabled(true); });
}
MainWindow::~MainWindow() { if(m_processor->isRunning()) { m_processor->stop(); m_processor->wait(); } }

void MainWindow::setupUi() {
    setWindowTitle("StarTrail Pro (Accelerated)"); resize(1100, 750); setStyleSheet(ULTRA_DARK_STYLE);
    QWidget *cen = new QWidget; setCentralWidget(cen); QHBoxLayout *mainLay = new QHBoxLayout(cen); mainLay->setContentsMargins(0,0,0,0); mainLay->setSpacing(0);
    QFrame *side = new QFrame; side->setFixedWidth(320); side->setStyleSheet("background: #181818; border-right: 1px solid #333;");
    QVBoxLayout *sLay = new QVBoxLayout(side); sLay->setContentsMargins(15,25,15,25); sLay->setSpacing(15);
    m_dropLabel = new DropLabel; m_dropLabel->setFixedHeight(120);
    connect(m_dropLabel, &DropLabel::fileDropped, this, &MainWindow::onFileDropped); connect(m_dropLabel, &DropLabel::clicked, this, &MainWindow::selectInputFile);
    sLay->addWidget(m_dropLabel); m_lblFileName = new QLabel("未选择文件"); m_lblFileName->setStyleSheet("color: #777; font-size: 11px;"); sLay->addWidget(m_lblFileName);
    QGroupBox *grpP = new QGroupBox("参数"); QVBoxLayout *pl = new QVBoxLayout(grpP);
    QHBoxLayout *h1 = new QHBoxLayout; h1->addWidget(new QLabel("长度:")); m_spinTrail = new QSpinBox; m_spinTrail->setRange(1,99999); m_spinTrail->setValue(120); h1->addWidget(m_spinTrail);
    QHBoxLayout *h2 = new QHBoxLayout; h2->addWidget(new QLabel("柔和:")); m_spinFade = new QDoubleSpinBox; m_spinFade->setRange(0,0.99); m_spinFade->setValue(0.85); h2->addWidget(m_spinFade);
    pl->addLayout(h1); pl->addLayout(h2); sLay->addWidget(grpP); sLay->addStretch();
    m_btnStart = new QPushButton("配置并开始导出..."); m_btnStart->setFixedHeight(50); m_btnStart->setEnabled(false); m_btnStart->setStyleSheet("background-color: #00A8E8; color: black; font-weight: bold; font-size: 15px;");
    connect(m_btnStart, &QPushButton::clicked, this, &MainWindow::selectOutputPath); sLay->addWidget(m_btnStart); mainLay->addWidget(side);
    QWidget *pre = new QWidget; QVBoxLayout *prl = new QVBoxLayout(pre); m_lblPreview = new QLabel("PREVIEW"); m_lblPreview->setAlignment(Qt::AlignCenter); m_lblPreview->setStyleSheet("background: #000; border-radius: 6px;"); m_lblPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding); prl->addWidget(m_lblPreview);
    QHBoxLayout *inf = new QHBoxLayout; m_lblStatus = new QLabel("Ready"); m_lblSpeed = new QLabel(""); inf->addWidget(m_lblStatus); inf->addStretch(); inf->addWidget(m_lblSpeed); prl->addLayout(inf);
    m_progressBar = new QProgressBar; prl->addWidget(m_progressBar); mainLay->addWidget(pre);
}
void MainWindow::onFileDropped(QString path) { m_inPath = path; m_lblFileName->setText(QFileInfo(path).fileName()); m_btnStart->setEnabled(true); }
void MainWindow::selectInputFile() { QString path = QFileDialog::getOpenFileName(this, "选择视频"); if (!path.isEmpty()) onFileDropped(path); }
void MainWindow::selectOutputPath() { if (m_inPath.isEmpty()) return; RenderConfigDialog dlg(m_inPath, this); if (dlg.exec() != QDialog::Accepted) return; RenderSettings settings = dlg.getSettings(); QString suffix = settings.outputFormat; QString defaultName = QFileInfo(m_inPath).completeBaseName() + "_StarTrail" + suffix; QString savePath = QFileDialog::getSaveFileName(this, "保存", QFileInfo(m_inPath).dir().filePath(defaultName), "Video (*"+suffix+")"); if (savePath.isEmpty()) return; startRenderPipeline(settings, savePath); }
void MainWindow::startRenderPipeline(RenderSettings settings, QString savePath) {
    m_btnStart->setEnabled(false); m_dropLabel->setEnabled(false); m_wantLivePhoto = settings.exportLivePhoto; m_wantVideo = settings.exportVideo;
    ProcessParams p; p.inPath = m_inPath; p.outPath = savePath; p.trailLength = m_spinTrail->value(); p.fadeStrength = m_spinFade->value(); p.targetRes = settings.targetHeight; p.isMov = (settings.outputFormat == ".mov"); p.useOpenCL = settings.useOpenCL; p.startFrame = settings.startFrame; p.endFrame = settings.endFrame;
    cv::VideoCapture cap(m_inPath.toStdString()); int w = cap.get(cv::CAP_PROP_FRAME_WIDTH); int h = cap.get(cv::CAP_PROP_FRAME_HEIGHT); cap.release();
    if (settings.cropRatioMode == 99 && !settings.manualCropRect.isEmpty()) { QRect r = settings.manualCropRect; p.finalCropRect = cv::Rect(r.x(), r.y(), r.width(), r.height()); } else { p.finalCropRect = calculateRatioCrop(w, h, settings.cropRatioMode); }
    m_processor->setParams(p); m_processor->start();
}
void MainWindow::onProcessingFinished(QString outPath) { m_btnStart->setEnabled(true); m_dropLabel->setEnabled(true); m_lblStatus->setText("渲染完成"); if (m_wantLivePhoto) exportLivePhotoFlow(outPath); else QMessageBox::information(this, "完成", "视频已保存"); }
void MainWindow::exportLivePhotoFlow(QString videoPath) { CoverSelectorDialog dlg(videoPath, this); if (dlg.exec() == QDialog::Accepted) { cv::Mat cover = dlg.getSelectedImage(); QFileInfo vi(videoPath); QString jpgPath = vi.dir().filePath(vi.completeBaseName() + ".jpg"); std::string sJpg = jpgPath.toStdString(); cv::imwrite(sJpg, cover); QMessageBox::information(this, "成功", "实况照片已生成"); } }
void MainWindow::onPreviewUpdated(QImage img) { m_lblPreview->setPixmap(QPixmap::fromImage(img).scaled(m_lblPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)); }
void MainWindow::onProgress(int c, int t, double fps) { m_progressBar->setMaximum(t); m_progressBar->setValue(c); m_lblStatus->setText(QString("处理中... %1/%2").arg(c).arg(t)); m_lblSpeed->setText(QString::number(fps, 'f', 1) + " FPS"); }
