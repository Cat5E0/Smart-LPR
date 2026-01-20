#include "visionengine.h"
#include <QDebug>
#include <vector>
#include <algorithm>

using namespace cv;
using namespace std;

VisionEngine::VisionEngine(QObject *parent) : QObject(parent) {}

// ==========================================
// 1. 原子函数实现
// ==========================================

void VisionEngine::preprocessResize(const cv::Mat &src, cv::Mat &dst, float &scale) {
    if(src.cols > RESIZE_MAX_WIDTH) {
        scale = (float)RESIZE_MAX_WIDTH / src.cols;
        resize(src, dst, Size(), scale, scale);
    } else {
        dst = src.clone();
        scale = 1.0;
    }
}

void VisionEngine::extractColorMask(const cv::Mat &src, cv::Mat &mask) {
    Mat hsv;
    cvtColor(src, hsv, COLOR_BGR2HSV);

    // 使用头文件中的宽松阈值 (S_MIN=20)
    Scalar lower(HSV_H_MIN, HSV_S_MIN, HSV_V_MIN);
    Scalar upper(HSV_H_MAX, HSV_S_MAX, HSV_V_MAX);

    inRange(hsv, lower, upper, mask);

    // 形态学：容忍倾斜
    Mat kernel = getStructuringElement(MORPH_RECT, Size(12, 4));
    morphologyEx(mask, mask, MORPH_CLOSE, kernel);
}

void VisionEngine::extractTextureFeatures(const cv::Mat &src, cv::Mat &gray, cv::Mat &sobel, cv::Mat &binary, cv::Mat &morph) {
    Mat blur;
    if(src.channels() == 3) cvtColor(src, gray, COLOR_BGR2GRAY);
    else gray = src.clone();

    // CLAHE 光照均衡化 (解决 DB/Weather 问题)
    Ptr<CLAHE> clahe = createCLAHE(2.0, Size(8, 8));
    clahe->apply(gray, gray);

    GaussianBlur(gray, blur, Size(5, 5), 0);
    Sobel(blur, sobel, CV_8U, 1, 0, 3);

    threshold(sobel, binary, 0, 255, THRESH_BINARY | THRESH_OTSU);

    // 形态学：加宽连接字符
    Mat kernel = getStructuringElement(MORPH_RECT, Size(12, 5));
    morphologyEx(binary, morph, MORPH_CLOSE, kernel);
}

// ==========================================
// 2. 业务逻辑
// ==========================================

void VisionEngine::processFrame(cv::Mat frame)
{
    if (frame.empty()) return;

    QElapsedTimer timer;
    timer.start();

    Mat resizeMat;
    float scale = 1.0;
    preprocessResize(frame, resizeMat, scale);

    cv::Rect plateRect = detectPlateRobust(resizeMat);

    // 坐标还原
    if(scale != 1.0 && plateRect.width > 0) {
        plateRect.x /= scale;
        plateRect.y /= scale;
        plateRect.width /= scale;
        plateRect.height /= scale;
    }

    // 边界安全检查
    if(plateRect.x < 0) plateRect.x = 0;
    if(plateRect.y < 0) plateRect.y = 0;
    if(plateRect.x + plateRect.width > frame.cols) plateRect.width = frame.cols - plateRect.x;
    if(plateRect.y + plateRect.height > frame.rows) plateRect.height = frame.rows - plateRect.y;

    ProcessResult result;
    result.found = (plateRect.width > 0 && plateRect.height > 0);
    result.roi = plateRect;

    if (result.found) {
        int padW = plateRect.width * 0.15;
        int padH = plateRect.height * 0.15;
        cv::Rect padded = plateRect;
        padded.x = max(0, plateRect.x - padW);
        padded.y = max(0, plateRect.y - padH);
        padded.width = min(frame.cols - padded.x, plateRect.width + 2*padW);
        padded.height = min(frame.rows - padded.y, plateRect.height + 2*padH);

        result.roi = padded;
        Mat crop = frame(padded);

        if(crop.cols < 120 || crop.rows < 40) {
            Mat upscaled;
            resize(crop, upscaled, Size(), 2.0, 2.0, INTER_CUBIC);
            result.plateImage = matToQImage(upscaled);
        } else {
            result.plateImage = matToQImage(crop);
        }
    }

    result.displayImage = matToQImage(frame);
    result.costMs = timer.elapsed();

    emit processingFinished(result);
}

// === V5.0 核心：双流定位 ===
cv::Rect VisionEngine::detectPlateRobust(const cv::Mat &src)
{
    vector<Rect> candidates;

    // 1. 颜色流
    vector<Rect> colorRects = findByColor(src);
    candidates.insert(candidates.end(), colorRects.begin(), colorRects.end());

    // 2. 纹理流
    vector<Rect> textureRects = findByTexture(src);
    candidates.insert(candidates.end(), textureRects.begin(), textureRects.end());

    Rect bestRect(0,0,0,0);
    double maxScore = -1.0;

    for(const auto &r : candidates) {
        // [修复] 尺寸初筛：过滤掉不可能的极小框
        if(r.width < 60 || r.height < 20) continue;
        if(r.width > src.cols / 2 || r.height > src.rows / 2) continue;

        // 长宽比初筛
        float ratio = (float)r.width / r.height;
        if(ratio < 1.2 || ratio > 7.0) continue;

        double s = scorePlate(src, r);
        if(s > maxScore) {
            maxScore = s;
            bestRect = r;
        }
    }

    return bestRect;
}

vector<Rect> VisionEngine::findByColor(const cv::Mat &src) {
    Mat mask;
    extractColorMask(src, mask);
    vector<vector<Point>> contours;
    findContours(mask, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    vector<Rect> rects;
    for(const auto &c : contours) {
        RotatedRect rr = minAreaRect(c);
        if(verifySizes(rr)) rects.push_back(rr.boundingRect());
    }
    return rects;
}

vector<Rect> VisionEngine::findByTexture(const cv::Mat &src) {
    Mat gray, sobel, binary, morph;
    extractTextureFeatures(src, gray, sobel, binary, morph);
    vector<vector<Point>> contours;
    findContours(morph, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);
    vector<Rect> rects;
    for(const auto &c : contours) {
        RotatedRect rr = minAreaRect(c);
        if(verifySizes(rr)) rects.push_back(rr.boundingRect());
    }
    return rects;
}

PipelineDebugData VisionEngine::getDebugPipelineData(cv::Mat frame) {
    PipelineDebugData data;
    if (frame.empty()) return data;
    float scale;
    preprocessResize(frame, data.resized, scale);
    extractColorMask(data.resized, data.hsvMask);
    extractTextureFeatures(data.resized, data.gray, data.sobel, data.binary, data.morph);
    return data;
}

// ==========================================
// 4. 辅助函数
// ==========================================

bool VisionEngine::verifySizes(const cv::RotatedRect &candidate) {
    float w = candidate.size.width;
    float h = candidate.size.height;
    float longSide = max(w, h);
    float shortSide = min(w, h);

    if(shortSide == 0) return false;
    float aspect = longSide / shortSide;

    if(aspect < 1.2 || aspect > 7.0) return false;

    // [核心修复] 提升最小尺寸门槛
    // 之前是 <30，太小了，导致截取到噪点发给阿里云报错 InvalidImage
    // 现在要求至少 64x24
    if(longSide < 64 || shortSide < 24) return false;
    if(longSide > 1000) return false;

    return true;
}

double VisionEngine::scorePlate(const cv::Mat &src, const cv::Rect &rect) {
    // 1. 安全检查
    Rect safeRect = rect & Rect(0,0, src.cols, src.rows);
    if(safeRect.area() <= 0) return -1.0;

    // ---------------------------------------------------------
    // 【核心修复】颜色评分逻辑优化
    // ---------------------------------------------------------
    Mat roi = src(safeRect);
    Mat hsvRoi;
    cvtColor(roi, hsvRoi, COLOR_BGR2HSV);

    Mat blueMask;
    inRange(hsvRoi,
            Scalar(HSV_H_MIN, HSV_S_MIN, HSV_V_MIN),
            Scalar(HSV_H_MAX, HSV_S_MAX, HSV_V_MAX),
            blueMask);

    double blueRatio = (double)countNonZero(blueMask) / safeRect.area();

    // [关键修改] 不要因为蓝色少 (<0.05) 就直接 return -1.0
    // 雨天、夜晚、反光会导致蓝色丢失，只要纹理好，也要保留
    double colorScore = 0.0;
    if (blueRatio < 0.05) {
        colorScore = 0.1; // 低分保底
    } else {
        colorScore = 0.5 + blueRatio; // 高分奖励
    }

    // ---------------------------------------------------------
    // 2. 纹理穿线分析
    // ---------------------------------------------------------
    Mat grayRoi, sobelRoi, binRoi;
    if(roi.channels() == 3) cvtColor(roi, grayRoi, COLOR_BGR2GRAY);
    else grayRoi = roi;

    Sobel(grayRoi, sobelRoi, CV_8U, 1, 0, 3);
    threshold(sobelRoi, binRoi, 0, 255, THRESH_BINARY | THRESH_OTSU);

    int validLines = 0;
    int totalJumps = 0;
    int rowsToScan[] = {binRoi.rows / 4, binRoi.rows / 2, binRoi.rows * 3 / 4};

    for(int r : rowsToScan) {
        if(r < 0 || r >= binRoi.rows) continue;
        int jumps = 0;
        const uchar* ptr = binRoi.ptr<uchar>(r);
        for(int c = 0; c < binRoi.cols - 1; c++) {
            if(ptr[c] != ptr[c+1]) jumps++;
        }
        totalJumps += jumps;
        validLines++;
    }
    double avgJumps = (validLines > 0) ? (double)totalJumps / validLines : 0;

    // ---------------------------------------------------------
    // 3. 综合打分
    // ---------------------------------------------------------

    // A. 纹理分
    double textureScore = 0.0;
    if (avgJumps < 5) textureScore = 0.01;
    else if (avgJumps > 45) textureScore = 0.01;
    else {
        textureScore = 1.0 - abs(avgJumps - 18.0) / 25.0;
        if(textureScore < 0) textureScore = 0.01;
    }

    // B. 形状分
    double ratio = (double)safeRect.width / safeRect.height;
    double ratioScore = 1.0 - abs(ratio - 3.5) / 5.0;
    if(ratioScore < 0) ratioScore = 0;

    // D. 尺寸惩罚
    double sizePenalty = 1.0;
    if(safeRect.width > src.cols * 0.55) sizePenalty = 0.2;

    // 最终得分
    double effectiveArea = min((double)safeRect.area(), 40000.0);

    // 只有当纹理和颜色都极差时，才认为是垃圾
    if(textureScore < 0.2 && colorScore < 0.2) return -1.0;

    return effectiveArea * ratioScore * textureScore * colorScore * sizePenalty;
}

QImage VisionEngine::matToQImage(const cv::Mat &mat) {
    if(mat.type() == CV_8UC1) {
        QImage image(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_Grayscale8);
        return image.copy();
    } else if(mat.type() == CV_8UC3) {
        QImage image(mat.data, mat.cols, mat.rows, mat.step, QImage::Format_RGB888);
        return image.rgbSwapped();
    }
    return QImage();
}
