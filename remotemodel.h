#ifndef REMOTEMODEL_H
#define REMOTEMODEL_H

#include <QObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QImage>
#include <QByteArray>

class RemoteModel : public QObject
{
    Q_OBJECT
public:
    explicit RemoteModel(QObject *parent = nullptr);
    ~RemoteModel();

    void recognizeLicensePlate(const QImage &image);

signals:
    void recognitionFinished(QString plateNumber, double confidence, QRect rect);
    void errorOccurred(QString errorMsg);
    void statusLog(QString msg);

private slots:
    void onOssUploadFinished();
    void onApiFinished();

private:
    QString percentEncode(const QString &input);
    QString calculateApiSignature(QMap<QString, QString> params, const QString &secret);
    QByteArray hmacSha256(const QByteArray &key, const QByteArray &data);
    QString sha256Hex(const QByteArray &data);

    // 核心辅助函数
    void performOcrRequest();
    void parseServerTimeAndAdjust(QNetworkReply *reply);

private:
    QNetworkAccessManager *netManager;
    QByteArray currentImageData;

    // --- [修正] 恢复您原始的正确密钥 ---
    const QString accessKeyId = "LTAI5tRktaGKnLTgxUb4FVRw";
    const QString accessKeySecret = "gNASf41xb4CnUZ7rXfPV4wpjyv7x2U"; // 已修正回原始值
    const QString bucketName = "qt-ubuntu";
    const QString region = "cn-shanghai";

    // 自动对时与重试机制
    qint64 timeOffset = 0;     // 本地时间与服务器时间的偏差
    int retryCount = 0;
    const int MAX_RETRIES = 3;
    QString currentUploadedUrl;
};

#endif // REMOTEMODEL_H
