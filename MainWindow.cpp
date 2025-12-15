#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>
#include <QPainter>
#include <QMouseEvent>
#include <QDebug>
#include <QFile>
#include <QByteArray>
#include <QTimer>
#include <QDataStream>

// ================= MotionPhotoMuxer (动态照片生成器) =================
// 核心逻辑：构造符合 Google Photos 标准的 XMP Metadata 并插入 JPEG
bool MotionPhotoMuxer::mux(const QString &jpgPath, const QString &mp4Path, const QString &outPath) {
    QFile fJpg(jpgPath);
    QFile fMp4(mp4Path);

    if (!fJpg.open(QIODevice::ReadOnly) || !fMp4.open(QIODevice::ReadOnly)) return false;

    QByteArray jpgData = fJpg.readAll();
    QByteArray mp4Data = fMp4.readAll();
    fJpg.close();
    fMp4.close();

    if (jpgData.size() < 2 || (unsigned char)jpgData[0] != 0xFF || (unsigned char)jpgData[1] != 0xD8) {
        qDebug() << "Not a valid JPEG";
        return false;
    }

    // 1. 构造 XMP 数据包
    // 使用 Google MicroVideo V1 标准，这是最兼容的格式
    // 关键是 GCamera:MicroVideoOffset = 视频文件字节数
    long long videoSize = mp4Data.size();
    QString xmpContent =
        "<x:xmpmeta xmlns:x=\"adobe:ns:meta/\" x:xmptk=\"Adobe XMP Core 5.1.0-jc003\">"
        "  <rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">"
        "    <rdf:Description rdf:about=\"\""
        "        xmlns:GCamera=\"http://ns.google.com/photos/1.0/camera/\">"
        "      <GCamera:MicroVideo>1</GCamera:MicroVideo>"
        "      <GCamera:MicroVideoVersion>1</GCamera:MicroVideoVersion>"
        "      <GCamera:MicroVideoOffset>" + QString::number(videoSize) + "</GCamera:MicroVideoOffset>"
                                       "      <GCamera:MicroVideoPresentationTimestampUs>" + QString::number(videoSize / 2) + "</GCamera:MicroVideoPresentationTimestampUs>"
                                           "    </rdf:Description>"
                                           "  </rdf:RDF>"
                                           "</x:xmpmeta>";

    // 2. 构造 APP1 Header
    // 格式: FF E1 [Length 2bytes] [Namespace "http://ns.adobe.com/xap/1.0/\0"] [XMP Content]
    QByteArray app1;
    app1.append((char)0xFF);
    app1.append((char)0xE1);

    QByteArray namespaceUri = "http://ns.adobe.com/xap/1.0/";
    namespaceUri.append((char)0); // null terminator

    // 计算长度 (2 bytes length field + namespace len + xmp len)
    int length = 2 + namespaceUri.size() + xmpContent.toUtf8().size();
    app1.append((char)((length >> 8) & 0xFF));
    app1.append((char)(length & 0xFF));
    app1.append(namespaceUri);
    app1.append(xmpContent.toUtf8());

    // 3. 插入 APP1 到 JPEG (通常插在 SOI (FF D8) 之后)
    // 简单的插入逻辑：在 index 2 处插入
    QByteArray finalData = jpgData;
    finalData.insert(2, app1);

    // 4. 追加 MP4 数据到文件末尾
    finalData.append(mp4Data);

    // 5. 写入输出文件
    QFile fOut(outPath);
    if (!fOut.open(QIODevice::WriteOnly)) return false;
    fOut.write(finalData);
    fOut.close();

    return true;
}

// ================= 辅助函数 =================

cv::Mat customImread(const QString &path) {
    cv::Mat img;
    std::string sPath = path.toLocal8Bit().constData();
    QString ext = QFileInfo(path).suffix().toLower();

    if (ext == "dng" || ext == "tif" || ext == "tiff" || ext == "cr2" || ext == "nef" || ext == "arw") {
        std::vector<cv::Mat> pages;
        try {
            cv::imreadmulti(sPath, pages, cv::IMREAD_ANYCOLOR | cv::IMREAD_ANYDEPTH);
        } catch (...) { qDebug() << "imreadmulti failed for" << path; }

        if (!pages.empty()) {
            size_t maxIdx = 0; int maxPixels = 0;
            for (size_t i = 0; i < pages.size(); ++i) {
                if (pages[i].empty()) continue;
                int p = pages[i].cols * pages[i].rows;
                if (p > maxPixels) { maxPixels = p; maxIdx = i; }
            }
            if (maxPixels > 0) img = pages[maxIdx];
        }
    }

    if (img.empty()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readAll();
            if (!data.isEmpty()) {
                cv::Mat rawData(1, data.size(), CV_8UC1, (void*)data.constData());
                try { img = cv::imdecode(rawData, cv::IMREAD_UNCHANGED); } catch (...) {}
            }
        }
    }

    if (img.empty()) { try { img = cv::imread(sPath, cv::IMREAD_UNCHANGED); } catch (...) {} }
    return img;
}

QImage matToQImage(const cv::Mat &mat) {
    if(mat.empty()) return QImage();
    if(mat.type() == CV_8UC3) {
        QImage img(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_RGB888);
        return img.rgbSwapped();
    }
    if(mat.type() == CV_16UC3) {
        cv::Mat tmp; mat.convertTo(tmp, CV_8UC3, 255.0/65535.0);
        QImage img(tmp.data, tmp.cols, tmp.rows, tmp.step, QImage::Format_RGB888);
        return img.rgbSwapped();
    }
    if(mat.type() == CV_16UC1) {
        cv::Mat tmp, color; double scale = 255.0 / 65535.0;
        mat.convertTo(tmp, CV_8UC1, scale); cv::cvtColor(tmp, color, cv::COLOR_GRAY2RGB);
        QImage img(color.data, color.cols, color.rows, color.step, QImage::Format_RGB888);
        return img.copy();
    }
    return QImage();
}

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

// ================= FrameProvider Implementation =================
FrameProvider::FrameProvider() : m_isVideo(false), m_cap(nullptr), m_currentIndex(0), m_total(0), m_w(0), m_h(0), m_fps(30.0) {}
FrameProvider::~FrameProvider() { close(); }
void FrameProvider::close() { if (m_cap) { delete m_cap; m_cap = nullptr; } m_files.clear(); m_total = 0; }
bool FrameProvider::openVideo(const QString &path) {
    close(); m_isVideo = true; m_mainPath = path; m_cap = new cv::VideoCapture(path.toStdString());
    if (m_cap->isOpened()) {
        m_total = (int)m_cap->get(cv::CAP_PROP_FRAME_COUNT); m_w = (int)m_cap->get(cv::CAP_PROP_FRAME_WIDTH); m_h = (int)m_cap->get(cv::CAP_PROP_FRAME_HEIGHT);
        m_fps = m_cap->get(cv::CAP_PROP_FPS); if(m_fps <= 0) m_fps = 25.0; return true;
    } return false;
}
bool FrameProvider::openSequence(const QStringList &files) {
    close(); if(files.isEmpty()) return false; m_isVideo = false; m_files = files; m_files.sort();
    m_mainPath = files.first(); m_total = files.size(); m_currentIndex = 0; m_fps = 25.0;
    cv::Mat tmp = customImread(m_mainPath); if (!tmp.empty()) { m_w = tmp.cols; m_h = tmp.rows; return true; } return false;
}
bool FrameProvider::isOpened() const { return m_total > 0; }
int FrameProvider::totalFrames() const { return m_total; }
double FrameProvider::fps() const { return m_fps; }
int FrameProvider::width() const { return m_w; }
int FrameProvider::height() const { return m_h; }
QString FrameProvider::getSourcePath() const { return m_mainPath; }
bool FrameProvider::read(cv::Mat &image) {
    if (m_isVideo) { if (!m_cap) return false; return m_cap->read(image); }
    else {
        if (m_currentIndex >= m_files.size()) return false;
        image = customImread(m_files[m_currentIndex]);
        if (!image.empty() && image.type() == CV_16UC3) image.convertTo(image, CV_8UC3, 255.0/65535.0);
        else if (!image.empty() && image.type() == CV_16UC1) { cv::Mat t; image.convertTo(t, CV_8UC1, 255.0/65535.0); cv::cvtColor(t, image, cv::COLOR_GRAY2BGR); }
        if (!image.empty() && (image.cols != m_w || image.rows != m_h)) cv::resize(image, image, cv::Size(m_w, m_h));
        m_currentIndex++; return !image.empty();
    }
}
bool FrameProvider::seek(int frameIndex) {
    if (frameIndex < 0 || frameIndex >= m_total) return false;
    if (m_isVideo) return m_cap->set(cv::CAP_PROP_POS_FRAMES, frameIndex);
    else { m_currentIndex = frameIndex; return true; }
}

// ================= DropLabel Implementation =================
DropLabel::DropLabel(QWidget *parent) : QLabel(parent) {
    setText("\n📂\n拖入视频或图片序列(文件夹)\n支持 DNG/Raw/JPG"); setAlignment(Qt::AlignCenter); setObjectName("DropZone");
    setAcceptDrops(true); setCursor(Qt::PointingHandCursor); setStyleSheet("border: 2px dashed #444; border-radius: 10px; color: #888; font-size: 14px;");
}
void DropLabel::dragEnterEvent(QDragEnterEvent *e) { if (e->mimeData()->hasUrls()) e->acceptProposedAction(); }
void DropLabel::dropEvent(QDropEvent *e) {
    if (e->mimeData()->urls().isEmpty()) return;
    QStringList paths; for(const QUrl &url : e->mimeData()->urls()) paths.append(url.toLocalFile()); emit filesDropped(paths);
}
void DropLabel::mousePressEvent(QMouseEvent *e) { if (e->button() == Qt::LeftButton) emit clicked(); QLabel::mousePressEvent(e); }

// ================= CropEditorDialog Implementation =================
CropEditorDialog::CropEditorDialog(const cv::Mat &frame, QRect currentRect, QWidget *parent)
    : QDialog(parent), m_origFrame(frame), m_mode(ModeNone), m_activeHandle(HandleNone)
{
    setWindowTitle("框选裁剪区域"); resize(900, 650); setMouseTracking(true);
    QImage rawImg = matToQImage(frame);
    m_scaleFactor = std::min(850.0 / rawImg.width(), 600.0 / rawImg.height()); if(m_scaleFactor > 1.0) m_scaleFactor = 1.0;
    m_displayImage = rawImg.scaled(rawImg.width() * m_scaleFactor, rawImg.height() * m_scaleFactor, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_offsetX = (width() - m_displayImage.width()) / 2; m_offsetY = (height() - m_displayImage.height()) / 2;
    if(m_offsetX < 0) m_offsetX = 0; if(m_offsetY < 0) m_offsetY = 0;
    if (!currentRect.isEmpty() && currentRect.width() > 0) {
        m_selectionRect = QRect(currentRect.x() * m_scaleFactor + m_offsetX, currentRect.y() * m_scaleFactor + m_offsetY, currentRect.width() * m_scaleFactor, currentRect.height() * m_scaleFactor);
    } else { m_selectionRect = QRect(m_offsetX, m_offsetY, m_displayImage.width(), m_displayImage.height()); }
    QPushButton *btnOk = new QPushButton("确认", this);
    btnOk->setStyleSheet("background-color: #00A8E8; color: black; font-weight: bold; padding: 8px 20px; border-radius: 4px;");
    btnOk->setGeometry(width() - 130, height() - 50, 110, 35); connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
}
QRect CropEditorDialog::getFinalCropRect() {
    double rx = (m_selectionRect.x() - m_offsetX) / m_scaleFactor; double ry = (m_selectionRect.y() - m_offsetY) / m_scaleFactor;
    double rw = m_selectionRect.width() / m_scaleFactor; double rh = m_selectionRect.height() / m_scaleFactor;
    int x = std::max(0, (int)rx); int y = std::max(0, (int)ry); int w = std::min(m_origFrame.cols - x, (int)rw); int h = std::min(m_origFrame.rows - y, (int)rh);
    if (w <= 0 || h <= 0) return QRect(0,0, m_origFrame.cols, m_origFrame.rows); return QRect(x, y, w, h);
}
QRect CropEditorDialog::getImageRect() { return QRect(m_offsetX, m_offsetY, m_displayImage.width(), m_displayImage.height()); }
CropEditorDialog::ResizeHandle CropEditorDialog::hitTest(const QPoint &pos) {
    if (!m_selectionRect.isValid()) return HandleNone; int t = 10; QRect r = m_selectionRect;
    if (abs(pos.x()-r.left())<t && abs(pos.y()-r.top())<t) return HandleTopLeft; if (abs(pos.x()-r.right())<t && abs(pos.y()-r.top())<t) return HandleTopRight;
    if (abs(pos.x()-r.left())<t && abs(pos.y()-r.bottom())<t) return HandleBottomLeft; if (abs(pos.x()-r.right())<t && abs(pos.y()-r.bottom())<t) return HandleBottomRight;
    if (abs(pos.x()-r.left())<t && pos.y()>r.top() && pos.y()<r.bottom()) return HandleLeft; if (abs(pos.x()-r.right())<t && pos.y()>r.top() && pos.y()<r.bottom()) return HandleRight;
    if (abs(pos.y()-r.top())<t && pos.x()>r.left() && pos.x()<r.right()) return HandleTop; if (abs(pos.y()-r.bottom())<t && pos.x()>r.left() && pos.x()<r.right()) return HandleBottom;
    if (r.contains(pos)) return HandleInside; return HandleNone;
}
void CropEditorDialog::updateCursorIcon(const QPoint &pos) {
    ResizeHandle h = hitTest(pos);
    switch(h) { case HandleTopLeft: case HandleBottomRight: setCursor(Qt::SizeFDiagCursor); break; case HandleTopRight: case HandleBottomLeft: setCursor(Qt::SizeBDiagCursor); break; case HandleTop: case HandleBottom: setCursor(Qt::SizeVerCursor); break; case HandleLeft: case HandleRight: setCursor(Qt::SizeHorCursor); break; case HandleInside: setCursor(Qt::SizeAllCursor); break; default: setCursor(Qt::CrossCursor); break; }
}
void CropEditorDialog::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) { m_lastMousePos = e->pos(); ResizeHandle h = hitTest(e->pos()); if (h != HandleNone && h != HandleInside) { m_mode = ModeResizing; m_activeHandle = h; } else if (h == HandleInside) { m_mode = ModeMoving; } else { m_mode = ModeDrawing; m_startPos = e->pos(); m_selectionRect = QRect(); } update(); }
}
void CropEditorDialog::mouseMoveEvent(QMouseEvent *e) {
    if (m_mode == ModeNone) { updateCursorIcon(e->pos()); return; } QPoint diff = e->pos() - m_lastMousePos; QRect imgRect = getImageRect();
    if (m_mode == ModeMoving) { m_selectionRect.translate(diff); m_selectionRect = m_selectionRect.intersected(imgRect); }
    else if (m_mode == ModeResizing) { QRect r = m_selectionRect; int minS = 20; if (m_activeHandle==HandleLeft||m_activeHandle==HandleTopLeft||m_activeHandle==HandleBottomLeft) r.setLeft(std::min(r.right()-minS, r.left()+diff.x())); if (m_activeHandle==HandleRight||m_activeHandle==HandleTopRight||m_activeHandle==HandleBottomRight) r.setRight(std::max(r.left()+minS, r.right()+diff.x())); if (m_activeHandle==HandleTop||m_activeHandle==HandleTopLeft||m_activeHandle==HandleTopRight) r.setTop(std::min(r.bottom()-minS, r.top()+diff.y())); if (m_activeHandle==HandleBottom||m_activeHandle==HandleBottomLeft||m_activeHandle==HandleBottomRight) r.setBottom(std::max(r.top()+minS, r.bottom()+diff.y())); m_selectionRect = r.normalized().intersected(imgRect); }
    else if (m_mode == ModeDrawing) { m_selectionRect = QRect(m_startPos, e->pos()).normalized().intersected(imgRect); } m_lastMousePos = e->pos(); update();
}
void CropEditorDialog::mouseReleaseEvent(QMouseEvent *) { m_mode = ModeNone; m_activeHandle = HandleNone; m_selectionRect = m_selectionRect.normalized(); }
void CropEditorDialog::paintEvent(QPaintEvent *) {
    QPainter p(this); p.fillRect(rect(), QColor("#121212")); m_offsetX = (width() - m_displayImage.width()) / 2; m_offsetY = (height() - m_displayImage.height()) / 2;
    if(m_offsetX < 0) m_offsetX = 0; if(m_offsetY < 0) m_offsetY = 0; p.drawImage(m_offsetX, m_offsetY, m_displayImage); QRect imgRect = getImageRect(); QRegion all(imgRect); QRegion crop(m_selectionRect); QRegion mask = all.subtracted(crop); p.setClipRegion(mask); p.fillRect(imgRect, QColor(0, 0, 0, 160)); p.setClipping(false);
    if (m_selectionRect.isValid()) { p.setPen(QPen(QColor("#00A8E8"), 2, Qt::SolidLine)); p.setBrush(Qt::NoBrush); p.drawRect(m_selectionRect); p.setBrush(QColor("#00A8E8")); p.setPen(Qt::NoPen); int hs = 8, hs2 = 4; QVector<QPoint> pts = {m_selectionRect.topLeft(), m_selectionRect.topRight(), m_selectionRect.bottomLeft(), m_selectionRect.bottomRight()}; for(const QPoint &pt : pts) p.drawRect(pt.x()-hs2, pt.y()-hs2, hs, hs); p.setPen(Qt::white); p.drawText(m_selectionRect.topLeft()-QPoint(0, 8), QString("%1 x %2").arg((int)(m_selectionRect.width()/m_scaleFactor)).arg((int)(m_selectionRect.height()/m_scaleFactor))); }
}

// ================= RenderConfigDialog Implementation =================
RenderConfigDialog::RenderConfigDialog(FrameProvider *provider, QWidget *parent)
    : QDialog(parent), m_provider(provider)
{
    setWindowTitle("导出与裁剪设置"); resize(650, 750);
    QVBoxLayout *lay = new QVBoxLayout(this);

    QGroupBox *grpPrev = new QGroupBox("视频时长选择"); QVBoxLayout *lPrev = new QVBoxLayout(grpPrev);
    m_lblVideoPreview = new QLabel; m_lblVideoPreview->setAlignment(Qt::AlignCenter); m_lblVideoPreview->setStyleSheet("background: #000; border: 1px solid #333;"); m_lblVideoPreview->setMinimumHeight(240); lPrev->addWidget(m_lblVideoPreview);
    QHBoxLayout *hSlider = new QHBoxLayout;
    m_sliderTimeline = new QSlider(Qt::Horizontal); m_sliderTimeline->setRange(0, m_provider->totalFrames() - 1); m_sliderTimeline->setValue(0); connect(m_sliderTimeline, &QSlider::valueChanged, this, &RenderConfigDialog::onTimelineChanged);
    m_lblCurrentFrame = new QLabel("第 0 帧"); m_lblCurrentFrame->setFixedWidth(80); hSlider->addWidget(m_sliderTimeline); hSlider->addWidget(m_lblCurrentFrame); lPrev->addLayout(hSlider);
    QHBoxLayout *hSetBtns = new QHBoxLayout; QPushButton *btnSetStart = new QPushButton("设为起点"); QPushButton *btnSetEnd = new QPushButton("设为终点"); connect(btnSetStart, &QPushButton::clicked, this, &RenderConfigDialog::onSetStartClicked); connect(btnSetEnd, &QPushButton::clicked, this, &RenderConfigDialog::onSetEndClicked); hSetBtns->addStretch(); hSetBtns->addWidget(btnSetStart); hSetBtns->addWidget(btnSetEnd); hSetBtns->addStretch(); lPrev->addLayout(hSetBtns); lay->addWidget(grpPrev);

    QGroupBox *grpCrop = new QGroupBox("1. 裁剪"); QVBoxLayout *lCrop = new QVBoxLayout(grpCrop);
    QHBoxLayout *hTime = new QHBoxLayout; m_spinStartFrame = new QSpinBox; m_spinStartFrame->setRange(0, m_provider->totalFrames() - 1); m_spinStartFrame->setValue(0); m_spinEndFrame = new QSpinBox; m_spinEndFrame->setRange(1, m_provider->totalFrames()); m_spinEndFrame->setValue(m_provider->totalFrames()); hTime->addWidget(new QLabel("帧范围:")); hTime->addWidget(m_spinStartFrame); hTime->addWidget(new QLabel("至")); hTime->addWidget(m_spinEndFrame); lCrop->addLayout(hTime); m_lblDuration = new QLabel; lCrop->addWidget(m_lblDuration); connect(m_spinStartFrame, QOverload<int>::of(&QSpinBox::valueChanged), this, &RenderConfigDialog::updateDurationLabel); connect(m_spinEndFrame, QOverload<int>::of(&QSpinBox::valueChanged), this, &RenderConfigDialog::updateDurationLabel);

    QHBoxLayout *hRatio = new QHBoxLayout; hRatio->addWidget(new QLabel("画幅:")); m_cmbCropRatio = new QComboBox; m_cmbCropRatio->addItem("原始比例", 0); m_cmbCropRatio->addItem("16:9", 1); m_cmbCropRatio->addItem("9:16", 2); m_cmbCropRatio->addItem("1:1", 3); m_cmbCropRatio->addItem("4:5", 4); m_cmbCropRatio->addItem("手动框选...", 99); hRatio->addWidget(m_cmbCropRatio); lCrop->addLayout(hRatio);
    m_btnEditCrop = new QPushButton("📐 编辑区域"); m_btnEditCrop->setVisible(false); connect(m_btnEditCrop, &QPushButton::clicked, this, &RenderConfigDialog::openCropEditor); lCrop->addWidget(m_btnEditCrop); connect(m_cmbCropRatio, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &RenderConfigDialog::onCropModeChanged); lay->addWidget(grpCrop);

    QGroupBox *grpRes = new QGroupBox("2. 导出设置"); QVBoxLayout *lRes = new QVBoxLayout(grpRes);
    m_cmbRes = new QComboBox; m_cmbRes->addItem("原始分辨率", 0); m_cmbRes->addItem("4K (2160p)", 2160); m_cmbRes->addItem("1080p", 1080); m_cmbRes->addItem("720p", 720); m_cmbRes->setCurrentIndex(2);
    lRes->addWidget(new QLabel("输出分辨率:")); lRes->addWidget(m_cmbRes);

    QHBoxLayout *hFps = new QHBoxLayout; hFps->addWidget(new QLabel("输出帧率(FPS):"));
    m_spinFps = new QDoubleSpinBox; m_spinFps->setRange(1.0, 120.0); m_spinFps->setSingleStep(1.0); m_spinFps->setValue(m_provider->fps());
    connect(m_spinFps, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &RenderConfigDialog::updateDurationLabel);
    hFps->addWidget(m_spinFps); lRes->addLayout(hFps); lay->addWidget(grpRes);

    updateDurationLabel();

    QGroupBox *grpMode = new QGroupBox("3. 导出"); QVBoxLayout *lMode = new QVBoxLayout(grpMode); m_rbVideoOnly = new QRadioButton("仅视频"); m_rbVideoOnly->setChecked(true); m_rbLivePhoto = new QRadioButton("仅实况"); m_rbBoth = new QRadioButton("全部"); lMode->addWidget(m_rbVideoOnly); lMode->addWidget(m_rbLivePhoto); lMode->addWidget(m_rbBoth); lay->addWidget(grpMode);
    QHBoxLayout *hFmt = new QHBoxLayout; m_cmbFormat = new QComboBox; m_cmbFormat->addItem("MP4", ".mp4"); m_cmbFormat->addItem("MOV", ".mov"); hFmt->addWidget(new QLabel("格式:")); hFmt->addWidget(m_cmbFormat); lay->addLayout(hFmt); m_chkOpenCL = new QCheckBox("GPU加速"); m_chkOpenCL->setChecked(true); lay->addWidget(m_chkOpenCL);
    lay->addStretch();
    QHBoxLayout *hBtn = new QHBoxLayout; QPushButton *btnC = new QPushButton("取消"); QPushButton *btnOk = new QPushButton("开始"); btnOk->setStyleSheet("background-color: #00A8E8; color: black; font-weight: bold;"); connect(btnC, &QPushButton::clicked, this, &QDialog::reject); connect(btnOk, &QPushButton::clicked, this, &QDialog::accept); hBtn->addStretch(); hBtn->addWidget(btnC); hBtn->addWidget(btnOk); lay->addLayout(hBtn);

    QTimer *debounceTimer = new QTimer(this); debounceTimer->setObjectName("previewTimer"); debounceTimer->setSingleShot(true); debounceTimer->setInterval(150);
    connect(debounceTimer, &QTimer::timeout, this, [this](){ int value = m_sliderTimeline->value(); m_provider->seek(value); cv::Mat f; if (m_provider->read(f)) { int w = m_lblVideoPreview->width(); int h = m_lblVideoPreview->height(); if(w<=0)w=400; if(h<=0)h=240; cv::resize(f, f, cv::Size(w, h)); QImage img = matToQImage(f); m_lblVideoPreview->setPixmap(QPixmap::fromImage(img)); } });
    onTimelineChanged(0);
}
RenderConfigDialog::~RenderConfigDialog() {}

void RenderConfigDialog::onTimelineChanged(int value) { m_lblCurrentFrame->setText(QString("%1").arg(value)); QTimer *t = findChild<QTimer*>("previewTimer"); if(t) t->start(); }
void RenderConfigDialog::onSetStartClicked() { m_spinStartFrame->setValue(m_sliderTimeline->value()); }
void RenderConfigDialog::onSetEndClicked() { m_spinEndFrame->setValue(m_sliderTimeline->value()); }
void RenderConfigDialog::updateDurationLabel() {
    int s=m_spinStartFrame->value(); int e=m_spinEndFrame->value(); if(s>=e){m_spinStartFrame->setValue(e-1); s=e-1;}
    double fps = m_spinFps->value(); if(fps <= 0) fps = 25.0;
    m_lblDuration->setText(QString("长: %1 帧 (%2 s @ %3 fps)").arg(e-s).arg((e-s)/fps, 0, 'f', 1).arg(fps));
}
void RenderConfigDialog::onCropModeChanged(int i) { if(m_cmbCropRatio->itemData(i).toInt()==99) { m_btnEditCrop->setVisible(true); if(m_currentManualRect.isEmpty()) openCropEditor(); } else m_btnEditCrop->setVisible(false); }
void RenderConfigDialog::openCropEditor() { m_provider->seek(m_sliderTimeline->value()); cv::Mat f; m_provider->read(f); if(f.empty()) return; CropEditorDialog dlg(f, m_currentManualRect, this); if(dlg.exec()==QDialog::Accepted) { m_currentManualRect = dlg.getFinalCropRect(); m_btnEditCrop->setText(QString("区域: %1x%2").arg(m_currentManualRect.width()).arg(m_currentManualRect.height())); } }
RenderSettings RenderConfigDialog::getSettings() {
    RenderSettings s; s.targetHeight=m_cmbRes->currentData().toInt(); s.exportVideo=m_rbVideoOnly->isChecked()||m_rbBoth->isChecked(); s.exportLivePhoto=m_rbLivePhoto->isChecked()||m_rbBoth->isChecked(); s.useOpenCL=m_chkOpenCL->isChecked(); s.outputFormat=m_cmbFormat->currentData().toString(); s.startFrame=m_spinStartFrame->value(); s.endFrame=m_spinEndFrame->value(); s.cropRatioMode=m_cmbCropRatio->currentData().toInt(); s.manualCropRect=m_currentManualRect; s.targetFps = m_spinFps->value(); return s;
}

// ================= VideoWriterWorker Implementation =================
VideoWriterWorker::VideoWriterWorker(QString path, int w, int h, double fps, bool isMov)
    : m_path(path), m_width(w), m_height(h), m_fps(fps), m_running(true) { if(m_width%2!=0)m_width--; if(m_height%2!=0)m_height--; }
void VideoWriterWorker::addFrame(const cv::Mat &frame) { while (true) { m_mutex.lock(); if (m_queue.size() < 15) { m_queue.enqueue(frame.clone()); m_condition.wakeOne(); m_mutex.unlock(); break; } m_mutex.unlock(); QThread::msleep(30); } }
void VideoWriterWorker::stop() { m_running=false; m_condition.wakeAll(); wait(); }
void VideoWriterWorker::run() {
    int fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1'); cv::VideoWriter writer;
    writer.open(m_path.toStdString(), fourcc, m_fps, cv::Size(m_width, m_height));
    while(m_running || !m_queue.isEmpty()) { cv::Mat frame; { QMutexLocker l(&m_mutex); while(m_queue.isEmpty()&&m_running) m_condition.wait(&m_mutex); if(!m_queue.isEmpty()) frame=m_queue.dequeue(); } if(!frame.empty()) { if(frame.cols!=m_width || frame.rows!=m_height) cv::resize(frame, frame, cv::Size(m_width, m_height), 0, 0, cv::INTER_AREA); writer.write(frame); } } writer.release();
}

// ================= ProcessorThread Implementation =================
void ProcessorThread::setParams(const ProcessParams &params) { m_params = params; }
void ProcessorThread::stop() { m_running = false; }
void ProcessorThread::run() {
    m_running = true; FrameProvider provider;
    if (m_params.isVideo) { if(!provider.openVideo(m_params.videoPath)) { emit errorOccurred("无法打开视频"); return; } }
    else { if(!provider.openSequence(m_params.imageFiles)) { emit errorOccurred("无法打开图片序列"); return; } }
    cv::ocl::setUseOpenCL(m_params.useOpenCL);
    int total = provider.totalFrames();
    double fps = m_params.targetFps > 0 ? m_params.targetFps : 30.0;

    cv::Rect cropRect = m_params.finalCropRect;
    int finalW = cropRect.width; int finalH = cropRect.height;
    if(m_params.targetRes>0 && m_params.targetRes<finalH) { double s = (double)m_params.targetRes/finalH; finalW=(int)(finalW*s); finalH=m_params.targetRes; }
    VideoWriterWorker *writer = new VideoWriterWorker(m_params.outPath, finalW, finalH, fps, m_params.isMov);
    writer->start();

    int start = std::max(0, m_params.startFrame); int end = std::min(total, m_params.endFrame); if(end<=start) end=total;
    int processCount = end - start; provider.seek(start);
    bool infinite = m_params.trailLength >= processCount;
    std::deque<cv::Mat> buffer; std::vector<float> weights; float fadeStart = std::max(0.05, 1.0 - m_params.fadeStrength);
    for(int i=0; i<m_params.trailLength; ++i) { float t=(float)i/std::max(1,m_params.trailLength-1); weights.push_back(fadeStart+t*(1.0f-fadeStart)); }
    cv::Mat g_accum; cv::UMat u_accum; QElapsedTimer timer; timer.start(); int p_h=360; int p_w=(int)(finalW*((double)p_h/finalH));

    for(int i=0; i<processCount; ++i) {
        if(!m_running) break; cv::Mat rawFrame; if(!provider.read(rawFrame)) break; cv::Mat frame_cpu = rawFrame(cropRect).clone(); cv::Mat finalFrame;
        if(infinite) { if(m_params.useOpenCL) { cv::UMat u_frame = frame_cpu.getUMat(cv::ACCESS_READ); if(u_accum.empty()) u_accum=u_frame.clone(); else cv::max(u_accum, u_frame, u_accum); finalFrame = u_accum.getMat(cv::ACCESS_READ); } else { if(g_accum.empty()) g_accum=frame_cpu.clone(); else cv::max(g_accum, frame_cpu, g_accum); finalFrame=g_accum; } }
        else { buffer.push_back(frame_cpu.clone()); if(buffer.size()>(size_t)m_params.trailLength) buffer.pop_front(); size_t bLen = buffer.size(); if(bLen>1) { int off=m_params.trailLength-bLen; cv::Mat accum; cv::convertScaleAbs(buffer[0], accum, weights[off]); cv::Mat tmp; for(size_t k=1; k<bLen; ++k) { float w=weights[off+k]; if(w>0.99f) cv::max(accum, buffer[k], accum); else { cv::convertScaleAbs(buffer[k], tmp, w); cv::max(accum, tmp, accum); } } finalFrame = accum; } else finalFrame = frame_cpu; }
        writer->addFrame(finalFrame);
        if(i%5==0) { cv::Mat small; cv::resize(finalFrame, small, cv::Size(p_w, p_h), 0, 0, cv::INTER_NEAREST); emit previewUpdated(matToQImage(small)); double e=timer.elapsed()/1000.0; emit progressUpdated(i+1, processCount, (e>0)?(i+1)/e:0); }
    }
    writer->stop(); delete writer; emit finished(m_params.outPath);
}

// ================= CoverSelectorDialog Implementation (Fixed) =================
CoverSelectorDialog::CoverSelectorDialog(QString videoPath, QWidget *parent)
    : QDialog(parent), m_videoPath(videoPath)
{
    setWindowTitle("选择实况封面");
    resize(800, 600);
    m_cap = new cv::VideoCapture(videoPath.toStdString());
    m_totalFrames = (int)m_cap->get(cv::CAP_PROP_FRAME_COUNT);
    m_currentIdx = m_totalFrames - 1;

    QVBoxLayout *lay = new QVBoxLayout(this);
    m_lblPreview = new QLabel;
    m_lblPreview->setAlignment(Qt::AlignCenter);
    m_lblPreview->setStyleSheet("background: #000; border: 1px solid #333;");
    m_lblPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    lay->addWidget(m_lblPreview);

    QHBoxLayout *ctrl = new QHBoxLayout;
    m_slider = new QSlider(Qt::Horizontal);
    m_slider->setRange(0, m_totalFrames - 1);
    m_slider->setValue(m_currentIdx);
    connect(m_slider, &QSlider::valueChanged, this, &CoverSelectorDialog::onSliderValueChanged);

    m_lblInfo = new QLabel(QString::number(m_totalFrames));
    ctrl->addWidget(new QLabel("选择封面:"));
    ctrl->addWidget(m_slider);
    ctrl->addWidget(m_lblInfo);
    lay->addLayout(ctrl);

    QHBoxLayout *btns = new QHBoxLayout;
    QPushButton *btnOk = new QPushButton("确认");
    btnOk->setStyleSheet("background-color: #00A8E8; color: black; font-weight: bold; padding: 10px;");
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    btns->addStretch();
    btns->addWidget(btnOk);
    lay->addLayout(btns);

    updatePreview();
}

CoverSelectorDialog::~CoverSelectorDialog() {
    if (m_cap) { m_cap->release(); delete m_cap; }
}

void CoverSelectorDialog::onSliderValueChanged(int v) {
    m_currentIdx = v;
    m_lblInfo->setText(QString("%1/%2").arg(v).arg(m_totalFrames));
    updatePreview();
}

void CoverSelectorDialog::updatePreview() {
    if (!m_cap) return;
    m_cap->set(cv::CAP_PROP_POS_FRAMES, m_currentIdx);
    cv::Mat f;
    if (m_cap->read(f)) {
        m_selectedFrame = f.clone();
        int h = f.rows; int dispH = 500; int dispW = (int)(f.cols * ((double)dispH / h));
        cv::Mat s; cv::resize(f, s, cv::Size(dispW, dispH));
        m_lblPreview->setPixmap(QPixmap::fromImage(matToQImage(s)));
    }
}

cv::Mat CoverSelectorDialog::getSelectedImage() { return m_selectedFrame; }

// ================= MainWindow Implementation =================
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), m_inputProvider(new FrameProvider) {
    setupUi();
    m_processor = new ProcessorThread;
    connect(m_processor, &ProcessorThread::progressUpdated, this, &MainWindow::onProgress);
    connect(m_processor, &ProcessorThread::previewUpdated, this, &MainWindow::onPreviewUpdated);
    connect(m_processor, &ProcessorThread::finished, this, &MainWindow::onProcessingFinished);
    connect(m_processor, &ProcessorThread::errorOccurred, this, [this](QString m){ QMessageBox::critical(this, "Error", m); m_btnStart->setEnabled(true); });
}
MainWindow::~MainWindow() { if(m_processor->isRunning()) { m_processor->stop(); m_processor->wait(); } delete m_inputProvider; }
void MainWindow::setupUi() {
    setWindowTitle("StarTrail Pro (Raw/DNG Support)"); resize(1100, 750); setStyleSheet(ULTRA_DARK_STYLE);
    QWidget *cen = new QWidget; setCentralWidget(cen); QHBoxLayout *mainLay = new QHBoxLayout(cen); mainLay->setContentsMargins(0,0,0,0); mainLay->setSpacing(0);
    QFrame *side = new QFrame; side->setFixedWidth(320); side->setStyleSheet("background: #181818; border-right: 1px solid #333;");
    QVBoxLayout *sLay = new QVBoxLayout(side); sLay->setContentsMargins(15,25,15,25); sLay->setSpacing(15);
    m_dropLabel = new DropLabel; m_dropLabel->setFixedHeight(120); connect(m_dropLabel, &DropLabel::filesDropped, this, &MainWindow::onFilesDropped); connect(m_dropLabel, &DropLabel::clicked, this, &MainWindow::selectInput); sLay->addWidget(m_dropLabel); m_lblFileName = new QLabel("未选择文件"); m_lblFileName->setStyleSheet("color: #777; font-size: 11px;"); sLay->addWidget(m_lblFileName);
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
void MainWindow::onFilesDropped(QStringList paths) {
    if(paths.isEmpty()) return; QStringList validImages;
    for(const QString &p : paths) { QFileInfo fi(p); if(fi.isDir()) { QDir dir(p); QStringList filters = {"*.jpg", "*.jpeg", "*.png", "*.tif", "*.tiff", "*.dng", "*.cr2", "*.nef", "*.arw"}; QStringList imgs = dir.entryList(filters, QDir::Files); for(const QString &img : imgs) validImages.append(dir.filePath(img)); } else { QString ext = fi.suffix().toLower(); if(ext == "mp4" || ext == "mov" || ext == "avi" || ext == "mkv") { if(m_inputProvider->openVideo(p)) { m_lblFileName->setText("视频: " + fi.fileName()); m_btnStart->setEnabled(true); return; } } else { validImages.append(p); } } }
    if(!validImages.isEmpty()) { if(m_inputProvider->openSequence(validImages)) { m_lblFileName->setText(QString("图片序列: %1 张").arg(validImages.size())); m_btnStart->setEnabled(true); } else { QMessageBox::warning(this, "Error", "无法加载图片序列"); } }
}
void MainWindow::selectInput() { QString p = QFileDialog::getOpenFileName(this, "选择视频或第一张图片", "", "Media (*.mp4 *.mov *.dng *.jpg *.png *.tif);;All (*.*)"); if(!p.isEmpty()) { QStringList lst; lst << p; QString ext = QFileInfo(p).suffix().toLower(); if(ext != "mp4" && ext != "mov" && ext != "avi") { QDir dir = QFileInfo(p).dir(); QStringList filters = {"*."+ext}; QStringList siblings = dir.entryList(filters, QDir::Files); if(siblings.size() > 1) { if(QMessageBox::question(this, "序列检测", QString("检测到同目录下有 %1 张图片，是否作为序列导入？").arg(siblings.size())) == QMessageBox::Yes) { QStringList fullPaths; for(const QString &s : siblings) fullPaths << dir.filePath(s); onFilesDropped(fullPaths); return; } } } onFilesDropped(lst); } }
void MainWindow::selectOutputPath() {
    if (!m_inputProvider->isOpened()) return;
    RenderConfigDialog dlg(m_inputProvider, this);
    if (dlg.exec() != QDialog::Accepted) return;
    RenderSettings settings = dlg.getSettings();
    QString suffix = settings.outputFormat;
    QString defaultName = QFileInfo(m_inputProvider->getSourcePath()).completeBaseName() + "_StarTrail" + suffix;
    QString savePath = QFileDialog::getSaveFileName(this, "保存", QFileInfo(m_inputProvider->getSourcePath()).dir().filePath(defaultName), "Video (*"+suffix+")");
    if (savePath.isEmpty()) return;
    startRenderPipeline(settings, savePath);
}
void MainWindow::startRenderPipeline(RenderSettings settings, QString savePath) {
    m_btnStart->setEnabled(false); m_dropLabel->setEnabled(false); m_wantLivePhoto = settings.exportLivePhoto; m_wantVideo = settings.exportVideo;
    ProcessParams p; p.isVideo = m_inputProvider->isVideo();
    if(p.isVideo) p.videoPath = m_inputProvider->getSourcePath();
    else { QFileInfo firstFile(m_inputProvider->getSourcePath()); QDir dir = firstFile.dir(); QStringList filters; filters << "*." + firstFile.suffix(); QStringList fList = dir.entryList(filters, QDir::Files); fList.sort(); for(const QString &f : fList) p.imageFiles << dir.filePath(f); }
    p.outPath = savePath; p.trailLength = m_spinTrail->value(); p.fadeStrength = m_spinFade->value(); p.targetRes = settings.targetHeight; p.isMov = (settings.outputFormat == ".mov"); p.useOpenCL = settings.useOpenCL; p.startFrame = settings.startFrame; p.endFrame = settings.endFrame; p.targetFps = settings.targetFps;
    if (settings.cropRatioMode == 99 && !settings.manualCropRect.isEmpty()) { QRect r = settings.manualCropRect; p.finalCropRect = cv::Rect(r.x(), r.y(), r.width(), r.height()); } else { p.finalCropRect = calculateRatioCrop(m_inputProvider->width(), m_inputProvider->height(), settings.cropRatioMode); }
    m_processor->setParams(p); m_processor->start();
}
void MainWindow::onProcessingFinished(QString outPath) {
    m_btnStart->setEnabled(true); m_dropLabel->setEnabled(true);
    m_lblStatus->setText("渲染完成");

    // 渲染结束，如果用户选择了动态照片
    if (m_wantLivePhoto) {
        // 先生成了一个视频文件 outPath
        // 现在要让用户选封面，并生成最终的 Motion Photo (JPG)

        CoverSelectorDialog dlg(outPath, this);
        if (dlg.exec() == QDialog::Accepted) {
            cv::Mat cover = dlg.getSelectedImage();

            // 构造输出的 JPG 路径 (同目录)
            QFileInfo vi(outPath);
            QString finalJpgPath = vi.dir().filePath(vi.completeBaseName() + ".jpg");

            // 先保存一个临时 JPG 用于封装
            QString tempJpg = finalJpgPath + ".tmp.jpg";
            cv::imwrite(tempJpg.toStdString(), cover);

            // 调用合成器
            bool ok = MotionPhotoMuxer::mux(tempJpg, outPath, finalJpgPath);
            QFile::remove(tempJpg); // 清理

            QString msg = ok ? "成功生成动态照片: " + finalJpgPath : "动态照片合成失败";
            if(ok && !m_wantVideo) QFile::remove(outPath); // 如果只想要实况，删掉中间视频

            QMessageBox::information(this, "结果", msg);
        }
    } else {
        QMessageBox::information(this, "完成", "视频已保存");
    }
}
void MainWindow::exportLivePhotoFlow(QString videoPath) { /* Unused now, logic moved to onProcessingFinished */ }
void MainWindow::onPreviewUpdated(QImage img) { m_lblPreview->setPixmap(QPixmap::fromImage(img).scaled(m_lblPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation)); }
void MainWindow::onProgress(int c, int t, double fps) { m_progressBar->setMaximum(t); m_progressBar->setValue(c); m_lblStatus->setText(QString("处理中... %1/%2").arg(c).arg(t)); m_lblSpeed->setText(QString::number(fps, 'f', 1) + " FPS"); }
