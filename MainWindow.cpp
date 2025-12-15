#include "MainWindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFrame>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QElapsedTimer> // 修改: 使用 QElapsedTimer 替代 QTime

// 工具函数: cv::Mat 转 QImage
QImage matToQImage(const cv::Mat &mat) {
    if(mat.empty()) return QImage();
    if(mat.type() == CV_8UC3) {
        // OpenCV 是 BGR, Qt 是 RGB
        // 我们不进行克隆，而是交换通道显示
        QImage img(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_RGB888);
        return img.rgbSwapped();
    }
    return QImage();
}

// ================= DropLabel =================
DropLabel::DropLabel(QWidget *parent) : QLabel(parent) {
    setText("\n📂\n拖入视频文件");
    setAlignment(Qt::AlignCenter);
    setObjectName("DropZone");
    setAcceptDrops(true);
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

// ================= VideoWriterWorker =================
VideoWriterWorker::VideoWriterWorker(QString path, int srcW, int srcH, double fps, int targetH, bool isMov)
    : m_path(path), m_srcW(srcW), m_srcH(srcH), m_fps(fps), m_running(true)
{
    // 计算缩放
    double scale = 1.0;
    if (targetH > 0 && targetH < srcH) {
        scale = (double)targetH / srcH;
    }
    m_targetW = (int)(srcW * scale);
    m_targetH = (int)(srcH * scale);

    // 确保偶数
    if (m_targetW % 2 != 0) m_targetW--;
    if (m_targetH % 2 != 0) m_targetH--;

    m_isMov = isMov;
}

void VideoWriterWorker::addFrame(const cv::Mat &frame) {
    QMutexLocker locker(&m_mutex);
    m_queue.enqueue(frame.clone()); // 需要深拷贝因为源 Mat 可能会变
    m_condition.wakeOne();
}

void VideoWriterWorker::stop() {
    m_running = false;
    m_condition.wakeAll();
    wait();
}

void VideoWriterWorker::run() {
    int fourcc = cv::VideoWriter::fourcc('a', 'v', 'c', '1'); // H.264
    cv::VideoWriter writer;

    // 打开文件
    // 注意: Windows下路径中文问题，通常 toStdString() 即可，但在某些本地化环境可能需要处理
    writer.open(m_path.toStdString(), fourcc, m_fps, cv::Size(m_targetW, m_targetH));

    while (m_running || !m_queue.isEmpty()) {
        cv::Mat frame;
        {
            QMutexLocker locker(&m_mutex);
            while (m_queue.isEmpty() && m_running) {
                m_condition.wait(&m_mutex);
            }
            if (!m_queue.isEmpty())
                frame = m_queue.dequeue();
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

// ================= ProcessorThread =================
void ProcessorThread::setParams(const ProcessParams &params) {
    m_params = params;
}

void ProcessorThread::stop() {
    m_running = false;
}

void ProcessorThread::run() {
    m_running = true;

    cv::VideoCapture cap(m_params.inPath.toStdString());
    if (!cap.isOpened()) {
        emit errorOccurred("无法打开视频文件");
        return;
    }

    int total = (int)cap.get(cv::CAP_PROP_FRAME_COUNT);
    int w = (int)cap.get(cv::CAP_PROP_FRAME_WIDTH);
    int h = (int)cap.get(cv::CAP_PROP_FRAME_HEIGHT);
    double fps = cap.get(cv::CAP_PROP_FPS);
    if (fps <= 0) fps = 30.0;

    VideoWriterWorker *writer = new VideoWriterWorker(
        m_params.outPath, w, h, fps, m_params.targetRes, m_params.isMov
        );
    writer->start();

    // 算法准备
    bool infinite = m_params.trailLength >= total;
    std::deque<cv::Mat> buffer;

    // 预计算权重
    std::vector<float> weights;
    float fadeStart = std::max(0.05, 1.0 - m_params.fadeStrength);
    for (int i = 0; i < m_params.trailLength; ++i) {
        float t = (float)i / (m_params.trailLength - 1); // 0.0 to 1.0
        weights.push_back(fadeStart + t * (1.0f - fadeStart));
    }

    cv::Mat g_accum;
    QElapsedTimer timer; timer.start(); // 修改: 使用 QElapsedTimer

    // 预览尺寸
    int pw = (int)(w * (360.0 / h));
    int ph = 360;

    for (int i = 0; i < total; ++i) {
        if (!m_running) break;

        cv::Mat frame;
        if (!cap.read(frame)) break;

        cv::Mat finalFrame;

        if (infinite) {
            // 无限模式：累加
            if (g_accum.empty()) g_accum = frame.clone();
            else cv::max(g_accum, frame, g_accum);
            finalFrame = g_accum;
        } else {
            // 滑动窗口模式
            buffer.push_back(frame.clone());
            if (buffer.size() > (size_t)m_params.trailLength) {
                buffer.pop_front();
            }

            size_t bLen = buffer.size();
            if (bLen > 1) {
                // 加权计算
                // 取最后 bLen 个权重
                int wOffset = m_params.trailLength - bLen;

                // 初始化 accum 为第一帧 * 权重
                // OpenCV C++ 中简单的 frame * float 会自动处理 saturate_cast
                cv::Mat accum;
                cv::convertScaleAbs(buffer[0], accum, weights[wOffset]);

                for (size_t k = 1; k < bLen; ++k) {
                    float weight = weights[wOffset + k];
                    cv::Mat weightedNext;
                    if (weight >= 0.99f) weightedNext = buffer[k];
                    else cv::convertScaleAbs(buffer[k], weightedNext, weight);

                    cv::max(accum, weightedNext, accum);
                }
                finalFrame = accum;
            } else {
                finalFrame = frame;
            }
        }

        writer->addFrame(finalFrame);

        // 预览和进度
        if (i % 5 == 0) {
            cv::Mat small;
            cv::resize(finalFrame, small, cv::Size(pw, ph), 0, 0, cv::INTER_NEAREST);
            emit previewUpdated(matToQImage(small));

            double elapsed = timer.elapsed() / 1000.0;
            double speed = (elapsed > 0) ? (i + 1) / elapsed : 0;
            emit progressUpdated(i + 1, total, speed);
        }
    }

    cap.release();
    writer->stop();
    delete writer;

    emit finished(m_params.outPath);
}

// ================= CoverSelectorDialog =================
CoverSelectorDialog::CoverSelectorDialog(QString videoPath, QWidget *parent)
    : QDialog(parent), m_videoPath(videoPath), m_currentIdx(0)
{
    setWindowTitle("选择实况封面");
    resize(900, 650);

    m_cap = new cv::VideoCapture(videoPath.toStdString());
    m_totalFrames = (int)m_cap->get(cv::CAP_PROP_FRAME_COUNT);
    m_currentIdx = m_totalFrames - 1;

    QVBoxLayout *layout = new QVBoxLayout(this);

    m_lblPreview = new QLabel(this);
    m_lblPreview->setAlignment(Qt::AlignCenter);
    m_lblPreview->setStyleSheet("background-color: #000; border: 1px solid #333;");
    m_lblPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_lblPreview);

    QHBoxLayout *ctrlLayout = new QHBoxLayout;
    ctrlLayout->addWidget(new QLabel("选择帧:"));

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, m_totalFrames - 1);
    m_slider->setValue(m_currentIdx);
    connect(m_slider, &QSlider::valueChanged, this, &CoverSelectorDialog::onSliderValueChanged);
    ctrlLayout->addWidget(m_slider);

    m_lblInfo = new QLabel(QString::number(m_totalFrames), this);
    ctrlLayout->addWidget(m_lblInfo);
    layout->addLayout(ctrlLayout);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    QPushButton *btnOk = new QPushButton("✅ 导出实况文件", this);
    btnOk->setStyleSheet("background-color: #00A8E8; color: black; font-weight: bold; padding: 10px;");
    connect(btnOk, &QPushButton::clicked, this, &QDialog::accept);

    btnLayout->addStretch();
    btnLayout->addWidget(btnOk);
    layout->addLayout(btnLayout);

    updatePreview();
}

CoverSelectorDialog::~CoverSelectorDialog() {
    m_cap->release();
    delete m_cap;
}

void CoverSelectorDialog::onSliderValueChanged(int value) {
    m_currentIdx = value;
    m_lblInfo->setText(QString("%1/%2").arg(value).arg(m_totalFrames));
    updatePreview();
}

void CoverSelectorDialog::updatePreview() {
    m_cap->set(cv::CAP_PROP_POS_FRAMES, m_currentIdx);
    cv::Mat frame;
    if (m_cap->read(frame)) {
        m_selectedFrame = frame.clone();
        // 缩放
        int h = frame.rows;
        int w = frame.cols;
        int dispH = 500;
        int dispW = (int)(w * ((double)dispH / h));

        cv::Mat small;
        cv::resize(frame, small, cv::Size(dispW, dispH));

        m_lblPreview->setPixmap(QPixmap::fromImage(matToQImage(small)));
    }
}

cv::Mat CoverSelectorDialog::getSelectedImage() {
    return m_selectedFrame;
}

// ================= MainWindow =================
MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setupUi();
    m_processor = new ProcessorThread;
    connect(m_processor, &ProcessorThread::progressUpdated, this, &MainWindow::onProgress);
    connect(m_processor, &ProcessorThread::previewUpdated, this, &MainWindow::onPreviewUpdated);
    connect(m_processor, &ProcessorThread::finished, this, &MainWindow::onProcessingFinished);
    connect(m_processor, &ProcessorThread::errorOccurred, this, [this](QString msg){
        QMessageBox::critical(this, "Error", msg);
        m_btnRun->setEnabled(true);
    });
}

MainWindow::~MainWindow() {
    if (m_processor->isRunning()) {
        m_processor->stop();
        m_processor->wait();
    }
}

void MainWindow::setupUi() {
    setWindowTitle("StarTrail Pro Max (C++)");
    resize(1050, 720);
    setStyleSheet(ULTRA_DARK_STYLE);

    QWidget *central = new QWidget;
    setCentralWidget(central);
    QHBoxLayout *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(0,0,0,0);
    mainLayout->setSpacing(0);

    // --- 侧边栏 ---
    QFrame *sidebar = new QFrame;
    sidebar->setFixedWidth(320);
    sidebar->setStyleSheet("background-color: #181818; border-right: 1px solid #333;");
    QVBoxLayout *sideLayout = new QVBoxLayout(sidebar);
    sideLayout->setContentsMargins(15,25,15,25);
    sideLayout->setSpacing(15);

    m_dropLabel = new DropLabel;
    m_dropLabel->setFixedHeight(100);
    connect(m_dropLabel, &DropLabel::fileDropped, this, &MainWindow::onFileDropped);
    sideLayout->addWidget(m_dropLabel);

    m_lblFile = new QLabel("未选择文件");
    m_lblFile->setStyleSheet("color: #777; font-size: 11px;");
    sideLayout->addWidget(m_lblFile);

    // 参数
    QGroupBox *grpParams = new QGroupBox("合成参数");
    QVBoxLayout *pLayout = new QVBoxLayout(grpParams);

    QHBoxLayout *h1 = new QHBoxLayout;
    h1->addWidget(new QLabel("💫 长度:"));
    m_spinTrail = new QSpinBox;
    m_spinTrail->setRange(1, 99999);
    m_spinTrail->setValue(120);
    h1->addWidget(m_spinTrail);

    QHBoxLayout *h2 = new QHBoxLayout;
    h2->addWidget(new QLabel("🌫️ 柔和:"));
    m_spinFade = new QDoubleSpinBox;
    m_spinFade->setRange(0.0, 0.99);
    m_spinFade->setValue(0.8);
    h2->addWidget(m_spinFade);

    pLayout->addLayout(h1);
    pLayout->addLayout(h2);
    sideLayout->addWidget(grpParams);

    // 导出
    QGroupBox *grpOut = new QGroupBox("导出设置");
    QVBoxLayout *oLayout = new QVBoxLayout(grpOut);

    oLayout->addWidget(new QLabel("输出格式:"));
    m_cmbFmt = new QComboBox;
    m_cmbFmt->addItems({"MP4 (推荐)", "MOV (Apple)"});
    oLayout->addWidget(m_cmbFmt);

    oLayout->addWidget(new QLabel("分辨率:"));
    m_cmbRes = new QComboBox;
    m_cmbRes->addItem("原始分辨率 (最大)");
    m_cmbRes->addItem("4K UHD (2160p)");
    m_cmbRes->addItem("Full HD (1080p)", 1080); // userData
    m_cmbRes->addItem("HD (720p)", 720);
    m_cmbRes->setCurrentIndex(2); // Default 1080p
    oLayout->addWidget(m_cmbRes);
    sideLayout->addWidget(grpOut);

    // 按钮
    m_btnRun = new QPushButton("开始渲染");
    m_btnRun->setFixedHeight(45);
    m_btnRun->setEnabled(false);
    m_btnRun->setStyleSheet("background-color: #00A8E8; color: #000; font-weight: bold;");
    connect(m_btnRun, &QPushButton::clicked, this, &MainWindow::startProcessing);
    sideLayout->addWidget(m_btnRun);

    m_btnLive = new QPushButton("🎬 制作实况照片");
    m_btnLive->setFixedHeight(45);
    m_btnLive->setEnabled(false);
    m_btnLive->setStyleSheet("background-color: #BB86FC; color: #000; font-weight: bold;");
    connect(m_btnLive, &QPushButton::clicked, this, &MainWindow::makeLivePhoto);
    sideLayout->addWidget(m_btnLive);

    sideLayout->addStretch();
    mainLayout->addWidget(sidebar);

    // --- 预览区 ---
    QWidget *previewArea = new QWidget;
    QVBoxLayout *preLayout = new QVBoxLayout(previewArea);

    m_lblPreview = new QLabel("PREVIEW");
    m_lblPreview->setAlignment(Qt::AlignCenter);
    m_lblPreview->setStyleSheet("background: #000; border-radius: 6px;");
    m_lblPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    preLayout->addWidget(m_lblPreview);

    QHBoxLayout *infoLayout = new QHBoxLayout;
    m_lblStatus = new QLabel("Ready");
    m_lblSpeed = new QLabel("");
    infoLayout->addWidget(m_lblStatus);
    infoLayout->addStretch();
    infoLayout->addWidget(m_lblSpeed);
    preLayout->addLayout(infoLayout);

    m_progressBar = new QProgressBar;
    preLayout->addWidget(m_progressBar);

    mainLayout->addWidget(previewArea);
}

void MainWindow::onFileDropped(QString path) {
    m_inPath = path;
    m_lblFile->setText(QFileInfo(path).fileName());
    m_btnRun->setEnabled(true);
}

void MainWindow::selectFile() {
    // 省略文件选择器，通过拖拽即可
}

void MainWindow::startProcessing() {
    if (m_inPath.isEmpty()) return;

    QString ext = (m_cmbFmt->currentIndex() == 0) ? ".mp4" : ".mov";
    int targetRes = 0;
    if (m_cmbRes->currentIndex() == 1) targetRes = 2160;
    else if (m_cmbRes->currentIndex() == 2) targetRes = 1080;
    else if (m_cmbRes->currentIndex() == 3) targetRes = 720;

    QString baseName = QFileInfo(m_inPath).completeBaseName();
    QString outPath = QFileInfo(m_inPath).absolutePath() + "/" + baseName + "_StarTrail" + ext;

    ProcessParams p;
    p.inPath = m_inPath;
    p.outPath = outPath;
    p.trailLength = m_spinTrail->value();
    p.fadeStrength = m_spinFade->value();
    p.targetRes = targetRes;
    p.isMov = (m_cmbFmt->currentIndex() == 1);

    m_btnRun->setEnabled(false);
    m_btnLive->setEnabled(false);

    m_processor->setParams(p);
    m_processor->start();
}

void MainWindow::onProcessingFinished(QString outPath) {
    m_lastVideoPath = outPath;
    m_btnRun->setEnabled(true);
    m_btnLive->setEnabled(true);
    m_lblStatus->setText("完成");
    QMessageBox::information(this, "OK", "渲染完成:\n" + outPath);
}

void MainWindow::onPreviewUpdated(QImage img) {
    m_lblPreview->setPixmap(QPixmap::fromImage(img).scaled(m_lblPreview->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::onProgress(int current, int total, double fps) {
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(current);
    m_lblStatus->setText(QString("处理中 %1/%2").arg(current).arg(total));
    m_lblSpeed->setText(QString::number(fps, 'f', 1) + " FPS");
}

void MainWindow::makeLivePhoto() {
    if (m_lastVideoPath.isEmpty()) return;
    CoverSelectorDialog dlg(m_lastVideoPath, this);
    if (dlg.exec() == QDialog::Accepted) {
        exportLivePhoto(dlg.getSelectedImage());
    }
}

void MainWindow::exportLivePhoto(const cv::Mat &coverImg) {
    QString savePath = QFileDialog::getSaveFileName(this, "保存实况", "", "Image (*.jpg)");
    if (savePath.isEmpty()) return;

    // 1. 保存 JPG
    // cv::imwrite 支持 UTF-8 路径需要注意，在 Windows 上最好用 toLocal8Bit 或宽字符
    // 这里使用 Qt 的 QFile 辅助或者 OpenCV 的 imencode 然后写文件
    // 简单起见直接 imwrite，注意路径不含中文可能更稳
    std::string jpgPath = savePath.toStdString();
    cv::imwrite(jpgPath, coverImg);

    // 2. 复制 MOV
    QString movPath = QFileInfo(savePath).path() + "/" + QFileInfo(savePath).completeBaseName() + ".mov";
    if (QFile::exists(movPath)) QFile::remove(movPath);
    QFile::copy(m_lastVideoPath, movPath);

    QMessageBox::information(this, "Success", "实况包已导出 (JPG + MOV)");
}
