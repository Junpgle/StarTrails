#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QElapsedTimer>
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

// ================= DropLabel (完善点击和拖拽) =================
DropLabel::DropLabel(QWidget *parent) : QLabel(parent) {
    setText("\n📂\n点击或拖拽视频文件\n到此处");
    setAlignment(Qt::AlignCenter);
    setObjectName("DropZone");
    setAcceptDrops(true);
    setCursor(Qt::PointingHandCursor);
    setStyleSheet("border: 2px dashed #444; border-radius: 10px; color: #888; font-size: 14px;");
}

void DropLabel::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void DropLabel::dropEvent(QDropEvent *event) {
    QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty()) {
        emit fileDropped(urls.first().toLocalFile());
    }
}

void DropLabel::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        emit clicked();
    }
    QLabel::mousePressEvent(event);
}

// ================= RenderConfigDialog (新导出对话框) =================
RenderConfigDialog::RenderConfigDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("导出设置");
    resize(400, 350);
    QVBoxLayout *lay = new QVBoxLayout(this);

    // 1. 分辨率 (影响速度)
    QGroupBox *grpRes = new QGroupBox("输出分辨率 (分辨率越低越快)");
    QVBoxLayout *lRes = new QVBoxLayout(grpRes);
    m_cmbRes = new QComboBox;
    m_cmbRes->addItem("原始分辨率 (最慢, 画质最高)", 0);
    m_cmbRes->addItem("4K UHD (2160p)", 2160);
    m_cmbRes->addItem("Full HD (1080p) - 推荐", 1080);
    m_cmbRes->addItem("HD (720p) - 极速", 720);
    m_cmbRes->setCurrentIndex(2); // 默认 1080p
    lRes->addWidget(m_cmbRes);
    lay->addWidget(grpRes);

    // 2. 导出内容
    QGroupBox *grpMode = new QGroupBox("导出内容");
    QVBoxLayout *lMode = new QVBoxLayout(grpMode);
    m_rbVideoOnly = new QRadioButton("仅导出视频文件");
    m_rbLivePhoto = new QRadioButton("仅导出实况照片 (Live Photo)");
    m_rbBoth = new QRadioButton("同时导出视频和实况照片");
    m_rbVideoOnly->setChecked(true);

    QButtonGroup *bg = new QButtonGroup(this);
    bg->addButton(m_rbVideoOnly);
    bg->addButton(m_rbLivePhoto);
    bg->addButton(m_rbBoth);
    lMode->addWidget(m_rbVideoOnly);
    lMode->addWidget(m_rbLivePhoto);
    lMode->addWidget(m_rbBoth);
    lay->addWidget(grpMode);

    // 3. 格式
    QHBoxLayout *hFmt = new QHBoxLayout;
    hFmt->addWidget(new QLabel("视频容器:"));
    m_cmbFormat = new QComboBox;
    m_cmbFormat->addItem("MP4 (通用)", ".mp4");
    m_cmbFormat->addItem("MOV (Apple)", ".mov");
    hFmt->addWidget(m_cmbFormat);
    lay->addLayout(hFmt);

    // 4. 加速
    m_chkOpenCL = new QCheckBox("启用 GPU 加速 (OpenCL)");
    m_chkOpenCL->setChecked(true);
    m_chkOpenCL->setToolTip("如果您的显卡支持，将大幅提升堆栈计算速度");
    lay->addWidget(m_chkOpenCL);

    lay->addStretch();

    // 按钮
    QHBoxLayout *hBtn = new QHBoxLayout;
    QPushButton *btnCancel = new QPushButton("取消");
    QPushButton *btnOk = new QPushButton("开始渲染");
    btnOk->setStyleSheet("background-color: #00A8E8; color: black; font-weight: bold; padding: 8px;");
    connect(btnCancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    hBtn->addStretch();
    hBtn->addWidget(btnCancel);
    hBtn->addWidget(btnOk);
    lay->addLayout(hBtn);
}

RenderSettings RenderConfigDialog::getSettings() {
    RenderSettings s;
    s.targetHeight = m_cmbRes->currentData().toInt();
    s.exportVideo = m_rbVideoOnly->isChecked() || m_rbBoth->isChecked();
    s.exportLivePhoto = m_rbLivePhoto->isChecked() || m_rbBoth->isChecked();
    s.useOpenCL = m_chkOpenCL->isChecked();
    s.outputFormat = m_cmbFormat->currentData().toString();
    return s;
}

// ================= VideoWriterWorker =================
VideoWriterWorker::VideoWriterWorker(QString path, int srcW, int srcH, double fps, int targetH, bool isMov)
    : m_path(path), m_srcW(srcW), m_srcH(srcH), m_fps(fps), m_running(true)
{
    double scale = 1.0;
    if (targetH > 0 && targetH < srcH) scale = (double)targetH / srcH;
    m_targetW = (int)(srcW * scale);
    m_targetH = (int)(srcH * scale);
    if (m_targetW % 2 != 0) m_targetW--;
    if (m_targetH % 2 != 0) m_targetH--;
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
    writer.open(m_path.toStdString(), fourcc, m_fps, cv::Size(m_targetW, m_targetH));

    while (m_running || !m_queue.isEmpty()) {
        cv::Mat frame;
        {
            QMutexLocker locker(&m_mutex);
            while (m_queue.isEmpty() && m_running) m_condition.wait(&m_mutex);
            if (!m_queue.isEmpty()) frame = m_queue.dequeue();
        }

        if (!frame.empty()) {
            if (m_targetH != m_srcH) {
                cv::resize(frame, frame, cv::Size(m_targetW, m_targetH), 0, 0, cv::INTER_AREA);
            }
            writer.write(frame);
        }
    }
    writer.release();
}

// ================= ProcessorThread (GPU加速核心) =================
void ProcessorThread::setParams(const ProcessParams &params) { m_params = params; }
void ProcessorThread::stop() { m_running = false; }

void ProcessorThread::run() {
    m_running = true;
    cv::VideoCapture cap(m_params.inPath.toStdString());
    if (!cap.isOpened()) { emit errorOccurred("无法打开视频"); return; }

    // 设置 OpenCL
    cv::ocl::setUseOpenCL(m_params.useOpenCL);

    int total = (int)cap.get(cv::CAP_PROP_FRAME_COUNT);
    int w = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH);
    int h = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    double fps = cap.get(cv::CAP_PROP_FPS); if (fps<=0) fps=30;

    VideoWriterWorker *writer = new VideoWriterWorker(m_params.outPath, w, h, fps, m_params.targetRes, m_params.isMov);
    writer->start();

    // 算法逻辑
    bool infinite = m_params.trailLength >= total;

    // 使用 UMat (显存) 或 Mat (内存)
    // 为了简化代码，我们主要逻辑用 Mat, 但运算部分如果用 UMat 会自动加速
    // 不过混合队列操作比较复杂，最简单的加速是 reduce loop 的优化

    std::deque<cv::Mat> buffer;

    // 预计算权重
    std::vector<float> weights;
    float fadeStart = std::max(0.05, 1.0 - m_params.fadeStrength);
    for (int i=0; i<m_params.trailLength; ++i) {
        float t = (float)i / (std::max(1, m_params.trailLength - 1));
        weights.push_back(fadeStart + t * (1.0f - fadeStart));
    }

    // 累加器
    cv::Mat g_accum;    // CPU accumulator
    cv::UMat u_accum;   // GPU accumulator (for infinite mode)

    QElapsedTimer timer; timer.start();
    int pw = (int)(w * (360.0/h)), ph=360;

    for (int i=0; i<total; ++i) {
        if (!m_running) break;

        cv::Mat frame_cpu;
        if (!cap.read(frame_cpu)) break;

        cv::Mat finalFrame;

        if (infinite) {
            // === 无限模式 (极速) ===
            // 只需要一次 Max 操作，非常适合 GPU
            if (m_params.useOpenCL) {
                cv::UMat u_frame = frame_cpu.getUMat(cv::ACCESS_READ);
                if (u_accum.empty()) u_accum = u_frame.clone();
                else cv::max(u_accum, u_frame, u_accum);
                finalFrame = u_accum.getMat(cv::ACCESS_READ); // 回读到 CPU 用于写入
            } else {
                if (g_accum.empty()) g_accum = frame_cpu.clone();
                else cv::max(g_accum, frame_cpu, g_accum);
                finalFrame = g_accum;
            }
        } else {
            // === 滑动窗口模式 ===
            // 这里因为涉及到频繁的队列进出，纯 GPU 可能会因为数据传输变慢
            // 所以我们主要优化计算部分
            buffer.push_back(frame_cpu.clone());
            if (buffer.size() > (size_t)m_params.trailLength) buffer.pop_front();

            size_t bLen = buffer.size();
            if (bLen > 1) {
                // 计算当前帧
                // 这是一个 O(N) 操作，N=TrailLength.
                // 可以通过并行化加速
                int wOffset = m_params.trailLength - bLen;

                // 初始化
                cv::Mat accum;
                cv::convertScaleAbs(buffer[0], accum, weights[wOffset]);

                // 并行化处理还是有点难，因为 max 是顺序依赖？不，max 是可交换的。
                // 简单的优化：如果开启 OpenCL，我们可以尝试将部分 Mat 转 UMat 计算
                // 但在这个循环里频繁 upload/download 可能会更慢。
                // 最佳策略：保持 CPU SIMD 优化 (OpenCV 默认)

                for (size_t k=1; k<bLen; ++k) {
                    float weight = weights[wOffset+k];
                    // 只有当 weight < 1.0 时才进行 scale，减少计算量
                    if (weight > 0.99f) {
                        cv::max(accum, buffer[k], accum);
                    } else {
                        cv::Mat tmp;
                        cv::convertScaleAbs(buffer[k], tmp, weight);
                        cv::max(accum, tmp, accum);
                    }
                }
                finalFrame = accum;
            } else {
                finalFrame = frame_cpu;
            }
        }

        writer->addFrame(finalFrame);

        if (i % 5 == 0) {
            cv::Mat small;
            cv::resize(finalFrame, small, cv::Size(pw, ph), 0, 0, cv::INTER_NEAREST);
            emit previewUpdated(matToQImage(small));

            double elap = timer.elapsed() / 1000.0;
            double spd = (elap>0) ? (i+1)/elap : 0;
            emit progressUpdated(i+1, total, spd);
        }
    }

    cap.release();
    writer->stop();
    delete writer;
    emit finished(m_params.outPath);
}

// ================= CoverSelectorDialog =================
CoverSelectorDialog::CoverSelectorDialog(QString videoPath, QWidget *parent)
    : QDialog(parent), m_videoPath(videoPath)
{
    setWindowTitle("选择实况封面 (关键帧)");
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
    m_slider->setRange(0, m_totalFrames-1);
    m_slider->setValue(m_currentIdx);
    connect(m_slider, &QSlider::valueChanged, this, &CoverSelectorDialog::onSliderValueChanged);

    m_lblInfo = new QLabel(QString::number(m_totalFrames));
    ctrl->addWidget(new QLabel("选择封面:"));
    ctrl->addWidget(m_slider);
    ctrl->addWidget(m_lblInfo);
    lay->addLayout(ctrl);

    QHBoxLayout *btns = new QHBoxLayout;
    QPushButton *btnOk = new QPushButton("确认并保存");
    btnOk->setStyleSheet("background-color: #00A8E8; color: black; font-weight: bold; padding: 10px;");
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);
    btns->addStretch();
    btns->addWidget(btnOk);
    lay->addLayout(btns);

    updatePreview();
}

CoverSelectorDialog::~CoverSelectorDialog() { delete m_cap; }

void CoverSelectorDialog::onSliderValueChanged(int v) {
    m_currentIdx = v;
    m_lblInfo->setText(QString("%1/%2").arg(v).arg(m_totalFrames));
    updatePreview();
}

void CoverSelectorDialog::updatePreview() {
    m_cap->set(cv::CAP_PROP_POS_FRAMES, m_currentIdx);
    cv::Mat f;
    if(m_cap->read(f)) {
        m_selectedFrame = f.clone();
        int h = f.rows;
        int dispH = 500;
        int dispW = (int)(f.cols * ((double)dispH/h));
        cv::Mat s; cv::resize(f, s, cv::Size(dispW, dispH));
        m_lblPreview->setPixmap(QPixmap::fromImage(matToQImage(s)));
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
    connect(m_processor, &ProcessorThread::errorOccurred, this, [this](QString m){
        QMessageBox::critical(this, "Error", m);
        m_btnStart->setEnabled(true);
    });
}

MainWindow::~MainWindow() {
    if(m_processor->isRunning()) { m_processor->stop(); m_processor->wait(); }
}

void MainWindow::setupUi() {
    setWindowTitle("StarTrail Pro (Accelerated)");
    resize(1100, 750);
    setStyleSheet(ULTRA_DARK_STYLE);

    QWidget *cen = new QWidget; setCentralWidget(cen);
    QHBoxLayout *mainLay = new QHBoxLayout(cen);
    mainLay->setContentsMargins(0,0,0,0); mainLay->setSpacing(0);

    // Sidebar
    QFrame *side = new QFrame; side->setFixedWidth(320);
    side->setStyleSheet("background: #181818; border-right: 1px solid #333;");
    QVBoxLayout *sLay = new QVBoxLayout(side);
    sLay->setContentsMargins(15,25,15,25); sLay->setSpacing(15);

    m_dropLabel = new DropLabel;
    m_dropLabel->setFixedHeight(120);
    connect(m_dropLabel, &DropLabel::fileDropped, this, &MainWindow::onFileDropped);
    connect(m_dropLabel, &DropLabel::clicked, this, &MainWindow::selectInputFile);
    sLay->addWidget(m_dropLabel);

    m_lblFileName = new QLabel("未选择文件");
    m_lblFileName->setStyleSheet("color: #777; font-size: 11px;");
    sLay->addWidget(m_lblFileName);

    QGroupBox *grpP = new QGroupBox("参数");
    QVBoxLayout *pl = new QVBoxLayout(grpP);
    QHBoxLayout *h1 = new QHBoxLayout; h1->addWidget(new QLabel("长度:"));
    m_spinTrail = new QSpinBox; m_spinTrail->setRange(1,99999); m_spinTrail->setValue(120); h1->addWidget(m_spinTrail);
    QHBoxLayout *h2 = new QHBoxLayout; h2->addWidget(new QLabel("柔和:"));
    m_spinFade = new QDoubleSpinBox; m_spinFade->setRange(0,0.99); m_spinFade->setValue(0.85); h2->addWidget(m_spinFade);
    pl->addLayout(h1); pl->addLayout(h2);
    sLay->addWidget(grpP);

    sLay->addStretch();

    m_btnStart = new QPushButton("配置并开始导出...");
    m_btnStart->setFixedHeight(50);
    m_btnStart->setEnabled(false);
    m_btnStart->setStyleSheet("background-color: #00A8E8; color: black; font-weight: bold; font-size: 15px;");
    connect(m_btnStart, &QPushButton::clicked, this, &MainWindow::selectOutputPath);
    sLay->addWidget(m_btnStart);

    mainLay->addWidget(side);

    // Preview
    QWidget *pre = new QWidget;
    QVBoxLayout *prl = new QVBoxLayout(pre);
    m_lblPreview = new QLabel("PREVIEW");
    m_lblPreview->setAlignment(Qt::AlignCenter);
    m_lblPreview->setStyleSheet("background: #000; border-radius: 6px;");
    m_lblPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    prl->addWidget(m_lblPreview);

    QHBoxLayout *inf = new QHBoxLayout;
    m_lblStatus = new QLabel("Ready");
    m_lblSpeed = new QLabel("");
    inf->addWidget(m_lblStatus); inf->addStretch(); inf->addWidget(m_lblSpeed);
    prl->addLayout(inf);

    m_progressBar = new QProgressBar;
    prl->addWidget(m_progressBar);

    mainLay->addWidget(pre);
}

void MainWindow::onFileDropped(QString path) {
    m_inPath = path;
    m_lblFileName->setText(QFileInfo(path).fileName());
    m_btnStart->setEnabled(true);
}

// 完善的导入逻辑
void MainWindow::selectInputFile() {
    QString path = QFileDialog::getOpenFileName(this, "选择视频", "", "Video Files (*.mp4 *.mov *.avi *.mkv)");
    if (!path.isEmpty()) {
        onFileDropped(path);
    }
}

// 导出配置对话框
void MainWindow::selectOutputPath() {
    if (m_inPath.isEmpty()) return;

    // 弹出配置对话框
    RenderConfigDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted) return;

    RenderSettings settings = dlg.getSettings();

    // 生成默认文件名
    QString baseName = QFileInfo(m_inPath).completeBaseName();
    QString suffix = settings.outputFormat;
    QString defaultName = baseName + "_StarTrail" + suffix;

    QString savePath = QFileDialog::getSaveFileName(this, "保存目标视频 (Master File)",
                                                    QFileInfo(m_inPath).dir().filePath(defaultName),
                                                    "Video (*" + suffix + ")");
    if (savePath.isEmpty()) return;

    startRenderPipeline(settings, savePath);
}

void MainWindow::startRenderPipeline(RenderSettings settings, QString savePath) {
    // 锁定UI
    m_btnStart->setEnabled(false);
    m_dropLabel->setEnabled(false);

    // 保存意图
    m_wantLivePhoto = settings.exportLivePhoto;
    m_wantVideo = settings.exportVideo; // 其实视频总是会生成的，这里指最后是否保留

    ProcessParams p;
    p.inPath = m_inPath;
    p.outPath = savePath;
    p.trailLength = m_spinTrail->value();
    p.fadeStrength = m_spinFade->value();
    p.targetRes = settings.targetHeight;
    p.isMov = (settings.outputFormat == ".mov");
    p.useOpenCL = settings.useOpenCL;

    m_processor->setParams(p);
    m_processor->start();
}

void MainWindow::onProcessingFinished(QString outPath) {
    m_btnStart->setEnabled(true);
    m_dropLabel->setEnabled(true);
    m_lblStatus->setText("渲染完成");

    // 渲染完成后，检查是否需要制作 Live Photo
    if (m_wantLivePhoto) {
        exportLivePhotoFlow(outPath);
    } else {
        QMessageBox::information(this, "完成", "视频已保存至:\n" + outPath);
    }
}

void MainWindow::exportLivePhotoFlow(QString videoPath) {
    // 弹出封面选择器
    CoverSelectorDialog dlg(videoPath, this);
    if (dlg.exec() == QDialog::Accepted) {
        cv::Mat cover = dlg.getSelectedImage();

        // 自动保存封面图 (同名 jpg)
        QFileInfo videoInfo(videoPath);
        QString jpgPath = videoInfo.dir().filePath(videoInfo.completeBaseName() + ".jpg");

        // 保存 JPG
        std::string sJpg = jpgPath.toStdString(); // 注意编码
        cv::imwrite(sJpg, cover);

        QString msg = "实况照片已生成!\n\n1. 视频: " + videoInfo.fileName() + "\n2. 封面: " + QFileInfo(jpgPath).fileName();

        if (!m_wantVideo) {
            // 如果用户选了"仅实况"，其实我们已经生成了视频。
            // 逻辑上视频是实况的一部分，所以必须保留视频文件。
            // 这里只是提示语的区别。
            msg += "\n\n(注意: 实况照片必须包含视频文件，请勿删除视频)";
        }

        QMessageBox::information(this, "实况导出成功", msg);
    }
}

void MainWindow::onPreviewUpdated(QImage img) {
    m_lblPreview->setPixmap(QPixmap::fromImage(img).scaled(m_lblPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::onProgress(int c, int t, double fps) {
    m_progressBar->setMaximum(t); m_progressBar->setValue(c);
    m_lblStatus->setText(QString("渲染中... %1/%2").arg(c).arg(t));
    m_lblSpeed->setText(QString::number(fps, 'f', 1) + " FPS");
}
