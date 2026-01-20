#ifndef COLORANALYZER_H
#define COLORANALYZER_H

#include <QObject>
#include <QString>
#include <QMap>
#include <QVector>
#include <opencv2/opencv.hpp>

struct ChannelStats {
    QVector<float> h_vals;
    QVector<float> s_vals;
    QVector<float> v_vals;
};

enum ChannelType { CHANNEL_H, CHANNEL_S, CHANNEL_V };

class ColorAnalyzer : public QObject
{
    Q_OBJECT
public:
    explicit ColorAnalyzer(QObject *parent = nullptr);
    void startAnalysis(const QString &dirPath);

signals:
    void progressUpdated(int current, int total);
    void analysisFinished(QString resultMsg);
    void logMessage(QString msg);

private:
    void run(QString dirPath);
    cv::Rect parseGtBox(const QString &fileName);
    QString extractCategory(const QString &path);

    void generateReport(const QString &outputDir, const QMap<QString, ChannelStats> &data);

    // 生成网格图
    cv::Mat drawGridImage(const QMap<QString, ChannelStats> &data, ChannelType type, const QString &title);
    cv::Mat drawMultiScatterGrid(const QMap<QString, ChannelStats> &data, const QString &title);

    // 内部绘制单元
    cv::Mat drawSingleHistogram(const QVector<float> &values, int maxRange, const QString &subTitle, const cv::Scalar &barColor);
    cv::Mat drawSingleScatter(const QVector<float> &h, const QVector<float> &s, const QString &subTitle,
                              float recMinH, float recMaxH, float recMinS, float recMaxS);
};

#endif // COLORANALYZER_H
