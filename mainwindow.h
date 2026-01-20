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
#include <QComboBox> // [新增] 下拉框
#include <QTableWidget>
#include <QMap>
#include <QHash>     // [新增] 哈希表
#include <QStringList> // [新增]
#include <opencv2/opencv.hpp>

#include "visionengine.h"
#include "remotemodel.h"
#include "imageprocessor.h"
#include "coloranalyzer.h"

struct BatchItem {
    QString filePath;
    QString fileName;
    QString groundTruth;

    // [修改] 支持多标签 (例如同时属于 Tilt 和 Blur)
    QStringList categories;

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
        categories.append("Normal"); // 默认为 Normal
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

    // 颜色分析
    void onBtnAnalyzeColor();
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

    // [修改] 返回多标签列表
    QStringList extractCategories(const QString &filePath);

    // [新增] 加载官方 Split 文件
    void loadSplitFiles(const QString &ccpdRootPath);

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
    QPushButton *btnAnalyzeColor;

    QPushButton *btnPrev;
    QPushButton *btnNext;

    // [修改] 替换 CheckBox 为 ComboBox
    QComboBox *comboModeSelect;

    QTextEdit *txtLog;

    QTimer *timer;
    VisionEngine *visionEngine;
    QThread *visionThread;
    RemoteModel *remoteModel;
    ImageProcessor *imgProcessor;
    ColorAnalyzer *colorAnalyzer;

    cv::Mat currentMat;
    bool isBatchRunning;
    QVector<BatchItem> currentBatchData;

    // [修改] 类别映射表 (文件名 -> 类别列表)
    QHash<QString, QStringList> fileCategoryMap;

    enum RunStage { IDLE, STAGE_BASELINE, STAGE_METHOD };
    RunStage currentStage;

    MethodStats statsBase;
    MethodStats statsMethod;

    struct FileInfo { QString path; QString name; QString truth; QStringList categories; };
    QVector<FileInfo> loadedFiles;

    int currentBatchIndex;
    QString tempDirPath;
    qint64 itemStartTime;
};

#endif // MAINWINDOW_H
