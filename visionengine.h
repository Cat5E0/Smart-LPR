#ifndef VISIONENGINE_H
#define VISIONENGINE_H

#include <QObject>
#include <QImage>
#include <QElapsedTimer>
#include <opencv2/opencv.hpp>

struct ProcessResult {
    bool found;
    cv::Rect roi;          // 最终定位的 ROI
    QImage displayImage;   // 用于显示的带框图
    QImage plateImage;     // 切割下来的车牌图
    long costMs;
};

class VisionEngine : public QObject
{
    Q_OBJECT
public:
    explicit VisionEngine(QObject *parent = nullptr);

    Q_INVOKABLE void processFrame(cv::Mat frame);

    QImage matToQImage(const cv::Mat &mat);

signals:
    void processingFinished(ProcessResult result);

private:
    // [核心] V5.0 鲁棒定位算法
    cv::Rect detectPlateRobust(const cv::Mat &src);

    // 辅助：颜色检测流
    std::vector<cv::Rect> findByColor(const cv::Mat &src);

    // 辅助：纹理检测流 (Sobel)
    std::vector<cv::Rect> findByTexture(const cv::Mat &src);

    // 辅助：校验与评分
    bool verifySizes(const cv::RotatedRect &candidate);
    double scorePlate(const cv::Mat &src, const cv::Rect &rect);
};

#endif // VISIONENGINE_H
