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

    // CLAHE 光照均衡化 (保留这个神器，解决 DB 问题)
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
        // [修复] 尺寸初筛：必须通过基础几何检查
        // 增加宽度上限：如果是 1600宽的图，车牌不太可能超过 800宽(1/2)
        if(r.width < 30 || r.height < 10) continue;
        if(r.width > src.cols / 2 || r.height > src.rows / 2) continue;

        // 长宽比初筛 (宽泛一点，交给 scorePlate 做精细检查)
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

    // 宽高比：1.2 ~ 7.0 (保持之前的放宽策略，容忍旋转)
    if(aspect < 1.2 || aspect > 7.0) return false;

    // [新增] 绝对尺寸上限：防止把整个屏幕当车牌
    // 假设 Resize 后宽度约 1600，如果长边 > 1000 肯定是假的
    if(longSide < 30 || shortSide < 8) return false;
    if(longSide > 1000) return false;

    return true;
}

double VisionEngine::scorePlate(const cv::Mat &src, const cv::Rect &rect) {
    // 1. 安全检查 & 边界处理
    // 稍微把框往里缩一点点 (Padding)，避免边缘背景干扰颜色判断
    Rect safeRect = rect & Rect(0,0, src.cols, src.rows);
    if(safeRect.area() <= 0) return -1.0;

    // ---------------------------------------------------------
    // 【新增核心 1】颜色验证 (Color Confirmation)
    // 专门解决：进气格栅、散热孔、黑色保险杠被误检的问题
    // ---------------------------------------------------------
    Mat roi = src(safeRect);
    Mat hsvRoi;
    cvtColor(roi, hsvRoi, COLOR_BGR2HSV);

    Mat blueMask;
    // 使用头文件里的宽松阈值 (S_MIN=20)
    inRange(hsvRoi,
            Scalar(HSV_H_MIN, HSV_S_MIN, HSV_V_MIN),
            Scalar(HSV_H_MAX, HSV_S_MAX, HSV_V_MAX),
            blueMask);

    // 计算蓝色像素占比
    double blueRatio = (double)countNonZero(blueMask) / safeRect.area();

    // [必杀技] 如果一个框里几乎没有蓝色 (< 5%)，那它绝对是进气格栅或烂树叶
    // 直接给极低分，不用往下算了
    if (blueRatio < 0.05) return -1.0;

    // ---------------------------------------------------------
    // 2. 纹理穿线分析 (Scanline) - 优化版
    // ---------------------------------------------------------
    Mat grayRoi, sobelRoi, binRoi;
    if(roi.channels() == 3) cvtColor(roi, grayRoi, COLOR_BGR2GRAY);
    else grayRoi = roi;

    Sobel(grayRoi, sobelRoi, CV_8U, 1, 0, 3);
    threshold(sobelRoi, binRoi, 0, 255, THRESH_BINARY | THRESH_OTSU);

    int validLines = 0;
    int totalJumps = 0;
    // 扫描 3 行：25%, 50%, 75% 高度处
    int rowsToScan[] = {binRoi.rows / 4, binRoi.rows / 2, binRoi.rows * 3 / 4};

    for(int r : rowsToScan) {
        if(r < 0 || r >= binRoi.rows) continue;
        int jumps = 0;
        const uchar* ptr = binRoi.ptr<uchar>(r);
        for(int c = 0; c < binRoi.cols - 1; c++) {
            // 检测跳变
            if(ptr[c] != ptr[c+1]) jumps++;
        }
        totalJumps += jumps;
        validLines++;
    }
    double avgJumps = (validLines > 0) ? (double)totalJumps / validLines : 0;

    // ---------------------------------------------------------
    // 3. 综合打分 (Score Calculation)
    // ---------------------------------------------------------

    // A. 纹理分：针对车牌字符结构 (10-28次跳变)
    double textureScore = 0.0;
    if (avgJumps < 5) textureScore = 0.01;      // 太空 (路标/栏杆)
    else if (avgJumps > 40) textureScore = 0.01; // 太乱 (树丛/地面)
    else {
        // 距离 18 (典型车牌跳变数) 越近分越高
        textureScore = 1.0 - abs(avgJumps - 18.0) / 25.0;
        if(textureScore < 0) textureScore = 0.01;
    }

    // B. 形状分
    double ratio = (double)safeRect.width / safeRect.height;
    double ratioScore = 1.0 - abs(ratio - 3.5) / 5.0;
    if(ratioScore < 0) ratioScore = 0;

    // C. 颜色分 (新增)：蓝色越多越可信
    // 我们可以让 colorScore 从 0.5 到 1.5 浮动，奖励颜色正的
    double colorScore = 0.5 + blueRatio;

    // D. 尺寸惩罚：依然压制巨大框
    double sizePenalty = 1.0;
    if(safeRect.width > src.cols * 0.55) sizePenalty = 0.2; // 超过55%宽度，严重扣分

    // 最终得分 = 面积(封顶) * 形状 * 纹理 * 颜色 * 惩罚
    double effectiveArea = min((double)safeRect.area(), 40000.0);

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
