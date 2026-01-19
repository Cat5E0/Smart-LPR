QT       += core gui network
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++11

# 输出程序名称
TARGET = MagicPortrait
TEMPLATE = app

# 源文件 (严格对应你截图中的文件名)
SOURCES += \
    imageprocessor.cpp \
    logger.cpp \
    main.cpp \
    mainwindow.cpp \
    aliyunanimehandler.cpp \
    remotemodel.cpp \
    visionengine.cpp

# 头文件
HEADERS += \
    imageprocessor.h \
    logger.h \
    mainwindow.h \
    aliyunanimehandler.h \
    remotemodel.h \
    visionengine.h

# --- OpenCV 配置 (Ubuntu 18.04 默认路径) ---
INCLUDEPATH +=  /usr/include/opencv \
                      /usr/include/opencv2

# 链接库顺序
LIBS += -lopencv_objdetect -lopencv_highgui -lopencv_imgcodecs -lopencv_imgproc -lopencv_core -lopencv_videoio
