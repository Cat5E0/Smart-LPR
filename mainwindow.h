#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QButtonGroup>
#include <QTextEdit>
#include <QTimer>
#include <QThread>
#include <QListWidget>
#include <QCheckBox>
#include <opencv2/opencv.hpp>
#include <QStringList>
#include <QVector>
#include <QTableWidget>

// 包含必要的自定义头文件
#include "visionengine.h"
#include "remotemodel.h"
#include "imageprocessor.h"

// [结构体定义] 批处理项
struct BatchItem {
    QString filePath;
    QString fileName;
    QString groundTruth; // 真值

    // --- Baseline (全云端) 数据 ---
    QString plateBase;
    cv::Rect rectBase;   // 红色框
    long timeBase;       // 耗时
    bool successBase;

    // --- Method (OpenCV+云) 数据 ---
    QString plateMethod;
    cv::Rect rectMethod; // 绿色框
    long timeMethod;     // 耗时
    bool successMethod;

    // 状态
    bool processed;
    QString errorReason;

    BatchItem() {
        rectBase = cv::Rect(0,0,0,0);
        rectMethod = cv::Rect(0,0,0,0);
        plateBase = "---";
        plateMethod = "---";
        timeBase = 0;
        timeMethod = 0;
        processed = false;
        successBase = false;
        successMethod = false;
    }
};

// [结构体定义] 统计指标
struct BenchmarkMetrics {
    int totalCount = 0;
    int successCount = 0;
    int correctCount = 0;
    long minTime = 999999, maxTime = 0, totalTime = 0;
    double avgTime = 0.0;
    double fps = 0.0;
    double networkThroughput = 0.0;
};

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onBtnOpenFile();
    void onBtnBatchProcess();
    void onBtnRun();
    void onBtnExportErrors();
    void onBtnDebugCV();

    void onBtnPrev();
    void onBtnNext();
    void onListFileClicked(QListWidgetItem *item);

    void onVisionProcessed(ProcessResult result);
    void onCloudResult(QString plate, double conf, QRect rect);
    void onCloudError(QString errorMsg);
    void onLogMsg(QString msg);

private:
    void setupUi();
    void showImageInLabel(const QImage &img, QLabel *lbl);
    void drawBoxesAndShow(const cv::Mat &src, const cv::Rect &rectBase, const cv::Rect &rectMethod);

    void startBenchmarkPhase();
    void processNextBatchItem();
    void finishBenchmarkPhase();
    void showComparisonReport();

    void displayResultToUI(const BatchItem &item);
    void displayBatchItem(int index);

    QString parseGroundTruth(const QString &fileName);
    void updateListItemStatus(int index);
    QString cleanPlateString(const QString &input);

    // UI 组件指针
    QLabel *lblVideo;
    QLabel *lblPlateImage;
    QLabel *lblResTruth;
    QLabel *lblResBase;
    QLabel *lblResMethod;
    QLabel *lblStatMin;
    QLabel *lblStatMax;
    QLabel *lblStatAvg;
    QLabel *lblStatTotalTime;
    QLabel *lblCurrentMode;
    QListWidget *listFileQueue;
    QLabel *lblBatchStatus;

    QPushButton *btnFile;
    QPushButton *btnBatch;
    QPushButton *btnRun;
    QPushButton *btnExport;
    QPushButton *btnDebugCV;
    QPushButton *btnPrev;
    QPushButton *btnNext;
    QCheckBox *checkAutoCompare;
    QTextEdit *txtLog;

    QTimer *timer;
    VisionEngine *visionEngine;
    QThread *visionThread;
    RemoteModel *remoteModel;
    ImageProcessor *imgProcessor;

    cv::Mat currentMat;
    bool isBatchRunning;
    QVector<BatchItem> currentBatchData;

    enum RunStage { IDLE, STAGE_BASELINE, STAGE_METHOD };
    RunStage currentStage;

    BenchmarkMetrics baselineMetrics;
    BenchmarkMetrics methodMetrics;

    struct FileInfo { QString path; QString name; QString truth; };
    QVector<FileInfo> loadedFiles;

    int currentBatchIndex;
    QString tempDirPath;
    qint64 itemStartTime;
};

#endif // MAINWINDOW_H
