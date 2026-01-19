#include "mainwindow.h"
#include <QApplication>
#include <QMetaType> // [新增] 引入元类型头文件
#include <opencv2/opencv.hpp> // [新增] 引入 OpenCV 头文件

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // 【核心修复】注册 cv::Mat 类型，允许它在信号/槽和 invokeMethod 中跨线程传递
    qRegisterMetaType<cv::Mat>("cv::Mat");
    // 如果你用了 ProcessResult 结构体在信号里传，最好也注册一下（虽然现在的代码只在信号里传了，通常会自动推导，但注册更保险）
    qRegisterMetaType<ProcessResult>("ProcessResult");

    // 设置全局字体
    QFont font("Sans Serif", 10);
    font.setStyleHint(QFont::SansSerif);
    a.setFont(font);

    MainWindow w;
    w.show();

    return a.exec();
}
