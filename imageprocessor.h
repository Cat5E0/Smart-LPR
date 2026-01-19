#ifndef IMAGEPROCESSOR_H
#define IMAGEPROCESSOR_H

#include <opencv2/opencv.hpp>

class ImageProcessor
{
public:
    ImageProcessor();

    cv::Mat toGray(const cv::Mat &src);
    cv::Mat applyGaussianBlur(const cv::Mat &src);
    cv::Mat applySobel(const cv::Mat &src);
    cv::Mat toBinary(const cv::Mat &src);
    cv::Mat applyMorphology(const cv::Mat &src);

    // 新增：获取颜色遮罩，用于调试颜色阈值
    cv::Mat getColorMask(const cv::Mat &src);
};

#endif // IMAGEPROCESSOR_H
