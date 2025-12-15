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
#include <QDebug>
#include <deque>

// OpenCV
#include <opencv2/opencv.hpp>

// --- 样式表 ---
const QString ULTRA_DARK_STYLE = R"(
QMainWindow, QDialog { background-color: #181818; }
QWidget { color: #E0E0E0; font-family: "Segoe UI", sans-serif; font-size: 13px; }
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
QSlider::groove:horizontal {
    border: 1px solid #333; height: 6px; background: #1A1A1A; margin: 2px 0; border-radius: 3px;
}
QSlider::handle:horizontal {
    background: #00A8E8; width: 16px; height: 16px; margin: -5px 0; border-radius: 8px;
}
QLabel#DropZone {
    border: 2px dashed #444; border-radius: 10px; color: #666; font-size: 16px;
}
QLabel#DropZone:hover { border-color: #00A8E8; color: #00A8E8; background-color: #252525; }
)";

// --- 拖拽标签控件 ---
class DropLabel : public QLabel {
    Q_OBJECT
public:
    explicit DropLabel(QWidget *parent = nullptr);
protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
signals:
    void fileDropped(QString path);
};

// --- 视频写入线程 ---
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
    int m_srcW, m_srcH;
    double m_fps;
    int m_targetW, m_targetH;
    bool m_isMov;
    bool m_running;

    QQueue<cv::Mat> m_queue;
    QMutex m_mutex;
    QWaitCondition m_condition;
};

// --- 主处理线程 ---
struct ProcessParams {
    QString inPath;
    QString outPath;
    int trailLength;
    double fadeStrength;
    int targetRes;
    bool isMov;
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
    void selectFile();
    void startProcessing();
    void onProcessingFinished(QString outPath);
    void onPreviewUpdated(QImage img);
    void onProgress(int current, int total, double fps);
    void makeLivePhoto();

private:
    void setupUi();
    void exportLivePhoto(const cv::Mat &coverImg);

    // UI Elements
    DropLabel *m_dropLabel;
    QLabel *m_lblFile;
    QSpinBox *m_spinTrail;
    QDoubleSpinBox *m_spinFade;
    QComboBox *m_cmbRes;
    QComboBox *m_cmbFmt;
    QPushButton *m_btnRun;
    QPushButton *m_btnLive;
    QLabel *m_lblPreview;
    QLabel *m_lblStatus;
    QLabel *m_lblSpeed;
    QProgressBar *m_progressBar;

    // Logic
    ProcessorThread *m_processor;
    QString m_inPath;
    QString m_lastVideoPath;
};

#endif // MAINWINDOW_H
