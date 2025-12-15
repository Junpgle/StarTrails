#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QQueue>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QDialog>
#include <QSlider>
#include <QDragEnterEvent>
#include <QMimeData>
#include <QRadioButton>
#include <QCheckBox>
#include <QButtonGroup>
#include <deque>

// OpenCV
#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp> // OpenCL 支持

// --- 样式表 ---
const QString ULTRA_DARK_STYLE = R"(
QMainWindow, QDialog { background-color: #181818; }
QWidget { color: #E0E0E0; font-family: "Segoe UI", "Microsoft YaHei", sans-serif; font-size: 13px; }
QGroupBox {
    border: 1px solid #333; border-radius: 6px; margin-top: 22px;
    background-color: #202020; font-weight: bold; color: #00A8E8;
}
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }
QPushButton {
    background-color: #333; border: 1px solid #444; border-radius: 4px; padding: 6px 12px; color: #E0E0E0;
}
QPushButton:hover { background-color: #444; border-color: #00A8E8; }
QPushButton:pressed { background-color: #00A8E8; color: #000; }
QPushButton:disabled { background-color: #222; color: #555; border-color: #2a2a2a; }
QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background-color: #252525; border: 1px solid #333; border-radius: 4px; padding: 4px; color: #FFF;
}
QComboBox::drop-down { border: none; }
QProgressBar {
    border: none; background-color: #1A1A1A; height: 6px; border-radius: 3px;
}
QProgressBar::chunk { background-color: #00A8E8; border-radius: 3px; }
)";

// --- 拖拽标签 ---
class DropLabel : public QLabel {
    Q_OBJECT
public:
    explicit DropLabel(QWidget *parent = nullptr);
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override; // 点击也可以选择文件
signals:
    void fileDropped(QString path);
    void clicked();
};

// --- 渲染配置对话框 (新) ---
struct RenderSettings {
    int targetHeight; // 0=Original, 1080, 720
    bool exportVideo;
    bool exportLivePhoto;
    bool useOpenCL;   // GPU加速
    QString outputFormat; // .mp4 or .mov
};

class RenderConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit RenderConfigDialog(QWidget *parent = nullptr);
    RenderSettings getSettings();

private:
    QComboBox *m_cmbRes;
    QRadioButton *m_rbVideoOnly;
    QRadioButton *m_rbLivePhoto;
    QRadioButton *m_rbBoth;
    QCheckBox *m_chkOpenCL;
    QComboBox *m_cmbFormat;
};

// --- 视频写入工作线程 ---
class VideoWriterWorker : public QThread {
    Q_OBJECT
public:
    VideoWriterWorker(QString path, int srcW, int srcH, double fps, int targetH, bool isMov);
    void addFrame(const cv::Mat &frame);
    void stop();

protected:
    void run() override;

private:
    QString m_path;
    int m_srcW, m_srcH, m_targetW, m_targetH;
    double m_fps;
    bool m_running;
    QQueue<cv::Mat> m_queue;
    QMutex m_mutex;
    QWaitCondition m_condition;
};

// --- 主处理线程 (支持 UMat 加速) ---
struct ProcessParams {
    QString inPath;
    QString outPath;
    int trailLength;
    double fadeStrength;
    int targetRes; // 目标高度
    bool isMov;
    bool useOpenCL; // 启用 GPU
};

class ProcessorThread : public QThread {
    Q_OBJECT
public:
    void setParams(const ProcessParams &params);
    void stop();

signals:
    void progressUpdated(int current, int total, double fps);
    void previewUpdated(QImage img);
    void finished(QString outPath);
    void errorOccurred(QString msg);

protected:
    void run() override;

private:
    ProcessParams m_params;
    bool m_running;
};

// --- 封面选择对话框 ---
class CoverSelectorDialog : public QDialog {
    Q_OBJECT
public:
    explicit CoverSelectorDialog(QString videoPath, QWidget *parent = nullptr);
    ~CoverSelectorDialog();
    cv::Mat getSelectedImage();

private slots:
    void onSliderValueChanged(int value);
    void updatePreview();

private:
    QString m_videoPath;
    cv::VideoCapture *m_cap;
    int m_totalFrames;
    int m_currentIdx;
    cv::Mat m_selectedFrame;
    QLabel *m_lblPreview;
    QLabel *m_lblInfo;
    QSlider *m_slider;
};

// --- 主窗口 ---
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onFileDropped(QString path);
    void selectInputFile();     // 导入逻辑
    void selectOutputPath();    // 导出逻辑(启动渲染对话框)

    void onProcessingFinished(QString outPath);
    void onPreviewUpdated(QImage img);
    void onProgress(int current, int total, double fps);

private:
    void setupUi();
    void startRenderPipeline(RenderSettings settings, QString savePath);
    void exportLivePhotoFlow(QString videoPath); // 实况导出流程

    // UI Components
    DropLabel *m_dropLabel;
    QLabel *m_lblFileName;
    QSpinBox *m_spinTrail;
    QDoubleSpinBox *m_spinFade;
    QPushButton *m_btnStart;

    QLabel *m_lblPreview;
    QLabel *m_lblStatus;
    QLabel *m_lblSpeed;
    QProgressBar *m_progressBar;

    ProcessorThread *m_processor;
    QString m_inPath;

    // 暂存用户的导出意图
    bool m_wantLivePhoto;
    bool m_wantVideo;
};

#endif // MAINWINDOW_H
