#include "mainwindow.h"
#include "logger.h"
#include <QFileDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QDateTime>
#include <QMessageBox>
#include <QDir>
#include <QDebug>
#include <QStandardPaths>
#include <QDialog>
#include <QApplication>
#include <QTableWidget>
#include <QHeaderView>

// === CCPD 映射表 ===
const QStringList PROVINCES = {
    "皖", "沪", "津", "渝", "冀", "晋", "蒙", "辽", "吉", "黑",
    "苏", "浙", "京", "闽", "赣", "鲁", "豫", "鄂", "湘", "粤",
    "桂", "琼", "川", "贵", "云", "藏", "陕", "甘", "青", "宁", "新",
    "警", "学", "O"
};
const QStringList ALPHABETS = {
    "A", "B", "C", "D", "E", "F", "G", "H", "J", "K", "L", "M", "N",
    "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z", "0"
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
    this->resize(1440, 900);
    this->setWindowTitle("SmartLPR");

    QHBoxLayout *mainLay = new QHBoxLayout(center);

    // 左侧列表
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

    // 中间视图
    QGroupBox *grpVideo = new QGroupBox("视图 [红框:Baseline | 绿框:Method]");
    QVBoxLayout *vl = new QVBoxLayout(grpVideo);

    lblVideo = new QLabel;
    lblVideo->setAlignment(Qt::AlignCenter);
    lblVideo->setStyleSheet("background: #111; border: 2px solid #555;");
    lblVideo->setScaledContents(false);
    lblVideo->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    // 统计
    QGroupBox *grpStats = new QGroupBox("实时统计");
    QGridLayout *statGrid = new QGridLayout(grpStats);

    lblCurrentMode = new QLabel("Mode: Idle");
    lblCurrentMode->setStyleSheet("font-weight: bold; color: blue; font-size: 14px;");

    lblStatMin = new QLabel("Min: -");
    lblStatMax = new QLabel("Max: -");
    lblStatAvg = new QLabel("Avg: -");
    lblStatTotalTime = new QLabel("Total: -");

    statGrid->addWidget(lblCurrentMode, 0, 0, 1, 2);
    statGrid->addWidget(lblStatMin, 1, 0);
    statGrid->addWidget(lblStatMax, 1, 1);
    statGrid->addWidget(lblStatAvg, 2, 0);
    statGrid->addWidget(lblStatTotalTime, 2, 1);

    vl->addWidget(lblVideo, 1);
    vl->addWidget(grpStats);
    mainLay->addWidget(grpVideo, 2);

    // 右侧控制
    QVBoxLayout *rightLay = new QVBoxLayout;

    QGroupBox *grpCtrl = new QGroupBox("控制台");
    QVBoxLayout *gl = new QVBoxLayout(grpCtrl);
    btnBatch = new QPushButton("📂 1. 加载测试集");
    btnFile = new QPushButton("📄 加载单张");
    checkAutoCompare = new QCheckBox("自动 A/B 对比");
    checkAutoCompare->setChecked(true);
    btnRun = new QPushButton("🚀 2. 运行评估");
    btnRun->setMinimumHeight(50);
    btnRun->setStyleSheet("background: #007bff; color: white; font-weight: bold;");

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
    gl->addWidget(btnExport);
    gl->addWidget(btnDebugCV);
    rightLay->addWidget(grpCtrl);

    // 三方结果显示
    QGroupBox *grpRes = new QGroupBox("结果对比");
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

QString MainWindow::parseGroundTruth(const QString &fileName) {
    QString base = QFileInfo(fileName).baseName();
    QStringList parts = base.split("-");
    if (parts.size() >= 5) {
        QString labelPart = parts[4];
        QStringList codes = labelPart.split("_");
        if (codes.size() >= 7) {
            QString plate = "";
            bool ok = true;
            int provIdx = codes[0].toInt();
            if(provIdx >= 0 && provIdx < PROVINCES.size()) plate += PROVINCES[provIdx]; else ok=false;
            int alphaIdx = codes[1].toInt();
            if(alphaIdx >= 0 && alphaIdx < ALPHABETS.size()) plate += ALPHABETS[alphaIdx]; else ok=false;
            for(int i=2; i < codes.size(); i++) {
                int adIdx = codes[i].toInt();
                if(adIdx >= 0 && adIdx < ADS.size()) plate += ADS[adIdx]; else ok=false;
            }
            if(ok) return plate;
        }
    }
    return base;
}

void MainWindow::onBtnBatchProcess() {
    QString dirPath = QFileDialog::getExistingDirectory(this, "选择 CCPD 数据集文件夹");
    if(dirPath.isEmpty()) return;
    QDir dir(dirPath);
    QStringList filters; filters << "*.jpg" << "*.jpeg" << "*.png";
    QFileInfoList fileList = dir.entryInfoList(filters, QDir::Files);
    loadedFiles.clear(); listFileQueue->clear();
    for(const auto &fi : fileList) {
        FileInfo info; info.path = fi.absoluteFilePath(); info.name = fi.fileName(); info.truth = parseGroundTruth(fi.fileName());
        loadedFiles.append(info);
        listFileQueue->addItem(info.truth);
    }
    lblBatchStatus->setText(QString("已加载: %1 张").arg(loadedFiles.size()));
}

void MainWindow::onBtnOpenFile() {
    QString path = QFileDialog::getOpenFileName(this, "选择图片");
    if(path.isEmpty()) return;
    loadedFiles.clear(); listFileQueue->clear();
    FileInfo info; info.path = path; info.name = QFileInfo(path).fileName(); info.truth = parseGroundTruth(info.name);
    loadedFiles.append(info);
    listFileQueue->addItem(info.truth);
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
        for(const auto &info : loadedFiles) {
            BatchItem item;
            item.filePath = info.path;
            item.fileName = info.name;
            item.groundTruth = info.truth;
            currentBatchData.append(item);
            listFileQueue->addItem(info.truth);
        }
        baselineMetrics = BenchmarkMetrics();
        methodMetrics = BenchmarkMetrics();
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

    // Baseline: Red
    if(rectBase.width > 0 && rectBase.height > 0) {
        cv::rectangle(draw, rectBase, cv::Scalar(0, 0, 255), 5);
        cv::putText(draw, "Base", rectBase.tl() - cv::Point(0, 10), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0,0,255), 2);
    }

    // Method: Green
    if(rectMethod.width > 0 && rectMethod.height > 0) {
        cv::rectangle(draw, rectMethod, cv::Scalar(0, 255, 0), 3);
        cv::putText(draw, "Method", rectMethod.br() + cv::Point(0, 30), cv::FONT_HERSHEY_SIMPLEX, 1.0, cv::Scalar(0,255,0), 2);
    }

    showImageInLabel(visionEngine->matToQImage(draw), lblVideo);
}

void MainWindow::finishBenchmarkPhase() {
    isBatchRunning = false;

    BenchmarkMetrics *m = (currentStage == STAGE_BASELINE) ? &baselineMetrics : &methodMetrics;
    m->totalCount = currentBatchData.size();
    long totalNetSize = 0;

    for(const auto &item : currentBatchData) {
        bool success = (currentStage == STAGE_BASELINE) ? item.successBase : item.successMethod;
        QString res = (currentStage == STAGE_BASELINE) ? item.plateBase : item.plateMethod;
        long t = (currentStage == STAGE_BASELINE) ? item.timeBase : item.timeMethod;

        QString cleanRes = cleanPlateString(res);
        QString cleanGt = cleanPlateString(item.groundTruth);

        if(success) m->successCount++;
        if(cleanRes == cleanGt && !cleanRes.isEmpty()) m->correctCount++;

        if(t > 0) {
            m->totalTime += t;
            if(t < m->minTime) m->minTime = t;
            if(t > m->maxTime) m->maxTime = t;
            totalNetSize += (currentStage == STAGE_BASELINE) ? 200 : 50;
        }
    }
    if (m->minTime == 999999) m->minTime = 0;
    if (m->successCount > 0) m->avgTime = (double)m->totalTime / m->successCount;
    double durationSec = m->totalTime / 1000.0;
    if(durationSec > 0) m->networkThroughput = totalNetSize / durationSec;

    if (currentStage == STAGE_BASELINE) {
        if (checkAutoCompare->isChecked()) {
            QTimer::singleShot(2000, this, [=](){
                onLogMsg("=== 阶段 2/2 (Method) 开始 ===");
                currentStage = STAGE_METHOD;
                startBenchmarkPhase();
            });
        } else {
            QMessageBox::information(this, "Done", "Baseline Finished.");
        }
    } else {
        showComparisonReport();
        currentStage = IDLE;
        lblCurrentMode->setText("Mode: Idle");
    }
}

void MainWindow::onCloudResult(QString plate, double conf, QRect rect) {
    if (!isBatchRunning) return;

    long now = QDateTime::currentMSecsSinceEpoch();
    long cost = now - itemStartTime;
    BatchItem &item = currentBatchData[currentBatchIndex];
    item.processed = true;

    if (currentStage == STAGE_BASELINE) {
        item.plateBase = plate;
        item.timeBase = cost;
        item.successBase = true;
        if(!rect.isEmpty()) {
            item.rectBase = cv::Rect(rect.x(), rect.y(), rect.width(), rect.height());
        }
    } else {
        item.plateMethod = plate;
        item.timeMethod = cost;
        item.successMethod = true;
    }

    BenchmarkMetrics *m = (currentStage == STAGE_BASELINE) ? &baselineMetrics : &methodMetrics;
    m->totalTime += cost;
    m->successCount++;
    double avg = m->totalTime / m->successCount;
    if(cost < m->minTime) m->minTime = cost;
    if(cost > m->maxTime) m->maxTime = cost;

    lblStatMin->setText(QString("Min: %1 ms").arg(m->minTime));
    lblStatMax->setText(QString("Max: %1 ms").arg(m->maxTime));
    lblStatAvg->setText(QString("Avg: %1 ms").arg((int)avg));
    lblStatTotalTime->setText(QString("Total: %1 s").arg(m->totalTime / 1000.0, 0, 'f', 1));

    updateListItemStatus(currentBatchIndex);
    displayResultToUI(item);
    drawBoxesAndShow(currentMat, item.rectBase, item.rectMethod);

    QString modeName = (currentStage == STAGE_BASELINE) ? "Baseline" : "Method";
    Logger::log(item.fileName, modeName, plate, item.groundTruth, (cleanPlateString(plate)==cleanPlateString(item.groundTruth)), conf, 0, 0, cost, true);

    currentBatchIndex++;
    QTimer::singleShot(1500, this, &MainWindow::processNextBatchItem);
}

void MainWindow::onVisionProcessed(ProcessResult result) {
    if (!isBatchRunning) return;

    BatchItem &item = currentBatchData[currentBatchIndex];
    item.rectMethod = result.roi;
    drawBoxesAndShow(currentMat, item.rectBase, item.rectMethod);

    if (result.found && !result.plateImage.isNull()) {
        showImageInLabel(result.plateImage, lblPlateImage);
        remoteModel->recognizeLicensePlate(result.plateImage);
    } else {
        onCloudError("OpenCV No ROI");
    }
}

void MainWindow::onCloudError(QString errorMsg) {
    if (!isBatchRunning) { onLogMsg("Err: " + errorMsg); return; }

    long now = QDateTime::currentMSecsSinceEpoch();
    long cost = now - itemStartTime;
    BatchItem &item = currentBatchData[currentBatchIndex];
    item.processed = true;

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
    displayResultToUI(item);

    QString modeName = (currentStage == STAGE_BASELINE) ? "Baseline" : "Method";
    Logger::log(item.fileName, modeName, "Error", item.groundTruth, false, 0, 0, 0, cost, false);

    currentBatchIndex++;
    QTimer::singleShot(1500, this, &MainWindow::processNextBatchItem);
}

QString MainWindow::cleanPlateString(const QString &input) {
    QString res;
    for(QChar c : input) {
        if(c.isLetterOrNumber() || (c.script() == QChar::Script_Han)) {
            if(c == 'O') res.append('0');
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

    QString text = data.groundTruth;
    QString gt = cleanPlateString(data.groundTruth);
    if(!data.plateBase.isEmpty() && data.plateBase != "---")
        text += (cleanPlateString(data.plateBase) == gt) ? " [B:√]" : " [B:×]";
    if(!data.plateMethod.isEmpty() && data.plateMethod != "---")
        text += (cleanPlateString(data.plateMethod) == gt) ? " [M:√]" : " [M:×]";

    it->setText(text);
    if (index == currentBatchIndex) it->setForeground(Qt::blue);
    else it->setForeground(Qt::black);
}

void MainWindow::showComparisonReport() {
    QDialog *dlg = new QDialog(this);
    dlg->setWindowTitle("Final Benchmark Report");
    dlg->resize(700, 450);
    QVBoxLayout *lay = new QVBoxLayout(dlg);
    QTableWidget *table = new QTableWidget(5, 3);
    table->setHorizontalHeaderLabels({"Metrics", "Baseline", "Method"});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    auto setItem = [&](int r, int c, QString txt) {
        QTableWidgetItem *it = new QTableWidgetItem(txt);
        it->setTextAlignment(Qt::AlignCenter);
        table->setItem(r, c, it);
    };

    double accBase = baselineMetrics.totalCount ? (double)baselineMetrics.correctCount / baselineMetrics.totalCount * 100.0 : 0;
    double accMeth = methodMetrics.totalCount ? (double)methodMetrics.correctCount / methodMetrics.totalCount * 100.0 : 0;

    setItem(0, 0, "Accuracy");
    setItem(0, 1, QString::number(accBase, 'f', 2) + "%");
    setItem(0, 2, QString::number(accMeth, 'f', 2) + "%");
    setItem(1, 0, "Avg Latency");
    setItem(1, 1, QString::number(baselineMetrics.avgTime, 'f', 0) + " ms");
    setItem(1, 2, QString::number(methodMetrics.avgTime, 'f', 0) + " ms");
    lay->addWidget(table);
    dlg->exec();
}

void MainWindow::onBtnExportErrors() {
    QString path = QFileDialog::getSaveFileName(this, "Export", "error_report.csv", "CSV (*.csv)");
    if(path.isEmpty()) return;
    QFile file(path);
    if(file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "FileName,GroundTruth,BaseResult,MethodResult,Reason\n";
        for(const auto &item : currentBatchData) {
            QString cleanGt = cleanPlateString(item.groundTruth);
            bool baseOk = (cleanPlateString(item.plateBase) == cleanGt);
            bool methOk = (cleanPlateString(item.plateMethod) == cleanGt);
            if(!baseOk || !methOk) {
                out << item.fileName << "," << item.groundTruth << "," << item.plateBase << "," << item.plateMethod << "," << item.errorReason << "\n";
            }
        }
        file.close();
        QMessageBox::information(this, "Success", "Exported!");
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

// 【超级调试功能】模拟 VisionEngine 的处理流水线，显示 6 个关键步骤图
void MainWindow::onBtnDebugCV() {
    if(currentBatchIndex < 0 || currentBatchIndex >= currentBatchData.size()) {
        QMessageBox::information(this, "Tip", "请先选择一张图片");
        return;
    }
    cv::Mat raw = cv::imread(currentBatchData[currentBatchIndex].filePath.toStdString());
    if(raw.empty()) return;

    QDialog *dlg = new QDialog(this);
    dlg->setWindowTitle("OpenCV Pipeline Full Inspection");
    dlg->resize(1400, 800);
    QGridLayout *grid = new QGridLayout(dlg);

    // 1. Resize (模拟 VisionEngine 的缩放)
    cv::Mat resized;
    float scale = 1.0;
    if(raw.cols > 1600) {
        scale = 1600.0 / raw.cols;
        cv::resize(raw, resized, cv::Size(), scale, scale);
    } else {
        resized = raw.clone();
    }

    // 2. 颜色流
    cv::Mat hsvMask = imgProcessor->getColorMask(resized);

    // 3. 纹理流
    cv::Mat gray = imgProcessor->toGray(resized);
    cv::Mat blur = imgProcessor->applyGaussianBlur(gray);
    cv::Mat sobel = imgProcessor->applySobel(blur);
    cv::Mat binary = imgProcessor->toBinary(sobel);
    cv::Mat morph = imgProcessor->applyMorphology(binary);

    // Helper to add label
    auto addView = [&](QString title, cv::Mat m, int r, int c) {
        QLabel *lbl = new QLabel;
        lbl->setPixmap(QPixmap::fromImage(visionEngine->matToQImage(m)).scaled(450, 300, Qt::KeepAspectRatio));
        lbl->setStyleSheet("border: 1px solid black;");
        QVBoxLayout *box = new QVBoxLayout;
        QLabel *t = new QLabel(title);
        t->setAlignment(Qt::AlignCenter);
        t->setStyleSheet("font-weight: bold; font-size: 14px;");
        box->addWidget(t);
        box->addWidget(lbl);
        grid->addLayout(box, r, c);
    };

    addView("1. Original (Resized)", resized, 0, 0);
    addView("2. Grayscale", gray, 0, 1);
    addView("3. Color Mask (Blue+Green)", hsvMask, 0, 2);

    addView("4. Sobel (Texture)", sobel, 1, 0);
    addView("5. Binary (Otsu)", binary, 1, 1);
    addView("6. Morphology (Close)", morph, 1, 2);

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
