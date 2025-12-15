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
#include <QPainter>
#include <QMouseEvent>
#include <deque>
#include <vector>

// OpenCV
#include <opencv2/opencv.hpp>
#include <opencv2/core/ocl.hpp>

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

// --- 动态照片合成器 (新算法) ---
class MotionPhotoMuxer {
public:
    // 将 jpg 和 mp4 合并为一个 Google Motion Photo (JPG文件)
    static bool mux(const QString &jpgPath, const QString &mp4Path, const QString &outPath);
};

// --- 帧提供者 ---
class FrameProvider {
public:
    FrameProvider();
    ~FrameProvider();
    bool openVideo(const QString &path);
    bool openSequence(const QStringList &files);
    void close();
    bool isOpened() const;
    int totalFrames() const;
    double fps() const;
    int width() const;
    int height() const;
    bool isVideo() const { return m_isVideo; }
    QString getSourcePath() const;
    bool read(cv::Mat &image);
    bool seek(int frameIndex);

private:
    bool m_isVideo;
    cv::VideoCapture *m_cap;
    QStringList m_files;
    int m_currentIndex;
    int m_total;
    int m_w, m_h;
    double m_fps;
    QString m_mainPath;
};

// --- 拖拽标签 ---
class DropLabel : public QLabel {
    Q_OBJECT
public:
    explicit DropLabel(QWidget *parent = nullptr);
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
signals:
    void filesDropped(QStringList paths);
    void clicked();
};

// --- 可视化裁剪编辑器 ---
class CropEditorDialog : public QDialog {
    Q_OBJECT
public:
    explicit CropEditorDialog(const cv::Mat &frame, QRect currentRect, QWidget *parent = nullptr);
    QRect getFinalCropRect();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QImage m_displayImage;
    cv::Mat m_origFrame;
    double m_scaleFactor;
    int m_offsetX, m_offsetY;
    QRect m_selectionRect;

    enum EditMode { ModeNone, ModeDrawing, ModeMoving, ModeResizing };
    enum ResizeHandle { HandleNone, HandleTop, HandleBottom, HandleLeft, HandleRight, HandleTopLeft, HandleTopRight, HandleBottomLeft, HandleBottomRight, HandleInside };
    EditMode m_mode;
    ResizeHandle m_activeHandle;
    QPoint m_lastMousePos;
    QPoint m_startPos;

    ResizeHandle hitTest(const QPoint &pos);
    void updateCursorIcon(const QPoint &pos);
    QRect getImageRect();
};

// --- 渲染设置结构体 ---
struct RenderSettings {
    int targetHeight;
    bool exportVideo;
    bool exportLivePhoto;
    bool useOpenCL;
    QString outputFormat;
    int startFrame;
    int endFrame;
    int cropRatioMode;
    QRect manualCropRect;
    double targetFps;
};

// --- 渲染配置对话框 ---
class RenderConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit RenderConfigDialog(FrameProvider *provider, QWidget *parent = nullptr);
    ~RenderConfigDialog();
    RenderSettings getSettings();

private slots:
    void updateDurationLabel();
    void onCropModeChanged(int index);
    void openCropEditor();
    void onTimelineChanged(int value);
    void onSetStartClicked();
    void onSetEndClicked();

private:
    QComboBox *m_cmbRes;
    QRadioButton *m_rbVideoOnly;
    QRadioButton *m_rbLivePhoto;
    QRadioButton *m_rbBoth;
    QCheckBox *m_chkOpenCL;
    QComboBox *m_cmbFormat;
    QDoubleSpinBox *m_spinFps;

    QSpinBox *m_spinStartFrame;
    QSpinBox *m_spinEndFrame;
    QLabel *m_lblDuration;

    QComboBox *m_cmbCropRatio;
    QPushButton *m_btnEditCrop;
    QRect m_currentManualRect;

    FrameProvider *m_provider;

    QLabel *m_lblVideoPreview;
    QSlider *m_sliderTimeline;
    QLabel *m_lblCurrentFrame;
};

// --- 视频写入工作线程 ---
class VideoWriterWorker : public QThread {
    Q_OBJECT
public:
    VideoWriterWorker(QString path, int w, int h, double fps, bool isMov);
    void addFrame(const cv::Mat &frame);
    void stop();
protected:
    void run() override;
private:
    QString m_path;
    int m_width, m_height;
    double m_fps;
    bool m_running;
    QQueue<cv::Mat> m_queue;
    QMutex m_mutex;
    QWaitCondition m_condition;
};

// --- 主处理线程 ---
struct ProcessParams {
    bool isVideo;
    QString videoPath;
    QStringList imageFiles;

    QString outPath;
    int trailLength;
    double fadeStrength;
    int targetRes;
    bool isMov;
    bool useOpenCL;
    int startFrame;
    int endFrame;
    cv::Rect finalCropRect;
    double targetFps;
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
    void onFilesDropped(QStringList paths);
    void selectInput();
    void selectOutputPath();

    void onProcessingFinished(QString outPath);
    void onPreviewUpdated(QImage img);
    void onProgress(int current, int total, double fps);

private:
    void setupUi();
    void startRenderPipeline(RenderSettings settings, QString savePath);
    void exportLivePhotoFlow(QString videoPath);

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
    FrameProvider *m_inputProvider;

    bool m_wantLivePhoto;
    bool m_wantVideo;
};

#endif // MAINWINDOW_H
