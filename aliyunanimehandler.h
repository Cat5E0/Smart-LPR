#ifndef ALIYUNANIMEHANDLER_H
#define ALIYUNANIMEHANDLER_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QImage>
#include <QMap>
#include <QByteArray>

class AliyunAnimeHandler : public QObject
{
    Q_OBJECT
public:
    explicit AliyunAnimeHandler(QObject *parent = nullptr);
    ~AliyunAnimeHandler();

    void generateStyle(const QImage &image, const QString &style);

private:
    QNetworkAccessManager *netManager;

    // 阿里云配置
    const QString accessKeyId = "*******";
    const QString accessKeySecret = "*********";
    const QString bucketName = "qt-ubuntu"; // 你的Bucket名
    const QString region = "cn-shanghai";

    // 缓存变量
    QString currentStyle;
    QByteArray currentImageData;

    // 签名工具函数
    QByteArray hmacSha256(const QByteArray &key, const QByteArray &data);
    QString sha256Hex(const QByteArray &data);
    QString calculateApiSignature(QMap<QString, QString> params, const QString &secret);
    QString percentEncode(const QString &input);

signals:
    void generationSuccess(QString downloadUrl);
    void errorOccurred(QString errorMsg);
    void statusLog(QString msg);

private slots:
    void onOssUploadFinished();
    void onApiFinished();
};

#endif
