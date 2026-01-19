#include "visionengine.h"
#include <QDebug>
#include <vector>
#include <algorithm>

using namespace cv;
using namespace std;

VisionEngine::VisionEngine(QObject *parent) : QObject(parent) {}

void VisionEngine::processFrame(cv::Mat frame)
{
    if (frame.empty()) return;

    QElapsedTimer timer;
    timer.start();

    // 1. 【优化 1】调整缩放阈值，从 800 改为 1600
    // 原来的 800 像素太小，导致远处的车牌宽度不足 40px，被 verifySizes 过滤掉
    Mat resizeMat;
    float scale = 1.0;
    if(frame.cols > 1600) {
        scale = 1600.0 / frame.cols;
        resize(frame, resizeMat, Size(), scale, scale);
    } else {
        resizeMat = frame.clone();
    }

    // 2. V5.0 鲁棒定位
    cv::Rect plateRect = detectPlateRobust(resizeMat);

    // 3. 还原坐标
    if(scale != 1.0 && plateRect.width > 0) {
        plateRect.x /= scale;
        plateRect.y /= scale;
        plateRect.width /= scale;
        plateRect.height /= scale;
    }

    // 边界检查
    if(plateRect.x < 0) plateRect.x = 0;
    if(plateRect.y < 0) plateRect.y = 0;
    if(plateRect.x + plateRect.width > frame.cols) plateRect.width = frame.cols - plateRect.x;
    if(plateRect.y + plateRect.height > frame.rows) plateRect.height = frame.rows - plateRect.y;

    ProcessResult result;
    result.found = (plateRect.width > 0 && plateRect.height > 0);
    result.roi = plateRect;

    // 4. [工业级优化] 扩边 + 自动放大
    if (result.found) {
        // 【优化 2】加大 Padding，防止切断边缘字符
        // 之前是 0.1 / 0.15，现在稍微调大一点点宽度余量
        int padW = plateRect.width * 0.15;
        int padH = plateRect.height * 0.15;

        cv::Rect padded = plateRect;
        padded.x = max(0, plateRect.x - padW);
        padded.y = max(0, plateRect.y - padH);
        padded.width = min(frame.cols - padded.x, plateRect.width + 2*padW);
        padded.height = min(frame.rows - padded.y, plateRect.height + 2*padH);

        result.roi = padded;
        Mat crop = frame(padded);

        // [关键] 自动放大：解决 InvalidImage.Content 报错
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

    // 流派1：颜色检测 (针对蓝牌/绿牌)
    vector<Rect> colorRects = findByColor(src);
    candidates.insert(candidates.end(), colorRects.begin(), colorRects.end());

    // 流派2：纹理检测 (Sobel)
    vector<Rect> textureRects = findByTexture(src);
    candidates.insert(candidates.end(), textureRects.begin(), textureRects.end());

    // 评分筛选
    Rect bestRect(0,0,0,0);
    double maxScore = -1.0;

    for(const auto &r : candidates) {
        if(r.width < 30 || r.height < 10) continue;
        float ratio = (float)r.width / r.height;
        if(ratio < 2.0 || ratio > 6.0) continue;

        double s = scorePlate(src, r);
        if(s > maxScore) {
            maxScore = s;
            bestRect = r;
        }
    }

    return bestRect;
}

// 颜色流
vector<Rect> VisionEngine::findByColor(const cv::Mat &src) {
    Mat hsv, maskBlue, maskGreen, mask;
    cvtColor(src, hsv, COLOR_BGR2HSV);

    // 【优化 3】放宽颜色阈值，适应阴影
    // Blue: H(95-135), S/V(30+)
    inRange(hsv, Scalar(95, 30, 30), Scalar(135, 255, 255), maskBlue);
    // Green: H(35-90), S/V(30+)
    inRange(hsv, Scalar(35, 30, 30), Scalar(90, 255, 255), maskGreen);

    add(maskBlue, maskGreen, mask);

    // 适当增大腐蚀膨胀核
    Mat kernel = getStructuringElement(MORPH_RECT, Size(15, 3));
    morphologyEx(mask, mask, MORPH_CLOSE, kernel);

    vector<vector<Point>> contours;
    findContours(mask, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

    vector<Rect> rects;
    for(const auto &c : contours) {
        RotatedRect rr = minAreaRect(c);
        if(verifySizes(rr)) rects.push_back(rr.boundingRect());
    }
    return rects;
}

// 纹理流
vector<Rect> VisionEngine::findByTexture(const cv::Mat &src) {
    Mat gray, blur, sobel, shold, morph;
    cvtColor(src, gray, COLOR_BGR2GRAY);
    GaussianBlur(gray, blur, Size(5, 5), 0);

    Sobel(blur, sobel, CV_8U, 1, 0, 3);
    threshold(sobel, shold, 0, 255, THRESH_BINARY | THRESH_OTSU);

    Mat kernel = getStructuringElement(MORPH_RECT, Size(17, 3));
    morphologyEx(shold, morph, MORPH_CLOSE, kernel);

    vector<vector<Point>> contours;
    findContours(morph, contours, RETR_EXTERNAL, CHAIN_APPROX_SIMPLE);

    vector<Rect> rects;
    for(const auto &c : contours) {
        RotatedRect rr = minAreaRect(c);
        if(verifySizes(rr)) rects.push_back(rr.boundingRect());
    }
    return rects;
}

bool VisionEngine::verifySizes(const cv::RotatedRect &candidate) {
    float w = candidate.size.width;
    float h = candidate.size.height;
    float width = max(w, h);
    float height = min(w, h);
    float aspect = width / height;
    if(aspect < 2.0 || aspect > 6.0) return false;
    if(width < 40 || height < 10) return false;
    return true;
}

double VisionEngine::scorePlate(const cv::Mat &src, const cv::Rect &rect) {
    Rect safeRect = rect & Rect(0,0, src.cols, src.rows);
    if(safeRect.area() <= 0) return -1.0;
    double areaScore = safeRect.area();
    double ratio = (double)safeRect.width / safeRect.height;
    double ratioScore = 1.0 - abs(ratio - 3.5) / 3.5;
    return areaScore * ratioScore;
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
