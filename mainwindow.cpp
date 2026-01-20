#include "mainwindow.h"
#include "logger.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QDateTime>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QDebug>
#include <QStandardPaths>
#include <QDialog>
#include <QApplication>
#include <QTableWidget>
#include <QHeaderView>
#include <QPropertyAnimation>
#include <QtConcurrent>
#include <cmath> // [新增] 用于数学计算

// === CCPD 映射表 ===
const QStringList PROVINCES = {
    "皖", "沪", "津", "渝", "冀", "晋", "蒙", "辽", "吉", "黑",
    "苏", "浙", "京", "闽", "赣", "鲁", "豫", "鄂", "湘", "粤",
    "桂", "琼", "川", "贵", "云", "藏", "陕", "甘", "青", "宁", "新",
    "警", "学", "O"
};

// [修正] 原代码里是数字 '0'，CCPD标准其实是字母 'O'
const QStringList ALPHABETS = {
    "A", "B", "C", "D", "E", "F", "G", "H", "J", "K", "L", "M", "N",
    "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "O"
};

const QStringList ADS = {
    "A", "B", "C", "D", "E", "F", "G", "H", "J", "K", "L", "M", "N",
    "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "0",
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "O"
};

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), isBatchRunning(false), currentStage(IDLE), currentBatchIndex(-1)
{
    setupUi();

    QString picturesLoc = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if (picturesLoc.isEmpty()) picturesLoc = QDir::homePath();
    tempDirPath = picturesLoc + "/SmartLPR_Run";
    QDir dir(tempDirPath);
    if (!dir.exists()) dir.mkpath(".");

    visionThread = new QThread(this);
    visionEngine = new VisionEngine();
    visionEngine->moveToThread(visionThread);
    connect(visionEngine, &VisionEngine::processingFinished, this, &MainWindow::onVisionProcessed);
    visionThread->start();

    remoteModel = new RemoteModel(this);
    connect(remoteModel, &RemoteModel::recognitionFinished, this, &MainWindow::onCloudResult);
    connect(remoteModel, &RemoteModel::errorOccurred, this, &MainWindow::onCloudError);
    connect(remoteModel, &RemoteModel::statusLog, this, &MainWindow::onLogMsg);

    imgProcessor = new ImageProcessor();

    colorAnalyzer = new ColorAnalyzer(this);
    connect(colorAnalyzer, &ColorAnalyzer::progressUpdated, this, &MainWindow::onAnalysisProgress);
    connect(colorAnalyzer, &ColorAnalyzer::analysisFinished, this, &MainWindow::onAnalysisFinished);
    connect(colorAnalyzer, &ColorAnalyzer::logMessage, this, &MainWindow::onLogMsg);

    timer = new QTimer(this);
}

MainWindow::~MainWindow()
{
    visionThread->quit();
    visionThread->wait();
    delete visionEngine;
    delete imgProcessor;
}

void MainWindow::setupUi()
{
    QWidget *center = new QWidget;
    setCentralWidget(center);
    this->resize(1550, 950);
    this->setWindowTitle("SmartLPR - Pro Benchmark (Real-time Analysis)");

    QHBoxLayout *mainLay = new QHBoxLayout(center);

    // --- 左侧 ---
    QGroupBox *grpList = new QGroupBox("测试队列");
    QVBoxLayout *listLay = new QVBoxLayout(grpList);
    listFileQueue = new QListWidget;
    listFileQueue->setStyleSheet("font-size: 13px; font-family: Consolas;");
    connect(listFileQueue, &QListWidget::itemClicked, this, &MainWindow::onListFileClicked);

    lblBatchStatus = new QLabel("Ready");
    QHBoxLayout *navLay = new QHBoxLayout;
    btnPrev = new QPushButton("<<");
    btnNext = new QPushButton(">>");
    connect(btnPrev, &QPushButton::clicked, this, &MainWindow::onBtnPrev);
    connect(btnNext, &QPushButton::clicked, this, &MainWindow::onBtnNext);
    navLay->addWidget(btnPrev);
    navLay->addWidget(btnNext);

    listLay->addWidget(lblBatchStatus);
    listLay->addWidget(listFileQueue);
    listLay->addLayout(navLay);
    mainLay->addWidget(grpList, 1);

    // --- 中间 ---
    QGroupBox *grpVideo = new QGroupBox("视图 [红框:Baseline | 绿框:Method]");
    QVBoxLayout *vl = new QVBoxLayout(grpVideo);
    lblVideo = new QLabel;
    lblVideo->setAlignment(Qt::AlignCenter);
    lblVideo->setStyleSheet("background: #111; border: 2px solid #555;");
    lblVideo->setScaledContents(false);
    lblVideo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    // 统计面板
    QGroupBox *grpStats = new QGroupBox("实时效能分析 (Real-time Metrics)");
    QVBoxLayout *statLayout = new QVBoxLayout(grpStats);

    lblCurrentMode = new QLabel("Mode: Idle");
    lblCurrentMode->setStyleSheet("font-weight: bold; color: blue; font-size: 14px;");
    statLayout->addWidget(lblCurrentMode);

    // [UI 修改] 扩展表格列数，增加 Weather 和 Tilt
    // 列定义: FPS, AP, Normal, Weather, Rotate, Tilt, Blur, Challenge, DB
    tableMainStats = new QTableWidget(2, 9);
    QStringList headers;
    headers << "FPS" << "AP (All)" << "Normal" << "Weather" << "Rotate" << "Tilt" << "Blur" << "Challenge" << "DB";
    QStringList vHeaders;
    vHeaders << "Baseline" << "Method";

    tableMainStats->setHorizontalHeaderLabels(headers);
    tableMainStats->setVerticalHeaderLabels(vHeaders);
    tableMainStats->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    tableMainStats->setStyleSheet(
        "QTableWidget { font-size: 12px; gridline-color: #ccc; }"
        "QHeaderView::section { background-color: #f0f0f0; font-weight: bold; }"
    );
    tableMainStats->setFixedHeight(120);

    for(int r=0; r<2; r++) for(int c=0; c<9; c++)
        tableMainStats->setItem(r, c, new QTableWidgetItem("-"));

    statLayout->addWidget(tableMainStats);
    vl->addWidget(lblVideo, 3);
    vl->addWidget(grpStats, 1);
    mainLay->addWidget(grpVideo, 3);

    // --- 右侧 ---
    QVBoxLayout *rightLay = new QVBoxLayout;
    QGroupBox *grpCtrl = new QGroupBox("控制台");
    QVBoxLayout *gl = new QVBoxLayout(grpCtrl);

    btnBatch = new QPushButton("📂 1. 加载测试集");
    btnFile = new QPushButton("📄 加载单张");
    checkAutoCompare = new QCheckBox("自动 A/B 对比");
    checkAutoCompare->setChecked(true);

    btnRun = new QPushButton("🚀 2. 运行评估");
    btnRun->setMinimumHeight(45);
    btnRun->setStyleSheet("background: #007bff; color: white; font-weight: bold;");

    btnAnalyzeColor = new QPushButton("🧪 3. 颜色分布分析 (脚本)");
    btnAnalyzeColor->setMinimumHeight(35);
    btnAnalyzeColor->setStyleSheet("background: #17a2b8; color: white;");
    connect(btnAnalyzeColor, &QPushButton::clicked, this, &MainWindow::onBtnAnalyzeColor);

    btnExport = new QPushButton("💾 导出错误报告");
    btnDebugCV = new QPushButton("🛠️ OpenCV 全流程调试");
    btnDebugCV->setStyleSheet("background: #6f42c1; color: white;");

    connect(btnFile, &QPushButton::clicked, this, &MainWindow::onBtnOpenFile);
    connect(btnBatch, &QPushButton::clicked, this, &MainWindow::onBtnBatchProcess);
    connect(btnRun, &QPushButton::clicked, this, &MainWindow::onBtnRun);
    connect(btnExport, &QPushButton::clicked, this, &MainWindow::onBtnExportErrors);
    connect(btnDebugCV, &QPushButton::clicked, this, &MainWindow::onBtnDebugCV);

    gl->addWidget(btnBatch);
    gl->addWidget(btnFile);
    gl->addWidget(checkAutoCompare);
    gl->addWidget(btnRun);
    gl->addWidget(btnAnalyzeColor);
    gl->addWidget(btnExport);
    gl->addWidget(btnDebugCV);
    rightLay->addWidget(grpCtrl);

    QGroupBox *grpRes = new QGroupBox("单图结果");
    QVBoxLayout *rl = new QVBoxLayout(grpRes);
    lblPlateImage = new QLabel("ROI");
    lblPlateImage->setAlignment(Qt::AlignCenter);
    lblPlateImage->setFixedHeight(80);
    lblPlateImage->setStyleSheet("background: #ccc; border: 1px dashed gray;");
    lblResTruth = new QLabel("真值: ---");
    lblResTruth->setStyleSheet("font-weight: bold; color: black; font-size: 14px;");
    lblResBase = new QLabel("Base: ---");
    lblResBase->setStyleSheet("color: #d32f2f; font-size: 14px;");
    lblResMethod = new QLabel("Method: ---");
    lblResMethod->setStyleSheet("color: #2e7d32; font-size: 14px;");
    rl->addWidget(lblPlateImage);
    rl->addWidget(lblResTruth);
    rl->addWidget(lblResBase);
    rl->addWidget(lblResMethod);
    rl->addStretch();
    rightLay->addWidget(grpRes);

    txtLog = new QTextEdit;
    txtLog->setMaximumHeight(150);
    txtLog->setReadOnly(true);
    rightLay->addWidget(txtLog);

    mainLay->addLayout(rightLay, 1);
}

// ==========================================================
// [核心修复] 分类逻辑: 唯成分论，文件名里有啥就是啥
// ==========================================================
QString MainWindow::extractCategory(const QString &filePath) {
    QString fileName = QFileInfo(filePath).fileName();
    QString lowerName = fileName.toLower();

    // 1. Weather (天气): 包含 weather
    if (lowerName.contains("weather")) return "Weather";

    // 2. Rotate (旋转): 包含 rotate
    if (lowerName.contains("rotate")) return "Rotate";

    // 3. Tilt (倾斜): 包含 tilt
    if (lowerName.contains("tilt")) return "Tilt";

    // 4. Blur (模糊): 包含 blur
    if (lowerName.contains("blur")) return "Blur";

    // 5. DB (困难背景/双层): 包含 db
    if (lowerName.contains("db")) return "DB";

    // 6. FN (极难/漏检): 包含 fn
    if (lowerName.contains("fn")) return "FN";

    // 7. Challenge (挑战): 包含 challenge
    if (lowerName.contains("challenge")) return "Challenge";

    // 8. Base (基础): 包含 base -> 归为 Normal
    if (lowerName.contains("base")) return "Normal";

    // 9. 兜底策略: 如果文件名里没有任何标记，默认归为 Normal
    // 不要再去算角度了！因为Base数据本身就有角度，会导致误判。
    return "Normal";
}

QString MainWindow::parseGroundTruth(const QString &fileName) {
    QString base = QFileInfo(fileName).baseName();
    QStringList parts = base.split("-");

    // CCPD文件名格式: Area-Tilt-BBox-Vertices-LP-Brightness-Blurriness
    // 车牌信息在第5部分 (索引4)
    if (parts.size() >= 5) {
        QString labelPart = parts[4];
        QStringList codes = labelPart.split("_");
        if (codes.size() >= 7) {
            QString plate = "";
            bool ok = true;

            // 省份
            int provIdx = codes[0].toInt();
            if(provIdx >= 0 && provIdx < PROVINCES.size()) plate += PROVINCES[provIdx]; else ok=false;

            // 字母 (CCPD中 'O' 是填充位，不是数字0)
            int alphaIdx = codes[1].toInt();
            if(alphaIdx >= 0 && alphaIdx < ALPHABETS.size()) plate += ALPHABETS[alphaIdx]; else ok=false;

            // 后5位
            for(int i=2; i < codes.size(); i++) {
                int adIdx = codes[i].toInt();
                if(adIdx >= 0 && adIdx < ADS.size()) plate += ADS[adIdx]; else ok=false;
            }
            if(ok) return plate;
        }
    }
    // 如果解析失败，直接返回文件名作为真值（方便排查）
    return base;
}

void MainWindow::onBtnAnalyzeColor() {
    QString dirPath = QFileDialog::getExistingDirectory(this, "选择 CCPD 数据集 (用于颜色分析)");
    if(dirPath.isEmpty()) return;
    showToast("后台分析中... 请查看日志");
    colorAnalyzer->startAnalysis(dirPath);
}

void MainWindow::onAnalysisProgress(int current, int total) {
    if(current % 50 == 0)
        onLogMsg(QString("颜色分析进度: %1 / %2").arg(current).arg(total));
}

void MainWindow::onAnalysisFinished(QString resultMsg) {
    showToast("分析完成！");
    onLogMsg(resultMsg);
    QMessageBox::information(this, "Completed", resultMsg);
}

void MainWindow::onBtnBatchProcess() {
    QString dirPath = QFileDialog::getExistingDirectory(this, "选择 CCPD 数据集文件夹");
    if(dirPath.isEmpty()) return;

    QStringList filters; filters << "*.jpg" << "*.jpeg" << "*.png";
    QDirIterator it(dirPath, filters, QDir::Files, QDirIterator::Subdirectories);

    loadedFiles.clear();
    listFileQueue->clear();

    while (it.hasNext()) {
        QString filePath = it.next();
        QString fileName = QFileInfo(filePath).fileName();

        if (filePath.contains("/Analysis_Result/")) continue;
        // 过滤非 CCPD 格式文件 (必须包含 '-')
        if (!fileName.contains("-")) continue;

        QFileInfo fi(filePath);
        FileInfo info;
        info.path = filePath;
        info.name = fi.fileName();
        info.truth = parseGroundTruth(fi.fileName());
        info.category = extractCategory(filePath);

        loadedFiles.append(info);
        listFileQueue->addItem("[" + info.category + "] " + info.truth);
    }

    if (loadedFiles.isEmpty()) {
        QMessageBox::warning(this, "警告", "未找到有效的 CCPD 格式图片！\n请确认目录选择正确。");
    }

    lblBatchStatus->setText(QString("已加载: %1 张").arg(loadedFiles.size()));

    // 重置表格 (9列)
    for(int r=0; r<2; r++) for(int c=0; c<9; c++)
        tableMainStats->setItem(r, c, new QTableWidgetItem("-"));
}

void MainWindow::onBtnOpenFile() {
    QString path = QFileDialog::getOpenFileName(this, "选择图片");
    if(path.isEmpty()) return;
    loadedFiles.clear(); listFileQueue->clear();

    FileInfo info;
    info.path = path;
    info.name = QFileInfo(path).fileName();
    info.truth = parseGroundTruth(info.name);
    info.category = extractCategory(path);

    loadedFiles.append(info);
    listFileQueue->addItem("[" + info.category + "] " + info.truth);
}

void MainWindow::onBtnRun() {
    if (loadedFiles.isEmpty()) { QMessageBox::warning(this, "提示", "请先加载数据"); return; }
    if (checkAutoCompare->isChecked()) {
        onLogMsg("=== 自动 A/B 对比: 阶段 1/2 (Baseline) ===");
        currentStage = STAGE_BASELINE;
    } else {
        currentStage = STAGE_METHOD;
    }
    startBenchmarkPhase();
}

void MainWindow::startBenchmarkPhase() {
    if (currentStage == STAGE_BASELINE || !checkAutoCompare->isChecked()) {
        currentBatchData.clear();
        listFileQueue->clear();

        statsBase = MethodStats();
        statsMethod = MethodStats();

        for(const auto &info : loadedFiles) {
            BatchItem item;
            item.filePath = info.path;
            item.fileName = info.name;
            item.groundTruth = info.truth;
            item.category = info.category;
            currentBatchData.append(item);
            listFileQueue->addItem(info.truth);
        }
    } else {
        for(auto &item : currentBatchData) {
            item.processed = false;
        }
    }

    QString stageName = (currentStage == STAGE_BASELINE) ? "Baseline" : "Method";
    lblCurrentMode->setText("Mode: " + stageName);

    currentBatchIndex = 0;
    isBatchRunning = true;
    processNextBatchItem();
}

void MainWindow::processNextBatchItem() {
    if(!isBatchRunning) return;

    if(currentBatchIndex >= currentBatchData.size()) {
        finishBenchmarkPhase();
        return;
    }

    updateListItemStatus(currentBatchIndex);
    listFileQueue->scrollToItem(listFileQueue->item(currentBatchIndex));
    QApplication::processEvents();

    BatchItem &item = currentBatchData[currentBatchIndex];
    itemStartTime = QDateTime::currentMSecsSinceEpoch();

    currentMat = cv::imread(item.filePath.toStdString());
    if(currentMat.empty()) { onCloudError("Image Load Failed"); return; }

    drawBoxesAndShow(currentMat, item.rectBase, item.rectMethod);

    if (currentStage == STAGE_BASELINE) {
        QImage fullImg = visionEngine->matToQImage(currentMat);
        remoteModel->recognizeLicensePlate(fullImg);
    } else {
        QMetaObject::invokeMethod(visionEngine, "processFrame", Qt::QueuedConnection, Q_ARG(cv::Mat, currentMat));
    }
}

void MainWindow::drawBoxesAndShow(const cv::Mat &src, const cv::Rect &rectBase, const cv::Rect &rectMethod) {
    if(src.empty()) return;
    cv::Mat draw = src.clone();

    if(rectBase.width > 0 && rectBase.height > 0) {
        cv::rectangle(draw, rectBase, cv::Scalar(0, 0, 255), 5);
        cv::putText(draw, "Base", rectBase.tl() - cv::Point(0, 10), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0,0,255), 2);
    }
    if(rectMethod.width > 0 && rectMethod.height > 0) {
        cv::rectangle(draw, rectMethod, cv::Scalar(0, 255, 0), 3);
        cv::putText(draw, "Method", rectMethod.br() + cv::Point(0, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0,255,0), 2);
    }
    showImageInLabel(visionEngine->matToQImage(draw), lblVideo);
}

void MainWindow::showToast(const QString &text) {
    QDialog *toast = new QDialog(this, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    toast->setAttribute(Qt::WA_TranslucentBackground);
    toast->setAttribute(Qt::WA_DeleteOnClose);

    QVBoxLayout *layout = new QVBoxLayout(toast);
    QLabel *lbl = new QLabel(text);
    lbl->setStyleSheet("background-color: rgba(0, 120, 215, 230); color: white; border-radius: 8px; padding: 15px 40px; font-size: 18px; font-weight: bold;");
    lbl->setAlignment(Qt::AlignCenter);
    layout->addWidget(lbl);
    layout->setMargin(0);

    toast->adjustSize();
    toast->move(this->geometry().center() - toast->rect().center());
    toast->show();

    QTimer::singleShot(2500, toast, [=]() {
        QPropertyAnimation *anim = new QPropertyAnimation(toast, "windowOpacity");
        anim->setDuration(500);
        anim->setStartValue(1.0);
        anim->setEndValue(0.0);
        connect(anim, &QPropertyAnimation::finished, toast, &QDialog::close);
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    });
}

void MainWindow::finishBenchmarkPhase() {
    isBatchRunning = false;

    if (currentStage == STAGE_BASELINE) {
        if (checkAutoCompare->isChecked()) {
            showToast("Baseline 阶段完成，即将开始 Method 阶段...");
            QTimer::singleShot(2000, this, [=](){
                onLogMsg("=== 阶段 2/2 (Method) 开始 ===");
                currentStage = STAGE_METHOD;
                startBenchmarkPhase();
            });
        } else {
            showToast("Baseline 评估完成！");
        }
    } else {
        showToast("所有评估已完成！结果已显示在表格中。");
        currentStage = IDLE;
        lblCurrentMode->setText("Mode: Idle");
    }
}

// [UI 刷新] 支持9列数据
void MainWindow::updateTableUI() {
    auto updateRow = [&](int row, const MethodStats &stats) {
        double fps = 0.0;
        if (stats.totalTimeMs > 0) fps = (double)stats.totalProcessed / (stats.totalTimeMs / 1000.0);
        tableMainStats->setItem(row, 0, new QTableWidgetItem(QString::number(fps, 'f', 1)));

        double ap = 0.0;
        if(stats.all.count > 0) ap = (double)stats.all.correct / stats.all.count * 100.0;
        QTableWidgetItem *itemAP = new QTableWidgetItem(QString::number(ap, 'f', 1) + "%");
        itemAP->setTextAlignment(Qt::AlignCenter);
        if(ap > 90.0) itemAP->setForeground(Qt::darkGreen);
        tableMainStats->setItem(row, 1, itemAP);

        // 对应 setupUi 里的列顺序:
        // Normal, Weather, Rotate, Tilt, Blur, Challenge, DB
        QStringList cats;
        cats << "Normal" << "Weather" << "Rotate" << "Tilt" << "Blur" << "Challenge" << "DB";

        for(int i=0; i<cats.size(); i++) {
            QString cat = cats[i];
            QString text = "-";
            if(stats.subsets.contains(cat)) {
                SubsetStats s = stats.subsets[cat];
                if(s.count > 0) {
                    double acc = (double)s.correct / s.count * 100.0;
                    text = QString::number(acc, 'f', 1) + "%";
                }
            }
            QTableWidgetItem *it = new QTableWidgetItem(text);
            it->setTextAlignment(Qt::AlignCenter);
            tableMainStats->setItem(row, 2 + i, it);
        }
    };

    updateRow(0, statsBase);
    updateRow(1, statsMethod);
}

void MainWindow::onCloudResult(QString plate, double conf, QRect rect) {
    if (!isBatchRunning) return;

    long now = QDateTime::currentMSecsSinceEpoch();
    long cost = now - itemStartTime;
    BatchItem &item = currentBatchData[currentBatchIndex];
    item.processed = true;

    QString cleanRes = cleanPlateString(plate);
    QString cleanGt = cleanPlateString(item.groundTruth);
    bool isCorrect = (cleanRes == cleanGt && !cleanRes.isEmpty());

    MethodStats *s = (currentStage == STAGE_BASELINE) ? &statsBase : &statsMethod;
    s->totalTimeMs += cost;
    s->totalProcessed++;

    s->all.count++;
    if(isCorrect) s->all.correct++;

    if(!s->subsets.contains(item.category)) s->subsets[item.category] = SubsetStats();
    s->subsets[item.category].count++;
    if(isCorrect) s->subsets[item.category].correct++;

    if (currentStage == STAGE_BASELINE) {
        item.plateBase = plate;
        item.timeBase = cost;
        item.correctBase = isCorrect;
        if(!rect.isEmpty()) item.rectBase = cv::Rect(rect.x(), rect.y(), rect.width(), rect.height());
    } else {
        item.plateMethod = plate;
        item.timeMethod = cost;
        item.correctMethod = isCorrect;
    }

    updateListItemStatus(currentBatchIndex);
    updateTableUI();
    displayResultToUI(item);
    drawBoxesAndShow(currentMat, item.rectBase, item.rectMethod);

    QString modeName = (currentStage == STAGE_BASELINE) ? "Baseline" : "Method";
    Logger::log(item.fileName, modeName, plate, item.groundTruth, isCorrect, conf, 0, 0, cost, true);

    currentBatchIndex++;
    QTimer::singleShot(2200, this, &MainWindow::processNextBatchItem);
}

void MainWindow::onVisionProcessed(ProcessResult result) {
    if (!isBatchRunning) return;
    BatchItem &item = currentBatchData[currentBatchIndex];
    item.rectMethod = result.roi;
    drawBoxesAndShow(currentMat, item.rectBase, item.rectMethod);

    QImage imageToUpload;
    if (result.found && !result.plateImage.isNull()) {
        showImageInLabel(result.plateImage, lblPlateImage);
        imageToUpload = result.plateImage;
    } else {
        lblPlateImage->clear();
        imageToUpload = visionEngine->matToQImage(currentMat);
    }

    remoteModel->recognizeLicensePlate(imageToUpload);
}

// ==========================================================
// [核心修复] 统计 Bug: 失败的样本必须计入总分母 (all.count)
// ==========================================================
void MainWindow::onCloudError(QString errorMsg) {
    if (!isBatchRunning) { onLogMsg("Err: " + errorMsg); return; }

    long now = QDateTime::currentMSecsSinceEpoch();
    long cost = now - itemStartTime;
    BatchItem &item = currentBatchData[currentBatchIndex];
    item.processed = true;

    // --- 修复开始 ---
    MethodStats *s = (currentStage == STAGE_BASELINE) ? &statsBase : &statsMethod;
    s->totalTimeMs += cost;
    s->totalProcessed++; // 总数+1

    s->all.count++; // 准确率分母+1
    // s->all.correct 不加，因为失败了

    // 子集统计也要加分母
    if(!s->subsets.contains(item.category)) s->subsets[item.category] = SubsetStats();
    s->subsets[item.category].count++;
    // --- 修复结束 ---

    if(currentStage == STAGE_BASELINE) {
        item.plateBase = "FAILED";
        item.timeBase = cost;
        item.successBase = false;
    } else {
        item.plateMethod = "FAILED";
        item.timeMethod = cost;
        item.successMethod = false;
    }
    item.errorReason = errorMsg;

    onLogMsg(QString("[%1] Err: %2").arg(item.groundTruth).arg(errorMsg));

    updateListItemStatus(currentBatchIndex);
    updateTableUI(); // 实时刷新表格

    displayResultToUI(item);

    QString modeName = (currentStage == STAGE_BASELINE) ? "Baseline" : "Method";
    Logger::log(item.fileName, modeName, "Error", item.groundTruth, false, 0, 0, 0, cost, false);

    currentBatchIndex++;
    QTimer::singleShot(2200, this, &MainWindow::processNextBatchItem);
}

QString MainWindow::cleanPlateString(const QString &input) {
    QString res;
    for(QChar c : input) {
        if(c.isLetterOrNumber() || (c.script() == QChar::Script_Han)) {
            if(c == 'O') res.append('0'); // 这里是清洗结果，为了对比方便
            else res.append(c);
        }
    }
    return res.toUpper();
}

void MainWindow::displayResultToUI(const BatchItem &item) {
    lblResTruth->setText("真值: " + item.groundTruth);
    QString gt = cleanPlateString(item.groundTruth);
    lblResBase->setText("Base: " + item.plateBase);
    lblResBase->setStyleSheet(cleanPlateString(item.plateBase) == gt ? "color: green;" : "color: red;");
    lblResMethod->setText("Method: " + item.plateMethod);
    lblResMethod->setStyleSheet(cleanPlateString(item.plateMethod) == gt ? "color: green;" : "color: red;");
}

void MainWindow::updateListItemStatus(int index) {
    if(index < 0 || index >= listFileQueue->count()) return;
    QListWidgetItem *it = listFileQueue->item(index);
    BatchItem &data = currentBatchData[index];
    QString text = QString("[%1] %2").arg(data.category).arg(data.groundTruth);
    QString gt = cleanPlateString(data.groundTruth);
    if(!data.plateBase.isEmpty() && data.plateBase != "---")
        text += (cleanPlateString(data.plateBase) == gt) ? " [B:√]" : " [B:×]";
    if(!data.plateMethod.isEmpty() && data.plateMethod != "---")
        text += (cleanPlateString(data.plateMethod) == gt) ? " [M:√]" : " [M:×]";
    it->setText(text);
    if (index == currentBatchIndex) it->setForeground(Qt::blue);
    else it->setForeground(Qt::black);
}

void MainWindow::onBtnExportErrors() {
    QString path = QFileDialog::getSaveFileName(this, "Export", "error_report.csv", "CSV (*.csv)");
    if(path.isEmpty()) return;
    QFile file(path);
    if(file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "FileName,Category,GroundTruth,BaseResult,MethodResult,Reason\n";
        for(const auto &item : currentBatchData) {
            QString cleanGt = cleanPlateString(item.groundTruth);
            bool baseOk = (cleanPlateString(item.plateBase) == cleanGt);
            bool methOk = (cleanPlateString(item.plateMethod) == cleanGt);
            if(!baseOk || !methOk) {
                out << item.fileName << "," << item.category << "," << item.groundTruth << "," << item.plateBase << "," << item.plateMethod << "," << item.errorReason << "\n";
            }
        }
        file.close();
        showToast("错误报告已导出！");
    }
}

void MainWindow::displayBatchItem(int index) {
    if(index < 0 || index >= currentBatchData.size()) return;
    BatchItem &item = currentBatchData[index];
    currentMat = cv::imread(item.filePath.toStdString());
    if(!currentMat.empty()) {
        drawBoxesAndShow(currentMat, item.rectBase, item.rectMethod);
    }
    displayResultToUI(item);
    if(item.rectMethod.width > 0) {
        cv::Mat crop = currentMat(item.rectMethod);
        showImageInLabel(visionEngine->matToQImage(crop), lblPlateImage);
    } else {
        lblPlateImage->clear();
    }
}

void MainWindow::showImageInLabel(const QImage &img, QLabel *lbl) {
    if(img.isNull()) { lbl->clear(); return; }
    lbl->setPixmap(QPixmap::fromImage(img).scaled(lbl->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::onBtnDebugCV() {
    if(currentBatchIndex < 0 || currentBatchIndex >= currentBatchData.size()) {
        showToast("请先选择一张图片");
        return;
    }
    cv::Mat raw = cv::imread(currentBatchData[currentBatchIndex].filePath.toStdString());
    if(raw.empty()) return;

    PipelineDebugData debugData = visionEngine->getDebugPipelineData(raw);

    QDialog *dlg = new QDialog(this);
    dlg->setWindowTitle("OpenCV Pipeline Full Inspection (Real Engine Data)");
    dlg->resize(1400, 800);
    QGridLayout *grid = new QGridLayout(dlg);

    auto addView = [&](QString title, cv::Mat m, int r, int c) {
        if(m.empty()) return;
        QLabel *lbl = new QLabel;
        lbl->setPixmap(QPixmap::fromImage(visionEngine->matToQImage(m)).scaled(450, 300, Qt::KeepAspectRatio));
        lbl->setStyleSheet("border: 1px solid black; background: #eee;");
        lbl->setAlignment(Qt::AlignCenter);

        QVBoxLayout *box = new QVBoxLayout;
        QLabel *t = new QLabel(title);
        t->setAlignment(Qt::AlignCenter);
        t->setStyleSheet("font-weight: bold; font-size: 14px;");
        box->addWidget(t);
        box->addWidget(lbl);
        grid->addLayout(box, r, c);
    };

    addView("1. Preprocessing (Resize)", debugData.resized, 0, 0);
    addView("2. Grayscale (Texture Input)", debugData.gray, 0, 1);
    addView("3. Color Mask (Blue+Green)", debugData.hsvMask, 0, 2);

    addView("4. Sobel Edges", debugData.sobel, 1, 0);
    addView("5. Otsu Threshold", debugData.binary, 1, 1);
    addView("6. Morphology (Close)", debugData.morph, 1, 2);

    dlg->exec();
}

void MainWindow::onLogMsg(QString msg) {
    txtLog->append(QDateTime::currentDateTime().toString("[HH:mm:ss] ") + msg);
}
void MainWindow::onBtnPrev() { if(currentBatchIndex > 0) displayBatchItem(--currentBatchIndex); }
void MainWindow::onBtnNext() { if(currentBatchIndex < currentBatchData.size()-1) displayBatchItem(++currentBatchIndex); }
void MainWindow::onListFileClicked(QListWidgetItem *item) {
    currentBatchIndex = listFileQueue->row(item);
    displayBatchItem(currentBatchIndex);
}
