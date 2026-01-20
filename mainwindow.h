#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QTimer>
#include <QThread>
#include <QListWidget>
#include <QCheckBox>
#include <QTableWidget>
#include <QMap>
#include <opencv2/opencv.hpp>

#include "visionengine.h"
#include "remotemodel.h"
#include "imageprocessor.h"
#include "coloranalyzer.h" // [新增]

struct BatchItem {
    QString filePath;
    QString fileName;
    QString groundTruth;
    QString category;

    // --- Baseline ---
    QString plateBase;
    cv::Rect rectBase;
    long timeBase;
    bool successBase;
    bool correctBase;

    // --- Method ---
    QString plateMethod;
    cv::Rect rectMethod;
    long timeMethod;
    bool successMethod;
    bool correctMethod;

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
        correctBase = false;
        successMethod = false;
        correctMethod = false;
        category = "Other";
    }
};

struct SubsetStats {
    int count = 0;
    int correct = 0;
};

struct MethodStats {
    long totalTimeMs = 0;
    int totalProcessed = 0;

    SubsetStats all;
    QMap<QString, SubsetStats> subsets;
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

    // [新增] 触发颜色分析
    void onBtnAnalyzeColor();
    // [新增] 颜色分析回调
    void onAnalysisProgress(int current, int total);
    void onAnalysisFinished(QString path);

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
    void showToast(const QString &msg);

    void displayResultToUI(const BatchItem &item);
    void displayBatchItem(int index);

    QString parseGroundTruth(const QString &fileName);
    QString extractCategory(const QString &filePath);

    void updateTableUI();

    void updateListItemStatus(int index);
    QString cleanPlateString(const QString &input);

    QLabel *lblVideo;
    QLabel *lblPlateImage;
    QLabel *lblResTruth;
    QLabel *lblResBase;
    QLabel *lblResMethod;
    QLabel *lblCurrentMode;
    QListWidget *listFileQueue;
    QLabel *lblBatchStatus;

    QTableWidget *tableMainStats;

    QPushButton *btnFile;
    QPushButton *btnBatch;
    QPushButton *btnRun;
    QPushButton *btnExport;
    QPushButton *btnDebugCV;
    QPushButton *btnAnalyzeColor; // [新增]

    QPushButton *btnPrev;
    QPushButton *btnNext;
    QCheckBox *checkAutoCompare;
    QTextEdit *txtLog;

    QTimer *timer;
    VisionEngine *visionEngine;
    QThread *visionThread;
    RemoteModel *remoteModel;
    ImageProcessor *imgProcessor;
    ColorAnalyzer *colorAnalyzer; // [新增]

    cv::Mat currentMat;
    bool isBatchRunning;
    QVector<BatchItem> currentBatchData;

    enum RunStage { IDLE, STAGE_BASELINE, STAGE_METHOD };
    RunStage currentStage;

    MethodStats statsBase;
    MethodStats statsMethod;

    struct FileInfo { QString path; QString name; QString truth; QString category; };
    QVector<FileInfo> loadedFiles;

    int currentBatchIndex;
    QString tempDirPath;
    qint64 itemStartTime;
};

#endif // MAINWINDOW_H
