#include "coloranalyzer.h"
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QTextStream>
#include <QtConcurrent>
#include <algorithm>
#include <numeric>
#include <QStandardPaths>
#include <QDateTime>

using namespace cv;
using namespace std;

ColorAnalyzer::ColorAnalyzer(QObject *parent) : QObject(parent) {}

void ColorAnalyzer::startAnalysis(const QString &dirPath) {
    QtConcurrent::run(this, &ColorAnalyzer::run, dirPath);
}

cv::Rect ColorAnalyzer::parseGtBox(const QString &fileName) {
    QString base = QFileInfo(fileName).baseName();
    QStringList parts = base.split("-");
    if (parts.size() < 3) return cv::Rect(0,0,0,0);
    QString bboxStr = parts[2];
    QStringList points = bboxStr.split("_");
    if (points.size() < 2) return cv::Rect(0,0,0,0);
    QStringList tl = points[0].split("&");
    QStringList br = points[1].split("&");
    if (tl.size() < 2 || br.size() < 2) return cv::Rect(0,0,0,0);
    int x1 = tl[0].toInt(); int y1 = tl[1].toInt();
    int x2 = br[0].toInt(); int y2 = br[1].toInt();
    return cv::Rect(x1, y1, x2 - x1, y2 - y1);
}

QString ColorAnalyzer::extractCategory(const QString &path) {
    QString lower = path.toLower();
    // 根据您的描述，这里只保留最核心的几个关键词匹配
    if (lower.contains("db")) return "DB";
    if (lower.contains("blur")) return "Blur";
    if (lower.contains("rotate")) return "Rotate";
    if (lower.contains("challenge")) return "Challenge";
    // 如果都没有，或者是 base，就归为 Normal
    return "Normal";
}

void ColorAnalyzer::run(QString dirPath) {
    emit logMessage("开始扫描数据集...");

    QDir sourceDir(dirPath);
    QString datasetName = sourceDir.dirName();
    QString timeStr = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString docsPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    QString baseReportDir = docsPath + "/SmartLPR_Analysis_Reports";
    QString folderName = QString("%1_Analysis_%2").arg(datasetName).arg(timeStr);
    QString resultDir = baseReportDir + "/" + folderName;

    QDir dir(resultDir);
    if (!dir.exists()) dir.mkpath(".");

    emit logMessage("结果路径: " + resultDir);

    QStringList filters; filters << "*.jpg" << "*.jpeg" << "*.png";
    QDirIterator it(dirPath, filters, QDir::Files, QDirIterator::Subdirectories);
    QList<QString> files;
    while(it.hasNext()) {
        QString p = it.next();
        if (!QFileInfo(p).fileName().contains("-")) continue;
        files.append(p);
    }

    if(files.isEmpty()) { emit logMessage("未找到有效图片"); return; }

    QString csvPath = resultDir + "/dataset_color_details.csv";
    QFile csv(csvPath);
    if(!csv.open(QIODevice::WriteOnly | QIODevice::Text)) {
        emit logMessage("无法写入 CSV");
        return;
    }
    QTextStream out(&csv);
    out << "FileName,Category,Mean_H,Mean_S,Mean_V\n";

    QMap<QString, ChannelStats> allStats;
    int total = files.size();
    int count = 0;

    for(const QString &filePath : files) {
        count++;
        if(count % 50 == 0) emit progressUpdated(count, total);

        Mat img = imread(filePath.toStdString());
        if(img.empty()) continue;

        Rect gtRect = parseGtBox(filePath);
        if(gtRect.width <= 0 || gtRect.height <= 0) continue;
        gtRect = gtRect & Rect(0, 0, img.cols, img.rows);
        if(gtRect.area() < 50) continue;

        Mat roi = img(gtRect);
        Mat hsv;
        cvtColor(roi, hsv, COLOR_BGR2HSV);

        Scalar m = mean(hsv);
        float h = m[0]; float s = m[1]; float v = m[2];
        QString cat = extractCategory(filePath);

        out << QFileInfo(filePath).fileName() << "," << cat << ","
            << QString::number(h, 'f', 1) << ","
            << QString::number(s, 'f', 1) << ","
            << QString::number(v, 'f', 1) << "\n";

        if(!allStats.contains(cat)) allStats[cat] = ChannelStats();
        allStats[cat].h_vals.append(h);
        allStats[cat].s_vals.append(s);
        allStats[cat].v_vals.append(v);
    }
    csv.close();

    emit logMessage("扫描完成，正在生成 6 张分析图表 (2x2 布局)...");
    generateReport(resultDir, allStats);
    emit analysisFinished("分析完成！\n总共生成 6 张分析图，保存在:\n" + resultDir);
}

// === 核心：生成 6 张图 ===
void ColorAnalyzer::generateReport(const QString &outputDir, const QMap<QString, ChannelStats> &data) {
    // 1. 合并所有数据计算全局统计
    ChannelStats globalStats;
    for(auto it = data.begin(); it != data.end(); ++it) {
        globalStats.h_vals.append(it.value().h_vals);
        globalStats.s_vals.append(it.value().s_vals);
        globalStats.v_vals.append(it.value().v_vals);
    }

    auto getRange = [](QVector<float> vals, float &p05, float &p95) {
        if(vals.isEmpty()) { p05=0; p95=0; return; }
        std::sort(vals.begin(), vals.end());
        p05 = vals[vals.size() * 0.05];
        p95 = vals[vals.size() * 0.95];
    };

    float gMinH, gMaxH, gMinS, gMaxS, gMinV, gMaxV;
    getRange(globalStats.h_vals, gMinH, gMaxH);
    getRange(globalStats.s_vals, gMinS, gMaxS);
    getRange(globalStats.v_vals, gMinV, gMaxV);

    // 生成文本报告
    QString reportPath = outputDir + "/summary_report.txt";
    QFile file(reportPath);
    if(file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        out << "=== SmartLPR Color Analysis Report ===\n";
        out << "Generated on: " << QDateTime::currentDateTime().toString() << "\n\n";

        // 遍历所有类别输出数据
        QMapIterator<QString, ChannelStats> i(data);
        while(i.hasNext()) {
            i.next();
            float p05, p95;
            QVector<float> h = i.value().h_vals;
            getRange(h, p05, p95);
            out << "Category [" << i.key() << "] Hue Range (5-95%): [" << (int)p05 << ", " << (int)p95 << "]\n";
        }

        out << "\n[GLOBAL RECOMMENDATION] (All Data)\n";
        out << "  Hue Range: [" << (int)gMinH << ", " << (int)gMaxH << "]\n";
        out << "  Sat Range: [" << (int)gMinS << ", " << (int)gMaxS << "]\n";
        file.close();
    }

    // === 生成 4 张 Grid 对比图 (2x2 布局) ===
    imwrite((outputDir + "/Grid_Hue.png").toStdString(), drawGridImage(data, CHANNEL_H, "Grid: Hue Distribution"));
    imwrite((outputDir + "/Grid_Sat.png").toStdString(), drawGridImage(data, CHANNEL_S, "Grid: Saturation Distribution"));
    imwrite((outputDir + "/Grid_Val.png").toStdString(), drawGridImage(data, CHANNEL_V, "Grid: Brightness Distribution"));
    imwrite((outputDir + "/Grid_Scatter.png").toStdString(), drawMultiScatterGrid(data, "Grid: H-S Scatter"));

    // === 生成 Global Coverage 图 ===
    Mat imgCov = drawSingleScatter(globalStats.h_vals, globalStats.s_vals,
                                   "Global Coverage Analysis (All Data)",
                                   gMinH, gMaxH, gMinS, gMaxS);
    putText(imgCov, format("Coverage: H[%d-%d], S[%d-%d]", (int)gMinH, (int)gMaxH, (int)gMinS, (int)gMaxS),
            Point(50, 40), FONT_HERSHEY_SIMPLEX, 0.8, Scalar(0, 255, 255), 2);
    imwrite((outputDir + "/Global_Coverage_Analysis.png").toStdString(), imgCov);

    // === 生成 Global 4in1 ===
    Mat hHist = drawSingleHistogram(globalStats.h_vals, 180, "Global Hue", Scalar(0,0,0));
    Mat sHist = drawSingleHistogram(globalStats.s_vals, 255, "Global Sat", Scalar(255,150,50));
    Mat vHist = drawSingleHistogram(globalStats.v_vals, 255, "Global Val", Scalar(100,200,100));
    Mat scPlot = drawSingleScatter(globalStats.h_vals, globalStats.s_vals, "Global Scatter", gMinH, gMaxH, gMinS, gMaxS);

    Mat row1, row2, full4;
    hconcat(hHist, sHist, row1);
    hconcat(vHist, scPlot, row2);
    vconcat(row1, row2, full4);
    imwrite((outputDir + "/Global_Summary_4in1.png").toStdString(), full4);
}

// === 绘制网格图 (固定 2行2列) ===
Mat ColorAnalyzer::drawGridImage(const QMap<QString, ChannelStats> &data, ChannelType type, const QString &title) {
    int w = 600, h = 400;
    int cols = 2; // [修改] 固定 2 列
    int rows = 2; // [修改] 固定 2 行

    Mat canvas(h * rows + 80, w * cols, CV_8UC3, Scalar(240, 240, 240));
    putText(canvas, title.toStdString(), Point(50, 55), FONT_HERSHEY_SIMPLEX, 1.5, Scalar(50,50,50), 2, LINE_AA);

    // 智能确定绘制顺序：Normal 永远第一，其他存在的类别往后排
    QStringList order;
    if(data.contains("Normal")) order << "Normal";
    for(auto k : data.keys()) {
        if(k != "Normal" && order.size() < 4) order << k; // 凑齐 4 个
    }

    for(int i=0; i<4; ++i) { // 强制只画 4 个格子
        int r = i / cols;
        int c = i % cols;
        Rect roi(c * w, r * h + 80, w, h);
        Mat cellROI = canvas(roi);

        if(i < order.size()) {
            QString cat = order[i];
            const ChannelStats &s = data[cat];
            QVector<float> vals;
            int maxRange = 255;
            Scalar color(100,100,100);

            if(type == CHANNEL_H) { vals = s.h_vals; maxRange = 180; color = Scalar(0,0,0); }
            else if(type == CHANNEL_S) { vals = s.s_vals; color = Scalar(255, 150, 50); }
            else { vals = s.v_vals; color = Scalar(100, 200, 100); }

            QString subTitle = QString("[%1] N=%2").arg(cat).arg(vals.size());
            Mat cell = drawSingleHistogram(vals, maxRange, subTitle, color);
            resize(cell, cellROI, Size(w, h));
        } else {
            // 如果不足4个类别，画个白板占位
            rectangle(cellROI, Point(0,0), Point(w,h), Scalar(230,230,230), -1);
        }
    }
    return canvas;
}

// === 绘制散点网格图 (固定 2行2列) ===
Mat ColorAnalyzer::drawMultiScatterGrid(const QMap<QString, ChannelStats> &data, const QString &title) {
    int w = 600, h = 400;
    int cols = 2, rows = 2; // [修改] 固定 2x2
    Mat canvas(h * rows + 80, w * cols, CV_8UC3, Scalar(50, 50, 50));

    putText(canvas, title.toStdString(), Point(50, 55), FONT_HERSHEY_SIMPLEX, 1.5, Scalar(200,200,200), 2, LINE_AA);

    QStringList order;
    if(data.contains("Normal")) order << "Normal";
    for(auto k : data.keys()) {
        if(k != "Normal" && order.size() < 4) order << k;
    }

    for(int i=0; i<4; ++i) {
        int r = i / cols;
        int c = i % cols;
        Rect roi(c * w, r * h + 80, w, h);
        Mat cellROI = canvas(roi);

        if(i < order.size()) {
            QString cat = order[i];
            const ChannelStats &s = data[cat];
            QVector<float> hvec = s.h_vals; std::sort(hvec.begin(), hvec.end());
            QVector<float> svec = s.s_vals; std::sort(svec.begin(), svec.end());
            float pH05=0, pH95=180, pS05=0, pS95=255;
            if(!hvec.isEmpty()) { pH05=hvec[hvec.size()*0.05]; pH95=hvec[hvec.size()*0.95]; }
            if(!svec.isEmpty()) { pS05=svec[svec.size()*0.05]; pS95=svec[svec.size()*0.95]; }

            Mat cell = drawSingleScatter(s.h_vals, s.s_vals, cat, pH05, pH95, pS05, pS95);
            resize(cell, cellROI, Size(w, h));
        } else {
            rectangle(cellROI, Point(0,0), Point(w,h), Scalar(40,40,40), -1);
        }
    }
    return canvas;
}

// === 单个直方图绘制 ===
Mat ColorAnalyzer::drawSingleHistogram(const QVector<float> &values, int maxRange, const QString &subTitle, const Scalar &barColor) {
    int w = 600, h = 400;
    int marginX = 50, marginY = 40;
    int plotW = w - 2 * marginX;
    int plotH = h - 2 * marginY;

    Mat canvas(h, w, CV_8UC3, Scalar(250, 250, 250));

    if(values.isEmpty()) return canvas;

    rectangle(canvas, Point(marginX, marginY), Point(w - marginX, h - marginY), Scalar(255, 255, 255), -1);
    rectangle(canvas, Point(marginX, marginY), Point(w - marginX, h - marginY), Scalar(200, 200, 200), 1);

    int binCount = maxRange;
    QVector<int> bins(binCount + 1, 0);
    int maxFreq = 0;
    for(float v : values) {
        int idx = (int)v; if(idx >= 0 && idx < bins.size()) { bins[idx]++; if(bins[idx]>maxFreq) maxFreq=bins[idx]; }
    }

    float binW = (float)plotW / binCount;
    for(int i=0; i<binCount; i++) {
        if(bins[i] == 0) continue;
        int barH = maxFreq > 0 ? cvRound(bins[i] * (float)plotH / maxFreq) : 0;
        Point p1(marginX + i*binW, h - marginY);
        Point p2(marginX + (i+1)*binW, h - marginY - barH);

        Scalar color = barColor;
        if(maxRange == 180) {
            Mat colorPix(1, 1, CV_8UC3, Scalar(i, 255, 255));
            cvtColor(colorPix, colorPix, COLOR_HSV2BGR);
            Vec3b bgr = colorPix.at<Vec3b>(0,0);
            color = Scalar(bgr[0], bgr[1], bgr[2]);
        }
        rectangle(canvas, p1, p2, color, -1);
    }

    int step = (maxRange == 180) ? 45 : 64;
    for(int v=0; v<=maxRange; v+=step) {
        int x = marginX + (int)((float)v/maxRange * plotW);
        line(canvas, Point(x, h-marginY), Point(x, h-marginY+5), Scalar(150,150,150), 1);
        putText(canvas, to_string(v), Point(x-10, h-marginY+20), FONT_HERSHEY_SIMPLEX, 0.4, Scalar(100,100,100), 1);
    }
    putText(canvas, subTitle.toStdString(), Point(marginX, marginY - 10), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(0,0,0), 1, LINE_AA);
    return canvas;
}

// === 单个散点图绘制 ===
Mat ColorAnalyzer::drawSingleScatter(const QVector<float> &h_vals, const QVector<float> &s_vals, const QString &subTitle,
                                     float recMinH, float recMaxH, float recMinS, float recMaxS) {
    int w = 600, h = 400;
    int marginX = 50, marginY = 40;
    int plotW = w - 2 * marginX;
    int plotH = h - 2 * marginY;

    Mat canvas(h, w, CV_8UC3, Scalar(40, 40, 40));
    rectangle(canvas, Point(marginX, marginY), Point(w - marginX, h - marginY), Scalar(0, 0, 0), -1);
    rectangle(canvas, Point(marginX, marginY), Point(w - marginX, h - marginY), Scalar(100, 100, 100), 1);

    if(h_vals.isEmpty()) return canvas;

    int rx1 = marginX + (int)(recMinH / 180.0 * plotW);
    int rx2 = marginX + (int)(recMaxH / 180.0 * plotW);
    int ry1 = h - marginY - (int)(recMinS / 255.0 * plotH);
    int ry2 = h - marginY - (int)(recMaxS / 255.0 * plotH);

    Mat overlay = canvas.clone();
    rectangle(overlay, Point(rx1, ry2), Point(rx2, ry1), Scalar(0, 165, 255), -1);
    addWeighted(overlay, 0.3, canvas, 0.7, 0, canvas);
    rectangle(canvas, Point(rx1, ry2), Point(rx2, ry1), Scalar(0, 165, 255), 1);

    for(int i=0; i<h_vals.size(); i++) {
        float hv = h_vals[i]; float sv = s_vals[i];
        int cx = marginX + (int)(hv / 180.0 * plotW);
        int cy = h - marginY - (int)(sv / 255.0 * plotH);
        Mat ptColor(1, 1, CV_8UC3, Scalar(hv, sv, 255));
        cvtColor(ptColor, ptColor, COLOR_HSV2BGR);
        Vec3b c = ptColor.at<Vec3b>(0,0);
        circle(canvas, Point(cx, cy), 1, Scalar(c[0], c[1], c[2]), -1);
    }

    for(int v=0; v<=180; v+=45) {
        int x = marginX + (int)((float)v/180.0*plotW);
        line(canvas, Point(x, h-marginY), Point(x, h-marginY+5), Scalar(150,150,150), 1);
        putText(canvas, to_string(v), Point(x-10, h-marginY+20), FONT_HERSHEY_SIMPLEX, 0.4, Scalar(180,180,180), 1);
    }

    putText(canvas, subTitle.toStdString(), Point(marginX, marginY - 10), FONT_HERSHEY_SIMPLEX, 0.6, Scalar(255,255,255), 1, LINE_AA);
    return canvas;
}
