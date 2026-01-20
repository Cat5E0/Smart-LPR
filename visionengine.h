#ifndef VISIONENGINE_H
#define VISIONENGINE_H

#include <QObject>
#include <QImage>
#include <QElapsedTimer>
#include <opencv2/opencv.hpp>

// 用于存储调试流程的中间图片
struct PipelineDebugData {
    cv::Mat resized;
    cv::Mat gray;
    cv::Mat hsvMask;
    cv::Mat sobel;
    cv::Mat binary;
    cv::Mat morph;
};

struct ProcessResult {
    bool found;
    cv::Rect roi;
    QImage displayImage;
    QImage plateImage;
    long costMs;
};

class VisionEngine : public QObject
{
    Q_OBJECT

public:
    // ==========================================
    // === [核心调优参数] ===
    // ==========================================

    // 1. 颜色阈值
    // H: 95-135 (标准蓝牌范围)
    static constexpr int HSV_H_MIN = 95;
    static constexpr int HSV_H_MAX = 135;

    // S: [修改] 从 30 降为 20，解决强光/反光导致车牌泛白无法识别的问题
    static constexpr int HSV_S_MIN = 20;
    static constexpr int HSV_S_MAX = 255;

    // V: 30-255 (保持低阈值以适应阴影)
    static constexpr int HSV_V_MIN = 30;
    static constexpr int HSV_V_MAX = 255;

    // 2. 图像预处理参数
    static constexpr int RESIZE_MAX_WIDTH = 1600; // 超过此宽度缩放

    // ==========================================

    explicit VisionEngine(QObject *parent = nullptr);

    Q_INVOKABLE void processFrame(cv::Mat frame);
    PipelineDebugData getDebugPipelineData(cv::Mat frame);
    QImage matToQImage(const cv::Mat &mat);

signals:
    void processingFinished(ProcessResult result);

private:
    cv::Rect detectPlateRobust(const cv::Mat &src);
    std::vector<cv::Rect> findByColor(const cv::Mat &src);
    std::vector<cv::Rect> findByTexture(const cv::Mat &src);
    bool verifySizes(const cv::RotatedRect &candidate);
    double scorePlate(const cv::Mat &src, const cv::Rect &rect);

    // 原子操作函数
    void preprocessResize(const cv::Mat &src, cv::Mat &dst, float &scale);
    void extractColorMask(const cv::Mat &src, cv::Mat &mask);
    void extractTextureFeatures(const cv::Mat &src, cv::Mat &gray, cv::Mat &sobel, cv::Mat &binary, cv::Mat &morph);
};

#endif // VISIONENGINE_H
