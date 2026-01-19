#include "imageprocessor.h"

using namespace cv;

ImageProcessor::ImageProcessor() {}

Mat ImageProcessor::toGray(const Mat &src) {
    Mat gray;
    if(src.channels() == 3) cvtColor(src, gray, COLOR_BGR2GRAY);
    else gray = src.clone();
    return gray;
}

Mat ImageProcessor::applyGaussianBlur(const Mat &src) {
    Mat blur;
    GaussianBlur(src, blur, Size(5, 5), 0);
    return blur;
}

Mat ImageProcessor::applySobel(const Mat &src) {
    Mat sobel;
    Sobel(src, sobel, CV_8U, 1, 0, 3);
    return sobel;
}

Mat ImageProcessor::toBinary(const Mat &src) {
    Mat bin;
    threshold(src, bin, 0, 255, THRESH_BINARY | THRESH_OTSU);
    return bin;
}

Mat ImageProcessor::applyMorphology(const Mat &src) {
    Mat morph;
    Mat kernel = getStructuringElement(MORPH_RECT, Size(17, 3));
    morphologyEx(src, morph, MORPH_CLOSE, kernel);
    return morph;
}

Mat ImageProcessor::getColorMask(const Mat &src) {
    Mat hsv, maskBlue, maskGreen, mask;
    cvtColor(src, hsv, COLOR_BGR2HSV);

    // 调试用的宽泛阈值，与 VisionEngine 保持一致
    inRange(hsv, Scalar(95, 43, 43), Scalar(135, 255, 255), maskBlue);
    inRange(hsv, Scalar(35, 43, 43), Scalar(90, 255, 255), maskGreen);

    add(maskBlue, maskGreen, mask);
    return mask;
}
